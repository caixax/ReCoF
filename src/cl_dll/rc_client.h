// ReCoF client additions (rc_client.cpp)

#ifndef RC_CLIENT_H
#define RC_CLIENT_H

// registers the ReCoF client cvars and commands (called from InitInput)
void RcClient_Init( void );

// +rc_ducktap and rc_autojump (called at the end of CL_CreateMove)
void RcClient_CreateMove( struct usercmd_s *cmd );

// rc_hlmovement: 1 when the server runs Half-Life 1 movement (physinfo "hlm", the key pm_shared reads)
int RcHLMovement( void );

// the custom crosshair, the movement mode tag and the speedometer (called from CHud::Redraw)
void RcClient_Redraw( float flTime, int intermission );

// rc_speedrun.cpp (B310, B311, B312): the speedometer and the other HUD numbers, the information panel and the run
// timer. Init registers their cvars (from RcClient_Init), Frame runs the timer and writes the panel's lines (from
// HUD_Frame), Redraw draws (from RcClient_Redraw)
void RcSpeedrun_Init( void );
void RcSpeedrun_Frame( void );
void RcSpeedrun_Redraw( void );

// rc_crash.cpp (B314): crash and hang reports in crashes\ next to cof.exe. Init installs them (from InitInput, after
// SteamAPI_Init), Frame is the hang watchdog's heartbeat (from HUD_Frame)
void RcCrash_Init( void );
void RcCrash_Frame( void );

// the run timer's view of the game: the engine's pause flag, the player's health and the map drawn (from
// V_CalcRefdef), a map loading (from CHud::VidInit)
void RcSpeedrun_View( int paused, int health );
void RcSpeedrun_VidInit( void );

// the values the LiveSplit autosplitter reads in client.dll, found by the tag (rc_client.cpp, docs/speedrun.md)
struct RcAutosplit_t
{
	char	magic[16];	// "ReCoF_AUTOSPLIT", written at start-up (the whole tag is in no other place)
	int		version;	// 1
	void	*cutscene;	// 4 bytes, retail +0x18ED64: gHUD.m_BlackBar_guess.m_iOn_guess
	void	*igt;		// 4 bytes, retail +0x1BE920: lastDrawTime (glows.cpp), 0 from a map's start to its first frame
	void	*music;		// 40 bytes, retail +0x5440C8: READ_STRING's buffer, the last string a message carried
	void	*savestate;	// 4 bytes, retail +0x18A3E0: gHUD.m_flSaveHackTime_guess
};

extern RcAutosplit_t g_RcAutosplit;

// rc_crosshair: 0 hides the Cry of Fear crosshair (asked by CHudAmmo::Draw)
int RcCrosshairOn( void );

// rc_viewmodel_ofs_forward, _right and _up (called from V_CalcNormalRefdef for the weapon model)
void RcViewmodelOffset( float *origin, const float *forward, const float *right, const float *up, float frametime );

// rc_viewmodel_fov (called around the studio draws of the weapon model and of the left hand item); End undoes
// what Begin did when Begin returned 1
int RcViewmodelFovBegin( void );
void RcViewmodelFovEnd( int on );

// B184 rc_rawinput (called from inputw32.cpp)
int RcRawInputOn( void );
void RcRawMouseReset( void );
void RcRawMouse( int *mx, int *my );
void RcRawMouseShutdown( void );

// B304 rc_vsync: sets the swap interval when rc_vsync or the GL context changes (called from HUD_Frame)
void RcVsyncFrame( void );

// rc_smooth.cpp (B315): smooth frames, pictures drawn between the game frames (rc_smoothframes). Init registers the
// cvars (from RcClient_Init), GameFrame counts the game frames (from HUD_Frame), View notes a game frame's view or gives
// a picture's camera move (from V_CalcRefdef), Redraw draws cl_showfps (from HUD_Redraw). RenderOnly is the number
// of the picture being drawn, 0 in a game frame, for code that changes something on every draw rather than with time;
// Ahead is the seconds since the game frame's view in such a picture
void RcSmooth_Init( void );
void RcSmooth_GameFrame( void );
int RcSmooth_View( struct ref_params_s *pparams, int bMove, float *move );
void RcSmooth_Redraw( void );
int RcSmooth_RenderOnly( void );
float RcSmooth_Ahead( void );

// rc_discord.cpp (B316): Discord Rich Presence. Init registers rc_discord (from RcClient_Init), Frame follows the game
// (from HUD_Frame), Shutdown takes the presence down (from HUD_Shutdown)
void RcDiscord_Init( void );
void RcDiscord_Frame( void );
void RcDiscord_Shutdown( void );

// rc_practice.cpp (B317): rc_wallhack, rc_novis's r_novis and rc_disable_vgui. Init registers them (from
// RcClient_Init), Frame keeps r_novis with rc_novis (from HUD_Frame); Wallhack returns 1 with the alpha and the blend
// while the wallhack is on (gl_renderer.cpp), DisableVgui whether the VGUI panels are hidden (the viewport)
void RcPractice_Init( void );
void RcPractice_Frame( void );
int RcWallhack( float *pflAlpha, int *pbAdditive );
int RcDisableVgui( void );

#endif // RC_CLIENT_H
