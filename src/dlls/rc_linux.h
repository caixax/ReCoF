// ReCoF: the Linux build of the server game DLL (hl.so). tools/build_linux.py includes this first in every unit;
// the Windows build never sees it. It gives the Windows and MSVC names the code uses their POSIX meaning.
#ifndef RC_LINUX_H
#define RC_LINUX_H

#if !defined( _WIN32 )

// The C++ library first: the code's own headers define min and max as macros (extdll.h), which libstdc++'s headers
// cannot take once defined, so everything a unit may include comes before them
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <utility>
#include <vector>
#endif

#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <fnmatch.h>
#include <stdint.h>

#define stricmp		strcasecmp
#define _stricmp	strcasecmp
#define strnicmp	strncasecmp
#define _strnicmp	strncasecmp
#define _snprintf	snprintf
#define _vsnprintf	vsnprintf
#define sprintf_s	snprintf		// sprintf_s( buffer, size, format, ... ): the same arguments
#define _alloca		alloca
#define __cdecl
#define _cdecl
#define __declspec( x )
#define _mkdir( p )	mkdir( ( p ), 0777 )

// MSVC's sized integer keywords (macros, so "unsigned __int32" works too)
#define __int64		long long
#define __int32		int
typedef unsigned int		DWORD;
typedef unsigned short		WORD;

#ifndef min
#define min( a, b )	( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif
#ifndef max
#define max( a, b )	( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#endif

#ifdef __cplusplus
inline char *itoa( int value, char *buffer, int radix )
{
	sprintf( buffer, radix == 16 ? "%x" : "%d", value );
	return buffer;
}

inline char *strlwr( char *s )
{
	for ( char *p = s; *p; p++ )
		*p = (char)tolower( (unsigned char)*p );
	return s;
}

inline char *strupr( char *s )
{
	for ( char *p = s; *p; p++ )
		*p = (char)toupper( (unsigned char)*p );
	return s;
}

// _strtime: the time as "hh:mm:ss" (func_timer)
inline char *_strtime( char *buffer )
{
	time_t now = time( NULL );
	strftime( buffer, 9, "%H:%M:%S", localtime( &now ) );
	return buffer;
}

// GetAsyncKeyState read the keyboard of the machine the server runs on (the cof_keypadnew keypad, skipping the
// developer commentary and cameras). A dedicated server has nobody at its keyboard: no key is ever down.
inline short GetAsyncKeyState( int )
{
	return 0;
}
enum
{
	VK_RETURN = 0x0D, VK_ESCAPE = 0x1B,
	VK_NUMPAD0 = 0x60, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8,
	VK_NUMPAD9
};

// FindFirstFile and friends, for the co-op carry files in txtfiles (multiplay_gamerules.cpp): a directory listing
// filtered by the file name pattern, backslashes taken as slashes. The listing closes itself when it runs out.
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE	( (HANDLE)(intptr_t)-1 )
struct WIN32_FIND_DATA
{
	char	cFileName[260];
};
struct RcFindState
{
	DIR		*dir;
	char	pattern[260];
};

inline void RcSlashes( char *path )
{
	for ( ; *path; path++ )
	{
		if ( *path == '\\' )
			*path = '/';
	}
}

inline int FindNextFile( HANDLE handle, WIN32_FIND_DATA *data )
{
	RcFindState *state = (RcFindState *)handle;
	if ( !state || !state->dir )
		return 0;

	struct dirent *entry;
	while ( ( entry = readdir( state->dir ) ) != NULL )
	{
		if ( fnmatch( state->pattern, entry->d_name, FNM_CASEFOLD ) == 0 )
		{
			strncpy( data->cFileName, entry->d_name, sizeof( data->cFileName ) - 1 );
			data->cFileName[sizeof( data->cFileName ) - 1] = 0;
			return 1;
		}
	}
	closedir( state->dir );
	state->dir = NULL;
	return 0;
}

inline HANDLE FindFirstFile( const char *search, WIN32_FIND_DATA *data )
{
	char path[1024];
	strncpy( path, search, sizeof( path ) - 1 );
	path[sizeof( path ) - 1] = 0;
	RcSlashes( path );

	char *slash = strrchr( path, '/' );
	const char *pattern = slash ? slash + 1 : path;
	const char *folder = ".";
	if ( slash )
	{
		*slash = 0;
		folder = path[0] ? path : "/";
	}

	DIR *dir = opendir( folder );
	if ( !dir )
		return INVALID_HANDLE_VALUE;

	RcFindState *state = new RcFindState;
	state->dir = dir;
	strncpy( state->pattern, pattern, sizeof( state->pattern ) - 1 );
	state->pattern[sizeof( state->pattern ) - 1] = 0;
	if ( !FindNextFile( state, data ) )
	{
		delete state;
		return INVALID_HANDLE_VALUE;
	}
	return state;
}

inline int FindClose( HANDLE handle )
{
	RcFindState *state = (RcFindState *)handle;
	if ( state && state != INVALID_HANDLE_VALUE )
	{
		if ( state->dir )
			closedir( state->dir );
		delete state;
	}
	return 1;
}

inline int DeleteFile( const char *file )
{
	char path[1024];
	strncpy( path, file, sizeof( path ) - 1 );
	path[sizeof( path ) - 1] = 0;
	RcSlashes( path );
	return unlink( path ) == 0;
}
#endif

#endif // !_WIN32

#endif // RC_LINUX_H
