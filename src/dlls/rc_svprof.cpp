// ReCoF server profiler (B337)
//
// rc_svprof 1 times the server game code of hl.dll and the engine functions it calls, and prints one line a
// second to the console (with -condebug it also lands in qconsole.log): for each scope (rc_svprof.h) the time
// it took in each frame on its own and with what it called, and its calls in each frame. rc_svprof_dump
// prints the last second as a table. The engine's own share of the server frame (the physics of entities,
// the player's movement, sending entities to the client) is outside hl.dll and not timed here; rc_prof in
// client.dll times the whole server frame from the client's side ("server").
//
// It only observes: while it is on, the engine functions in hl.dll's copy of the engine's table
// (g_engfuncs) are wrappers that time the call and pass it on unchanged, and they are put back when it is
// turned off. Nothing it does changes what the game does.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "rc_svprof.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <time.h>
#endif

#define RCSV_STACK		64

int g_iRcSvProf;

static cvar_t rc_svprof = { "rc_svprof", "0" };

static const char *s_szRcSvName[RCSV_COUNT] =
{
	"other", "player", "desired", "startframe", "think", "touch", "fullpack",
	"mthink", "runai", "look", "listen", "entsinbox", "localmove", "route", "cover", "move", "moveexec",
	"e_traceline", "e_tracehull", "e_monsterhull", "e_tracemodel", "e_tracetoss", "e_texture",
	"e_walkmove", "e_moveto", "e_droptofloor", "e_setorigin", "e_sphere", "e_findstring", "e_pvs",
	"e_contents", "e_sound", "e_bones"
};

static double	s_flTickMs;
static int		s_iStack[RCSV_STACK];
static double	s_flStart[RCSV_STACK];		// when each scope on the stack opened
static int		s_iDepth;
static int		s_iOverflow;
static int		s_iOpen[RCSV_COUNT];		// times each scope is on the stack, so recursion is timed once
static double	s_flLast;					// the last scope change

// the second being collected
static double	s_flWinStart;
static int		s_iWinFrames;
static double	s_flWinExcl[RCSV_COUNT], s_flWinIncl[RCSV_COUNT];
static int		s_iWinCalls[RCSV_COUNT];

// the last full second
static int		s_iShowFrames;
static double	s_flShowSeconds;
static double	s_flShowExcl[RCSV_COUNT], s_flShowIncl[RCSV_COUNT];
static int		s_iShowCalls[RCSV_COUNT];
static int		s_iShowMonsters, s_iShowCombat, s_iShowEdicts, s_iShowHighest;

static double RcSvNow( void )
{
#ifdef _WIN32
	LARGE_INTEGER c;
	QueryPerformanceCounter( &c );
	return (double)c.QuadPart * s_flTickMs;
#else
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

void RcSvProf_Push( int scope )
{
	if ( s_iDepth >= RCSV_STACK )
	{
		s_iOverflow++;
		return;
	}

	double now = RcSvNow();

	if ( s_iDepth )
		s_flWinExcl[s_iStack[s_iDepth - 1]] += now - s_flLast;
	s_flLast = now;

	s_iStack[s_iDepth] = scope;
	s_flStart[s_iDepth] = now;
	s_iDepth++;
	s_iOpen[scope]++;
	s_iWinCalls[scope]++;
}

void RcSvProf_Pop( void )
{
	if ( s_iOverflow )
	{
		s_iOverflow--;
		return;
	}

	if ( !s_iDepth )
		return;

	double now = RcSvNow();
	int scope = s_iStack[--s_iDepth];

	s_flWinExcl[scope] += now - s_flLast;
	s_flLast = now;

	if ( --s_iOpen[scope] == 0 )
		s_flWinIncl[scope] += now - s_flStart[s_iDepth];
}

//-----------------------------------------------------------------------------
// the engine functions, timed while rc_svprof is on
//-----------------------------------------------------------------------------
static enginefuncs_t s_RcSvEngine;	// the engine's own functions, saved while the wrappers are in g_engfuncs
static int s_bRcSvHooked;

static void RcTraceLine( const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr )
{
	RcSvScope s( RCSV_E_TRACELINE );
	s_RcSvEngine.pfnTraceLine( v1, v2, fNoMonsters, pentToSkip, ptr );
}

static void RcTraceToss( edict_t *pent, edict_t *pentToIgnore, TraceResult *ptr )
{
	RcSvScope s( RCSV_E_TRACETOSS );
	s_RcSvEngine.pfnTraceToss( pent, pentToIgnore, ptr );
}

static int RcTraceMonsterHull( edict_t *pEdict, const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr )
{
	RcSvScope s( RCSV_E_MONSTERHULL );
	return s_RcSvEngine.pfnTraceMonsterHull( pEdict, v1, v2, fNoMonsters, pentToSkip, ptr );
}

static void RcTraceHull( const float *v1, const float *v2, int fNoMonsters, int hullNumber, edict_t *pentToSkip, TraceResult *ptr )
{
	RcSvScope s( RCSV_E_TRACEHULL );
	s_RcSvEngine.pfnTraceHull( v1, v2, fNoMonsters, hullNumber, pentToSkip, ptr );
}

static void RcTraceModel( const float *v1, const float *v2, int hullNumber, edict_t *pent, TraceResult *ptr )
{
	RcSvScope s( RCSV_E_TRACEMODEL );
	s_RcSvEngine.pfnTraceModel( v1, v2, hullNumber, pent, ptr );
}

static const char *RcTraceTexture( edict_t *pTextureEntity, const float *v1, const float *v2 )
{
	RcSvScope s( RCSV_E_TEXTURE );
	return s_RcSvEngine.pfnTraceTexture( pTextureEntity, v1, v2 );
}

static int RcWalkMove( edict_t *ent, float yaw, float dist, int iMode )
{
	RcSvScope s( RCSV_E_WALKMOVE );
	return s_RcSvEngine.pfnWalkMove( ent, yaw, dist, iMode );
}

static void RcMoveToOrigin( edict_t *ent, const float *pflGoal, float dist, int iMoveType )
{
	RcSvScope s( RCSV_E_MOVETO );
	s_RcSvEngine.pfnMoveToOrigin( ent, pflGoal, dist, iMoveType );
}

static int RcDropToFloor( edict_t *e )
{
	RcSvScope s( RCSV_E_DROPTOFLOOR );
	return s_RcSvEngine.pfnDropToFloor( e );
}

static void RcSetOrigin( edict_t *e, const float *rgflOrigin )
{
	RcSvScope s( RCSV_E_SETORIGIN );
	s_RcSvEngine.pfnSetOrigin( e, rgflOrigin );
}

static edict_t *RcFindEntityInSphere( edict_t *pEdictStartSearchAfter, const float *org, float rad )
{
	RcSvScope s( RCSV_E_SPHERE );
	return s_RcSvEngine.pfnFindEntityInSphere( pEdictStartSearchAfter, org, rad );
}

static edict_t *RcFindEntityByString( edict_t *pEdictStartSearchAfter, const char *pszField, const char *pszValue )
{
	RcSvScope s( RCSV_E_FINDSTRING );
	return s_RcSvEngine.pfnFindEntityByString( pEdictStartSearchAfter, pszField, pszValue );
}

static edict_t *RcFindClientInPVS( edict_t *pEdict )
{
	RcSvScope s( RCSV_E_PVS );
	return s_RcSvEngine.pfnFindClientInPVS( pEdict );
}

static edict_t *RcEntitiesInPVS( edict_t *pplayer )
{
	RcSvScope s( RCSV_E_PVS );
	return s_RcSvEngine.pfnEntitiesInPVS( pplayer );
}

static int RcCheckVisibility( const edict_t *entity, unsigned char *pset )
{
	RcSvScope s( RCSV_E_PVS );
	return s_RcSvEngine.pfnCheckVisibility( entity, pset );
}

static int RcPointContents( const float *rgflVector )
{
	RcSvScope s( RCSV_E_CONTENTS );
	return s_RcSvEngine.pfnPointContents( rgflVector );
}

static void RcEmitSound( edict_t *entity, int channel, const char *sample, float volume, float attenuation, int fFlags, int pitch )
{
	RcSvScope s( RCSV_E_SOUND );
	s_RcSvEngine.pfnEmitSound( entity, channel, sample, volume, attenuation, fFlags, pitch );
}

static void RcGetBonePosition( const edict_t *pEdict, int iBone, float *rgflOrigin, float *rgflAngles )
{
	RcSvScope s( RCSV_E_BONES );
	s_RcSvEngine.pfnGetBonePosition( pEdict, iBone, rgflOrigin, rgflAngles );
}

static void RcGetAttachment( const edict_t *pEdict, int iAttachment, float *rgflOrigin, float *rgflAngles )
{
	RcSvScope s( RCSV_E_BONES );
	s_RcSvEngine.pfnGetAttachment( pEdict, iAttachment, rgflOrigin, rgflAngles );
}

static void RcSvHook( int on )
{
	if ( on == s_bRcSvHooked )
		return;

	if ( on )
	{
		s_RcSvEngine = g_engfuncs;
		g_engfuncs.pfnTraceLine = RcTraceLine;
		g_engfuncs.pfnTraceToss = RcTraceToss;
		g_engfuncs.pfnTraceMonsterHull = RcTraceMonsterHull;
		g_engfuncs.pfnTraceHull = RcTraceHull;
		g_engfuncs.pfnTraceModel = RcTraceModel;
		g_engfuncs.pfnTraceTexture = RcTraceTexture;
		g_engfuncs.pfnWalkMove = RcWalkMove;
		g_engfuncs.pfnMoveToOrigin = RcMoveToOrigin;
		g_engfuncs.pfnDropToFloor = RcDropToFloor;
		g_engfuncs.pfnSetOrigin = RcSetOrigin;
		g_engfuncs.pfnFindEntityInSphere = RcFindEntityInSphere;
		g_engfuncs.pfnFindEntityByString = RcFindEntityByString;
		g_engfuncs.pfnFindClientInPVS = RcFindClientInPVS;
		g_engfuncs.pfnEntitiesInPVS = RcEntitiesInPVS;
		g_engfuncs.pfnCheckVisibility = RcCheckVisibility;
		g_engfuncs.pfnPointContents = RcPointContents;
		g_engfuncs.pfnEmitSound = RcEmitSound;
		g_engfuncs.pfnGetBonePosition = RcGetBonePosition;
		g_engfuncs.pfnGetAttachment = RcGetAttachment;
	}
	else
	{
		g_engfuncs.pfnTraceLine = s_RcSvEngine.pfnTraceLine;
		g_engfuncs.pfnTraceToss = s_RcSvEngine.pfnTraceToss;
		g_engfuncs.pfnTraceMonsterHull = s_RcSvEngine.pfnTraceMonsterHull;
		g_engfuncs.pfnTraceHull = s_RcSvEngine.pfnTraceHull;
		g_engfuncs.pfnTraceModel = s_RcSvEngine.pfnTraceModel;
		g_engfuncs.pfnTraceTexture = s_RcSvEngine.pfnTraceTexture;
		g_engfuncs.pfnWalkMove = s_RcSvEngine.pfnWalkMove;
		g_engfuncs.pfnMoveToOrigin = s_RcSvEngine.pfnMoveToOrigin;
		g_engfuncs.pfnDropToFloor = s_RcSvEngine.pfnDropToFloor;
		g_engfuncs.pfnSetOrigin = s_RcSvEngine.pfnSetOrigin;
		g_engfuncs.pfnFindEntityInSphere = s_RcSvEngine.pfnFindEntityInSphere;
		g_engfuncs.pfnFindEntityByString = s_RcSvEngine.pfnFindEntityByString;
		g_engfuncs.pfnFindClientInPVS = s_RcSvEngine.pfnFindClientInPVS;
		g_engfuncs.pfnEntitiesInPVS = s_RcSvEngine.pfnEntitiesInPVS;
		g_engfuncs.pfnCheckVisibility = s_RcSvEngine.pfnCheckVisibility;
		g_engfuncs.pfnPointContents = s_RcSvEngine.pfnPointContents;
		g_engfuncs.pfnEmitSound = s_RcSvEngine.pfnEmitSound;
		g_engfuncs.pfnGetBonePosition = s_RcSvEngine.pfnGetBonePosition;
		g_engfuncs.pfnGetAttachment = s_RcSvEngine.pfnGetAttachment;
	}

	s_bRcSvHooked = on;
}

//-----------------------------------------------------------------------------
// the second's results
//-----------------------------------------------------------------------------

// the monsters alive, those in combat, the edicts in use and the highest one in use
static void RcSvCensus( void )
{
	s_iShowMonsters = s_iShowCombat = s_iShowEdicts = s_iShowHighest = 0;

	for ( int i = 1; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pEdict = INDEXENT( i );

		if ( !pEdict || pEdict->free )
			continue;

		s_iShowEdicts++;
		s_iShowHighest = i;

		if ( !( pEdict->v.flags & FL_MONSTER ) || pEdict->v.deadflag != DEAD_NO )
			continue;

		s_iShowMonsters++;

		CBaseEntity *pEntity = (CBaseEntity *)GET_PRIVATE( pEdict );
		CBaseMonster *pMonster = pEntity ? pEntity->MyMonsterPointer() : NULL;
		if ( pMonster && pMonster->m_MonsterState == MONSTERSTATE_COMBAT )
			s_iShowCombat++;
	}
}

static void RcSvPrintLine( void )
{
	char line[2048];
	int len;
	double f = s_iShowFrames ? 1.0 / s_iShowFrames : 0;
	double hl = 0, eng = 0;

	for ( int i = 0; i < RCSV_COUNT; i++ )
	{
		if ( i >= RCSV_E_TRACELINE )
			eng += s_flShowExcl[i];
		else
			hl += s_flShowExcl[i];
	}

	len = sprintf( line, "rcsv t=%.1f fr=%d mon=%d cmb=%d ed=%d hi=%d hl=%.3f eng=%.3f", gpGlobals->time, s_iShowFrames,
		s_iShowMonsters, s_iShowCombat, s_iShowEdicts, s_iShowHighest, hl * f, eng * f );

	for ( int i = 0; i < RCSV_COUNT; i++ )
	{
		if ( !s_iShowCalls[i] )
			continue;

		if ( len > (int)sizeof( line ) - 80 )
			break;

		len += sprintf( line + len, " %s=%.3f/%.3f/%.1f", s_szRcSvName[i], s_flShowExcl[i] * f, s_flShowIncl[i] * f,
			s_iShowCalls[i] * f );
	}

	strcpy( line + len, "\n" );
	g_engfuncs.pfnServerPrint( line );
}

static void RcSvProf_Dump( void )
{
	char line[160];

	if ( !s_iShowFrames )
	{
		g_engfuncs.pfnServerPrint( "rc_svprof: no second collected yet; set rc_svprof 1 and wait a second\n" );
		return;
	}

	double f = 1.0 / s_iShowFrames;

	sprintf( line, "rc_svprof: %d frames in %.2f s, %d monsters alive (%d in combat), %d edicts in use, the highest %d\n",
		s_iShowFrames, s_flShowSeconds, s_iShowMonsters, s_iShowCombat, s_iShowEdicts, s_iShowHighest );
	g_engfuncs.pfnServerPrint( line );
	g_engfuncs.pfnServerPrint( "rc_svprof: scope          own ms/frame  with calls  calls/frame\n" );

	for ( int i = 0; i < RCSV_COUNT; i++ )
	{
		if ( !s_iShowCalls[i] )
			continue;

		sprintf( line, "rc_svprof: %-14s %10.3f  %10.3f  %11.1f\n", s_szRcSvName[i], s_flShowExcl[i] * f,
			s_flShowIncl[i] * f, s_iShowCalls[i] * f );
		g_engfuncs.pfnServerPrint( line );
	}
}

static void RcSvClearWindow( double now )
{
	memset( s_flWinExcl, 0, sizeof( s_flWinExcl ) );
	memset( s_flWinIncl, 0, sizeof( s_flWinIncl ) );
	memset( s_iWinCalls, 0, sizeof( s_iWinCalls ) );
	s_iWinFrames = 0;
	s_flWinStart = now;
}

//-----------------------------------------------------------------------------
// the frame boundary, at the start of StartFrame, where no scope is open
//-----------------------------------------------------------------------------
void RcSvProf_Frame( void )
{
	double now = RcSvNow();
	int on = rc_svprof.value ? 1 : 0;

	s_iDepth = 0;
	s_iOverflow = 0;
	memset( s_iOpen, 0, sizeof( s_iOpen ) );

	if ( on != g_iRcSvProf )
	{
		g_iRcSvProf = on;
		RcSvHook( on );
		RcSvClearWindow( now );
		return;
	}

	if ( !on )
		return;

	s_iWinFrames++;

	if ( now - s_flWinStart < 1000.0 )
		return;

	s_iShowFrames = s_iWinFrames;
	s_flShowSeconds = ( now - s_flWinStart ) / 1000.0;
	memcpy( s_flShowExcl, s_flWinExcl, sizeof( s_flShowExcl ) );
	memcpy( s_flShowIncl, s_flWinIncl, sizeof( s_flShowIncl ) );
	memcpy( s_iShowCalls, s_iWinCalls, sizeof( s_iShowCalls ) );
	RcSvClearWindow( now );

	RcSvCensus();
	RcSvPrintLine();
}

void RcSvProf_Init( void )
{
#ifdef _WIN32
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	s_flTickMs = 1000.0 / (double)freq.QuadPart;
#endif

	CVAR_REGISTER( &rc_svprof );
	g_engfuncs.pfnAddServerCommand( "rc_svprof_dump", RcSvProf_Dump );
}
