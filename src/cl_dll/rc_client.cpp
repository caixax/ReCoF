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
#include "glmanager.h"
#include "vgui_TeamFortressViewport.h"
#include "Scope_guess.h"

#include <stdio.h>
#include <windows.h>

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

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
static cvar_t		*rc_vsync;	// B304
static HWND			s_hRawWnd;			// raw input 1: the game window, once hooked
static WNDPROC		s_pfnRawOldProc;	// raw input 1: the engine's window procedure
static HANDLE		s_hRawThread;		// raw input 2: the input thread
static DWORD		s_dwRawThreadId;
static HWND volatile	s_hRawThreadWnd;	// raw input 2: the thread's window, once it has the raw mouse
static int			s_iRawMode;			// raw input: the mode whose window was given the raw mouse, 0 for none
static int			s_bRawFailed;		// raw input: a window, the thread or the registration failed
static int			s_bRawRInput;		// raw input: RInput.dll is loaded and has the raw mouse
static volatile LONG	s_lRawDX, s_lRawDY;	// raw input: mouse counts since the last mouse move
static cvar_t		*rc_viewmodel_fov;
static cvar_t		*rc_viewmodel_ofs_forward;
static cvar_t		*rc_viewmodel_ofs_right;
static cvar_t		*rc_viewmodel_ofs_up;
static float		s_flViewmodelOfsScale = 1;	// viewmodel offsets: eased to 0 in iron sights
static cvar_t		*rc_crosshair;
static cvar_t		*rc_cross;
static cvar_t		*rc_cross_color;
static cvar_t		*rc_cross_alpha;
static cvar_t		*rc_cross_thickness;
static cvar_t		*rc_cross_size;
static cvar_t		*rc_cross_gap;
static cvar_t		*rc_cross_outline;
static cvar_t		*rc_cross_circle_radius;
static cvar_t		*rc_cross_dot_color;
static cvar_t		*rc_cross_dot_size;
static cvar_t		*rc_cross_top_line;
static cvar_t		*rc_cross_bottom_line;
static cvar_t		*rc_cross_left_line;
static cvar_t		*rc_cross_right_line;

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

	rc_rawinput = gEngfuncs.pfnRegisterVariable( "rc_rawinput", "2", FCVAR_ARCHIVE );
	rc_vsync = gEngfuncs.pfnRegisterVariable( "rc_vsync", "0", FCVAR_ARCHIVE );	// B304

	rc_viewmodel_fov = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_fov", "0", FCVAR_ARCHIVE );
	rc_viewmodel_ofs_forward = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_ofs_forward", "0", FCVAR_ARCHIVE );
	rc_viewmodel_ofs_right = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_ofs_right", "0", FCVAR_ARCHIVE );
	rc_viewmodel_ofs_up = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_ofs_up", "0", FCVAR_ARCHIVE );

	rc_crosshair = gEngfuncs.pfnRegisterVariable( "rc_crosshair", "1", FCVAR_ARCHIVE );
	rc_cross = gEngfuncs.pfnRegisterVariable( "rc_cross", "0", FCVAR_ARCHIVE );
	rc_cross_color = gEngfuncs.pfnRegisterVariable( "rc_cross_color", "0 255 0", FCVAR_ARCHIVE );
	rc_cross_alpha = gEngfuncs.pfnRegisterVariable( "rc_cross_alpha", "255", FCVAR_ARCHIVE );
	rc_cross_thickness = gEngfuncs.pfnRegisterVariable( "rc_cross_thickness", "2", FCVAR_ARCHIVE );
	rc_cross_size = gEngfuncs.pfnRegisterVariable( "rc_cross_size", "10", FCVAR_ARCHIVE );
	rc_cross_gap = gEngfuncs.pfnRegisterVariable( "rc_cross_gap", "3", FCVAR_ARCHIVE );
	rc_cross_outline = gEngfuncs.pfnRegisterVariable( "rc_cross_outline", "0", FCVAR_ARCHIVE );
	rc_cross_circle_radius = gEngfuncs.pfnRegisterVariable( "rc_cross_circle_radius", "0", FCVAR_ARCHIVE );
	rc_cross_dot_color = gEngfuncs.pfnRegisterVariable( "rc_cross_dot_color", "", FCVAR_ARCHIVE );
	rc_cross_dot_size = gEngfuncs.pfnRegisterVariable( "rc_cross_dot_size", "0", FCVAR_ARCHIVE );
	rc_cross_top_line = gEngfuncs.pfnRegisterVariable( "rc_cross_top_line", "1", FCVAR_ARCHIVE );
	rc_cross_bottom_line = gEngfuncs.pfnRegisterVariable( "rc_cross_bottom_line", "1", FCVAR_ARCHIVE );
	rc_cross_left_line = gEngfuncs.pfnRegisterVariable( "rc_cross_left_line", "1", FCVAR_ARCHIVE );
	rc_cross_right_line = gEngfuncs.pfnRegisterVariable( "rc_cross_right_line", "1", FCVAR_ARCHIVE );

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
// rc_cross: a crosshair set up by console variables, as BunnymodXT's bxt_cross: four lines around a gap, a dot
// and a circle, each with an optional black outline. It is drawn with alpha blending in OpenGL, since FillRGBA
// adds its color and cannot draw dark ones. It shows with or without a weapon and hides where the Cry of Fear
// crosshair does: with the HUD, in cutscenes, in iron sights and in a scope
//-----------------------------------------------------------------------------
static void RcCrossColor( const char *text, int *color )
{
	int r, g, b;

	if ( sscanf( text, "%d %d %d", &r, &g, &b ) == 3 )
	{
		color[0] = r;
		color[1] = g;
		color[2] = b;
	}
}

static void RcCrossRect( float x0, float y0, float x1, float y1, float grow )
{
	gl.glVertex3f( x0 - grow, y0 - grow, 0 );
	gl.glVertex3f( x1 + grow, y0 - grow, 0 );
	gl.glVertex3f( x1 + grow, y1 + grow, 0 );
	gl.glVertex3f( x0 - grow, y1 + grow, 0 );
}

static void RcCrossRing( float cx, float cy, float inner, float outer )
{
	int segments = (int)( outer * 1.5 );

	if ( segments < 16 )
		segments = 16;
	else if ( segments > 180 )
		segments = 180;

	if ( inner < 0 )
		inner = 0;

	gl.glBegin( GL_TRIANGLE_STRIP );

	for ( int i = 0; i <= segments; i++ )
	{
		float a = i * 2 * M_PI / segments;

		gl.glVertex3f( cx + cos( a ) * outer, cy + sin( a ) * outer, 0 );
		gl.glVertex3f( cx + cos( a ) * inner, cy + sin( a ) * inner, 0 );
	}

	gl.glEnd();
}

static void RcDrawCross( void )
{
	if ( !rc_cross->value || !gl.IsGLAllowed() || !gHUD.m_pCvarDraw->value )
		return;

	if ( gHUD.m_fPlayerDead || ( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL | HIDEHUD_HEALTH ) ) || g_iVisibleMouse )
		return;

	// a cutscene, the view on another entity, iron sights or a scope
	if ( gHUD.m_BlackBar_guess.m_iOn_guess || IsViewEntityActive_guess() || gHUD.m_bIronSights_guess ||
		( gViewPort && gViewPort->m_pScope_guess && gViewPort->m_pScope_guess->m_iScope_guess ) )
		return;

	int color[3] = { 0, 255, 0 };
	RcCrossColor( rc_cross_color->string, color );

	int dot[3] = { color[0], color[1], color[2] };
	RcCrossColor( rc_cross_dot_color->string, dot );

	int alpha = (int)rc_cross_alpha->value;
	alpha = alpha < 0 ? 0 : ( alpha > 255 ? 255 : alpha );

	float cx = ScreenWidth / 2;
	float cy = ScreenHeight / 2;
	float half = rc_cross_thickness->value / 2;
	float size = rc_cross_size->value;
	float gap = rc_cross_gap->value;
	float outline = rc_cross_outline->value > 0 ? rc_cross_outline->value : 0;
	float dothalf = rc_cross_dot_size->value / 2;
	float radius = rc_cross_circle_radius->value;

	// the lines, top, bottom, left and right
	float lines[4][4];
	int count = 0;

	if ( half > 0 && size > 0 )
	{
		if ( rc_cross_top_line->value )
		{
			lines[count][0] = cx - half; lines[count][1] = cy - gap - size; lines[count][2] = cx + half; lines[count][3] = cy - gap;
			count++;
		}

		if ( rc_cross_bottom_line->value )
		{
			lines[count][0] = cx - half; lines[count][1] = cy + gap; lines[count][2] = cx + half; lines[count][3] = cy + gap + size;
			count++;
		}

		if ( rc_cross_left_line->value )
		{
			lines[count][0] = cx - gap - size; lines[count][1] = cy - half; lines[count][2] = cx - gap; lines[count][3] = cy + half;
			count++;
		}

		if ( rc_cross_right_line->value )
		{
			lines[count][0] = cx + gap; lines[count][1] = cy - half; lines[count][2] = cx + gap + size; lines[count][3] = cy + half;
			count++;
		}
	}

	GLboolean bAlphaTest = gl.glIsEnabled( GL_ALPHA_TEST );

	gl.glDisable( GL_TEXTURE_2D );
	gl.glDisable( GL_ALPHA_TEST );
	gl.glEnable( GL_BLEND );
	gl.glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	// the outlines first, under everything
	if ( outline > 0 )
	{
		gl.glColor4ub( 0, 0, 0, alpha );

		if ( radius > 0 && half > 0 )
			RcCrossRing( cx, cy, radius - half - outline, radius + half + outline );

		gl.glBegin( GL_QUADS );

		for ( int i = 0; i < count; i++ )
			RcCrossRect( lines[i][0], lines[i][1], lines[i][2], lines[i][3], outline );

		if ( dothalf > 0 )
			RcCrossRect( cx - dothalf, cy - dothalf, cx + dothalf, cy + dothalf, outline );

		gl.glEnd();
	}

	gl.glColor4ub( color[0], color[1], color[2], alpha );

	if ( radius > 0 && half > 0 )
		RcCrossRing( cx, cy, radius - half, radius + half );

	gl.glBegin( GL_QUADS );

	for ( int i = 0; i < count; i++ )
		RcCrossRect( lines[i][0], lines[i][1], lines[i][2], lines[i][3], 0 );

	if ( dothalf > 0 )
	{
		gl.glColor4ub( dot[0], dot[1], dot[2], alpha );
		RcCrossRect( cx - dothalf, cy - dothalf, cx + dothalf, cy + dothalf, 0 );
	}

	gl.glEnd();

	// what FillRGBA leaves behind
	gl.glColor4ub( 255, 255, 255, 255 );
	gl.glEnable( GL_TEXTURE_2D );
	gl.glDisable( GL_BLEND );

	if ( bAlphaTest )
		gl.glEnable( GL_ALPHA_TEST );
}

//-----------------------------------------------------------------------------
// called from CHud::Redraw: the custom crosshair, the movement mode tag and the speedometer
//-----------------------------------------------------------------------------
void RcClient_Redraw( float flTime, int intermission )
{
	if ( intermission || !rc_speedometer )
		return;

	RcDrawCross();

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
// rc_crosshair 0 hides the red Cry of Fear crosshair and keeps the ammo display; cl_crosshair 0 hides both
//-----------------------------------------------------------------------------
int RcCrosshairOn( void )
{
	return !rc_crosshair || rc_crosshair->value != 0;
}

//-----------------------------------------------------------------------------
// rc_viewmodel_ofs_forward, _right and _up move the weapon model along the view, in units, as BunnymodXT's
// bxt_viewmodel_ofs_* do; the left hand item follows the weapon model. Iron sights ease them out over a
// quarter of a second, so the sights stay on the middle of the screen
//-----------------------------------------------------------------------------
void RcViewmodelOffset( float *origin, const float *forward, const float *right, const float *up, float frametime )
{
	if ( !rc_viewmodel_ofs_forward )
		return;

	float target = gHUD.m_bIronSights_guess ? 0 : 1;
	float step = frametime * 4;

	if ( s_flViewmodelOfsScale < target )
	{
		s_flViewmodelOfsScale += step;
		if ( s_flViewmodelOfsScale > target )
			s_flViewmodelOfsScale = target;
	}
	else if ( s_flViewmodelOfsScale > target )
	{
		s_flViewmodelOfsScale -= step;
		if ( s_flViewmodelOfsScale < target )
			s_flViewmodelOfsScale = target;
	}

	float f = rc_viewmodel_ofs_forward->value * s_flViewmodelOfsScale;
	float r = rc_viewmodel_ofs_right->value * s_flViewmodelOfsScale;
	float u = rc_viewmodel_ofs_up->value * s_flViewmodelOfsScale;

	for ( int i = 0; i < 3; i++ )
		origin[i] += forward[i] * f + right[i] * r + up[i] * u;
}

//-----------------------------------------------------------------------------
// rc_viewmodel_fov draws the weapon model and the left hand item with this horizontal field of view instead of
// the view's, as BunnymodXT's bxt_viewmodel_fov does (0 keeps the view's). The engine's projection is a
// symmetric frustum, so only its x and y scales change; the depth stays as the engine set it
//-----------------------------------------------------------------------------
int RcViewmodelFovBegin( void )
{
	if ( !rc_viewmodel_fov || rc_viewmodel_fov->value <= 0 || !gl.IsGLAllowed() )
		return 0;

	float fov = rc_viewmodel_fov->value;

	if ( fov > 170 )
		fov = 170;

	GLfloat m[16];
	GLint mode;

	gl.glGetFloatv( GL_PROJECTION_MATRIX, m );

	if ( m[0] <= 0 || m[11] != -1 )	// not a perspective projection
		return 0;

	float scale = 1.0 / ( tan( fov * M_PI / 360.0 ) * m[0] );
	m[0] *= scale;
	m[5] *= scale;

	gl.glGetIntegerv( GL_MATRIX_MODE, &mode );
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPushMatrix();
	gl.glLoadMatrixf( m );
	gl.glMatrixMode( mode );

	return 1;
}

void RcViewmodelFovEnd( int on )
{
	if ( !on )
		return;

	GLint mode;

	gl.glGetIntegerv( GL_MATRIX_MODE, &mode );
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPopMatrix();
	gl.glMatrixMode( mode );
}

//-----------------------------------------------------------------------------
// B184 rc_rawinput: raw mouse counts (WM_INPUT) instead of the cursor movement, which Windows accelerates
// and which the game forced to "enhance pointer precision".
// 1 reads them on the game window. Its procedure belongs to the engine, so the client chains its own in
// front of it the first time the mouse moves in game, and the counts come in when the engine reads its
// window messages, once a frame.
// 2 reads them the way RInput does: a thread of its own, with a window of its own, adds up each count as
// it arrives, and the mouse move takes everything that came in up to that moment.
//-----------------------------------------------------------------------------
#define RC_RAW_TAKE		( WM_APP + 1 )	// rc_rawinput 2: the input thread takes the raw mouse

extern "C" IMAGE_DOS_HEADER __ImageBase;	// client.dll's module handle, for the window class

static const char	s_szRawClass[] = "ReCoF raw input";

// adds up the counts of one WM_INPUT
static void RcRawAdd( LPARAM lParam )
{
	RAWINPUT ri;
	UINT size = sizeof( ri );

	if ( g_iVisibleMouse )
		return;	// a menu has the cursor; these counts would turn the view once it closes

	if ( GetRawInputData( (HRAWINPUT)lParam, RID_INPUT, &ri, &size, sizeof( RAWINPUTHEADER ) ) != (UINT)-1 &&
		 ri.header.dwType == RIM_TYPEMOUSE && !( ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) )
	{
		InterlockedExchangeAdd( &s_lRawDX, ri.data.mouse.lLastX );
		InterlockedExchangeAdd( &s_lRawDY, ri.data.mouse.lLastY );
	}
}

// rc_rawinput 1: in front of the engine's procedure of the game window
static LRESULT CALLBACK RcRawWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	if ( msg == WM_INPUT )
		RcRawAdd( lParam );

	return CallWindowProcA( s_pfnRawOldProc, hWnd, msg, wParam, lParam );
}

// gives the raw mouse of the process to hWnd; NULL with RIDEV_REMOVE gives it up
static BOOL RcRawRegister( HWND hWnd, DWORD dwFlags )
{
	RAWINPUTDEVICE rid;

	rid.usUsagePage = 1;	// generic desktop
	rid.usUsage = 2;		// mouse
	rid.dwFlags = dwFlags;
	rid.hwndTarget = hWnd;

	return RegisterRawInputDevices( &rid, 1, sizeof( rid ) );
}

// rc_rawinput 2: the window of the input thread, a message-only window that is never shown
static LRESULT CALLBACK RcRawThreadWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	if ( msg == WM_INPUT )
	{
		RcRawAdd( lParam );	// DefWindowProc below frees the input
	}
	else if ( msg == RC_RAW_TAKE )
	{
		// RIDEV_INPUTSINK, as a message-only window is never in the foreground. What comes in while the
		// game is in the background is dropped when the game gets the mouse back (RcRawMouseReset)
		if ( !RcRawRegister( hWnd, RIDEV_INPUTSINK ) )
			s_bRawFailed = 1;	// keep the cursor movement

		return 0;
	}

	return DefWindowProcA( hWnd, msg, wParam, lParam );
}

// rc_rawinput 2: the input thread
static DWORD WINAPI RcRawThread( LPVOID pParam )
{
	HWND hWnd;
	MSG msg;

	hWnd = CreateWindowExA( 0, s_szRawClass, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, (HINSTANCE)&__ImageBase, NULL );
	if ( !hWnd )
	{
		s_bRawFailed = 1;	// keep the cursor movement
		return 0;
	}

	// a count taken late turns the view one frame later, so this thread goes before the game's
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST );

	RcRawThreadWndProc( hWnd, RC_RAW_TAKE, 0, 0 );
	s_hRawThreadWnd = hWnd;

	while ( GetMessageA( &msg, NULL, 0, 0 ) > 0 )
		DispatchMessageA( &msg );

	s_hRawThreadWnd = NULL;
	DestroyWindow( hWnd );
	return 0;
}

// gives the raw mouse to the window of rc_rawinput 1 or 2; the counts start over
static void RcRawTake( int mode )
{
	InterlockedExchange( &s_lRawDX, 0 );
	InterlockedExchange( &s_lRawDY, 0 );
	s_iRawMode = mode;

	if ( mode == 2 )
	{
		if ( s_hRawThread )
		{
			// back from the game window or from RInput; a thread that has no window yet takes it itself
			if ( s_hRawThreadWnd )
				PostMessageA( s_hRawThreadWnd, RC_RAW_TAKE, 0, 0 );
			return;
		}

		WNDCLASSA wc;
		memset( &wc, 0, sizeof( wc ) );
		wc.lpfnWndProc = RcRawThreadWndProc;
		wc.hInstance = (HINSTANCE)&__ImageBase;
		wc.lpszClassName = s_szRawClass;

		if ( !RegisterClassA( &wc ) )
		{
			UnregisterClassA( s_szRawClass, wc.hInstance );	// left by a client.dll whose thread did not stop
			if ( !RegisterClassA( &wc ) )
			{
				s_bRawFailed = 1;	// keep the cursor movement
				return;
			}
		}

		s_hRawThread = CreateThread( NULL, 0, RcRawThread, NULL, 0, &s_dwRawThreadId );
		if ( !s_hRawThread )
			s_bRawFailed = 1;
		return;
	}

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

		if ( !hWnd )
		{
			s_bRawFailed = 1;	// keep the cursor movement
			return;
		}

		s_pfnRawOldProc = (WNDPROC)SetWindowLongPtrA( hWnd, GWLP_WNDPROC, (LONG_PTR)RcRawWndProc );
		s_hRawWnd = hWnd;
	}

	if ( !RcRawRegister( s_hRawWnd, 0 ) )
		s_bRawFailed = 1;	// keep the cursor movement
}

int RcRawInputOn( void )
{
	return rc_rawinput && rc_rawinput->value && !s_bRawFailed;
}

// called from IN_ActivateMouse: drop what came in while the console or a menu had the mouse
void RcRawMouseReset( void )
{
	InterlockedExchange( &s_lRawDX, 0 );
	InterlockedExchange( &s_lRawDY, 0 );
}

// called from IN_MouseMove: replaces *mx and *my with the raw counts since the last call
void RcRawMouse( int *mx, int *my )
{
	int mode;

	if ( !RcRawInputOn() )
		return;

	// B300: Windows gives the raw mouse of a process to one window, and RInput takes it for its own.
	// RInput hands its raw counts out through GetCursorPos, so *mx and *my already hold them: leave the
	// mouse to it, and take it back if it goes
	if ( GetModuleHandleA( "RInput.dll" ) )
	{
		if ( !s_bRawRInput )
			gEngfuncs.Con_Printf( "RInput is loaded: rc_rawinput leaves the mouse to it\n" );

		s_bRawRInput = 1;
		s_iRawMode = 0;
		return;
	}

	s_bRawRInput = 0;
	mode = ( rc_rawinput->value >= 2 ) ? 2 : 1;

	if ( s_iRawMode != mode )
	{
		RcRawTake( mode );
		return;	// the cursor movement this once
	}

	if ( mode == 2 && !s_hRawThreadWnd )
		return;	// the thread does not have the raw mouse yet

	*mx = InterlockedExchange( &s_lRawDX, 0 );
	*my = InterlockedExchange( &s_lRawDY, 0 );
}

// called from IN_Shutdown (B299). The engine unloads client.dll right after, so nothing of it may run on
// the input thread or stay in the game window's procedure: the engine destroys the window after the
// unload, and destroying it still runs its procedure. Left in front of the engine's, RcRawWndProc crashed
// the game on quit (Windows logs the fault in client.dll_unloaded), and a video mode restart goes the
// same way
void RcRawMouseShutdown( void )
{
	int i;

	if ( s_hRawThread )
	{
		// WM_QUIT needs the thread's message queue, which it has once it made its window
		for ( i = 0; i < 40 && WaitForSingleObject( s_hRawThread, 0 ) == WAIT_TIMEOUT; i++ )
		{
			PostThreadMessageA( s_dwRawThreadId, WM_QUIT, 0, 0 );
			WaitForSingleObject( s_hRawThread, 50 );
		}

		CloseHandle( s_hRawThread );
		s_hRawThread = NULL;
		UnregisterClassA( s_szRawClass, (HINSTANCE)&__ImageBase );
	}

	// another program that chained its procedure in front of this one would be cut off: leave it then
	if ( s_hRawWnd && GetWindowLongPtrA( s_hRawWnd, GWLP_WNDPROC ) == (LONG_PTR)RcRawWndProc )
		SetWindowLongPtrA( s_hRawWnd, GWLP_WNDPROC, (LONG_PTR)s_pfnRawOldProc );

	if ( s_iRawMode )
		RcRawRegister( NULL, RIDEV_REMOVE );	// not while RInput has the raw mouse (B300)

	s_hRawWnd = NULL;
	s_iRawMode = 0;
}

//-----------------------------------------------------------------------------
// B304 rc_vsync: the engine never sets the swap interval, so the driver's default applied, which is vsync
// on, and only the driver's control panel could turn it off. 0 turns it off, 1 on. It is set when the value
// or the GL context changes; a setting forced in the driver's control panel still wins
//-----------------------------------------------------------------------------
typedef BOOL (WINAPI *RcSwapInterval_t)( int interval );

void RcVsyncFrame( void )
{
	static HGLRC s_hContext;
	static int s_iApplied = -1;
	static RcSwapInterval_t s_pfnSwapInterval;

	if ( !rc_vsync )
		return;

	HGLRC hContext = wglGetCurrentContext();
	if ( !hContext )
		return;

	int iWant = rc_vsync->value ? 1 : 0;
	if ( hContext == s_hContext && iWant == s_iApplied )
		return;

	if ( hContext != s_hContext )
	{
		s_hContext = hContext;
		s_pfnSwapInterval = (RcSwapInterval_t)wglGetProcAddress( "wglSwapIntervalEXT" );
		if ( !s_pfnSwapInterval )
			gEngfuncs.Con_Printf( "rc_vsync: the driver has no wglSwapIntervalEXT, so vsync stays as the driver sets it\n" );
	}

	s_iApplied = iWant;
	if ( s_pfnSwapInterval )
		s_pfnSwapInterval( iWant );
}
