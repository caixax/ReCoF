// ReCoF client additions (rc_client.cpp)

#ifndef RC_CLIENT_H
#define RC_CLIENT_H

// rc_hlmovement: 1 when the server runs Half-Life 1 movement (physinfo "hlm", the key pm_shared reads)
int RcHLMovement( void );

// called from CHud::Redraw
void RcClient_Redraw( float flTime, int intermission );

#endif // RC_CLIENT_H
