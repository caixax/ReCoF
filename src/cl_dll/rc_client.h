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

// the run timer's view of the game: the engine's pause flag and the map drawn (from V_CalcRefdef), a map loading
// (from CHud::VidInit)
void RcSpeedrun_View( int paused );
void RcSpeedrun_VidInit( void );

// the values the LiveSplit autosplitter reads in client.dll, found by the tag (rc_client.cpp, docs/speedrun.md)
struct RcAutosplit_t
{
	char	magic[16];	// "ReCoF_AUTOSPLIT", written at start-up (the whole tag is in no other place)
	int		version;	// 1
	void	*cutscene;	// 4 bytes, retail +0x18ED64: gHUD.m_BlackBar_guess.m_iOn_guess
	void	*igt;		// 4 bytes, retail +0x1BE920: decaltextures[0] + 8 (0 until a map's decals load)
	void	*music;		// 40 bytes, retail +0x5440C8: READ_STRING's buffer + 0x58
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

#endif // RC_CLIENT_H
