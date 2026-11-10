// ReCoF smooth frames (B315): pictures drawn between the game's frames, with the view turned by the mouse and
// moved ahead by the player's velocity. Experimental, off by default (rc_smoothframes 0)
//
// The game runs 100 frames a second (Host_FilterTime caps fps_max at 100 without developer) and drew one picture
// per frame, so on a monitor faster than 100 Hz the view moved in 10 ms steps. hw.dll's _Host_Frame (host.c) now
// calls RcSmooth_Frame on each pass of its loop while it waits for the next game frame, handing it SCR_UpdateScreen,
// and RcSmooth_Frame decides whether to draw a picture now (rc_smoothframes). Nothing of the game runs in such a
// picture: cl.time stays, and RcSmooth_Frame sets cl.oldtime to cl.time and host_frametime to 0 around it (hw.dll
// hands it their addresses), so what moves with them (particles, screen shakes, the center print, the view's punch, bob and sway, the weapon's lag) does not
// move. Two things do:
//  - the mouse turns the view at once (IN_SmoothMouseMove, inputw32.cpp: the IN_MouseMove of a game frame, so the
//    angles the next game frame sends are the sum of the same mouse counts as without smooth frames);
//  - the camera and the weapon model move ahead by the player's velocity for the time since the game frame's
//    picture, stopped by the player's hull at walls and floors (RcSmooth_View). The next game frame then draws the
//    real position.
// Monsters, doors, animations and the HUD stay as the game frame left them. Code that would change something on
// every picture, rather than with time, asks RcSmooth_RenderOnly() and leaves it for the game frames.
//
// Pacing: the pictures split each game frame evenly, at the monitor's refresh rate or faster (rc_smoothframes_fps
// sets another rate when it is above the game frames' rate), and one is drawn only when it should end before the next game frame is due, so the game frames
// keep their timing and the msec of the player's commands. How long a picture takes is judged from the slowest recent
// picture in between, which is forgotten with time (the game frame's own picture is no guide: it also sets up the
// models' new poses and the lights, which the pictures reuse), and from RC_SMOOTH_FIRST when none was drawn lately, with
// a margin of a quarter of that and 0.5 ms; a picture that still ends too late adds the time it overran to the margin,
// which is forgotten within a second or two, so the pictures never stop for long.
// With rc_smoothframes on, cl_showfps shows the pictures and the game frames each second, "300 fps (100 logic)", and
// rc_smoothframes_debug prints why pictures are left out.

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "const.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "ref_params.h"
#include "event_api.h"
#include "demo_api.h"
#include "rc_client.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void IN_SmoothMouseMove( void );	// inputw32.cpp

#define RC_SMOOTH_MAX		15			// pictures between two game frames at most
#define RC_SMOOTH_MARGIN	0.0005		// seconds left free before the next game frame, besides RC_SMOOTH_SHARE of the picture
#define RC_SMOOTH_SHARE		0.25
#define RC_SMOOTH_EARLY		0.0005		// a picture due this soon is drawn now (the engine's loop sleeps 1 ms at a time)
#define RC_SMOOTH_FIRST		0.002		// what a picture is taken to need when none was drawn in the last RC_SMOOTH_RECENT s
#define RC_SMOOTH_RECENT	0.5
#define RC_SMOOTH_LEAST		0.0005		// the least a picture is taken to need
#define RC_SMOOTH_DECAY		0.98		// per game frame: the slowest recent picture is half forgotten in a third of a second
#define RC_SMOOTH_LATE_DECAY	0.99	// per game frame: the margin a late picture added is half forgotten in 0.7 s

static cvar_t	*rc_smoothframes;
static cvar_t	*rc_smoothframes_fps;
static cvar_t	*s_pFpsMax;
static cvar_t	*s_pFpsModem;
static cvar_t	*s_pDeveloper;
static cvar_t	*s_pShowFps;

static int		s_iRenderOnly;			// inside a picture of RcSmooth_Frame: its number (1, 2, ...), else 0
static int		s_iPictures;			// pictures of RcSmooth_Frame so far
static int		s_iGameFrames;			// game frames (HUD_Frame)
static int		s_iSeenFrames;			// s_iGameFrames at the last RcSmooth_Frame
static int		s_iViewFrames = -1;		// s_iGameFrames when the game frame's picture computed the view
static int		s_bViewSmooth;			// that view allows pictures in between: not paused, no intermission or demo
static int		s_bViewMove;			// and it is the player's own first person view, which may move ahead
static double	s_flCallEnd;			// when the last RcSmooth_Frame returned
static double	s_flFrameStart;			// when the game frame started, at the latest
static double	s_flFrameView;			// when the game frame's picture computed the view
static double	s_flRenderStart;		// when the current picture started
static double	s_flRenderCost;			// the slowest recent picture, forgotten with time
static double	s_flLastPicture;		// when the last picture ended
static double	s_flFramePicture;		// the game frame's own picture, from its view to the end of its HUD (rc_smoothframes_debug)
static double	s_flLateMargin;			// what pictures that ended too late overran, forgotten with time
static int		s_iSlot;				// the next picture of this game frame, 1 to n

// rc_smoothframes_debug: what happened over the last second
enum
{
	RCS_OFF = 0,	// game frames with rc_smoothframes 0
	RCS_NOVIEW,		// game frames that drew no game view (menus, loading)
	RCS_PAUSED,		// paused, intermission, demo
	RCS_RATE,		// the monitor (or rc_smoothframes_fps) is no faster than the game frames
	RCS_NOTIME,		// pictures left out: not enough time before the next game frame
	RCS_LATE,		// pictures that ended too late
	RCS_COUNT
};

struct RcSmoothStats_t
{
	int		iFrames;
	int		iPictures;
	int		iWhy[RCS_COUNT];
	double	flLogic;		// the longest time from a game frame's start to its view
	double	flFramePicture;	// the longest game frame's picture (view to the end of the HUD)
	double	flPicture;		// the longest picture in between
	double	flLeftMin;		// the time left before the next game frame when a game frame was over, least and most
	double	flLeftMax;
	int		iLeft;			// game frames those two are taken from
};

static RcSmoothStats_t	s_Stats;		// this second
static RcSmoothStats_t	s_LastStats;	// the last whole second
static double			s_flStatsStart;

static double	s_flRefreshTime;		// when the monitor's refresh rate was read
static int		s_iRefresh;				// that rate, 0 when unknown

// cl_showfps
static double	s_flCountStart;
static int		s_iCountDrawn;			// pictures with a view since s_flCountStart
static int		s_iCountGame;			// game frames since then
static int		s_iCountSmooth;			// pictures of RcSmooth_Frame since then
static int		s_iShowDrawn;
static int		s_iShowGame;
static int		s_bShowOurs;			// the last count had pictures of RcSmooth_Frame

static double RcSmoothNow( void )
{
	static double s_flPeriod;
	LARGE_INTEGER now;

	if ( !s_flPeriod )
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency( &freq );
		s_flPeriod = 1.0 / (double)freq.QuadPart;
	}

	QueryPerformanceCounter( &now );
	return (double)now.QuadPart * s_flPeriod;
}

// the length of a game frame as Host_FilterTime (host.c) sets it; with fps_modem the shorter of the two, since only
// the engine knows which one it uses, and a shorter frame only leaves fewer pictures
static double RcSmoothFrameTime( void )
{
	float fps = s_pFpsMax ? s_pFpsMax->value : 100.0f;

	if ( s_pFpsModem && s_pFpsModem->value > fps )
		fps = s_pFpsModem->value;

	if ( fps < 0.5f )
		fps = 0.5f;

	if ( ( !s_pDeveloper || !s_pDeveloper->value ) && fps > 100.0f )
		fps = 100.0f;

	if ( gEngfuncs.GetMaxClients() > 1 && fps < 20.0f )
		fps = 20.0f;

	return 1.0 / fps;
}

// the refresh rate of the monitor the game is on, read again every 2 s in a game frame (rc_vidmode and moving the
// window change it), since the driver may take a while to answer
static void RcSmoothRefresh( double now )
{
	if ( s_flRefreshTime && now - s_flRefreshTime < 2.0 && now >= s_flRefreshTime )
		return;

	s_flRefreshTime = now;
	s_iRefresh = 0;

	HDC hDC = wglGetCurrentDC();
	HWND hWnd = hDC ? WindowFromDC( hDC ) : NULL;
	HMONITOR hMonitor = hWnd ? MonitorFromWindow( hWnd, MONITOR_DEFAULTTONEAREST ) : MonitorFromWindow( NULL, MONITOR_DEFAULTTOPRIMARY );

	MONITORINFOEXA info;
	memset( &info, 0, sizeof( info ) );
	info.cbSize = sizeof( info );

	DEVMODEA mode;
	memset( &mode, 0, sizeof( mode ) );
	mode.dmSize = sizeof( mode );

	if ( GetMonitorInfoA( hMonitor, &info ) && EnumDisplaySettingsA( info.szDevice, ENUM_CURRENT_SETTINGS, &mode ) && mode.dmDisplayFrequency > 1 )
		s_iRefresh = mode.dmDisplayFrequency;
}

static void RcSmoothDebug_f( void );

// the rate the pictures aim for: rc_smoothframes_fps when it is above the game frames' rate, else the monitor's. A
// value up to the game's rate would leave no picture, and players take the cvar for a switch and set it to 1
static int RcSmoothRate( double flFrame )
{
	if ( rc_smoothframes_fps && rc_smoothframes_fps->value * flFrame > 1.0 )
		return (int)rc_smoothframes_fps->value;

	return s_iRefresh;
}

// how long the next picture is taken to need: the slowest recent picture, forgotten with time, or RC_SMOOTH_FIRST when
// none was drawn lately
static double RcSmoothCost( double now )
{
	if ( !s_flLastPicture || now - s_flLastPicture >= RC_SMOOTH_RECENT || now < s_flLastPicture )
		return RC_SMOOTH_FIRST;

	return ( s_flRenderCost > RC_SMOOTH_LEAST ) ? s_flRenderCost : RC_SMOOTH_LEAST;
}

// the time that must be left before the next game frame to draw a picture: what it is taken to need, and the margin
static double RcSmoothNeed( double now )
{
	double flCost = RcSmoothCost( now );
	return flCost * ( 1.0 + RC_SMOOTH_SHARE ) + RC_SMOOTH_MARGIN + s_flLateMargin;
}

void RcSmooth_Init( void )
{
	gEngfuncs.pfnAddCommand( "rc_smoothframes_debug", RcSmoothDebug_f );

	rc_smoothframes = gEngfuncs.pfnRegisterVariable( "rc_smoothframes", "0", FCVAR_ARCHIVE );	// experimental
	rc_smoothframes_fps = gEngfuncs.pfnRegisterVariable( "rc_smoothframes_fps", "0", FCVAR_ARCHIVE );

	s_pFpsMax = gEngfuncs.pfnGetCvarPointer( "fps_max" );
	s_pFpsModem = gEngfuncs.pfnGetCvarPointer( "fps_modem" );
	s_pDeveloper = gEngfuncs.pfnGetCvarPointer( "developer" );
	s_pShowFps = gEngfuncs.pfnGetCvarPointer( "cl_showfps" );
}

int RcSmooth_RenderOnly( void )
{
	return s_iRenderOnly;
}

// in a picture of RcSmooth_Frame, the seconds since the game frame's picture computed its view, else 0
float RcSmooth_Ahead( void )
{
	if ( !s_iRenderOnly )
		return 0;

	double dt = s_flRenderStart - s_flFrameView;
	double flFrame = RcSmoothFrameTime();

	if ( dt <= 0 )
		return 0;
	if ( dt > flFrame )
		dt = flFrame;	// a late game frame does not push the view further

	return (float)dt;
}

// HUD_Frame: a game frame. cl_showfps gets its value back from its text, in case RcSmooth_Redraw hid the engine's
// counter in the last picture
void RcSmooth_GameFrame( void )
{
	double now = RcSmoothNow();

	s_iGameFrames++;
	s_iCountGame++;

	s_flRenderCost *= RC_SMOOTH_DECAY;
	s_flLateMargin *= RC_SMOOTH_LATE_DECAY;

	s_Stats.iFrames++;
	if ( now - s_flStatsStart >= 1.0 || now < s_flStatsStart )
	{
		s_LastStats = s_Stats;
		memset( &s_Stats, 0, sizeof( s_Stats ) );
		s_flStatsStart = now;
	}

	if ( rc_smoothframes && rc_smoothframes->value )
		RcSmoothRefresh( now );

	if ( s_pShowFps && s_pShowFps->string )
		s_pShowFps->value = (float)atof( s_pShowFps->string );
}

// hw.dll host.c _Host_Frame, on each pass of its loop between two game frames: draws a picture through
// pfnUpdateScreen (SCR_UpdateScreen) when one is due. pClTime is &cl.time, and cl.oldtime is the double after it
// (client_state_t, engine_hw client.h); pHostFrameTime is &host_frametime
extern "C" __declspec( dllexport ) void RcSmooth_Frame( void ( *pfnUpdateScreen )( void ), double *pClTime, double *pHostFrameTime )
{
	double now = RcSmoothNow();

	// a game frame ran since the last call: it started after that call returned
	int bNewFrame = 0;
	if ( s_iSeenFrames != s_iGameFrames )
	{
		s_iSeenFrames = s_iGameFrames;
		s_flFrameStart = s_flCallEnd;
		s_iSlot = 1;
		bNewFrame = 1;
	}

	s_flCallEnd = now;

	double flFrame = RcSmoothFrameTime();
	int iRate = RcSmoothRate( flFrame );

	if ( bNewFrame && s_flFrameStart > 0 )
	{
		double flLeft = s_flFrameStart + flFrame - now;

		if ( !s_Stats.iLeft || flLeft < s_Stats.flLeftMin )
			s_Stats.flLeftMin = flLeft;
		if ( !s_Stats.iLeft || flLeft > s_Stats.flLeftMax )
			s_Stats.flLeftMax = flLeft;
		s_Stats.iLeft++;
	}

	// n pictures split the game frame into n + 1 even parts, at least as many as the monitor shows
	int n = (int)ceil( flFrame * iRate - 0.01 ) - 1;
	if ( n > RC_SMOOTH_MAX )
		n = RC_SMOOTH_MAX;

	// only after a game frame that drew the game's view, which a picture draws again
	int iWhy = -1;
	if ( !rc_smoothframes || !rc_smoothframes->value || !pfnUpdateScreen || !pClTime || !pHostFrameTime )
		iWhy = RCS_OFF;
	else if ( s_iViewFrames != s_iGameFrames )
		iWhy = RCS_NOVIEW;
	else if ( !s_bViewSmooth )
		iWhy = RCS_PAUSED;
	else if ( n < 1 )
		iWhy = RCS_RATE;

	if ( iWhy >= 0 )
	{
		if ( bNewFrame )
			s_Stats.iWhy[iWhy]++;
		return;
	}

	double flSlot = flFrame / ( n + 1 );

	if ( s_iSlot > n || now < s_flFrameView + s_iSlot * flSlot - RC_SMOOTH_EARLY )
		return;

	// the parts already over are not drawn late
	while ( s_iSlot <= n && now >= s_flFrameView + s_iSlot * flSlot - RC_SMOOTH_EARLY )
		s_iSlot++;

	// the next game frame must not wait for this picture
	if ( s_flFrameStart + flFrame - now < RcSmoothNeed( now ) )
	{
		s_Stats.iWhy[RCS_NOTIME]++;
		return;
	}

	IN_SmoothMouseMove();

	// the HUD's clock sees no time pass in the picture, and the game frames see their own deltas as before (the
	// menus' solve, which runs before the HUD is drawn, reads the last delta)
	float flOldTime = gHUD.m_fOldTime;
	float flTime = gHUD.m_flTime;
	double flTimeDelta = gHUD.m_flTimeDelta;
	gHUD.m_flTimeDelta = 0;

	// no time passes for the engine either: cl.time - cl.oldtime and host_frametime are 0
	double flClOldTime = pClTime[1];
	double flHostFrameTime = *pHostFrameTime;
	pClTime[1] = pClTime[0];
	*pHostFrameTime = 0;

	s_iPictures++;
	if ( s_iPictures <= 0 )
		s_iPictures = 1;

	s_iRenderOnly = s_iPictures;
	s_flRenderStart = now;
	pfnUpdateScreen();
	s_iRenderOnly = 0;

	pClTime[1] = flClOldTime;
	*pHostFrameTime = flHostFrameTime;

	gHUD.m_fOldTime = flOldTime;
	gHUD.m_flTime = flTime;
	gHUD.m_flTimeDelta = flTimeDelta;

	double end = RcSmoothNow();
	double cost = end - now;

	s_Stats.iPictures++;
	if ( cost > s_Stats.flPicture )
		s_Stats.flPicture = cost;

	// a picture longer than a whole game frame was held up by something else (a load, another program); taken as one
	// game frame, it is forgotten within a second or so
	if ( cost > flFrame )
		cost = flFrame;
	if ( cost > s_flRenderCost || end - s_flLastPicture >= RC_SMOOTH_RECENT || end < s_flLastPicture )
		s_flRenderCost = cost;
	s_flLastPicture = end;

	// it ended after the next game frame was due, so that frame started late: the margin grows by the overrun for a
	// while (at most half a game frame)
	if ( end > s_flFrameStart + flFrame )
	{
		s_Stats.iWhy[RCS_LATE]++;

		s_flLateMargin += end - ( s_flFrameStart + flFrame );
		if ( s_flLateMargin > flFrame * 0.5 )
			s_flLateMargin = flFrame * 0.5;
	}

	s_flCallEnd = end;
	s_iCountSmooth++;
}

// V_CalcRefdef, after V_CalcNormalRefdef, once per picture (not in the head shield's second pass). In a game frame
// it notes the view and returns 0; in a picture of RcSmooth_Frame it returns 1 with the move of the camera and the
// weapon models in move: the player's velocity for the time since the game frame's picture, when bMove (the
// player's own first person view)
int RcSmooth_View( struct ref_params_s *pparams, int bMove, float *move )
{
	s_iCountDrawn++;

	if ( !s_iRenderOnly )
	{
		s_iViewFrames = s_iGameFrames;
		s_flFrameView = RcSmoothNow();
		s_flFramePicture = 0;

		// the game frame started after the last RcSmooth_Frame returned
		if ( s_flCallEnd && s_flFrameView - s_flCallEnd > s_Stats.flLogic && s_flFrameView - s_flCallEnd < 1.0 )
			s_Stats.flLogic = s_flFrameView - s_flCallEnd;

		s_bViewSmooth = !pparams->paused && !pparams->intermission && !pparams->spectator && !pparams->demoplayback &&
			!( gEngfuncs.pDemoAPI && gEngfuncs.pDemoAPI->IsRecording() );
		s_bViewMove = bMove;
		return 0;
	}

	if ( !s_bViewMove || !bMove )
		return 0;

	float dt = RcSmooth_Ahead();
	if ( dt <= 0 )
		return 0;

	vec3_t end;
	VectorScale( pparams->simvel, dt, move );
	VectorAdd( pparams->simorg, move, end );

	// the player's hull stops where the game's movement would stop it (world, doors, monsters), so a landing or a wall
	// does not show the view going into them for a picture
	if ( !gEngfuncs.IsNoClipping() )
	{
		int iHull = ( gEngfuncs.pEventAPI && gEngfuncs.pEventAPI->EV_LocalPlayerDucking() ) ? 1 : 0;
		pmtrace_t *tr = gEngfuncs.PM_TraceLine( pparams->simorg, end, PM_TRACELINE_PHYSENTSONLY, iHull, -1 );

		if ( !tr || tr->startsolid || tr->allsolid )
			return 0;

		VectorScale( move, tr->fraction, move );
	}

	return 1;
}

// HUD_Redraw, each picture: cl_showfps with the pictures drawn and the game frames, "300 fps (100 logic)", while
// rc_smoothframes is on; otherwise the engine's counter
void RcSmooth_Redraw( void )
{
	double now = RcSmoothNow();
	double span = now - s_flCountStart;

	// the game frame's own picture, from its view to here; what follows (the menus, the buffer swap) is in the margin
	// and in the measured pictures
	if ( !s_iRenderOnly && s_iViewFrames == s_iGameFrames && now >= s_flFrameView )
	{
		s_flFramePicture = now - s_flFrameView;
		if ( s_flFramePicture > s_Stats.flFramePicture )
			s_Stats.flFramePicture = s_flFramePicture;
	}

	if ( span >= 0.5 || span < 0 )
	{
		if ( span >= 0.5 && span < 2.0 )
		{
			s_iShowDrawn = (int)( s_iCountDrawn / span + 0.5 );
			s_iShowGame = (int)( s_iCountGame / span + 0.5 );
			s_bShowOurs = 1;
		}
		else
		{
			s_bShowOurs = 0;
		}

		s_flCountStart = now;
		s_iCountDrawn = 0;
		s_iCountGame = 0;
		s_iCountSmooth = 0;
	}

	if ( !s_bShowOurs || !rc_smoothframes || !rc_smoothframes->value || !s_pShowFps || !s_pShowFps->string || !atof( s_pShowFps->string ) )
		return;

	// the engine's counter (SCR_DrawFPS, the only reader of the value) is left out of this picture;
	// RcSmooth_GameFrame puts the value back
	s_pShowFps->value = 0;

	char szText[64];
	_snprintf( szText, sizeof( szText ), "%i fps (%i logic)", s_iShowDrawn, s_iShowGame );
	szText[sizeof( szText ) - 1] = 0;

	gEngfuncs.pfnDrawSetTextColor( 1.0f, 1.0f, 1.0f );
	gEngfuncs.pfnDrawConsoleString( 2, 2, szText );
}

// rc_smoothframes_debug: the last second of smooth frames, and why pictures were left out
static void RcSmoothDebug_f( void )
{
	const RcSmoothStats_t &s = s_LastStats;
	double flFrame = RcSmoothFrameTime();
	int iRate = RcSmoothRate( flFrame );
	int n = (int)ceil( flFrame * iRate - 0.01 ) - 1;
	if ( n > RC_SMOOTH_MAX )
		n = RC_SMOOTH_MAX;
	if ( n < 0 )
		n = 0;

	gEngfuncs.Con_Printf( "rc_smoothframes %s, monitor %i Hz, target %i, game frame %.1f ms, %i pictures in each\n",
		( rc_smoothframes && rc_smoothframes->value ) ? "on" : "off", s_iRefresh, iRate, flFrame * 1000, n );
	gEngfuncs.Con_Printf( "last second: %i game frames, %i pictures in between\n", s.iFrames, s.iPictures );
	gEngfuncs.Con_Printf( "game frames without pictures: %i off, %i no game view, %i paused or demo, %i monitor too slow\n",
		s.iWhy[RCS_OFF], s.iWhy[RCS_NOVIEW], s.iWhy[RCS_PAUSED], s.iWhy[RCS_RATE] );
	gEngfuncs.Con_Printf( "pictures left out: %i no time before the next game frame; %i ended too late\n",
		s.iWhy[RCS_NOTIME], s.iWhy[RCS_LATE] );
	gEngfuncs.Con_Printf( "longest: game logic %.2f ms, game frame's picture %.2f ms, picture in between %.2f ms\n",
		s.flLogic * 1000, s.flFramePicture * 1000, s.flPicture * 1000 );
	gEngfuncs.Con_Printf( "time left when a game frame was over: %.2f to %.2f ms (%i frames)\n",
		s.flLeftMin * 1000, s.flLeftMax * 1000, s.iLeft );
	double now = RcSmoothNow();
	gEngfuncs.Con_Printf( "a picture is taken to need %.2f ms now, %.2f ms with the margin (%.2f ms of it after late ones)\n",
		RcSmoothCost( now ) * 1000, RcSmoothNeed( now ) * 1000, s_flLateMargin * 1000 );
}
