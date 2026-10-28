// ReCoF client additions
//
// The client features ReCoF adds. client.dll is built with a newer compiler than retail
// (docs/toolchains.md), so none of its addresses match retail; the LiveSplit autosplitter finds the
// values it reads through g_RcAutosplit below (docs/speedrun.md).

#include "hud.h"
#include "cl_util.h"
#include "kbutton.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "pm_defs.h"
#include "rc_client.h"
#include "rc_prof.h"

#include <stdio.h>
#include <windows.h>

extern "C" playermove_t *pmove;		// pm_shared.c: the move client prediction ran last

extern int g_iVisibleMouse;			// inputw32.cpp: a menu cursor is on screen

void KeyDown( kbutton_t *b );		// input.cpp
void KeyUp( kbutton_t *b );

static kbutton_t	in_rcducktap;
static cvar_t		*rc_autojump;
static cvar_t		*rc_ducktap_priority;
static cvar_t		*rc_jump_priority;
static cvar_t		*rc_speedometer;
static cvar_t		*rc_speedometer_jump;
static cvar_t		*rc_speedometer_3d;
static cvar_t		*rc_speedometer_pos;
static cvar_t		*rc_speedometer_color;
static int			s_bDucktapReleased;	// +rc_ducktap released duck on the frame before
static vec3_t		s_vecLastVelocity;	// speedometer: the velocity of the frame before
static int			s_iJumpSpeed;		// speedometer: the speed at the last takeoff
static cvar_t		*rc_rawinput;
static HWND			s_hRawWnd;			// raw input: the game window, once hooked
static WNDPROC		s_pfnRawOldProc;	// raw input: the engine's window procedure
static int			s_bRawFailed;		// raw input: the window or the registration failed
static int			s_iRawDX, s_iRawDY;	// raw input: mouse counts since the last mouse move

//-----------------------------------------------------------------------------
// Where this build keeps the values the LiveSplit autosplitter reads from client.dll, so a script can
// find them by the tag instead of by address (docs/speedrun.md, "ReCoF autosplitter block"). Each
// pointer gives the bytes retail has at the listed address; hw.dll keeps its retail addresses.
//-----------------------------------------------------------------------------
struct decaltexture_s;
extern decaltexture_s decaltextures[];	// gl_decals.cpp
char *READ_STRING( void );				// parsemsg.cpp

struct RcAutosplit_t
{
	char	magic[16];	// "ReCoF_AUTOSPLIT", written at start-up (the whole tag is in no other place)
	int		version;	// 1
	void	*cutscene;	// 4 bytes, retail +0x18ED64: gHUD.m_BlackBar_guess.m_iOn_guess
	void	*igt;		// 4 bytes, retail +0x1BE920: decaltextures[0] + 8 (0 until a map's decals load)
	void	*music;		// 40 bytes, retail +0x5440C8: READ_STRING's buffer + 0x58
	void	*savestate;	// 4 bytes, retail +0x18A3E0: gHUD.m_flSaveHackTime_guess
};

RcAutosplit_t g_RcAutosplit;

static void IN_RcDucktapDown( void ) { KeyDown( &in_rcducktap ); }
static void IN_RcDucktapUp( void ) { KeyUp( &in_rcducktap ); }

//-----------------------------------------------------------------------------
// called from InitInput
//-----------------------------------------------------------------------------
void RcClient_Init( void )
{
	g_RcAutosplit.version = 1;
	g_RcAutosplit.cutscene = &gHUD.m_BlackBar_guess.m_iOn_guess;
	g_RcAutosplit.igt = (char *)decaltextures + 8;
	g_RcAutosplit.music = READ_STRING() + 0x58;	// no message is being read here, so this only returns the buffer
	g_RcAutosplit.savestate = &gHUD.m_flSaveHackTime_guess;
	strcpy( g_RcAutosplit.magic, "ReCoF_AUTO" );
	strcat( g_RcAutosplit.magic, "SPLIT" );

	gEngfuncs.pfnAddCommand( "+rc_ducktap", IN_RcDucktapDown );
	gEngfuncs.pfnAddCommand( "-rc_ducktap", IN_RcDucktapUp );

	// not archived: rc_hlmovement turns it on, and a saved 1 would keep it on in the next game without it
	rc_autojump = gEngfuncs.pfnRegisterVariable( "rc_autojump", "0", 0 );
	rc_ducktap_priority = gEngfuncs.pfnRegisterVariable( "rc_ducktap_priority", "1", FCVAR_ARCHIVE );
	rc_jump_priority = gEngfuncs.pfnRegisterVariable( "rc_jump_priority", "0", FCVAR_ARCHIVE );

	rc_speedometer = gEngfuncs.pfnRegisterVariable( "rc_speedometer", "0", FCVAR_ARCHIVE );
	rc_speedometer_jump = gEngfuncs.pfnRegisterVariable( "rc_speedometer_jump", "1", FCVAR_ARCHIVE );
	rc_speedometer_3d = gEngfuncs.pfnRegisterVariable( "rc_speedometer_3d", "0", FCVAR_ARCHIVE );
	rc_speedometer_pos = gEngfuncs.pfnRegisterVariable( "rc_speedometer_pos", ".5 .9", FCVAR_ARCHIVE );
	rc_speedometer_color = gEngfuncs.pfnRegisterVariable( "rc_speedometer_color", "255 255 255", FCVAR_ARCHIVE );

	rc_rawinput = gEngfuncs.pfnRegisterVariable( "rc_rawinput", "1", FCVAR_ARCHIVE );

	RcProf_Init();
}

//-----------------------------------------------------------------------------
// rc_hlmovement: the server's movement mode, as client prediction sees it
//-----------------------------------------------------------------------------
int RcHLMovement( void )
{
	const char *value = gEngfuncs.PhysInfo_ValueForKey( "hlm" );

	return value && atoi( value ) != 0;
}

//-----------------------------------------------------------------------------
// called at the end of CL_CreateMove: +rc_ducktap and rc_autojump work on the buttons of the command, as
// RebuiltSimon's +rs_ducktap and rs_autojump do. With rc_jump_priority, holding jump pauses +rc_ducktap;
// with rc_ducktap_priority, holding +rc_ducktap pauses rc_autojump
//-----------------------------------------------------------------------------
void RcClient_CreateMove( usercmd_t *cmd )
{
	// rc_hlmovement: the sprint key does nothing in Half-Life movement (the server strips it too)
	if ( RcHLMovement() )
		cmd->buttons &= ~IN_RUN;

	if ( !pmove || !rc_autojump )
		return;

	int bOnGround = pmove->onground != -1;
	int bDucktap = ( in_rcducktap.state & 1 ) && !( rc_jump_priority->value && ( cmd->buttons & IN_JUMP ) );

	if ( bDucktap )
	{
		// press duck for one frame on the ground, release it on the next, so each landing ducktaps
		if ( pmove->movetype == MOVETYPE_WALK && bOnGround && s_bDucktapReleased )
		{
			cmd->buttons |= IN_DUCK;
			s_bDucktapReleased = 0;
		}
		else
		{
			cmd->buttons &= ~IN_DUCK;
			s_bDucktapReleased = 1;
		}
	}

	// release jump in the air, so holding it jumps again on landing
	if ( rc_autojump->value && pmove->movetype == MOVETYPE_WALK && !( rc_ducktap_priority->value && bDucktap ) )
	{
		if ( !bOnGround && pmove->waterlevel <= 1 )
			cmd->buttons &= ~IN_JUMP;
	}
}

//-----------------------------------------------------------------------------
// speedometer helpers
//-----------------------------------------------------------------------------
static float RcSpeed( const float *v )
{
	if ( rc_speedometer_3d->value )
		return sqrt( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );

	return sqrt( v[0] * v[0] + v[1] * v[1] );
}

static void RcDrawCentered( int x, int y, const char *text, const float *color )
{
	int w, h;

	gEngfuncs.pfnDrawConsoleStringLen( text, &w, &h );
	gEngfuncs.pfnDrawSetTextColor( color[0], color[1], color[2] );
	gEngfuncs.pfnDrawConsoleString( x - w / 2, y, (char *)text );
}

//-----------------------------------------------------------------------------
// called from CHud::Redraw: the movement mode tag and the speedometer
//-----------------------------------------------------------------------------
void RcClient_Redraw( float flTime, int intermission )
{
	if ( intermission || !rc_speedometer )
		return;

	// the tag keeps runs in Half-Life movement visible
	if ( RcHLMovement() )
	{
		gEngfuncs.pfnDrawSetTextColor( 1.0, 0.7, 0.2 );
		gEngfuncs.pfnDrawConsoleString( 8, 8, (char *)"HL movement" );
	}

	if ( !rc_speedometer->value || !pmove )
		return;

	const float *v = pmove->velocity;

	// the takeoff speed, measured as RebuiltSimon does: when the vertical speed leaves 0 or turns upward
	if ( ( v[2] != 0 && s_vecLastVelocity[2] == 0 ) || ( v[2] > 0 && s_vecLastVelocity[2] < 0 ) )
		s_iJumpSpeed = (int)RcSpeed( s_vecLastVelocity );

	VectorCopy( v, s_vecLastVelocity );

	float x = 0.5, y = 0.9;
	float color[3] = { 255, 255, 255 };

	sscanf( rc_speedometer_pos->string, "%f %f", &x, &y );
	sscanf( rc_speedometer_color->string, "%f %f %f", &color[0], &color[1], &color[2] );

	for ( int i = 0; i < 3; i++ )
		color[i] /= 255.0;

	char text[32];
	int px = (int)( x * ScreenWidth );
	int py = (int)( y * ScreenHeight );

	sprintf( text, "%d", (int)RcSpeed( v ) );
	RcDrawCentered( px, py, text, color );

	if ( rc_speedometer_jump->value )
	{
		int w, h;
		gEngfuncs.pfnDrawConsoleStringLen( text, &w, &h );

		sprintf( text, "%d", s_iJumpSpeed );
		RcDrawCentered( px, py + h + 2, text, color );
	}
}

//-----------------------------------------------------------------------------
// B184 rc_rawinput: raw mouse counts (WM_INPUT) instead of the cursor movement, which Windows accelerates
// and which the game forced to "enhance pointer precision". The game window's procedure belongs to the
// engine, so the client chains its own in front of it the first time the mouse moves in game.
//-----------------------------------------------------------------------------
static LRESULT CALLBACK RcRawWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	if ( msg == WM_INPUT && !g_iVisibleMouse )
	{
		RAWINPUT ri;
		UINT size = sizeof( ri );

		if ( GetRawInputData( (HRAWINPUT)lParam, RID_INPUT, &ri, &size, sizeof( RAWINPUTHEADER ) ) != (UINT)-1 &&
			 ri.header.dwType == RIM_TYPEMOUSE && !( ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) )
		{
			s_iRawDX += ri.data.mouse.lLastX;
			s_iRawDY += ri.data.mouse.lLastY;
		}
	}

	return CallWindowProcA( s_pfnRawOldProc, hWnd, msg, wParam, lParam );
}

int RcRawInputOn( void )
{
	return rc_rawinput && rc_rawinput->value && !s_bRawFailed;
}

// called from IN_ActivateMouse: drop what came in while the console or a menu had the mouse
void RcRawMouseReset( void )
{
	s_iRawDX = s_iRawDY = 0;
}

// called from IN_MouseMove: replaces *mx and *my with the raw counts since the last call
void RcRawMouse( int *mx, int *my )
{
	if ( !RcRawInputOn() )
		return;

	if ( !s_hRawWnd )
	{
		// the engine's window class (sys_sdlwind.cpp); skip another game's window
		HWND hWnd = NULL;
		DWORD pid;

		while ( ( hWnd = FindWindowExA( NULL, hWnd, "Valve001", NULL ) ) != NULL )
		{
			if ( GetWindowThreadProcessId( hWnd, &pid ) && pid == GetCurrentProcessId() )
				break;
		}

		RAWINPUTDEVICE rid;
		rid.usUsagePage = 1;	// generic desktop
		rid.usUsage = 2;		// mouse
		rid.dwFlags = 0;
		rid.hwndTarget = hWnd;

		if ( !hWnd || !RegisterRawInputDevices( &rid, 1, sizeof( rid ) ) )
		{
			s_bRawFailed = 1;	// keep the cursor movement
			return;
		}

		s_pfnRawOldProc = (WNDPROC)SetWindowLongPtrA( hWnd, GWLP_WNDPROC, (LONG_PTR)RcRawWndProc );
		s_hRawWnd = hWnd;
		s_iRawDX = s_iRawDY = 0;
		return;
	}

	*mx = s_iRawDX;
	*my = s_iRawDY;
	s_iRawDX = s_iRawDY = 0;
}
