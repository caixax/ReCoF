// ReCoF practice views (B317), the client half of BunnymodXT's bxt_wallhack, bxt_novis and bxt_disable_vgui:
//  - rc_wallhack draws the world see-through (gl_renderer.cpp), so monsters, items and brush entities show behind
//    walls; rc_wallhack_alpha (0.6) sets how much of the world shows, rc_wallhack_additive 1 adds it instead.
//    It needs sv_cheats 1 and the ReCoF renderer (gl_renderer 1).
//  - rc_novis (a server cvar, hl.dll game.cpp) also draws the whole map: the client sets the engine's r_novis while
//    it and sv_cheats are on, and puts it back after. The server sends every entity for it.
//  - rc_disable_vgui hides the VGUI panels: notes, subtitles, menus and the HUD parts VGUI draws
//    (vgui_TeamFortressViewport.cpp).

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "rc_client.h"

static void RcPracticeCommands( void );

static cvar_t	*rc_wallhack;
static cvar_t	*rc_wallhack_additive;
static cvar_t	*rc_wallhack_alpha;
static cvar_t	*rc_disable_vgui;
static cvar_t	*s_pCheats;
static cvar_t	*s_pNovis;		// the engine's r_novis
static bool		s_bNovisSet;	// rc_novis turned r_novis on

void RcPractice_Init( void )
{
	rc_wallhack = gEngfuncs.pfnRegisterVariable( "rc_wallhack", "0", 0 );
	rc_wallhack_additive = gEngfuncs.pfnRegisterVariable( "rc_wallhack_additive", "0", 0 );
	rc_wallhack_alpha = gEngfuncs.pfnRegisterVariable( "rc_wallhack_alpha", "0.6", 0 );
	rc_disable_vgui = gEngfuncs.pfnRegisterVariable( "rc_disable_vgui", "0", 0 );
	s_pCheats = gEngfuncs.pfnGetCvarPointer( "sv_cheats" );
	s_pNovis = gEngfuncs.pfnGetCvarPointer( "r_novis" );
	RcPracticeCommands();
}

static bool RcCheats( void )
{
	return s_pCheats && s_pCheats->value;
}

// HUD_Frame: r_novis follows rc_novis. The server's cvar is read from the same cvar list in a game this client hosts;
// a client of another server has no rc_novis and leaves r_novis alone
void RcPractice_Frame( void )
{
	if ( !s_pNovis )
		return;

	cvar_t *pRcNovis = gEngfuncs.pfnGetCvarPointer( "rc_novis" );
	bool bWant = pRcNovis && pRcNovis->value && RcCheats();

	if ( bWant && !s_pNovis->value )
	{
		gEngfuncs.Cvar_SetValue( "r_novis", 1 );
		s_bNovisSet = true;
	}
	else if ( !bWant && s_bNovisSet )
	{
		gEngfuncs.Cvar_SetValue( "r_novis", 0 );
		s_bNovisSet = false;
	}
}

int RcWallhack( float *pflAlpha, int *pbAdditive )
{
	if ( !rc_wallhack || !rc_wallhack->value || !RcCheats() )
		return 0;

	float flAlpha = rc_wallhack_alpha->value;
	if ( flAlpha < 0 )
		flAlpha = 0;
	if ( flAlpha > 1 )
		flAlpha = 1;

	*pflAlpha = flAlpha;
	*pbAdditive = rc_wallhack_additive->value != 0;
	return 1;
}

// the practice commands run on the server (hl.dll rc_cheats.cpp); the client has them too, so the console offers
// them as they are typed, and hands each one on with its arguments
static void RcForwardToServer_f( void )
{
	char szLine[512];
	int iLen = 0;
	szLine[0] = 0;

	for ( int i = 0; i < gEngfuncs.Cmd_Argc() && iLen < (int)sizeof( szLine ) - 2; i++ )
	{
		const char *pszArg = gEngfuncs.Cmd_Argv( i );
		int n = _snprintf( szLine + iLen, sizeof( szLine ) - iLen, i ? " %s" : "%s", pszArg );
		if ( n < 0 )
		{
			szLine[iLen] = 0;	// too long: the line stops at the last whole argument
			break;
		}
		iLen += n;
	}
	szLine[sizeof( szLine ) - 1] = 0;

	gEngfuncs.pfnServerCmd( szLine );
}

static void RcPracticeCommands( void )
{
	static const char *s_pszCommands[] = { "rc_ch_set_health", "rc_ch_set_pos", "rc_ch_set_pos_offset", "rc_ch_get_pos",
		"rc_ch_set_vel", "rc_ch_set_vel_angles", "rc_set_angles", "rc_ch_set_angles", "rc_ch_give" };	// rc_ch_give: B346

	for ( int i = 0; i < ARRAYSIZE( s_pszCommands ); i++ )
		gEngfuncs.pfnAddCommand( (char *)s_pszCommands[i], RcForwardToServer_f );
}

int RcDisableVgui( void )
{
	return rc_disable_vgui && rc_disable_vgui->value;
}
