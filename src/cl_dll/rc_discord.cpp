// ReCoF Discord Rich Presence (B316), laid out as BunnymodXT's for Half-Life:
//   ReCoF                                     (the Discord application's name)
//   Map: c_start | Version 0.2.1              (in the menus: Version 0.2.1)
//   Playing | FPS: 300.0 | Hard               (or In the menu; in co-op Playing co-op (2 of 4))
// with the time since the game started. The menus are the engine's own (no map loaded) and the 3D menu maps. It talks
// to the Discord client through discord-rpc (the src/external/discord-rpc submodule: the fork BunnymodXT uses, MIT),
// whose own thread keeps the connection and connects again when Discord starts later; nothing happens without a
// Discord client. rc_discord 0 turns it off. The presence used to come from the ReCoF Launcher; it is the game's now.

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "rc_client.h"
#include "rc_version.h"
#include "discord_rpc.h"

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// the ReCoF Discord application (the id is public: every Discord client that shows the presence gets it)
static const char RC_DISCORD_APP_ID[] = "1564718095418003586";

static cvar_t	*rc_discord;
static bool		s_bConnected;		// Discord_Initialize was called
static int64_t	s_iSessionStart;	// when the game started, for the elapsed time
static char		s_szShown[384];		// the place and the state last sent, so an unchanged presence is not sent again
static double	s_flSentAt;			// when it was last sent
static double	s_flFpsStart;		// the game frames of the last second, for the FPS when nothing is drawn
static int		s_iFpsFrames;
static float	s_flFrameFps;

#define RC_DISCORD_REFRESH	15.0	// seconds between updates that only change the FPS (Discord takes a few a minute)

// the maps that are menus rather than places
static bool RcDiscordMenuMap( const char *pszMap )
{
	static const char *s_pszMenus[] = { "c_game_menu1", "c_loadgame", "c_difficulty_settings", "c_server_settings",
		"c_video_stress_test" };

	for ( int i = 0; i < ARRAYSIZE( s_pszMenus ); i++ )
	{
		if ( !strcmp( pszMap, s_pszMenus[i] ) )
			return true;
	}
	return false;
}

// the map's name without maps/ and .bsp, empty when none is loaded
static void RcDiscordMap( char *pszOut, int iSize )
{
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	pszOut[0] = 0;
	if ( !pszLevel || !pszLevel[0] )
		return;

	if ( !strncmp( pszLevel, "maps/", 5 ) )
		pszLevel += 5;

	_snprintf( pszOut, iSize, "%s", pszLevel );
	pszOut[iSize - 1] = 0;

	char *pszDot = strrchr( pszOut, '.' );
	if ( pszDot && !_stricmp( pszDot, ".bsp" ) )
		*pszDot = 0;
}

// the difficulty the server set (hl.dll skillset: 1 to 4); a client of another server does not have it
static const char *RcDiscordDifficulty( void )
{
	switch ( (int)gEngfuncs.pfnGetCvarFloat( "difficulty" ) )
	{
	case 1:		return "Easy";
	case 2:		return "Normal";
	case 3:		return "Hard";
	case 4:		return "Nightmare";
	default:	return NULL;
	}
}

static double RcDiscordNow( void )
{
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter( &now );
	QueryPerformanceFrequency( &freq );
	return (double)now.QuadPart / (double)freq.QuadPart;
}

// the players in the game, in co-op
static int RcDiscordPlayers( void )
{
	int iPlayers = 0;
	for ( int i = 1; i <= gEngfuncs.GetMaxClients(); i++ )
	{
		hud_player_info_t info;
		memset( &info, 0, sizeof( info ) );
		gEngfuncs.pfnGetPlayerInfo( i, &info );
		if ( info.name && info.name[0] )
			iPlayers++;
	}
	return iPlayers;
}

void RcDiscord_Init( void )
{
	rc_discord = gEngfuncs.pfnRegisterVariable( "rc_discord", "1", FCVAR_ARCHIVE );
	s_iSessionStart = (int64_t)time( NULL );
}

// HUD_Frame: connects, follows the game, and lets discord-rpc hand over what its thread received
void RcDiscord_Frame( void )
{
	if ( !rc_discord || !rc_discord->value )
	{
		if ( s_bConnected )
			RcDiscord_Shutdown();
		return;
	}

	if ( !s_bConnected )
	{
		DiscordEventHandlers handlers;
		memset( &handlers, 0, sizeof( handlers ) );
		Discord_Initialize( RC_DISCORD_APP_ID, &handlers, 0, NULL );
		s_bConnected = true;
		s_szShown[0] = 0;
	}

	double now = RcDiscordNow();

	// the frames drawn each second while playing, else the game's frames each second (the menus)
	s_iFpsFrames++;
	if ( now - s_flFpsStart >= 1.0 || now < s_flFpsStart )
	{
		if ( now - s_flFpsStart < 2.0 && now >= s_flFpsStart )
			s_flFrameFps = (float)( s_iFpsFrames / ( now - s_flFpsStart ) );
		s_flFpsStart = now;
		s_iFpsFrames = 0;
	}
	float flFps = RcSmooth_Fps();
	if ( flFps <= 0 )
		flFps = s_flFrameFps;

	char szMap[64];
	RcDiscordMap( szMap, sizeof( szMap ) );
	bool bMenu = !szMap[0] || RcDiscordMenuMap( szMap );

	char szDetails[128], szState[128], szWhere[64];
	if ( bMenu )
		_snprintf( szDetails, sizeof( szDetails ), "Version %s", RECOF_VERSION );
	else
		_snprintf( szDetails, sizeof( szDetails ), "Map: %s | Version %s", szMap, RECOF_VERSION );
	szDetails[sizeof( szDetails ) - 1] = 0;

	int iMaxPlayers = gEngfuncs.GetMaxClients();
	if ( bMenu )
		_snprintf( szWhere, sizeof( szWhere ), "In the menu" );
	else if ( iMaxPlayers > 1 )
		_snprintf( szWhere, sizeof( szWhere ), "Playing co-op (%d of %d)", RcDiscordPlayers(), iMaxPlayers );
	else
		_snprintf( szWhere, sizeof( szWhere ), "Playing" );
	szWhere[sizeof( szWhere ) - 1] = 0;

	const char *pszDifficulty = RcDiscordDifficulty();
	if ( pszDifficulty )
		_snprintf( szState, sizeof( szState ), "%s | FPS: %.1f | %s", szWhere, flFps, pszDifficulty );
	else
		_snprintf( szState, sizeof( szState ), "%s | FPS: %.1f", szWhere, flFps );
	szState[sizeof( szState ) - 1] = 0;

	// a new place, state or difficulty goes at once; the FPS alone every RC_DISCORD_REFRESH seconds
	char szShow[384];
	_snprintf( szShow, sizeof( szShow ), "%s|%s|%s", szDetails, szWhere, pszDifficulty ? pszDifficulty : "" );
	szShow[sizeof( szShow ) - 1] = 0;

	bool bChanged = strcmp( szShow, s_szShown ) != 0;
	if ( bChanged || now - s_flSentAt >= RC_DISCORD_REFRESH || now < s_flSentAt )
	{
		strcpy( s_szShown, szShow );
		s_flSentAt = now;

		DiscordRichPresence presence;
		memset( &presence, 0, sizeof( presence ) );
		presence.details = szDetails;
		presence.state = szState;
		presence.startTimestamp = s_iSessionStart;
		presence.largeImageKey = "recof";	// an art asset of that name in the Discord application, if it has one
		presence.largeImageText = "ReCoF";
		presence.buttonLabel[0] = "Get ReCoF";
		presence.buttonUrl[0] = "https://github.com/caixax/ReCoF";
		Discord_UpdatePresence( &presence );
	}

	Discord_RunCallbacks();
}

// HUD_Shutdown, or rc_discord 0: the presence goes away and discord-rpc's thread ends
void RcDiscord_Shutdown( void )
{
	if ( !s_bConnected )
		return;

	Discord_ClearPresence();
	Discord_Shutdown();
	s_bConnected = false;
	s_szShown[0] = 0;
	s_flSentAt = 0;
}
