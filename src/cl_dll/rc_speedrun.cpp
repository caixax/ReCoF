// ReCoF speedrun HUD: RebuiltSimon's information panel and HUD numbers, and an in-game run timer
//
// RebuiltSimon, the Cry of Fear speedrun tool, is GPL 3, so nothing here comes from its source: the panel, the
// numbers and their cvars are made again from what it shows, with rc_ names (B310, B311). The run timer (B312)
// counts the time the LiveSplit script cryoffear.asl counts, from the same values: real time, without pauses,
// loads, menus and the cutscenes that take the control away. rc_timer_start, rc_timer_stop and rc_timer_reset
// work it by hand as BunnymodXT's bxt_timer_* do, and the ReCoF autosplitter for LiveSplit (B313) reads it
// through g_RcRunTimer, so LiveSplit's game time is the game's.

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "pm_defs.h"
#include "con_nprint.h"
#include "rc_client.h"
#include "rc_version.h"
#include "glmanager.h"
#include "vgui_TeamFortressViewport.h"
#include "HUDControl_guess.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <windows.h>

extern "C" playermove_t *pmove;		// pm_shared.c: the move client prediction ran last

// the speedometer and the other HUD numbers (B311)
static cvar_t	*rc_speedometer;
static cvar_t	*rc_speedometer_jump;
static cvar_t	*rc_speedometer_3d;
static cvar_t	*rc_speedometer_pos;
static cvar_t	*rc_speedometer_jump_pos;
static cvar_t	*rc_speedometer_color;
static cvar_t	*rc_hud_health;
static cvar_t	*rc_hud_health_pos;
static cvar_t	*rc_hud_stamina;
static cvar_t	*rc_hud_stamina_pos;
static cvar_t	*rc_hud_centered;
static vec3_t	s_vecLastVelocity;	// the velocity of the frame before
static int		s_iJumpSpeed;		// the speed at the last takeoff

// the information panel (B310)
static cvar_t	*rc_info;
static cvar_t	*rc_info_offset;
static cvar_t	*rc_info_color;
static cvar_t	*rc_info_background;
static cvar_t	*rc_info_background_color;
static cvar_t	*rc_info_stats;
static cvar_t	*rc_info_ttl;
static cvar_t	*rc_info_health;
static cvar_t	*rc_info_stamina;
static cvar_t	*rc_info_speed;
static con_nprint_t	s_RcInfo;		// the engine's notify line being written
static int		s_iRcInfoLines;		// lines the panel took on the last frame, for its background
static int		s_iRcInfoWidth;		// its widest line in pixels

// the run timer (B312)
static cvar_t	*rc_hud_timer;
static cvar_t	*rc_hud_timer_pos;
static cvar_t	*rc_hud_timer_color;
static cvar_t	*rc_timer_auto;
static bool		s_bRunActive;		// a run was started and not reset, as LiveSplit's timer
static bool		s_bRunEnded;		// the run reached an ending; its time stays
static bool		s_bRunHeld;			// rc_timer_stop: the time stays until rc_timer_start
static bool		s_bRunCounting;		// the last frame counted
static double	s_flRunTime;		// seconds counted
static float	s_flRunLastIgt;		// cryoffear.asl "igt" on the last frame
static LARGE_INTEGER	s_liRunLast;	// when the last frame ran, 0 before the first

// what the ReCoF autosplitter reads (B313), found in client.dll by its tag as g_RcAutosplit is. A block of its
// own, so the script addition in docs/speedrun.md, which looks for g_RcAutosplit with its version 1, still finds
// it. The time is a double at an 8 byte boundary, written in one store
struct RcRunTimer_t
{
	char	magic[16];		// "ReCoF_RUNTIMER", written at start-up (the whole tag is in no other place)
	int		version;		// 1
	int		state;			// 0 reset, 1 running, 2 stopped by rc_timer_stop, 3 ended at an ending
	double	time;			// seconds counted
	int		runId;			// one more each time a run starts from zero
	int		counting;		// the time advanced on the last frame
	char	recof[16];		// RECOF_VERSION
};

RcRunTimer_t g_RcRunTimer;

//-----------------------------------------------------------------------------
// helpers
//-----------------------------------------------------------------------------

// RebuiltSimon shows its HUD in a map, not in the 3D menu
static bool RcInGame( void )
{
	const char *pszLevel = gEngfuncs.pfnGetLevelName();

	return pszLevel && pszLevel[0] && strncmp( pszLevel, "maps/c_game_menu", 16 ) && gEngfuncs.GetLocalPlayer();
}

static float RcSpeed( const float *v )
{
	if ( rc_speedometer_3d->value )
		return sqrt( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );

	return sqrt( v[0] * v[0] + v[1] * v[1] );
}

// a normalized position "x y" (0 to 1 of the screen) in pixels; a value that does not parse keeps the default
static void RcParsePos( const cvar_t *cvar, float flDefX, float flDefY, int *x, int *y )
{
	float fx, fy;

	if ( sscanf( cvar->string, "%f %f", &fx, &fy ) != 2 )
	{
		fx = flDefX;
		fy = flDefY;
	}

	*x = (int)( fx * ScreenWidth );
	*y = (int)( fy * ScreenHeight );
}

// "r g b" or "r g b a", 0 to 255; a value that does not parse keeps what color holds
static void RcParseColor( const cvar_t *cvar, int *color, int count )
{
	int v[4];

	if ( sscanf( cvar->string, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3] ) < count )
		return;

	for ( int i = 0; i < count; i++ )
		color[i] = v[i] < 0 ? 0 : ( v[i] > 255 ? 255 : v[i] );
}

static int RcStamina( void )
{
	return ( gViewPort && gViewPort->m_pHUDControl_guess ) ? gViewPort->m_pHUDControl_guess->m_iStamina_guess : 0;
}

// a rectangle blended over the picture; FillRGBA adds its color and cannot darken
static void RcFillBlend( int x, int y, int w, int h, const int *rgba )
{
	if ( !gl.IsGLAllowed() )
		return;

	GLboolean bAlphaTest = gl.glIsEnabled( GL_ALPHA_TEST );

	gl.glDisable( GL_TEXTURE_2D );
	gl.glDisable( GL_ALPHA_TEST );
	gl.glEnable( GL_BLEND );
	gl.glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	gl.glColor4ub( rgba[0], rgba[1], rgba[2], rgba[3] );

	gl.glBegin( GL_QUADS );
	gl.glVertex3f( x, y, 0 );
	gl.glVertex3f( x + w, y, 0 );
	gl.glVertex3f( x + w, y + h, 0 );
	gl.glVertex3f( x, y + h, 0 );
	gl.glEnd();

	// what FillRGBA leaves behind
	gl.glColor4ub( 255, 255, 255, 255 );
	gl.glEnable( GL_TEXTURE_2D );
	gl.glDisable( GL_BLEND );

	if ( bAlphaTest )
		gl.glEnable( GL_ALPHA_TEST );
}

//-----------------------------------------------------------------------------
// HUD numbers, drawn with the number_0 to number_9 sprites of sprites/hud.txt as RebuiltSimon draws them
//-----------------------------------------------------------------------------
static int RcDigitWidth( int iDigit )
{
	wrect_t &rc = gHUD.GetSpriteRect( gHUD.m_HUD_number_0 + iDigit );

	return rc.right - rc.left;
}

static int RcDigitHeight( void )
{
	wrect_t &rc = gHUD.GetSpriteRect( gHUD.m_HUD_number_0 );

	return rc.bottom - rc.top;
}

// draws the digits of pszText from x and returns the x after them
static int RcDrawDigits( int x, int y, const char *pszText, const int *color )
{
	for ( const char *p = pszText; *p; p++ )
	{
		if ( *p < '0' || *p > '9' )
			continue;

		int iSprite = gHUD.m_HUD_number_0 + ( *p - '0' );

		SPR_Set( gHUD.GetSprite( iSprite ), color[0], color[1], color[2] );
		SPR_DrawAdditive( 0, x, y, &gHUD.GetSpriteRect( iSprite ) );
		x += RcDigitWidth( *p - '0' );
	}

	return x;
}

// a number at x, y, centered on x with rc_hud_centered; RebuiltSimon draws 0 for anything below 1
static void RcDrawNumber( int iValue, int x, int y, const int *color )
{
	char szText[16];

	sprintf( szText, "%d", iValue > 0 ? iValue : 0 );

	if ( rc_hud_centered->value )
	{
		int w = 0;

		for ( const char *p = szText; *p; p++ )
			w += RcDigitWidth( *p - '0' );

		x -= w / 2;
	}

	RcDrawDigits( x, y, szText, color );
}

// the colon and the point of the timer, squares in the color of the digits; returns the x after them
static int RcDrawSeparator( int x, int y, bool bColon, const int *color )
{
	int iTall = RcDigitHeight();
	int iSize = iTall / 6 > 2 ? iTall / 6 : 2;

	x += iSize / 2;

	if ( bColon )
	{
		FillRGBA( x, y + iTall / 3 - iSize / 2, iSize, iSize, color[0], color[1], color[2], 255 );
		FillRGBA( x, y + iTall * 2 / 3 - iSize / 2, iSize, iSize, color[0], color[1], color[2], 255 );
	}
	else
	{
		FillRGBA( x, y + iTall - iSize, iSize, iSize, color[0], color[1], color[2], 255 );
	}

	return x + iSize + iSize / 2;
}

//-----------------------------------------------------------------------------
// the run timer (B312). The values are the ones cryoffear.asl reads: the hw.dll ones at the offsets ReCoF keeps
// for it (tools/check_runner.py fails a build that moves them), the client.dll ones through g_RcAutosplit
// (rc_client.cpp). The script's rules are followed one for one; its test of combase.dll (a Windows file whose
// data changes with each Windows build) on the death screen is left out, so the timer keeps counting there.
//-----------------------------------------------------------------------------
#define RC_HW_PAUSE		0x11119E4	// float, nonzero while the game is paused
#define RC_HW_LOADING	0x10D279C	// float, 0 while a map loads
#define RC_HW_MAP		0xF1AA15	// the client's map as "c_nightmare.bsp"
#define RC_HW_MENUMAP	0x820495	// the server's map
#define RC_HW_CANMOVE	0x9FDC30	// float, 0 while the player cannot move

struct RcRunValues_t
{
	float	pause;
	float	loading;
	float	canmove;
	int		cutscene;
	float	igt;
	char	map[41];
	char	menumap[41];
	char	music[41];
};

static void RcCopyName( char *pszOut, const char *pszIn )
{
	strncpy( pszOut, pszIn, 40 );
	pszOut[40] = '\0';
}

static bool RcReadRun( RcRunValues_t *v )
{
	static char *s_pHw;

	if ( !s_pHw )
		s_pHw = (char *)GetModuleHandleA( "hw.dll" );	// the software engine sw.dll has other offsets: no timer

	if ( !s_pHw || !g_RcAutosplit.cutscene )
		return false;

	v->pause = *(float *)( s_pHw + RC_HW_PAUSE );
	v->loading = *(float *)( s_pHw + RC_HW_LOADING );
	v->canmove = *(float *)( s_pHw + RC_HW_CANMOVE );
	RcCopyName( v->map, s_pHw + RC_HW_MAP );
	RcCopyName( v->menumap, s_pHw + RC_HW_MENUMAP );

	v->cutscene = *(int *)g_RcAutosplit.cutscene;
	v->igt = *(float *)g_RcAutosplit.igt;
	RcCopyName( v->music, (const char *)g_RcAutosplit.music );
	return true;
}

// the first maps of the campaigns the script knows
static bool RcRunStartMap( const char *pszMap )
{
	static const char *s_pszMaps[] = { "c_nightmare.bsp", "c_doc_city.bsp", "c_rumpel1.bsp", "c_hallo_iambulletproof.bsp",
		"c_arvuti.bsp", "c_the_stairway1.bsp" };

	for ( int i = 0; i < ARRAYSIZE( s_pszMaps ); i++ )
	{
		if ( !strcmp( pszMap, s_pszMaps[i] ) )
			return true;
	}

	return false;
}

// cryoffear.asl start
static bool RcRunStart( const RcRunValues_t &v )
{
	if ( v.loading == 0 )
	{
		if ( !strcmp( v.map, "c_nightmare.bsp" ) || !strcmp( v.map, "c_doc_city.bsp" ) || !strcmp( v.map, "c_the_stairway1.bsp" ) )
			return true;

		if ( v.cutscene == 0 && ( !strcmp( v.map, "c_rumpel1.bsp" ) || !strcmp( v.map, "c_hallo_iambulletproof.bsp" ) || !strcmp( v.map, "c_arvuti.bsp" ) ) )
			return true;
	}

	return !strcmp( v.music, "coopstart.mp3" );
}

// cryoffear.asl reset
static bool RcRunReset( const RcRunValues_t &v, float flOldIgt )
{
	if ( RcRunStartMap( v.map ) && flOldIgt != 0 && v.igt == 0 )
		return true;

	return !strcmp( v.music, "Starting in 3 seconds" ) || !strcmp( v.map, "c_intro.bsp" );
}

// cryoffear.asl isLoading
static bool RcRunPaused( const RcRunValues_t &v )
{
	if ( v.pause != 0 || v.loading == 0 )
		return true;

	if ( v.cutscene && ( v.canmove == 0 || !strcmp( v.map, "c_subway2st3.bsp" ) || !strcmp( v.map, "c_trainscene.bsp" ) ) )
		return true;

	return !strcmp( v.menumap, "c_loadgame.bsp" ) || !strcmp( v.menumap, "c_difficulty_settings.bsp" ) || !strcmp( v.menumap, "c_game_menu1.bsp" );
}

// the endings cryoffear.asl splits on; the timer stops there
static bool RcRunEnd( const RcRunValues_t &v )
{
	static const char *s_pszMusic[] = { "endmusic1.mp3", "endmusic2.mp3", "endmusic3.mp3", "endmusic4.mp3", "coopend.mp3",
		"manhunt.mp3", "lifelover.mp3", "survive_hotel_terror.mp3", "collab_csong.mp3" };

	for ( int i = 0; i < ARRAYSIZE( s_pszMusic ); i++ )
	{
		if ( !strcmp( v.music, s_pszMusic[i] ) )
			return true;
	}

	return !strcmp( v.map, "c_doc_ending.bsp" );
}

static void RcRunNew( void )
{
	s_bRunActive = true;
	s_bRunEnded = false;
	s_bRunHeld = false;
	s_bRunCounting = false;
	s_flRunTime = 0;
	g_RcRunTimer.runId++;
}

static void RcRunClear( void )
{
	s_bRunActive = false;
	s_bRunEnded = false;
	s_bRunHeld = false;
	s_bRunCounting = false;
	s_flRunTime = 0;
}

// rc_timer_start: starts a run from zero after a reset, or goes on with a stopped one, as bxt_timer_start. Runners
// bind it as Half-Life runners do: bind DEL "map c_nightmare;rc_timer_reset;rc_timer_start"
static void RcTimerStart_f( void )
{
	if ( !s_bRunActive )
	{
		RcRunNew();
		return;
	}

	s_bRunHeld = false;
	s_bRunEnded = false;
}

// rc_timer_stop: the time stays until rc_timer_start
static void RcTimerStop_f( void )
{
	if ( s_bRunActive )
		s_bRunHeld = true;
}

// rc_timer_reset: back to zero and not running
static void RcTimerReset_f( void )
{
	RcRunClear();
}

static void RcRunPublish( void )
{
	g_RcRunTimer.state = !s_bRunActive ? 0 : ( s_bRunEnded ? 3 : ( s_bRunHeld ? 2 : 1 ) );
	g_RcRunTimer.time = s_flRunTime;
	g_RcRunTimer.counting = s_bRunCounting;
}

static void RcRunFrame( void )
{
	LARGE_INTEGER now, freq;
	double dt = 0;

	QueryPerformanceCounter( &now );
	QueryPerformanceFrequency( &freq );

	if ( s_liRunLast.QuadPart )
		dt = (double)( now.QuadPart - s_liRunLast.QuadPart ) / freq.QuadPart;

	s_liRunLast = now;

	RcRunValues_t v;
	if ( !RcReadRun( &v ) )
	{
		RcRunPublish();
		return;
	}

	bool bAuto = rc_timer_auto->value != 0;

	float flOldIgt = s_flRunLastIgt;
	s_flRunLastIgt = v.igt;

	if ( s_bRunActive )
	{
		// a frame counts its time when it and the one before count, so a map load that blocks between two frames
		// never counts; a frame over a second is taken for one that was missed
		bool bCounting = !s_bRunEnded && !s_bRunHeld && !RcRunPaused( v );

		if ( bCounting && s_bRunCounting && dt < 1.0 )
			s_flRunTime += dt;

		s_bRunCounting = bCounting;

		// rc_timer_auto: the script's reset and end. A run that has counted nothing is not reset: the bind above
		// starts one while c_nightmare loads, when the script's reset (the decal counter dropping to 0) comes next
		if ( bAuto && s_flRunTime > 0 && RcRunReset( v, flOldIgt ) )
			RcRunClear();
		else if ( bAuto && !s_bRunEnded && RcRunEnd( v ) )
			s_bRunEnded = true;
	}

	if ( bAuto && !s_bRunActive && RcRunStart( v ) )
		RcRunNew();

	RcRunPublish();
}

static void RcDrawTimer( void )
{
	if ( !rc_hud_timer->value || gHUD.m_HUD_number_0 < 0 )
		return;

	int x, y;
	int color[3] = { 255, 255, 255 };

	RcParsePos( rc_hud_timer_pos, 0.0f, 0.5f, &x, &y );
	RcParseColor( rc_hud_timer_color, color, 3 );

	// MM:SS:MS, and HH:MM:SS:MS from an hour on
	int ms = (int)( s_flRunTime * 1000 );
	int h = ms / 3600000;
	char szPart[16];

	if ( h )
	{
		sprintf( szPart, "%02d", h );
		x = RcDrawDigits( x, y, szPart, color );
		x = RcDrawSeparator( x, y, true, color );
	}

	sprintf( szPart, "%02d", ms / 60000 % 60 );
	x = RcDrawDigits( x, y, szPart, color );
	x = RcDrawSeparator( x, y, true, color );
	sprintf( szPart, "%02d", ms / 1000 % 60 );
	x = RcDrawDigits( x, y, szPart, color );
	x = RcDrawSeparator( x, y, true, color );
	sprintf( szPart, "%03d", ms % 1000 );
	RcDrawDigits( x, y, szPart, color );
}

//-----------------------------------------------------------------------------
// the information panel (B310): RebuiltSimon's lines in the engine's notify area at the top right, on a dark box
//-----------------------------------------------------------------------------
static void RcInfoLine( const char *pszFormat, ... )
{
	char szText[80];
	va_list argptr;

	va_start( argptr, pszFormat );
	_vsnprintf( szText, sizeof( szText ) - 1, pszFormat, argptr );
	va_end( argptr );
	szText[sizeof( szText ) - 1] = '\0';

	gEngfuncs.Con_NXPrintf( &s_RcInfo, (char *)"%s", szText );

	int w, h;
	gEngfuncs.pfnDrawConsoleStringLen( szText, &w, &h );
	if ( w > s_iRcInfoWidth )
		s_iRcInfoWidth = w;

	s_RcInfo.index++;
}

static void RcInfoFrame( void )
{
	if ( !rc_info->value || !RcInGame() )
	{
		s_iRcInfoLines = 0;
		return;
	}

	int color[3] = { 255, 255, 255 };
	RcParseColor( rc_info_color, color, 3 );

	s_RcInfo.index = (int)rc_info_offset->value;
	s_RcInfo.time_to_live = rc_info_ttl->value;
	for ( int i = 0; i < 3; i++ )
		s_RcInfo.color[i] = color[i] / 255.0f;

	s_iRcInfoWidth = 0;

	RcInfoLine( "ReCoF v%s", RECOF_VERSION );	// one line where RebuiltSimon has its name and a version line
	s_RcInfo.index++;

	if ( rc_info_stats->value && ( rc_info_health->value || rc_info_stamina->value || rc_info_speed->value ) )
	{
		RcInfoLine( ".::  Simon Info  ::." );

		if ( rc_info_health->value )
			RcInfoLine( "Health:    %d", gHUD.m_Health.m_iHealth );

		if ( rc_info_stamina->value )
			RcInfoLine( "Stamina:   %d", RcStamina() );

		if ( rc_info_speed->value )
			RcInfoLine( "Speed:     %d", pmove ? (int)RcSpeed( pmove->velocity ) : 0 );

		s_RcInfo.index++;
	}

	// the time since Windows started, as RebuiltSimon shows it: a clock a recording cannot hide
	ULONGLONG ullMs = GetTickCount64();
	RcInfoLine( "%02lu:%02lu:%02lu.%03lu", (unsigned long)( ullMs / 3600000 ), (unsigned long)( ullMs / 60000 % 60 ),
		(unsigned long)( ullMs / 1000 % 60 ), (unsigned long)( ullMs % 1000 ) );

	s_iRcInfoLines = s_RcInfo.index - (int)rc_info_offset->value;
}

static void RcDrawInfoBackground( void )
{
	if ( !rc_info->value || !rc_info_background->value || s_iRcInfoLines <= 0 )
		return;

	int rgba[4] = { 20, 20, 20, 160 };
	RcParseColor( rc_info_background_color, rgba, 4 );

	// the engine draws notify line i at 20 + i * the console font's height, ending 10 pixels from the right
	// (console.c Con_DrawDebugArea); the box has a line of margin above and below, and is RebuiltSimon's 9 font
	// heights wide unless a line needs more
	int w, h;
	gEngfuncs.pfnDrawConsoleStringLen( "0", &w, &h );

	int iWide = s_iRcInfoWidth + 20 > 9 * h ? s_iRcInfoWidth + 20 : 9 * h;
	int y = 20 + h * (int)rc_info_offset->value - h;

	RcFillBlend( ScreenWidth - iWide, y, iWide, ( s_iRcInfoLines + 2 ) * h, rgba );
}

//-----------------------------------------------------------------------------
// the HUD numbers (B311)
//-----------------------------------------------------------------------------
static void RcDrawHudNumbers( void )
{
	if ( gHUD.m_HUD_number_0 < 0 )
		return;

	int color[3] = { 255, 255, 255 };
	RcParseColor( rc_speedometer_color, color, 3 );

	int x, y;

	if ( rc_speedometer->value && pmove )
	{
		const float *v = pmove->velocity;

		// the takeoff speed, measured as RebuiltSimon does: when the vertical speed leaves 0 or turns upward
		if ( ( v[2] != 0 && s_vecLastVelocity[2] == 0 ) || ( v[2] > 0 && s_vecLastVelocity[2] < 0 ) )
			s_iJumpSpeed = (int)RcSpeed( s_vecLastVelocity );

		VectorCopy( v, s_vecLastVelocity );

		RcParsePos( rc_speedometer_pos, 0.5f, 0.9f, &x, &y );
		RcDrawNumber( (int)RcSpeed( v ), x, y, color );

		if ( rc_speedometer_jump->value )
		{
			RcParsePos( rc_speedometer_jump_pos, 0.5f, 0.95f, &x, &y );
			RcDrawNumber( s_iJumpSpeed, x, y, color );
		}
	}

	if ( rc_hud_health->value )
	{
		RcParsePos( rc_hud_health_pos, 0.02f, 0.73f, &x, &y );
		RcDrawNumber( gHUD.m_Health.m_iHealth, x, y, color );
	}

	if ( rc_hud_stamina->value )
	{
		RcParsePos( rc_hud_stamina_pos, 0.07f, 0.73f, &x, &y );
		RcDrawNumber( RcStamina(), x, y, color );
	}
}

//-----------------------------------------------------------------------------
// entry points
//-----------------------------------------------------------------------------
void RcSpeedrun_Init( void )
{
	rc_speedometer = gEngfuncs.pfnRegisterVariable( "rc_speedometer", "0", FCVAR_ARCHIVE );
	rc_speedometer_jump = gEngfuncs.pfnRegisterVariable( "rc_speedometer_jump", "1", FCVAR_ARCHIVE );
	rc_speedometer_3d = gEngfuncs.pfnRegisterVariable( "rc_speedometer_3d", "0", FCVAR_ARCHIVE );
	rc_speedometer_pos = gEngfuncs.pfnRegisterVariable( "rc_speedometer_pos", ".5 .9", FCVAR_ARCHIVE );
	rc_speedometer_jump_pos = gEngfuncs.pfnRegisterVariable( "rc_speedometer_jump_pos", ".5 .95", FCVAR_ARCHIVE );
	rc_speedometer_color = gEngfuncs.pfnRegisterVariable( "rc_speedometer_color", "255 255 255", FCVAR_ARCHIVE );
	rc_hud_health = gEngfuncs.pfnRegisterVariable( "rc_hud_health", "0", FCVAR_ARCHIVE );
	rc_hud_health_pos = gEngfuncs.pfnRegisterVariable( "rc_hud_health_pos", ".02 .73", FCVAR_ARCHIVE );
	rc_hud_stamina = gEngfuncs.pfnRegisterVariable( "rc_hud_stamina", "0", FCVAR_ARCHIVE );
	rc_hud_stamina_pos = gEngfuncs.pfnRegisterVariable( "rc_hud_stamina_pos", ".07 .73", FCVAR_ARCHIVE );
	rc_hud_centered = gEngfuncs.pfnRegisterVariable( "rc_hud_centered", "1", FCVAR_ARCHIVE );

	rc_info = gEngfuncs.pfnRegisterVariable( "rc_info", "0", FCVAR_ARCHIVE );
	rc_info_offset = gEngfuncs.pfnRegisterVariable( "rc_info_offset", "4", FCVAR_ARCHIVE );
	rc_info_color = gEngfuncs.pfnRegisterVariable( "rc_info_color", "255 255 255", FCVAR_ARCHIVE );
	rc_info_background = gEngfuncs.pfnRegisterVariable( "rc_info_background", "1", FCVAR_ARCHIVE );
	rc_info_background_color = gEngfuncs.pfnRegisterVariable( "rc_info_background_color", "20 20 20 160", FCVAR_ARCHIVE );
	rc_info_stats = gEngfuncs.pfnRegisterVariable( "rc_info_stats", "1", FCVAR_ARCHIVE );
	rc_info_ttl = gEngfuncs.pfnRegisterVariable( "rc_info_ttl", "0.1", FCVAR_ARCHIVE );
	rc_info_health = gEngfuncs.pfnRegisterVariable( "rc_info_health", "1", FCVAR_ARCHIVE );
	rc_info_stamina = gEngfuncs.pfnRegisterVariable( "rc_info_stamina", "1", FCVAR_ARCHIVE );
	rc_info_speed = gEngfuncs.pfnRegisterVariable( "rc_info_speed", "1", FCVAR_ARCHIVE );

	rc_hud_timer = gEngfuncs.pfnRegisterVariable( "rc_hud_timer", "0", FCVAR_ARCHIVE );
	rc_hud_timer_pos = gEngfuncs.pfnRegisterVariable( "rc_hud_timer_pos", "0 .5", FCVAR_ARCHIVE );
	rc_hud_timer_color = gEngfuncs.pfnRegisterVariable( "rc_hud_timer_color", "255 255 255", FCVAR_ARCHIVE );
	rc_timer_auto = gEngfuncs.pfnRegisterVariable( "rc_timer_auto", "1", FCVAR_ARCHIVE );
	gEngfuncs.pfnAddCommand( "rc_timer_start", RcTimerStart_f );
	gEngfuncs.pfnAddCommand( "rc_timer_stop", RcTimerStop_f );
	gEngfuncs.pfnAddCommand( "rc_timer_reset", RcTimerReset_f );

	g_RcRunTimer.version = 1;
	strncpy( g_RcRunTimer.recof, RECOF_VERSION, sizeof( g_RcRunTimer.recof ) - 1 );
	strcpy( g_RcRunTimer.magic, "ReCoF_RUN" );
	strcat( g_RcRunTimer.magic, "TIMER" );
}

void RcSpeedrun_Frame( void )
{
	if ( !rc_info )
		return;

	RcRunFrame();
	RcInfoFrame();
}

void RcSpeedrun_Redraw( void )
{
	if ( !rc_info || !RcInGame() )
		return;

	RcDrawInfoBackground();
	RcDrawHudNumbers();
	RcDrawTimer();
}
