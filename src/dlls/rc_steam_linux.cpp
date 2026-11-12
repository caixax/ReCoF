// ReCoF: built only for the Linux server game DLL (tools/build_linux.py). Cry of Fear's game DLL calls the Steam API
// for the player on the machine (SteamAPI_Init in GameDLLInit, the stats in client.cpp and multiplay_gamerules.cpp,
// the donator group in player.cpp); only dedicated servers load hl.so, which have no such player. These answer
// "no Steam", and the code's NULL tests skip the calls, as on a Windows server where SteamAPI_Init is not called
// (B325).
#if !defined( _WIN32 )

class ISteamUserStats;
class ISteamFriends;

extern "C" bool SteamAPI_Init()
{
	return false;
}

extern "C" ISteamUserStats *SteamUserStats()
{
	return 0;
}

extern "C" ISteamFriends *SteamFriends()
{
	return 0;
}

#endif
