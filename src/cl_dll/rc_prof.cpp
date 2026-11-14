// ReCoF frame profiler
//
// rc_prof 1 shows where the client spends each frame: the CPU time of each phase (rc_prof.h), averaged over
// half a second. rc_prof 2 adds the GPU time of the same phases from timestamp queries (GL_ARB_timer_query),
// read back a few frames later so the game never waits for the GPU. rc_prof_dump prints the numbers to the
// console (with -condebug they also land in qconsole.log). "server" is the local server's whole frame (B337),
// all of the game logic of hl.dll and the engine's physics for it, timed from the end of CL_CreateMove to
// HUD_Frame, which the engine calls just before and just after SV_Frame; rc_svprof in hl.dll splits it.
// Everything else outside the client scopes, which includes the engine, sound and the buffer swap, is
// "other". rc_prof_log 1 prints a line with the numbers every half second, for test runs (B337).

#include "hud.h"
#include "cl_util.h"
#include "glmanager.h"
#include "rc_prof.h"

#include <stdio.h>
#include <string.h>

#define RC_GL_TIMESTAMP					0x8E28
#define RC_GL_QUERY_RESULT				0x8866
#define RC_GL_QUERY_RESULT_AVAILABLE	0x8867

typedef void ( APIENTRY *RcGenQueries_t )( GLsizei n, GLuint *ids );
typedef void ( APIENTRY *RcQueryCounter_t )( GLuint id, GLenum target );
typedef void ( APIENTRY *RcGetQueryObjectiv_t )( GLuint id, GLenum pname, GLint *params );
typedef void ( APIENTRY *RcGetQueryObjectui64v_t )( GLuint id, GLenum pname, unsigned __int64 *params );

#define RC_STACK		32
#define RC_GPU_FRAMES	4		// frames in flight before their results are dropped
#define RC_GPU_MARKS	1024	// timestamps per frame (two per model)

struct RcGpuFrame_t
{
	int		count;
	int		pending;					// finished, results not read yet
	GLuint	query[RC_GPU_MARKS];
	int		scope[RC_GPU_MARKS];		// the scope the GPU works for after this mark
};

static const char *s_szScopeName[RCP_COUNT] =
{
	"other", "server", "view", "entities", "client frame", "world", "world 1st", "bump", "dlights", "world 2nd",
	"specular", "brush ents", "decals", "sky", "models", "mdl vertices", "mdl lighting", "mdl drawing",
	"mdl bones", "mdl studiolt", "bone jobs", "transparent", "particles", "water", "post fx", "hud"
};

// the scopes that issue GL work get their own GPU time; the GPU time of the others falls to "other"
static const bool s_bGLScope[RCP_COUNT] =
{
	false, false, false, false, false, true, true, true, true, true,
	true, true, true, true, true, false, false, false,
	false, false, false, true, true, true, true, true
};

static cvar_t		*rc_prof;
static cvar_t		*rc_prof_log;			// a line every half second (B337)
static int			s_iOn;					// rc_prof as read at the last frame boundary
static double		s_flTickMs;				// milliseconds per performance counter tick
static int			s_iStack[RC_STACK];
static int			s_iDepth;
static int			s_iOverflow;			// pushes the stack had no room for
static LONGLONG		s_llLast;				// the last scope change
static LONGLONG		s_llFrameStart;
static LONGLONG		s_llCpu[RCP_COUNT];		// ticks of this frame
static int			s_iCount[RCC_COUNT];

// the half second window the overlay shows
static LONGLONG		s_llWinStart;
static double		s_flWinCpu[RCP_COUNT], s_flWinGpu[RCP_COUNT];
static double		s_flWinFrame, s_flWinGpuFrame, s_flWinWorst;
static int			s_iWinFrames, s_iWinGpuFrames, s_iWinCount[RCC_COUNT];

static double		s_flShowCpu[RCP_COUNT], s_flShowGpu[RCP_COUNT];
static double		s_flShowFrame, s_flShowGpuFrame, s_flShowWorst, s_flShowCount[RCC_COUNT];
static double		s_flWinWorstCpu[RCP_COUNT], s_flShowWorstCpu[RCP_COUNT];	// the scopes of the worst frame (B338)
static int			s_iShowGpuValid;

static int						s_iGpuState;	// 0 not set up, 1 working, -1 not supported
static RcGpuFrame_t				s_Gpu[RC_GPU_FRAMES];
static int						s_iGpuCur;
static int						s_iGpuScope;	// the scope of the last mark
static RcGenQueries_t			s_pfnGenQueries;
static RcQueryCounter_t			s_pfnQueryCounter;
static RcGetQueryObjectiv_t		s_pfnGetQueryObjectiv;
static RcGetQueryObjectui64v_t	s_pfnGetQueryObjectui64v;

static int RcCurrent( void )
{
	return s_iDepth ? s_iStack[s_iDepth - 1] : RCP_OTHER;
}

static void RcGpuMark( int scope )
{
	RcGpuFrame_t *f = &s_Gpu[s_iGpuCur];

	if ( f->count >= RC_GPU_MARKS )
		return;

	s_pfnQueryCounter( f->query[f->count], RC_GL_TIMESTAMP );
	f->scope[f->count++] = scope;
	s_iGpuScope = scope;
}

// charges the time since the last change to the scope that was current
static void RcCharge( void )
{
	LARGE_INTEGER now;
	QueryPerformanceCounter( &now );
	s_llCpu[RcCurrent()] += now.QuadPart - s_llLast;
	s_llLast = now.QuadPart;
}

// marks the GPU when the scope it should charge changes
static void RcGpuFollow( void )
{
	if ( s_iOn < 2 || s_iGpuState != 1 )
		return;

	int cur = RcCurrent();

	// the parts of models are timed on the CPU only; the GPU keeps charging models, so they add no marks
	if ( cur == RCP_MDL_VERTS || cur == RCP_MDL_LIGHT || cur == RCP_MDL_DRAW || cur == RCP_MDL_BONES || cur == RCP_MDL_SLIGHT )
		cur = RCP_MODELS;

	int scope = s_bGLScope[cur] ? cur : RCP_OTHER;

	if ( scope != s_iGpuScope )
		RcGpuMark( scope );
}

void RcProf_Push( int scope )
{
	if ( !s_iOn )
		return;

	if ( s_iDepth >= RC_STACK )
	{
		s_iOverflow++;
		return;
	}

	RcCharge();
	s_iStack[s_iDepth++] = scope;
	RcGpuFollow();
}

void RcProf_Pop( void )
{
	if ( !s_iOn )
		return;

	if ( s_iOverflow )
	{
		s_iOverflow--;
		return;
	}

	if ( !s_iDepth )
		return;

	RcCharge();
	s_iDepth--;
	RcGpuFollow();
}

void RcProf_PopIf( int scope )
{
	if ( s_iOn && !s_iOverflow && RcCurrent() == scope && s_iDepth )
		RcProf_Pop();
}

void RcProf_Count( int counter )
{
	s_iCount[counter]++;
}

static void RcGpuSetup( void )
{
	s_iGpuState = -1;

	if ( !gl.IsExtensionSupported( "GL_ARB_timer_query" ) )
	{
		const char *version = (const char *)glGetString( GL_VERSION );
		int major = 0, minor = 0;

		if ( !version || sscanf( version, "%d.%d", &major, &minor ) != 2 || major * 10 + minor < 33 )
		{
			ConsolePrint( "rc_prof: the driver has no timer queries, so no GPU times\n" );
			return;
		}
	}

	s_pfnGenQueries = (RcGenQueries_t)gl.wglGetProcAddress( "glGenQueries" );
	if ( !s_pfnGenQueries )
		s_pfnGenQueries = (RcGenQueries_t)gl.wglGetProcAddress( "glGenQueriesARB" );
	s_pfnQueryCounter = (RcQueryCounter_t)gl.wglGetProcAddress( "glQueryCounter" );
	s_pfnGetQueryObjectiv = (RcGetQueryObjectiv_t)gl.wglGetProcAddress( "glGetQueryObjectiv" );
	if ( !s_pfnGetQueryObjectiv )
		s_pfnGetQueryObjectiv = (RcGetQueryObjectiv_t)gl.wglGetProcAddress( "glGetQueryObjectivARB" );
	s_pfnGetQueryObjectui64v = (RcGetQueryObjectui64v_t)gl.wglGetProcAddress( "glGetQueryObjectui64v" );

	if ( !s_pfnGenQueries || !s_pfnQueryCounter || !s_pfnGetQueryObjectiv || !s_pfnGetQueryObjectui64v )
	{
		ConsolePrint( "rc_prof: the timer query functions are missing, so no GPU times\n" );
		return;
	}

	for ( int i = 0; i < RC_GPU_FRAMES; i++ )
	{
		s_pfnGenQueries( RC_GPU_MARKS, s_Gpu[i].query );
		s_Gpu[i].count = 0;
		s_Gpu[i].pending = 0;
	}

	s_iGpuState = 1;
}

// reads a finished frame's timestamps if the GPU is done with them; returns 0 while they are in flight
static int RcGpuCollect( RcGpuFrame_t *f )
{
	GLint available = 0;

	if ( f->count < 2 )
	{
		f->pending = 0;
		return 1;
	}

	s_pfnGetQueryObjectiv( f->query[f->count - 1], RC_GL_QUERY_RESULT_AVAILABLE, &available );
	if ( !available )
		return 0;

	unsigned __int64 first, prev, t;
	s_pfnGetQueryObjectui64v( f->query[0], RC_GL_QUERY_RESULT, &first );
	prev = first;

	for ( int i = 1; i < f->count; i++ )
	{
		s_pfnGetQueryObjectui64v( f->query[i], RC_GL_QUERY_RESULT, &t );
		s_flWinGpu[f->scope[i - 1]] += (double)( t - prev ) / 1e6;
		prev = t;
	}

	s_flWinGpuFrame += (double)( prev - first ) / 1e6;
	s_iWinGpuFrames++;
	f->pending = 0;
	return 1;
}

static void RcClearWindow( void )
{
	memset( s_flWinCpu, 0, sizeof( s_flWinCpu ) );
	memset( s_flWinGpu, 0, sizeof( s_flWinGpu ) );
	memset( s_iWinCount, 0, sizeof( s_iWinCount ) );
	s_flWinFrame = s_flWinGpuFrame = s_flWinWorst = 0;
	s_iWinFrames = s_iWinGpuFrames = 0;
}

// rc_prof_log 1: the half second just shown, on one line of name=value pairs (B337)
static void RcProfLogLine( void )
{
	char line[2048];
	int len = sprintf( line, "rcp t=%.1f frame=%.3f fps=%.1f worst=%.2f models=%.1f monsters=%.1f gpumeshes=%.0f "
		"psystems=%.0f particles=%.0f", gEngfuncs.GetClientTime(), s_flShowFrame, s_flShowFrame > 0 ? 1000.0 / s_flShowFrame : 0.0,
		s_flShowWorst, s_flShowCount[RCC_MODELS], s_flShowCount[RCC_MONSTERS], s_flShowCount[RCC_GPUMESHES],
		s_flShowCount[RCC_PSYSTEMS], s_flShowCount[RCC_PARTICLES] );

	for ( int i = 0; i < RCP_COUNT && len < (int)sizeof( line ) - 40; i++ )
	{
		if ( s_flShowCpu[i] < 0.0005 )
			continue;

		line[len++] = ' ';
		for ( const char *s = s_szScopeName[i]; *s; s++ )
			line[len++] = ( *s == ' ' ) ? '_' : *s;
		len += sprintf( line + len, "=%.3f", s_flShowCpu[i] );
	}

	// the scopes that took 0.5 ms or more in the worst frame, with w_ before their names (B338)
	for ( int i = 0; i < RCP_COUNT && len < (int)sizeof( line ) - 40; i++ )
	{
		if ( s_flShowWorstCpu[i] < 0.5 )
			continue;

		len += sprintf( line + len, " w_" );
		for ( const char *s = s_szScopeName[i]; *s; s++ )
			line[len++] = ( *s == ' ' ) ? '_' : *s;
		len += sprintf( line + len, "=%.2f", s_flShowWorstCpu[i] );
	}

	// rc_prof 2: the GPU frame and the GPU time of each scope, with g_ before its name
	if ( s_iOn >= 2 && s_iGpuState == 1 && s_iShowGpuValid )
	{
		len += sprintf( line + len, " gpu=%.3f", s_flShowGpuFrame );

		for ( int i = 0; i < RCP_COUNT && len < (int)sizeof( line ) - 40; i++ )
		{
			if ( s_flShowGpu[i] < 0.0005 )
				continue;

			len += sprintf( line + len, " g_" );
			for ( const char *s = s_szScopeName[i]; *s; s++ )
				line[len++] = ( *s == ' ' ) ? '_' : *s;
			len += sprintf( line + len, "=%.3f", s_flShowGpu[i] );
		}
	}

	strcpy( line + len, "\n" );
	ConsolePrint( line );
}


//-----------------------------------------------------------------------------
// rc_perflog 1 (B340): a performance log a player can send. It writes cryoffear/perflogs/perflog_<date>.txt: what
// the computer is (processor, graphics card and driver, Windows, power plan, the game's settings), one line a
// second (frame rate, the slowest 1% of frames, the main thread's busy share, disk reads, page faults, the
// scopes of rc_prof and the GPU time), and a line for every stutter, a frame twice the recent average and
// 10 ms or more above it: where its time went (the scopes), how much of it the main thread was running (the
// rest it waited: the frame limiter, the driver, Windows), the disk reads and page faults in it, the map and the
// place. It turns the scopes and the GPU times on while it runs (rc_prof 2) without the overlay.
//-----------------------------------------------------------------------------
#include "rc_version.h"
#include <intrin.h>
#include <time.h>

typedef struct
{
	DWORD	cb;
	DWORD	PageFaultCount;
	SIZE_T	PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
	SIZE_T	QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage;
} RcProcMem_t;	// PROCESS_MEMORY_COUNTERS
typedef BOOL ( WINAPI *RcGetProcessMemoryInfo_t )( HANDLE, RcProcMem_t *, DWORD );
typedef LONG ( WINAPI *RcRtlGetVersion_t )( OSVERSIONINFOW * );
typedef BOOL ( WINAPI *RcQueryThreadCycleTime_t )( HANDLE, unsigned __int64 * );
typedef DWORD ( WINAPI *RcPowerGetActiveScheme_t )( HKEY, GUID ** );
typedef DWORD ( WINAPI *RcPowerReadFriendlyName_t )( HKEY, const GUID *, const GUID *, const GUID *, UCHAR *, DWORD * );

#define RC_PERF_TIMES	4096	// frames a second kept for the slowest 1%

static cvar_t					*rc_perflog;
static FILE						*s_pPerf;
static RcGetProcessMemoryInfo_t	s_pfnPerfMem;
static RcQueryThreadCycleTime_t	s_pfnPerfCycles;
static unsigned __int64			s_ullPerfTsc0, s_ullPerfCycles;
static LONGLONG					s_llPerfQpc0;
static ULONGLONG				s_ullPerfReads, s_ullPerfReadBytes;
static DWORD					s_dwPerfFaults;
static double					s_flPerfAvg;		// the recent frame time, which stutters are measured against
static int						s_iPerfSkip;		// frames not judged after the log starts or the map changes
static char						s_szPerfMap[64];

// the second being collected
static LONGLONG		s_llPerfSecStart;
static int			s_iPerfFrames, s_iPerfHitches;
static double		s_flPerfSum, s_flPerfWorst, s_flPerfBusy;
static float		s_flPerfTimes[RC_PERF_TIMES];
static double		s_flPerfCpu[RCP_COUNT];
static double		s_flPerfCount[RCC_COUNT];	// B341
static double		s_flPerfPause;				// ms of this second spent in times without HUD frames (B341)
static ULONGLONG	s_ullPerfSecReads, s_ullPerfSecReadBytes;
static DWORD		s_dwPerfSecFaults;

static int RcPerfCompare( const void *a, const void *b )
{
	float x = *(const float *)a, y = *(const float *)b;
	return ( x > y ) ? -1 : ( x < y ) ? 1 : 0;
}

static void RcPerfName( char *out, const char *name )
{
	for ( ; *name; name++ )
		*out++ = ( *name == ' ' ) ? '_' : *name;
	*out = 0;
}

static const char *RcPerfMap( void )
{
	const char *level = gEngfuncs.pfnGetLevelName();
	return ( level && *level ) ? level : "(none)";
}

// the main thread's CPU time in the frame, from its cycle count and the time stamp counter's rate
static double RcPerfBusyMs( LONGLONG qpcNow )
{
	unsigned __int64 cycles = 0;
	if ( !s_pfnPerfCycles || !s_pfnPerfCycles( GetCurrentThread(), &cycles ) )
		return 0.0;

	double ms = ( qpcNow - s_llPerfQpc0 ) * s_flTickMs;
	double tscPerMs = ms > 100.0 ? (double)( __rdtsc() - s_ullPerfTsc0 ) / ms : 0.0;
	double busy = tscPerMs > 0 ? (double)( cycles - s_ullPerfCycles ) / tscPerMs : 0.0;

	s_ullPerfCycles = cycles;
	return busy;
}

static void RcPerfHeader( void )
{
	char text[256];
	int regs[4];
	char brand[49];
	time_t now = time( NULL );
	struct tm *lt = localtime( &now );

	strftime( text, sizeof( text ), "%Y-%m-%d %H:%M:%S", lt );
	fprintf( s_pPerf, "ReCoF %s performance log, %s\n", RECOF_VERSION, text );

	memset( brand, 0, sizeof( brand ) );
	__cpuid( regs, 0x80000000 );
	if ( (unsigned int)regs[0] >= 0x80000004 )
	{
		__cpuid( (int *)( brand + 0 ), 0x80000002 );
		__cpuid( (int *)( brand + 16 ), 0x80000003 );
		__cpuid( (int *)( brand + 32 ), 0x80000004 );
	}
	int invariant = 0;
	if ( (unsigned int)regs[0] >= 0x80000007 )
	{
		__cpuid( regs, 0x80000007 );
		invariant = ( regs[3] >> 8 ) & 1;
	}

	SYSTEM_INFO si;
	GetSystemInfo( &si );
	DWORD_PTR process = 0, system = 0;
	int affinity = 0;
	if ( GetProcessAffinityMask( GetCurrentProcess(), &process, &system ) )
		for ( ; process; process &= process - 1 )
			affinity++;

	const char *b = brand;
	while ( *b == ' ' )
		b++;
	fprintf( s_pPerf, "cpu: %s, %d logical processors (%d for the game), invariant time stamp counter %s\n", *b ? b : "unknown",
		(int)si.dwNumberOfProcessors, affinity, invariant ? "yes" : "no" );

	// the clock the engine reads all the time: 10 MHz is Windows' own from the processor's counter, 14.3 MHz the
	// HPET and 3.58 MHz the ACPI timer, which some boards and tuning guides leave on and which cost far more a read
	LARGE_INTEGER freq, c0, c1, c;
	QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &c0 );
	for ( int i = 0; i < 10000; i++ )
		QueryPerformanceCounter( &c );
	QueryPerformanceCounter( &c1 );
	fprintf( s_pPerf, "timer: %.6f MHz, %.0f ns a read\n", freq.QuadPart / 1e6,
		( c1.QuadPart - c0.QuadPart ) * 1e9 / (double)freq.QuadPart / 10000.0 );

	MEMORYSTATUSEX mem;
	mem.dwLength = sizeof( mem );
	if ( GlobalMemoryStatusEx( &mem ) )
		fprintf( s_pPerf, "memory: %.1f GB, %.1f GB free\n", mem.ullTotalPhys / 1073741824.0, mem.ullAvailPhys / 1073741824.0 );

	OSVERSIONINFOW os;
	memset( &os, 0, sizeof( os ) );
	os.dwOSVersionInfoSize = sizeof( os );
	RcRtlGetVersion_t pfnVersion = (RcRtlGetVersion_t)GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "RtlGetVersion" );
	if ( pfnVersion && pfnVersion( &os ) == 0 )
		fprintf( s_pPerf, "windows: %lu.%lu build %lu\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber );

	HMODULE power = LoadLibraryA( "powrprof.dll" );
	if ( power )
	{
		RcPowerGetActiveScheme_t pfnActive = (RcPowerGetActiveScheme_t)GetProcAddress( power, "PowerGetActiveScheme" );
		RcPowerReadFriendlyName_t pfnName = (RcPowerReadFriendlyName_t)GetProcAddress( power, "PowerReadFriendlyName" );
		GUID *scheme = NULL;
		if ( pfnActive && pfnName && pfnActive( NULL, &scheme ) == 0 && scheme )
		{
			WCHAR name[128];
			DWORD size = sizeof( name );
			char ascii[128];
			if ( pfnName( NULL, scheme, NULL, NULL, (UCHAR *)name, &size ) == 0 &&
				 WideCharToMultiByte( CP_UTF8, 0, name, -1, ascii, sizeof( ascii ), NULL, NULL ) )
				fprintf( s_pPerf, "power plan: %s\n", ascii );
			LocalFree( scheme );
		}
		FreeLibrary( power );
	}

	const char *vendor = (const char *)glGetString( GL_VENDOR );
	const char *renderer = (const char *)glGetString( GL_RENDERER );
	const char *version = (const char *)glGetString( GL_VERSION );
	fprintf( s_pPerf, "gpu: %s, %s, OpenGL %s\n", vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?" );
	fprintf( s_pPerf, "screen: %d x %d\n", ScreenWidth, ScreenHeight );

	// the command line without the path of the game, which may hold the Windows user name
	const char *cmd = GetCommandLineA();
	if ( *cmd == '"' )
		cmd = strchr( cmd + 1, '"' ) ? strchr( cmd + 1, '"' ) + 1 : "";
	else
		cmd = strchr( cmd, ' ' ) ? strchr( cmd, ' ' ) : "";
	fprintf( s_pPerf, "launch options:%s\n", cmd );

	static const char *s_szCvars[] =
	{
		"fps_max", "developer", "gl_vsync", "rc_vsync", "rc_smoothframes", "rc_smoothframes_fps", "rc_threads",
		"rc_gpumodels", "rc_worldbatch", "rc_lightshader", "rc_dlightcull", "rc_rawinput", "gl_dynlight", "r_dynamic",
		"cg_water", "gl_bump", "r_weather", "gl_renderer", "cl_himodels", NULL
	};
	fprintf( s_pPerf, "settings:" );
	for ( int i = 0; s_szCvars[i]; i++ )
	{
		const char *value = gEngfuncs.pfnGetCvarString( (char *)s_szCvars[i] );
		if ( value )
			fprintf( s_pPerf, " %s=%s", s_szCvars[i], value );
	}
	fprintf( s_pPerf, "\n\n" );
	fprintf( s_pPerf, "Each second: the frame rate, the average frame, the slowest 1%% and the slowest frame (ms), the main thread's "
		"busy share, stutters, disk reads (operations/KB), page faults, then the CPU ms of each part of an average frame "
		"and the GPU frame. STUTTER lines: one frame, its ms against the recent average, how many of them the main thread "
		"was running, its disk reads and page faults, and the parts that took 0.5 ms or more in it.\n\n" );
	fflush( s_pPerf );
}

static void RcPerfClose( void )
{
	if ( !s_pPerf )
		return;

	fprintf( s_pPerf, "end of the log\n" );
	fclose( s_pPerf );
	s_pPerf = NULL;
}

static void RcPerfOpen( LONGLONG qpcNow )
{
	char dir[MAX_PATH], path[MAX_PATH], stamp[32];
	time_t now = time( NULL );

	_snprintf( dir, sizeof( dir ), "%s\\perflogs", gEngfuncs.pfnGetGameDirectory() );
	dir[sizeof( dir ) - 1] = 0;
	CreateDirectoryA( dir, NULL );
	strftime( stamp, sizeof( stamp ), "%Y%m%d_%H%M%S", localtime( &now ) );
	_snprintf( path, sizeof( path ), "%s\\perflog_%s.txt", dir, stamp );
	path[sizeof( path ) - 1] = 0;

	s_pPerf = fopen( path, "w" );
	if ( !s_pPerf )
	{
		gEngfuncs.Con_Printf( "rc_perflog: could not write %s\n", path );
		return;
	}

	if ( !s_pfnPerfMem )
		s_pfnPerfMem = (RcGetProcessMemoryInfo_t)GetProcAddress( GetModuleHandleA( "kernel32.dll" ), "K32GetProcessMemoryInfo" );
	if ( !s_pfnPerfCycles )
		s_pfnPerfCycles = (RcQueryThreadCycleTime_t)GetProcAddress( GetModuleHandleA( "kernel32.dll" ), "QueryThreadCycleTime" );

	RcPerfHeader();
	gEngfuncs.Con_Printf( "rc_perflog: writing %s (rc_perflog 0 ends it)\n", path );

	s_ullPerfTsc0 = __rdtsc();
	s_llPerfQpc0 = qpcNow;
	if ( s_pfnPerfCycles )
		s_pfnPerfCycles( GetCurrentThread(), &s_ullPerfCycles );

	IO_COUNTERS io;
	if ( GetProcessIoCounters( GetCurrentProcess(), &io ) )
	{
		s_ullPerfReads = io.ReadOperationCount;
		s_ullPerfReadBytes = io.ReadTransferCount;
	}
	RcProcMem_t mem;
	mem.cb = sizeof( mem );
	if ( s_pfnPerfMem && s_pfnPerfMem( GetCurrentProcess(), &mem, sizeof( mem ) ) )
		s_dwPerfFaults = mem.PageFaultCount;

	s_flPerfAvg = 0;
	s_iPerfSkip = 30;
	s_szPerfMap[0] = 0;
	s_llPerfSecStart = qpcNow;
	s_iPerfFrames = s_iPerfHitches = 0;
	s_flPerfSum = s_flPerfWorst = s_flPerfBusy = 0;
	memset( s_flPerfCpu, 0, sizeof( s_flPerfCpu ) );
	memset( s_flPerfCount, 0, sizeof( s_flPerfCount ) );
	s_flPerfPause = 0;
	s_ullPerfSecReads = s_ullPerfSecReadBytes = 0;
	s_dwPerfSecFaults = 0;
}

static void RcPerfSecond( LONGLONG qpcNow )
{
	if ( !s_iPerfFrames )
		return;

	double secs = ( ( qpcNow - s_llPerfSecStart ) * s_flTickMs - s_flPerfPause ) / 1000.0;
	double avg = s_flPerfSum / s_iPerfFrames;
	int n = s_iPerfFrames < RC_PERF_TIMES ? s_iPerfFrames : RC_PERF_TIMES;
	qsort( s_flPerfTimes, n, sizeof( float ), RcPerfCompare );
	double low = s_flPerfTimes[n / 100];	// the slowest 1%: the frame at the 99th percentile

	fprintf( s_pPerf, "%8.1f s %-16s %6.1f fps frame %6.2f 1%% %6.2f worst %6.2f busy %3.0f%% stutters %d reads %llu/%llu "
		"faults %lu |", gEngfuncs.GetClientTime(), RcPerfMap(), s_iPerfFrames / ( secs > 0 ? secs : 1 ), avg, low,
		s_flPerfWorst, 100.0 * s_flPerfBusy / ( s_flPerfSum > 0 ? s_flPerfSum : 1 ), s_iPerfHitches, s_ullPerfSecReads,
		s_ullPerfSecReadBytes / 1024, s_dwPerfSecFaults );

	char name[32];
	for ( int i = 0; i < RCP_COUNT; i++ )
	{
		double ms = s_flPerfCpu[i] / s_iPerfFrames;
		if ( ms < 0.01 )
			continue;
		RcPerfName( name, s_szScopeName[i] );
		fprintf( s_pPerf, " %s %.2f", name, ms );
	}
	if ( s_iShowGpuValid )
		fprintf( s_pPerf, " | gpu %.2f", s_flShowGpuFrame );
	fprintf( s_pPerf, " | models %.0f monsters %.0f particles %.0f psystems %.0f\n", s_flPerfCount[RCC_MODELS] / s_iPerfFrames,
		s_flPerfCount[RCC_MONSTERS] / s_iPerfFrames, s_flPerfCount[RCC_PARTICLES] / s_iPerfFrames,
		s_flPerfCount[RCC_PSYSTEMS] / s_iPerfFrames );	// B341
	fflush( s_pPerf );

	s_llPerfSecStart = qpcNow;
	s_iPerfFrames = s_iPerfHitches = 0;
	s_flPerfSum = s_flPerfWorst = s_flPerfBusy = 0;
	memset( s_flPerfCpu, 0, sizeof( s_flPerfCpu ) );
	memset( s_flPerfCount, 0, sizeof( s_flPerfCount ) );
	s_flPerfPause = 0;
	s_ullPerfSecReads = s_ullPerfSecReadBytes = 0;
	s_dwPerfSecFaults = 0;
}

// one frame (RcProf_Frame, before its scopes are cleared)
static void RcPerfFrame( double frame, LONGLONG qpcNow )
{
	double busy = RcPerfBusyMs( qpcNow );

	ULONGLONG reads = 0, readBytes = 0;
	IO_COUNTERS io;
	if ( GetProcessIoCounters( GetCurrentProcess(), &io ) )
	{
		reads = io.ReadOperationCount - s_ullPerfReads;
		readBytes = io.ReadTransferCount - s_ullPerfReadBytes;
		s_ullPerfReads = io.ReadOperationCount;
		s_ullPerfReadBytes = io.ReadTransferCount;
	}

	DWORD faults = 0;
	RcProcMem_t mem;
	mem.cb = sizeof( mem );
	if ( s_pfnPerfMem && s_pfnPerfMem( GetCurrentProcess(), &mem, sizeof( mem ) ) )
	{
		faults = mem.PageFaultCount - s_dwPerfFaults;
		s_dwPerfFaults = mem.PageFaultCount;
	}

	// a map change starts over: its loading is not a stutter of play
	const char *map = RcPerfMap();
	if ( strcmp( map, s_szPerfMap ) )
	{
		strncpy( s_szPerfMap, map, sizeof( s_szPerfMap ) - 1 );
		s_szPerfMap[sizeof( s_szPerfMap ) - 1] = 0;
		fprintf( s_pPerf, "%8.1f s map %s\n", gEngfuncs.GetClientTime(), s_szPerfMap );
		s_iPerfSkip = 30;
		s_flPerfAvg = 0;
	}

	// B341: a long time in which many pictures were drawn without a HUD frame (a menu, the inventory, a note, a
	// cutscene or a saved game loading) is not one slow frame: it is written as a pause and left out of the second
	if ( s_iCount[RCC_PICTURES] >= 10 && frame >= 500.0 )
	{
		fprintf( s_pPerf, "PAUSE   %8.3f s %s: %.0f ms without HUD frames, %d pictures (a menu, the inventory, a note, a "
			"cutscene or a load), reads %llu/%llu KB\n", gEngfuncs.GetClientTime(), map, frame, s_iCount[RCC_PICTURES], reads,
			readBytes / 1024 );
		s_flPerfPause += frame;
		s_iPerfSkip = 10;
		return;
	}

	if ( s_iPerfFrames < RC_PERF_TIMES )
		s_flPerfTimes[s_iPerfFrames] = (float)frame;
	s_iPerfFrames++;
	s_flPerfSum += frame;
	s_flPerfBusy += busy < frame ? busy : frame;
	if ( frame > s_flPerfWorst )
		s_flPerfWorst = frame;
	for ( int i = 0; i < RCP_COUNT; i++ )
		s_flPerfCpu[i] += s_llCpu[i] * s_flTickMs;
	for ( int c = 0; c < RCC_COUNT; c++ )
		s_flPerfCount[c] += s_iCount[c];
	s_ullPerfSecReads += reads;
	s_ullPerfSecReadBytes += readBytes;
	s_dwPerfSecFaults += faults;

	if ( s_iPerfSkip > 0 )
	{
		s_iPerfSkip--;
		s_flPerfAvg = s_flPerfAvg > 0 ? s_flPerfAvg * 0.9 + frame * 0.1 : frame;
		return;
	}

	if ( frame >= s_flPerfAvg * 2.0 && frame >= s_flPerfAvg + 10.0 )
	{
		s_iPerfHitches++;

		cl_entity_t *player = gEngfuncs.GetLocalPlayer();
		fprintf( s_pPerf, "STUTTER %8.3f s %s at %.0f %.0f %.0f: frame %.2f ms (average %.2f), main thread running %.2f, reads "
			"%llu/%llu KB, faults %lu |", gEngfuncs.GetClientTime(), map, player ? player->origin[0] : 0.0f,
			player ? player->origin[1] : 0.0f, player ? player->origin[2] : 0.0f, frame, s_flPerfAvg, busy, reads,
			readBytes / 1024, faults );

		char name[32];
		for ( int i = 0; i < RCP_COUNT; i++ )
		{
			double ms = s_llCpu[i] * s_flTickMs;
			if ( ms < 0.5 )
				continue;
			RcPerfName( name, s_szScopeName[i] );
			fprintf( s_pPerf, " %s %.2f", name, ms );
		}
		fprintf( s_pPerf, "\n" );
		fflush( s_pPerf );
	}

	// the recent average, which one slow frame moves only a little
	s_flPerfAvg = s_flPerfAvg * 0.95 + ( frame < s_flPerfAvg * 3.0 ? frame : s_flPerfAvg * 3.0 ) * 0.05;
}

//-----------------------------------------------------------------------------
// the frame boundary, at the start of HUD_Redraw: closes the frame that ends here and opens the next
//-----------------------------------------------------------------------------
void RcProf_Frame( void )
{
	LARGE_INTEGER now;
	QueryPerformanceCounter( &now );

	if ( s_iOn )
	{
		s_llCpu[RcCurrent()] += now.QuadPart - s_llLast;

		double frame = ( now.QuadPart - s_llFrameStart ) * s_flTickMs;
		for ( int i = 0; i < RCP_COUNT; i++ )
			s_flWinCpu[i] += s_llCpu[i] * s_flTickMs;
		for ( int c = 0; c < RCC_COUNT; c++ )
			s_iWinCount[c] += s_iCount[c];

		s_flWinFrame += frame;
		if ( frame > s_flWinWorst )
		{
			s_flWinWorst = frame;
			for ( int i = 0; i < RCP_COUNT; i++ )
				s_flWinWorstCpu[i] = s_llCpu[i] * s_flTickMs;
		}
		s_iWinFrames++;

		if ( s_pPerf )
			RcPerfFrame( frame, now.QuadPart );	// B340

		if ( s_iOn >= 2 && s_iGpuState == 1 )
		{
			RcGpuMark( RCP_OTHER );
			s_Gpu[s_iGpuCur].pending = 1;
		}
	}

	memset( s_llCpu, 0, sizeof( s_llCpu ) );
	memset( s_iCount, 0, sizeof( s_iCount ) );
	s_iDepth = 0;
	s_iOverflow = 0;
	s_llLast = s_llFrameStart = now.QuadPart;

	if ( ( now.QuadPart - s_llWinStart ) * s_flTickMs >= 500 )
	{
		if ( s_iWinFrames )
		{
			for ( int i = 0; i < RCP_COUNT; i++ )
				s_flShowCpu[i] = s_flWinCpu[i] / s_iWinFrames;
			for ( int c = 0; c < RCC_COUNT; c++ )
				s_flShowCount[c] = (double)s_iWinCount[c] / s_iWinFrames;
			s_flShowFrame = s_flWinFrame / s_iWinFrames;
			s_flShowWorst = s_flWinWorst;
			memcpy( s_flShowWorstCpu, s_flWinWorstCpu, sizeof( s_flShowWorstCpu ) );

			if ( s_iOn && rc_prof_log && rc_prof_log->value )
				RcProfLogLine();
		}

		s_iShowGpuValid = s_iWinGpuFrames > 0;
		if ( s_iShowGpuValid )
		{
			for ( int i = 0; i < RCP_COUNT; i++ )
				s_flShowGpu[i] = s_flWinGpu[i] / s_iWinGpuFrames;
			s_flShowGpuFrame = s_flWinGpuFrame / s_iWinGpuFrames;
		}

		RcClearWindow();
		s_llWinStart = now.QuadPart;
	}

	if ( s_pPerf && ( now.QuadPart - s_llPerfSecStart ) * s_flTickMs >= 1000 )
		RcPerfSecond( now.QuadPart );

	int perf = rc_perflog && rc_perflog->value;
	if ( perf && !s_pPerf )
		RcPerfOpen( now.QuadPart );
	else if ( !perf && s_pPerf )
		RcPerfClose();

	int on = rc_prof ? (int)rc_prof->value : 0;
	if ( s_pPerf && on < 2 )
		on = 2;	// B340: the log needs the scopes and the GPU times, without the overlay
	if ( on != s_iOn )
	{
		s_iOn = on;
		RcClearWindow();
		for ( int i = 0; i < RC_GPU_FRAMES; i++ )
			s_Gpu[i].pending = 0;
		s_iShowGpuValid = 0;
		s_flShowFrame = 0;
		s_llWinStart = now.QuadPart;
	}

	if ( s_iOn >= 2 )
	{
		if ( !s_iGpuState )
			RcGpuSetup();

		if ( s_iGpuState == 1 )
		{
			for ( int i = 0; i < RC_GPU_FRAMES; i++ )
			{
				if ( s_Gpu[i].pending )
					RcGpuCollect( &s_Gpu[i] );
			}

			// a frame still in flight when its slot comes round again is dropped rather than waited for
			s_iGpuCur = ( s_iGpuCur + 1 ) % RC_GPU_FRAMES;
			s_Gpu[s_iGpuCur].pending = 0;
			s_Gpu[s_iGpuCur].count = 0;
			RcGpuMark( RCP_OTHER );
		}
	}
}

static void RcDrawLine( int x, int y, const char *text )
{
	gEngfuncs.pfnDrawConsoleString( x, y, (char *)text );
}

//-----------------------------------------------------------------------------
// the overlay, top left (called at the end of CHud::Redraw)
//-----------------------------------------------------------------------------
void RcProf_Draw( void )
{
	if ( !s_iOn || !rc_prof || !rc_prof->value )
		return;

	char text[128];
	int w, h, x = 8, y = 24;

	gEngfuncs.pfnDrawConsoleStringLen( "00000.00", &w, &h );
	int valueWidth = w;
	int nameWidth = 0;
	for ( int i = 0; i < RCP_COUNT; i++ )
	{
		int nw, nh;
		gEngfuncs.pfnDrawConsoleStringLen( s_szScopeName[i], &nw, &nh );
		if ( nw > nameWidth )
			nameWidth = nw;
	}

	int gpu = s_iOn >= 2 && s_iGpuState == 1 && s_iShowGpuValid;

	gEngfuncs.pfnDrawSetTextColor( 1.0, 1.0, 1.0 );
	sprintf( text, "frame %.2f ms (%.0f fps), worst %.2f ms", s_flShowFrame, s_flShowFrame > 0 ? 1000.0 / s_flShowFrame : 0.0, s_flShowWorst );
	RcDrawLine( x, y, text );
	y += h;

	if ( s_iOn >= 2 )
	{
		if ( s_iGpuState == -1 )
			strcpy( text, "gpu: no timer queries on this driver" );
		else if ( !gpu )
			strcpy( text, "gpu: waiting for results" );
		else
			sprintf( text, "gpu frame %.2f ms", s_flShowGpuFrame );
		RcDrawLine( x, y, text );
		y += h;
	}

	sprintf( text, "models %.1f (monsters %.1f), dlights %.1f, world draws %.0f, gpu meshes %.0f", s_flShowCount[RCC_MODELS],
		s_flShowCount[RCC_MONSTERS], s_flShowCount[RCC_DLIGHTS], s_flShowCount[RCC_WORLDDRAWS], s_flShowCount[RCC_GPUMESHES] );
	RcDrawLine( x, y, text );
	y += h;
	sprintf( text, "particles %.0f in %.0f systems", s_flShowCount[RCC_PARTICLES], s_flShowCount[RCC_PSYSTEMS] );	// B341
	RcDrawLine( x, y, text );
	y += h;

	int col1 = x + nameWidth + 8;
	int col2 = col1 + valueWidth + 8;

	gEngfuncs.pfnDrawSetTextColor( 0.7, 0.7, 0.7 );
	RcDrawLine( col1, y, "cpu ms" );
	if ( gpu )
		RcDrawLine( col2, y, "gpu ms" );
	y += h;

	for ( int i = 0; i < RCP_COUNT; i++ )
	{
		if ( s_flShowCpu[i] < 0.005 && ( !gpu || s_flShowGpu[i] < 0.005 ) )
			continue;

		if ( i == RCP_OTHER )
			gEngfuncs.pfnDrawSetTextColor( 0.7, 0.7, 0.7 );
		else
			gEngfuncs.pfnDrawSetTextColor( 1.0, 1.0, 1.0 );

		RcDrawLine( x, y, s_szScopeName[i] );
		sprintf( text, "%.2f", s_flShowCpu[i] );
		RcDrawLine( col1, y, text );
		if ( gpu )
		{
			sprintf( text, "%.2f", s_flShowGpu[i] );
			RcDrawLine( col2, y, text );
		}
		y += h;
	}
}

static void RcProf_Dump( void )
{
	char line[160];

	if ( !s_iOn )
	{
		ConsolePrint( "rc_prof is 0: set rc_prof 1 (2 for GPU times as well) and wait a second\n" );
		return;
	}

	int gpu = s_iOn >= 2 && s_iGpuState == 1 && s_iShowGpuValid;

	sprintf( line, "rc_prof: frame %.2f ms (%.0f fps), worst %.2f ms, models %.1f (monsters %.1f), dlights %.1f, world draws %.0f, gpu meshes %.0f\n",
		s_flShowFrame, s_flShowFrame > 0 ? 1000.0 / s_flShowFrame : 0.0, s_flShowWorst, s_flShowCount[RCC_MODELS], s_flShowCount[RCC_MONSTERS],
		s_flShowCount[RCC_DLIGHTS], s_flShowCount[RCC_WORLDDRAWS], s_flShowCount[RCC_GPUMESHES] );
	ConsolePrint( line );

	if ( gpu )
	{
		sprintf( line, "rc_prof: gpu frame %.2f ms\n", s_flShowGpuFrame );
		ConsolePrint( line );
	}

	for ( int i = 0; i < RCP_COUNT; i++ )
	{
		if ( gpu )
			sprintf( line, "rc_prof: %-12s cpu %6.2f ms  gpu %6.2f ms\n", s_szScopeName[i], s_flShowCpu[i], s_flShowGpu[i] );
		else
			sprintf( line, "rc_prof: %-12s cpu %6.2f ms\n", s_szScopeName[i], s_flShowCpu[i] );
		ConsolePrint( line );
	}
}

void RcProf_Init( void )
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	s_flTickMs = 1000.0 / (double)freq.QuadPart;

	rc_prof = gEngfuncs.pfnRegisterVariable( "rc_prof", "0", 0 );
	rc_prof_log = gEngfuncs.pfnRegisterVariable( "rc_prof_log", "0", 0 );
	rc_perflog = gEngfuncs.pfnRegisterVariable( "rc_perflog", "0", 0 );	// B340
	gEngfuncs.pfnAddCommand( "rc_prof_dump", RcProf_Dump );
}
