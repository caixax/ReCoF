// ReCoF smooth frames (B315): pictures drawn between the game's frames, with the view turned by the mouse and
// moved ahead by the player's velocity
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
// sets another rate), and one is drawn only when it should end before the next game frame is due, judged from the
// slowest recent picture with a margin, so the game frames keep their timing and the msec of the player's commands.
// cl_showfps then shows the pictures and the game frames each second: "240 fps (100 logic)".

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
#define RC_SMOOTH_MARGIN	0.0005		// seconds left free before the next game frame
#define RC_SMOOTH_EARLY		0.0005		// a picture due this soon is drawn now (the engine's loop sleeps 1 ms at a time)
#define RC_SMOOTH_FIRST		0.002		// the time a picture is thought to take before one was measured
#define RC_SMOOTH_DECAY		0.97		// how fast the slowest recent picture is forgotten, per picture

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
static double	s_flRenderCost;			// the slowest recent picture, forgotten slowly
static int		s_iSlot;				// the next picture of this game frame, 1 to n

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

void RcSmooth_Init( void )
{
	rc_smoothframes = gEngfuncs.pfnRegisterVariable( "rc_smoothframes", "1", FCVAR_ARCHIVE );
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
	s_iGameFrames++;
	s_iCountGame++;

	if ( rc_smoothframes && rc_smoothframes->value )
		RcSmoothRefresh( RcSmoothNow() );

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
	if ( s_iSeenFrames != s_iGameFrames )
	{
		s_iSeenFrames = s_iGameFrames;
		s_flFrameStart = s_flCallEnd;
		s_iSlot = 1;
	}

	s_flCallEnd = now;

	if ( !rc_smoothframes || !rc_smoothframes->value || !pfnUpdateScreen || !pClTime || !pHostFrameTime )
		return;

	// only after a game frame that drew the game's view, which a picture draws again
	if ( s_iViewFrames != s_iGameFrames || !s_bViewSmooth )
		return;

	double flFrame = RcSmoothFrameTime();
	int iRate = ( rc_smoothframes_fps->value > 0 ) ? (int)rc_smoothframes_fps->value : s_iRefresh;

	// n pictures split the game frame into n + 1 even parts, at least as many as the monitor shows
	int n = (int)ceil( flFrame * iRate - 0.01 ) - 1;
	if ( n < 1 )
		return;
	if ( n > RC_SMOOTH_MAX )
		n = RC_SMOOTH_MAX;

	double flSlot = flFrame / ( n + 1 );

	if ( s_iSlot > n || now < s_flFrameView + s_iSlot * flSlot - RC_SMOOTH_EARLY )
		return;

	// the parts already over are not drawn late
	while ( s_iSlot <= n && now >= s_flFrameView + s_iSlot * flSlot - RC_SMOOTH_EARLY )
		s_iSlot++;

	// the next game frame must not wait for this picture
	double flCost = s_flRenderCost > 0 ? s_flRenderCost : RC_SMOOTH_FIRST;
	if ( s_flFrameStart + flFrame - now < flCost + RC_SMOOTH_MARGIN )
		return;

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

	s_flRenderCost *= RC_SMOOTH_DECAY;
	if ( cost > s_flRenderCost )
		s_flRenderCost = cost;

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

// HUD_Redraw, each picture: cl_showfps with the pictures drawn and the game frames, "240 fps (100 logic)",
// while pictures of RcSmooth_Frame are drawn; otherwise the engine's counter
void RcSmooth_Redraw( void )
{
	double now = RcSmoothNow();
	double span = now - s_flCountStart;

	if ( span >= 0.5 || span < 0 )
	{
		if ( span >= 0.5 && span < 2.0 )
		{
			s_iShowDrawn = (int)( s_iCountDrawn / span + 0.5 );
			s_iShowGame = (int)( s_iCountGame / span + 0.5 );
			s_bShowOurs = ( s_iCountSmooth > 0 );
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

	if ( !s_bShowOurs || !s_pShowFps || !s_pShowFps->string || !atof( s_pShowFps->string ) )
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
