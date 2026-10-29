// ReCoF frame profiler
//
// rc_prof 1 shows where the client spends each frame: the CPU time of each phase (rc_prof.h), averaged over
// half a second. rc_prof 2 adds the GPU time of the same phases from timestamp queries (GL_ARB_timer_query),
// read back a few frames later so the game never waits for the GPU. rc_prof_dump prints the numbers to the
// console (with -condebug they also land in qconsole.log). Everything outside the client scopes, which
// includes the engine, the server game logic of hl.dll, sound and the buffer swap, is "other";
// host_speeds 1 splits that part.

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
	"other", "view", "entities", "client frame", "world", "world 1st", "bump", "dlights", "world 2nd",
	"specular", "brush ents", "decals", "sky", "models", "mdl vertices", "mdl lighting", "mdl drawing",
	"transparent", "water", "post fx", "hud"
};

// the scopes that issue GL work get their own GPU time; the GPU time of the others falls to "other"
static const bool s_bGLScope[RCP_COUNT] =
{
	false, false, false, false, true, true, true, true, true,
	true, true, true, true, true, false, false, false, true, true, true, true
};

static cvar_t		*rc_prof;
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
	if ( cur == RCP_MDL_VERTS || cur == RCP_MDL_LIGHT || cur == RCP_MDL_DRAW )
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
			s_flWinWorst = frame;
		s_iWinFrames++;

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

	int on = rc_prof ? (int)rc_prof->value : 0;
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
	if ( !s_iOn )
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

	sprintf( text, "models %.1f, dlights %.1f, world draws %.0f", s_flShowCount[RCC_MODELS], s_flShowCount[RCC_DLIGHTS], s_flShowCount[RCC_WORLDDRAWS] );
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

	sprintf( line, "rc_prof: frame %.2f ms (%.0f fps), worst %.2f ms, models %.1f, dlights %.1f, world draws %.0f\n", s_flShowFrame,
		s_flShowFrame > 0 ? 1000.0 / s_flShowFrame : 0.0, s_flShowWorst, s_flShowCount[RCC_MODELS], s_flShowCount[RCC_DLIGHTS],
		s_flShowCount[RCC_WORLDDRAWS] );
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
	gEngfuncs.pfnAddCommand( "rc_prof_dump", RcProf_Dump );
}
