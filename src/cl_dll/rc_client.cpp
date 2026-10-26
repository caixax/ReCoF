// ReCoF client additions
//
// Every variable ReCoF adds to client.dll lives in this file, which is linked after all retail objects
// (tools/units.json, the client link order), so its data comes after theirs and no address the LiveSplit
// autosplitter reads moves (docs/speedrun.md). Keep the globals here uninitialized: initialized data would
// land in .data, in front of the retail uninitialized globals, and move them.

#include "hud.h"
#include "cl_util.h"
#include "rc_client.h"

//-----------------------------------------------------------------------------
// rc_hlmovement: the server's movement mode, as client prediction sees it
//-----------------------------------------------------------------------------
int RcHLMovement( void )
{
	const char *value = gEngfuncs.PhysInfo_ValueForKey( "hlm" );

	return value && atoi( value ) != 0;
}

//-----------------------------------------------------------------------------
// HUD: the movement mode tag, so runs in Half-Life movement are always visible
//-----------------------------------------------------------------------------
void RcClient_Redraw( float flTime, int intermission )
{
	if ( intermission )
		return;

	if ( RcHLMovement() )
	{
		gEngfuncs.pfnDrawSetTextColor( 1.0, 0.7, 0.2 );
		gEngfuncs.pfnDrawConsoleString( 8, 8, (char *)"HL movement" );
	}
}
