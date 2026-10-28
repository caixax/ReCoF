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

#endif // RC_CLIENT_H
