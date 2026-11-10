// ReCoF crash and hang reports (B314)
//
// Retail leaves crashes to Steam's breakpad (the engine registers it in sys_dll2.cpp, the client in InitInput). The
// engine's CEngineAPI::Run catches every crash of the main thread with an SE translator that calls Steam's
// SteamAPI_WriteMiniDump, which writes a minidump into Steam\dumps without a word and leaves the game window frozen
// on its last frame; and nothing notices a game that stops running frames. Here, from the client:
//  - RcCrash_Report, which that translator now calls first (hw.dll sys_dll2.cpp), and an unhandled exception
//    filter for the other threads, installed after the client's SteamAPI_Init so it replaces breakpad's, hand
//    the crash to a reporter thread made at start-up (the crashed thread may have no stack left). The reporter puts
//    the desktop back, writes crashes\crash-<time>.dmp and .txt next to cof.exe, shows the report in a message box
//    (Ctrl+C copies its text) and ends the process;
//  - a watchdog thread writes crashes\hang-<time>.dmp and .txt when the game has run no frame for 30 s and its
//    window stopped answering, and asks whether to close the game or keep waiting.
// The launch option -rc_nocrashhandler leaves the retail behavior. rc_crashtest 1 crashes and rc_crashtest 2 hangs
// the game on purpose, to see the reports.

#include "wrect.h"
#include "cl_dll.h"		// gEngfuncs; nothing else of the client, so the reports depend on as little as can break
#include "rc_client.h"
#include "rc_version.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#define RC_HANG_SECONDS		30			// no frame for this long, and a window that stopped answering, is a hang
#define RC_REPORT_SIZE		32768

typedef BOOL ( WINAPI *MiniDumpWriteDump_t )( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
	PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );
typedef BOOL ( WINAPI *SymInitialize_t )( HANDLE, PCSTR, BOOL );
typedef DWORD ( WINAPI *SymSetOptions_t )( DWORD );
typedef BOOL ( WINAPI *StackWalk64_t )( DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
	PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64 );

static MiniDumpWriteDump_t			s_pfnMiniDumpWriteDump;
static StackWalk64_t				s_pfnStackWalk64;
static PFUNCTION_TABLE_ACCESS_ROUTINE64	s_pfnSymFunctionTableAccess64;
static PGET_MODULE_BASE_ROUTINE64	s_pfnSymGetModuleBase64;

static bool					s_bInstalled;
static char					s_szDir[MAX_PATH];		// <folder of cof.exe>\crashes
static char					s_szGameDir[MAX_PATH];	// the folder of cof.exe, where qconsole.log goes
static char					s_szMap[64];			// the map being played, kept by the frame hook
static DWORD				s_dwMainThread;
static HANDLE				s_hMainThread;
static DWORD				s_dwReporterThread;
static HANDLE				s_hCrashEvent;
static EXCEPTION_POINTERS	*s_pCrashPointers;
static DWORD				s_dwCrashThread;
static volatile LONG		s_lCrashing;
static volatile LONG		s_lFrames;				// one more each client frame
static int					s_iCrashTest;			// rc_crashtest 2 hangs the next frame
static char					s_szReport[RC_REPORT_SIZE];
static char					s_szBox[4096];

//-----------------------------------------------------------------------------
// text helpers; the report is built in a static buffer, since the heap may be what broke
//-----------------------------------------------------------------------------
static void RcAdd( char *pszOut, size_t size, const char *pszFormat, ... )
{
	size_t len = strlen( pszOut );
	if ( len + 1 >= size )
		return;

	va_list argptr;
	va_start( argptr, pszFormat );
	_vsnprintf( pszOut + len, size - len - 1, pszFormat, argptr );
	va_end( argptr );
	pszOut[size - 1] = '\0';
}

static const char *RcExceptionName( DWORD dwCode )
{
	switch ( dwCode )
	{
	case EXCEPTION_ACCESS_VIOLATION:		return "Access violation";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:	return "Array bounds exceeded";
	case EXCEPTION_DATATYPE_MISALIGNMENT:	return "Datatype misalignment";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:		return "Float divide by zero";
	case EXCEPTION_FLT_INVALID_OPERATION:	return "Float invalid operation";
	case EXCEPTION_FLT_OVERFLOW:			return "Float overflow";
	case EXCEPTION_FLT_STACK_CHECK:			return "Float stack check";
	case EXCEPTION_ILLEGAL_INSTRUCTION:		return "Illegal instruction";
	case EXCEPTION_IN_PAGE_ERROR:			return "In page error";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:		return "Integer divide by zero";
	case EXCEPTION_INT_OVERFLOW:			return "Integer overflow";
	case EXCEPTION_PRIV_INSTRUCTION:		return "Privileged instruction";
	case EXCEPTION_STACK_OVERFLOW:			return "Stack overflow";
	case 0xC0000409:						return "Stack buffer overrun";
	case 0xE06D7363:						return "C++ exception";
	}

	return "Exception";
}

// "client.dll+0x1A2B3" for an address in a loaded module, or the bare address
static void RcAddressName( DWORD dwAddress, char *pszOut, size_t size )
{
	HMODULE hModule = NULL;
	char szPath[MAX_PATH];

	if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)dwAddress, &hModule ) &&
		GetModuleFileNameA( hModule, szPath, sizeof( szPath ) ) )
	{
		const char *pszName = strrchr( szPath, '\\' );
		_snprintf( pszOut, size, "%s+0x%X", pszName ? pszName + 1 : szPath, dwAddress - (DWORD)hModule );
	}
	else
	{
		_snprintf( pszOut, size, "0x%08X", dwAddress );
	}

	pszOut[size - 1] = '\0';
}

// an address inside the code of a loaded module: candidate return addresses on a stack
static bool RcIsCode( DWORD dwAddress )
{
	MEMORY_BASIC_INFORMATION mbi;

	if ( !VirtualQuery( (LPCVOID)dwAddress, &mbi, sizeof( mbi ) ) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE )
		return false;

	return ( mbi.Protect & ( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) != 0;
}

static void RcAddStack( const CONTEXT *pContext, HANDLE hThread )
{
	char szName[MAX_PATH];

	RcAdd( s_szReport, sizeof( s_szReport ), "\r\nStack:\r\n" );

	if ( s_pfnStackWalk64 )
	{
		CONTEXT context = *pContext;
		STACKFRAME64 frame;
		memset( &frame, 0, sizeof( frame ) );
		frame.AddrPC.Offset = context.Eip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = context.Ebp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = context.Esp;
		frame.AddrStack.Mode = AddrModeFlat;

		for ( int i = 0; i < 40; i++ )
		{
			if ( !s_pfnStackWalk64( IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), hThread, &frame, &context, NULL,
				s_pfnSymFunctionTableAccess64, s_pfnSymGetModuleBase64, NULL ) || !frame.AddrPC.Offset )
				break;

			RcAddressName( (DWORD)frame.AddrPC.Offset, szName, sizeof( szName ) );
			RcAdd( s_szReport, sizeof( s_szReport ), "  %s\r\n", szName );
		}
	}

	// code built without frame pointers breaks the walk: list what looks like return addresses on the stack too
	RcAdd( s_szReport, sizeof( s_szReport ), "\r\nCode addresses on the stack:\r\n" );

	DWORD stack[1024];
	SIZE_T read = 0;
	ReadProcessMemory( GetCurrentProcess(), (LPCVOID)pContext->Esp, stack, sizeof( stack ), &read );

	int found = 0;
	for ( SIZE_T i = 0; i < read / sizeof( DWORD ) && found < 40; i++ )
	{
		if ( !RcIsCode( stack[i] ) )
			continue;

		RcAddressName( stack[i], szName, sizeof( szName ) );
		RcAdd( s_szReport, sizeof( s_szReport ), "  esp+0x%03X  %s\r\n", (unsigned)( i * 4 ), szName );
		found++;
	}
}

static void RcAddModules( void )
{
	RcAdd( s_szReport, sizeof( s_szReport ), "\r\nModules:\r\n" );

	HANDLE hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, GetCurrentProcessId() );
	if ( hSnap == INVALID_HANDLE_VALUE )
		return;

	MODULEENTRY32 me;
	me.dwSize = sizeof( me );
	for ( BOOL ok = Module32First( hSnap, &me ); ok; ok = Module32Next( hSnap, &me ) )
	{
		// the link time stamp tells the exact build of a module
		DWORD dwStamp = 0;
		IMAGE_DOS_HEADER *pDos = (IMAGE_DOS_HEADER *)me.modBaseAddr;
		if ( pDos->e_magic == IMAGE_DOS_SIGNATURE )
			dwStamp = ( (IMAGE_NT_HEADERS *)( me.modBaseAddr + pDos->e_lfanew ) )->FileHeader.TimeDateStamp;

		RcAdd( s_szReport, sizeof( s_szReport ), "  %08X %8X %08X  %s\r\n", (DWORD)me.modBaseAddr, me.modBaseSize, dwStamp, me.szExePath );
	}

	CloseHandle( hSnap );
}

// the last lines of qconsole.log, written next to cof.exe with -condebug (Play ReCoF.bat uses it)
static void RcAddConsole( void )
{
	char szPath[MAX_PATH];
	_snprintf( szPath, sizeof( szPath ), "%s\\qconsole.log", s_szGameDir );
	szPath[sizeof( szPath ) - 1] = '\0';

	HANDLE hFile = CreateFileA( szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return;

	static char s_szTail[4097];
	DWORD dwSize = GetFileSize( hFile, NULL );
	DWORD dwRead = 0;
	if ( dwSize != INVALID_FILE_SIZE )
	{
		SetFilePointer( hFile, dwSize > 4096 ? dwSize - 4096 : 0, NULL, FILE_BEGIN );
		ReadFile( hFile, s_szTail, 4096, &dwRead, NULL );
	}
	CloseHandle( hFile );
	s_szTail[dwRead] = '\0';

	// start on a whole line
	const char *pszStart = s_szTail;
	if ( dwSize > 4096 )
	{
		const char *pszLine = strchr( s_szTail, '\n' );
		pszStart = pszLine ? pszLine + 1 : s_szTail;
	}

	RcAdd( s_szReport, sizeof( s_szReport ), "\r\nLast lines of qconsole.log:\r\n%s\r\n", pszStart );
}

//-----------------------------------------------------------------------------
// files and the message box
//-----------------------------------------------------------------------------
static void RcFileName( char *pszOut, size_t size, const char *pszKind, const char *pszExt )
{
	SYSTEMTIME st;
	GetLocalTime( &st );
	_snprintf( pszOut, size, "%s\\%s-%04d-%02d-%02d_%02d-%02d-%02d.%s", s_szDir, pszKind, st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, pszExt );
	pszOut[size - 1] = '\0';
}

static bool RcWriteDump( const char *pszPath, EXCEPTION_POINTERS *pPointers, DWORD dwThread )
{
	if ( !s_pfnMiniDumpWriteDump )
		return false;

	HANDLE hFile = CreateFileA( pszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return false;

	MINIDUMP_EXCEPTION_INFORMATION mei;
	mei.ThreadId = dwThread;
	mei.ExceptionPointers = pPointers;
	mei.ClientPointers = FALSE;

	MINIDUMP_TYPE type = (MINIDUMP_TYPE)( MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo |
		MiniDumpWithUnloadedModules );
	BOOL ok = s_pfnMiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), hFile, type, pPointers ? &mei : NULL, NULL, NULL );
	CloseHandle( hFile );
	return ok != FALSE;
}

static void RcWriteText( const char *pszPath )
{
	HANDLE hFile = CreateFileA( pszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return;

	DWORD dwWritten;
	WriteFile( hFile, s_szReport, (DWORD)strlen( s_szReport ), &dwWritten, NULL );
	CloseHandle( hFile );
}

// the game's window, which covers the screen in full screen, and the display mode it set
static BOOL CALLBACK RcFindWindow( HWND hWnd, LPARAM lParam )
{
	if ( IsWindowVisible( hWnd ) && !GetWindow( hWnd, GW_OWNER ) )
	{
		*(HWND *)lParam = hWnd;
		return FALSE;
	}

	return TRUE;
}

static HWND RcMainWindow( void )
{
	HWND hWnd = NULL;
	EnumThreadWindows( s_dwMainThread, RcFindWindow, (LPARAM)&hWnd );
	return hWnd;
}

// before the message box: the cursor out of the game's window and, for a crash, the desktop's display mode back.
// A hang may still go on (the player can keep waiting), so its display mode stays
static void RcFreeTheScreen( bool bCrash )
{
	ClipCursor( NULL );

	if ( !bCrash )
		return;

	ChangeDisplaySettingsA( NULL, 0 );

	HWND hWnd = RcMainWindow();
	if ( hWnd )
		ShowWindowAsync( hWnd, SW_MINIMIZE );	// async: the window's thread is the one that crashed
}

static void RcOpenFolder( const char *pszFile )
{
	char szCommand[MAX_PATH * 2];
	_snprintf( szCommand, sizeof( szCommand ), "explorer.exe /select,\"%s\"", pszFile );
	szCommand[sizeof( szCommand ) - 1] = '\0';

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	if ( CreateProcessA( NULL, szCommand, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) )
	{
		CloseHandle( pi.hThread );
		CloseHandle( pi.hProcess );
	}
}

// the report: header, place, registers, stack, modules, console tail
static void RcBuildReport( bool bHang, EXCEPTION_POINTERS *pPointers, const CONTEXT *pContext, HANDLE hThread, char *pszWhere, size_t whereSize )
{
	SYSTEMTIME st;
	GetLocalTime( &st );

	s_szReport[0] = '\0';
	RcAdd( s_szReport, sizeof( s_szReport ), "Cry of Fear %s (ReCoF %s)\r\n", bHang ? "stopped responding" : "crashed", RECOF_VERSION );
	RcAdd( s_szReport, sizeof( s_szReport ), "Time: %04d-%02d-%02d %02d:%02d:%02d\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
	RcAdd( s_szReport, sizeof( s_szReport ), "Map: %s\r\n", s_szMap[0] ? s_szMap : "(none)" );

	pszWhere[0] = '\0';
	if ( bHang )
	{
		RcAdd( s_szReport, sizeof( s_szReport ), "No frame for %d s and the window stopped answering.\r\n", RC_HANG_SECONDS );
		RcAddressName( pContext->Eip, pszWhere, whereSize );
		RcAdd( s_szReport, sizeof( s_szReport ), "Main thread at: %s\r\n", pszWhere );
	}
	else
	{
		EXCEPTION_RECORD *pRecord = pPointers->ExceptionRecord;
		RcAddressName( (DWORD)pRecord->ExceptionAddress, pszWhere, whereSize );
		RcAdd( s_szReport, sizeof( s_szReport ), "%s (0x%08X) at %s\r\n", RcExceptionName( pRecord->ExceptionCode ), pRecord->ExceptionCode, pszWhere );

		if ( pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRecord->NumberParameters >= 2 )
		{
			const char *pszOp = pRecord->ExceptionInformation[0] == 0 ? "reading" : ( pRecord->ExceptionInformation[0] == 8 ? "running" : "writing" );
			RcAdd( s_szReport, sizeof( s_szReport ), "%s address 0x%08X\r\n", pszOp, (DWORD)pRecord->ExceptionInformation[1] );
		}

		RcAdd( s_szReport, sizeof( s_szReport ), "Thread: %u%s\r\n", s_dwCrashThread, s_dwCrashThread == s_dwMainThread ? " (main)" : "" );
	}

	RcAdd( s_szReport, sizeof( s_szReport ), "\r\nRegisters:\r\n  eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X\r\n  eip=%08X esp=%08X ebp=%08X efl=%08X\r\n",
		pContext->Eax, pContext->Ebx, pContext->Ecx, pContext->Edx, pContext->Esi, pContext->Edi, pContext->Eip, pContext->Esp, pContext->Ebp, pContext->EFlags );

	RcAddStack( pContext, hThread );
	RcAddModules();
	RcAddConsole();
}

// the message box: the place and the first frames, and where the files are; Ctrl+C in a message box copies its text
static void RcBoxText( bool bHang, const char *pszWhere, const char *pszTxt, const char *pszDmp, bool bDump )
{
	s_szBox[0] = '\0';

	if ( bHang )
		RcAdd( s_szBox, sizeof( s_szBox ), "Cry of Fear has run no frame for %d seconds and its window stopped answering (ReCoF %s).\r\n\r\nThe game is at %s\r\n", RC_HANG_SECONDS, RECOF_VERSION, pszWhere );
	else
		RcAdd( s_szBox, sizeof( s_szBox ), "Cry of Fear crashed (ReCoF %s).\r\n\r\n", RECOF_VERSION );

	// the first lines of the report after its header: the exception and the stack
	const char *pszStack = strstr( s_szReport, "\r\nStack:\r\n" );
	if ( !bHang )
	{
		const char *pszLine = strstr( s_szReport, "Map: " );
		const char *pszEnd = strstr( s_szReport, "\r\nRegisters:" );
		if ( pszLine && pszEnd && pszEnd > pszLine )
			RcAdd( s_szBox, sizeof( s_szBox ), "%.*s\r\n", (int)( pszEnd - pszLine ), pszLine );
	}

	if ( pszStack )
	{
		// the walked stack, and the code addresses found on the stack when the walk stops early
		int lines = 0;
		const char *p = pszStack + 2;
		const char *pszStop = strstr( p, "\r\nModules:" );
		while ( *p && lines < 16 && ( !pszStop || p < pszStop ) )
		{
			const char *pszNext = strstr( p, "\r\n" );
			if ( !pszNext )
				break;
			RcAdd( s_szBox, sizeof( s_szBox ), "%.*s\r\n", (int)( pszNext - p ), p );
			p = pszNext + 2;
			lines++;
		}
	}

	RcAdd( s_szBox, sizeof( s_szBox ), "\r\nThe full report%s were saved:\r\n%s\r\n", bDump ? " and a memory dump" : "", pszTxt );
	if ( bDump )
		RcAdd( s_szBox, sizeof( s_szBox ), "%s\r\n", pszDmp );
	RcAdd( s_szBox, sizeof( s_szBox ), "\r\nPress Ctrl+C to copy this text.\r\n" );
}

//-----------------------------------------------------------------------------
// crash
//-----------------------------------------------------------------------------
static void RcReportCrash( void )
{
	char szDmp[MAX_PATH], szTxt[MAX_PATH], szWhere[MAX_PATH];

	CreateDirectoryA( s_szDir, NULL );
	RcFileName( szDmp, sizeof( szDmp ), "crash", "dmp" );
	RcFileName( szTxt, sizeof( szTxt ), "crash", "txt" );

	// the dump first: it needs nothing from the heap, which the crash may have broken
	bool bDump = RcWriteDump( szDmp, s_pCrashPointers, s_dwCrashThread );

	HANDLE hThread = OpenThread( THREAD_ALL_ACCESS, FALSE, s_dwCrashThread );
	RcBuildReport( false, s_pCrashPointers, s_pCrashPointers->ContextRecord, hThread ? hThread : GetCurrentThread(), szWhere, sizeof( szWhere ) );
	if ( hThread )
		CloseHandle( hThread );

	RcWriteText( szTxt );
	RcFreeTheScreen( true );
	RcBoxText( false, szWhere, szTxt, szDmp, bDump );
	RcAdd( s_szBox, sizeof( s_szBox ), "\r\nOpen the folder with the report?" );

	if ( MessageBoxA( NULL, s_szBox, "Cry of Fear crashed", MB_ICONERROR | MB_YESNO | MB_TOPMOST | MB_SETFOREGROUND ) == IDYES )
		RcOpenFolder( szTxt );
}

static DWORD WINAPI RcReporterThread( LPVOID )
{
	WaitForSingleObject( s_hCrashEvent, INFINITE );
	RcReportCrash();
	TerminateProcess( GetCurrentProcess(), s_pCrashPointers->ExceptionRecord->ExceptionCode );
	return 0;
}

// hands a crash to the reporter thread and never returns; the reporter ends the process
static void RcCrashTake( EXCEPTION_POINTERS *pPointers )
{
	// the reporter itself crashed: end with what was written
	if ( GetCurrentThreadId() == s_dwReporterThread )
		TerminateProcess( GetCurrentProcess(), pPointers->ExceptionRecord->ExceptionCode );

	// a second crash on another thread waits for the first report
	if ( InterlockedExchange( &s_lCrashing, 1 ) )
		Sleep( INFINITE );

	// this thread may be out of stack: the reporter thread does the work
	s_pCrashPointers = pPointers;
	s_dwCrashThread = GetCurrentThreadId();
	SetEvent( s_hCrashEvent );
	Sleep( INFINITE );
}

// crashes of every thread but the main one, whose crashes the engine catches first (RcCrash_Report)
static LONG WINAPI RcCrashFilter( EXCEPTION_POINTERS *pPointers )
{
	if ( IsDebuggerPresent() )
		return EXCEPTION_CONTINUE_SEARCH;

	RcCrashTake( pPointers );
	return EXCEPTION_EXECUTE_HANDLER;
}

// crashes of the main thread: CEngineAPI::Run (hw.dll sys_dll2.cpp) catches them with an SE translator, which
// calls this when it is exported and returns to Steam's crash handler only when the reports are off
extern "C" __declspec( dllexport ) void RcCrash_Report( EXCEPTION_POINTERS *pPointers )
{
	if ( !s_bInstalled || IsDebuggerPresent() )
		return;

	RcCrashTake( pPointers );
}

//-----------------------------------------------------------------------------
// hang
//-----------------------------------------------------------------------------
static void RcReportHang( void )
{
	char szDmp[MAX_PATH], szTxt[MAX_PATH], szWhere[MAX_PATH];

	CreateDirectoryA( s_szDir, NULL );
	RcFileName( szDmp, sizeof( szDmp ), "hang", "dmp" );
	RcFileName( szTxt, sizeof( szTxt ), "hang", "txt" );

	bool bDump = RcWriteDump( szDmp, NULL, 0 );

	// the main thread's place and stack, read while it is held still
	CONTEXT context;
	memset( &context, 0, sizeof( context ) );
	context.ContextFlags = CONTEXT_FULL;
	SuspendThread( s_hMainThread );
	GetThreadContext( s_hMainThread, &context );
	RcBuildReport( true, NULL, &context, s_hMainThread, szWhere, sizeof( szWhere ) );
	ResumeThread( s_hMainThread );

	RcWriteText( szTxt );
	RcFreeTheScreen( false );
	RcBoxText( true, szWhere, szTxt, szDmp, bDump );
	RcAdd( s_szBox, sizeof( s_szBox ), "\r\nClose the game? No keeps waiting." );

	if ( MessageBoxA( NULL, s_szBox, "Cry of Fear is not responding", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND ) == IDYES )
		TerminateProcess( GetCurrentProcess(), 0xDEAD );
}

static DWORD WINAPI RcWatchdogThread( LPVOID )
{
	LONG lLast = 0;
	DWORD dwSince = GetTickCount();
	bool bReported = false;

	for ( ;; )
	{
		Sleep( 1000 );

		// keep this handler in place: anything that registers another one later (a Steam crash handler) loses it
		if ( SetUnhandledExceptionFilter( RcCrashFilter ) != RcCrashFilter )
			OutputDebugStringA( "ReCoF: the crash handler was replaced and is in place again\n" );

		LONG lFrames = s_lFrames;
		if ( lFrames != lLast )
		{
			lLast = lFrames;
			dwSince = GetTickCount();
			bReported = false;
			continue;
		}

		if ( bReported || s_lCrashing || !lFrames || GetTickCount() - dwSince < RC_HANG_SECONDS * 1000 )
			continue;

		// a long map load still answers its window; a modal error box (Sys_Error) answers too
		HWND hWnd = RcMainWindow();
		if ( !hWnd || !IsHungAppWindow( hWnd ) )
			continue;

		bReported = true;
		RcReportHang();
	}

	return 0;
}

//-----------------------------------------------------------------------------
// rc_crashtest 1 crashes now, rc_crashtest 2 hangs the next frame: to see the reports
//-----------------------------------------------------------------------------
static void RcCrashTest_f( void )
{
	int iMode = atoi( gEngfuncs.Cmd_Argv( 1 ) );

	if ( iMode == 1 )
	{
		gEngfuncs.Con_Printf( "rc_crashtest: crashing on purpose\n" );
		*(volatile int *)0x10 = 1;
	}
	else if ( iMode == 2 )
	{
		gEngfuncs.Con_Printf( "rc_crashtest: the game stops responding on purpose; the report comes after %d s\n", RC_HANG_SECONDS );
		s_iCrashTest = 2;
	}
	else
	{
		gEngfuncs.Con_Printf( "rc_crashtest 1: crash the game on purpose; rc_crashtest 2: make it stop responding. Reports go to %s\n", s_bInstalled ? s_szDir : "nowhere (-rc_nocrashhandler)" );
	}
}

//-----------------------------------------------------------------------------
// entry points
//-----------------------------------------------------------------------------

// after the client's SteamAPI_Init (InitInput), so this filter replaces the Steam crash handler
void RcCrash_Init( void )
{
	gEngfuncs.pfnAddCommand( "rc_crashtest", RcCrashTest_f );

	if ( gEngfuncs.CheckParm( (char *)"-rc_nocrashhandler", NULL ) )
		return;

	HMODULE hDbgHelp = LoadLibraryA( "dbghelp.dll" );
	if ( hDbgHelp )
	{
		s_pfnMiniDumpWriteDump = (MiniDumpWriteDump_t)GetProcAddress( hDbgHelp, "MiniDumpWriteDump" );
		s_pfnStackWalk64 = (StackWalk64_t)GetProcAddress( hDbgHelp, "StackWalk64" );
		s_pfnSymFunctionTableAccess64 = (PFUNCTION_TABLE_ACCESS_ROUTINE64)GetProcAddress( hDbgHelp, "SymFunctionTableAccess64" );
		s_pfnSymGetModuleBase64 = (PGET_MODULE_BASE_ROUTINE64)GetProcAddress( hDbgHelp, "SymGetModuleBase64" );

		SymSetOptions_t pfnSymSetOptions = (SymSetOptions_t)GetProcAddress( hDbgHelp, "SymSetOptions" );
		SymInitialize_t pfnSymInitialize = (SymInitialize_t)GetProcAddress( hDbgHelp, "SymInitialize" );
		if ( pfnSymSetOptions && pfnSymInitialize )
		{
			pfnSymSetOptions( SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS );
			if ( !pfnSymInitialize( GetCurrentProcess(), NULL, TRUE ) )
				s_pfnStackWalk64 = NULL;	// the walk needs the module list; the stack scan still runs
		}
		else
		{
			s_pfnStackWalk64 = NULL;
		}
	}

	// the reports go next to cof.exe, where qconsole.log is
	GetModuleFileNameA( NULL, s_szGameDir, sizeof( s_szGameDir ) );
	char *pszSlash = strrchr( s_szGameDir, '\\' );
	if ( pszSlash )
		*pszSlash = '\0';
	_snprintf( s_szDir, sizeof( s_szDir ), "%s\\crashes", s_szGameDir );
	s_szDir[sizeof( s_szDir ) - 1] = '\0';

	s_dwMainThread = GetCurrentThreadId();
	DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_hMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS );

	s_hCrashEvent = CreateEventA( NULL, FALSE, FALSE, NULL );
	HANDLE hReporter = CreateThread( NULL, 256 * 1024, RcReporterThread, NULL, 0, &s_dwReporterThread );
	HANDLE hWatchdog = CreateThread( NULL, 256 * 1024, RcWatchdogThread, NULL, 0, NULL );
	if ( !s_hCrashEvent || !hReporter || !hWatchdog )
		return;

	CloseHandle( hReporter );
	CloseHandle( hWatchdog );

	SetUnhandledExceptionFilter( RcCrashFilter );
	s_bInstalled = true;
	gEngfuncs.Con_DPrintf( "Crash and hang reports go to %s\n", s_szDir );
}

// every client frame (HUD_Frame): the watchdog's heartbeat, and the map for the reports
void RcCrash_Frame( void )
{
	if ( s_iCrashTest == 2 )
	{
		for ( ;; )
			Sleep( 100 );
	}

	InterlockedIncrement( &s_lFrames );

	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	if ( pszLevel && strcmp( pszLevel, s_szMap ) )
	{
		strncpy( s_szMap, pszLevel, sizeof( s_szMap ) - 1 );
		s_szMap[sizeof( s_szMap ) - 1] = '\0';
	}
}
