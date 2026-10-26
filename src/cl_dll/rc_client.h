// ReCoF client additions (rc_client.cpp)

#ifndef RC_CLIENT_H
#define RC_CLIENT_H

// registers the ReCoF client cvars and commands (called from InitInput)
void RcClient_Init( void );

// +rc_ducktap and rc_autojump (called at the end of CL_CreateMove)
void RcClient_CreateMove( struct usercmd_s *cmd );

// rc_hlmovement: 1 when the server runs Half-Life 1 movement (physinfo "hlm", the key pm_shared reads)
int RcHLMovement( void );

// the movement mode tag and the speedometer (called from CHud::Redraw)
void RcClient_Redraw( float flTime, int intermission );

#endif // RC_CLIENT_H
