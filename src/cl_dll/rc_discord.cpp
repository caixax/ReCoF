// ReCoF Discord Rich Presence (B316): Discord shows that the player is in ReCoF, its version, the map and, in
// co-op, how many players there are, as BunnymodXT does for Half-Life. It talks to the Discord client through
// discord-rpc (src/external/discord-rpc, the fork BunnymodXT uses, MIT), whose own thread keeps the connection and
// connects again when Discord starts later. Nothing happens without a Discord client. rc_discord 0 turns it off.
// The presence used to come from the ReCoF Launcher while the game ran; it is the game's now.

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "rc_client.h"
#include "rc_version.h"
#include "discord-rpc/discord_rpc.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

// the ReCoF Discord application (the id is public: every Discord client that shows the presence gets it)
static const char RC_DISCORD_APP_ID[] = "1564718095418003586";

static cvar_t	*rc_discord;
static bool		s_bConnected;		// Discord_Initialize was called
static int64_t	s_iSessionStart;	// when the game started, for the elapsed time
static char		s_szShown[384];		// what was last sent, so an unchanged presence is not sent again

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

	char szMap[64];
	RcDiscordMap( szMap, sizeof( szMap ) );

	char szDetails[128], szState[128];
	_snprintf( szDetails, sizeof( szDetails ), "ReCoF %s", RECOF_VERSION );

	int iMaxPlayers = gEngfuncs.GetMaxClients();
	if ( !szMap[0] || RcDiscordMenuMap( szMap ) )
		_snprintf( szState, sizeof( szState ), "In the menus" );
	else if ( iMaxPlayers > 1 )
		_snprintf( szState, sizeof( szState ), "Co-op on %s (%d of %d)", szMap, RcDiscordPlayers(), iMaxPlayers );
	else
		_snprintf( szState, sizeof( szState ), "On %s", szMap );
	szDetails[sizeof( szDetails ) - 1] = 0;
	szState[sizeof( szState ) - 1] = 0;

	char szShow[384];
	_snprintf( szShow, sizeof( szShow ), "%s|%s", szDetails, szState );
	szShow[sizeof( szShow ) - 1] = 0;

	// Discord takes a few updates a minute at most; discord-rpc sends the latest when it may
	if ( strcmp( szShow, s_szShown ) )
	{
		strcpy( s_szShown, szShow );

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
}
