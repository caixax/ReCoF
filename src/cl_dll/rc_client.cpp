// ReCoF client additions
//
// Every variable ReCoF adds to client.dll lives in this file, which is linked after all retail objects
// (tools/units.json, the client link order), so its data comes after theirs and no address the LiveSplit
// autosplitter reads moves (docs/speedrun.md). Keep the globals here uninitialized: initialized data would
// land in .data, in front of the retail uninitialized globals, and move them.

#include "hud.h"
#include "cl_util.h"
#include "kbutton.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "pm_defs.h"
#include "rc_client.h"

#include <stdio.h>

extern "C" playermove_t *pmove;		// pm_shared.c: the move client prediction ran last

void KeyDown( kbutton_t *b );		// input.cpp
void KeyUp( kbutton_t *b );

static kbutton_t	in_rcducktap;
static cvar_t		*rc_autojump;
static cvar_t		*rc_ducktap_priority;
static cvar_t		*rc_jump_priority;
static cvar_t		*rc_speedometer;
static cvar_t		*rc_speedometer_jump;
static cvar_t		*rc_speedometer_3d;
static cvar_t		*rc_speedometer_pos;
static cvar_t		*rc_speedometer_color;
static int			s_bDucktapReleased;	// +rc_ducktap released duck on the frame before
static vec3_t		s_vecLastVelocity;	// speedometer: the velocity of the frame before
static int			s_iJumpSpeed;		// speedometer: the speed at the last takeoff

static void IN_RcDucktapDown( void ) { KeyDown( &in_rcducktap ); }
static void IN_RcDucktapUp( void ) { KeyUp( &in_rcducktap ); }

//-----------------------------------------------------------------------------
// called from InitInput
//-----------------------------------------------------------------------------
void RcClient_Init( void )
{
	gEngfuncs.pfnAddCommand( "+rc_ducktap", IN_RcDucktapDown );
	gEngfuncs.pfnAddCommand( "-rc_ducktap", IN_RcDucktapUp );

	rc_autojump = gEngfuncs.pfnRegisterVariable( "rc_autojump", "0", FCVAR_ARCHIVE );
	rc_ducktap_priority = gEngfuncs.pfnRegisterVariable( "rc_ducktap_priority", "1", FCVAR_ARCHIVE );
	rc_jump_priority = gEngfuncs.pfnRegisterVariable( "rc_jump_priority", "0", FCVAR_ARCHIVE );

	rc_speedometer = gEngfuncs.pfnRegisterVariable( "rc_speedometer", "0", FCVAR_ARCHIVE );
	rc_speedometer_jump = gEngfuncs.pfnRegisterVariable( "rc_speedometer_jump", "1", FCVAR_ARCHIVE );
	rc_speedometer_3d = gEngfuncs.pfnRegisterVariable( "rc_speedometer_3d", "0", FCVAR_ARCHIVE );
	rc_speedometer_pos = gEngfuncs.pfnRegisterVariable( "rc_speedometer_pos", ".5 .9", FCVAR_ARCHIVE );
	rc_speedometer_color = gEngfuncs.pfnRegisterVariable( "rc_speedometer_color", "255 255 255", FCVAR_ARCHIVE );
}

//-----------------------------------------------------------------------------
// rc_hlmovement: the server's movement mode, as client prediction sees it
//-----------------------------------------------------------------------------
int RcHLMovement( void )
{
	const char *value = gEngfuncs.PhysInfo_ValueForKey( "hlm" );

	return value && atoi( value ) != 0;
}

//-----------------------------------------------------------------------------
// called at the end of CL_CreateMove: +rc_ducktap and rc_autojump work on the buttons of the command, as
// RebuiltSimon's +rs_ducktap and rs_autojump do. With rc_jump_priority, holding jump pauses +rc_ducktap;
// with rc_ducktap_priority, holding +rc_ducktap pauses rc_autojump
//-----------------------------------------------------------------------------
void RcClient_CreateMove( usercmd_t *cmd )
{
	if ( !pmove || !rc_autojump )
		return;

	int bOnGround = pmove->onground != -1;
	int bDucktap = ( in_rcducktap.state & 1 ) && !( rc_jump_priority->value && ( cmd->buttons & IN_JUMP ) );

	if ( bDucktap )
	{
		// press duck for one frame on the ground, release it on the next, so each landing ducktaps
		if ( pmove->movetype == MOVETYPE_WALK && bOnGround && s_bDucktapReleased )
		{
			cmd->buttons |= IN_DUCK;
			s_bDucktapReleased = 0;
		}
		else
		{
			cmd->buttons &= ~IN_DUCK;
			s_bDucktapReleased = 1;
		}
	}

	// release jump in the air, so holding it jumps again on landing
	if ( rc_autojump->value && pmove->movetype == MOVETYPE_WALK && !( rc_ducktap_priority->value && bDucktap ) )
	{
		if ( !bOnGround && pmove->waterlevel <= 1 )
			cmd->buttons &= ~IN_JUMP;
	}
}

//-----------------------------------------------------------------------------
// speedometer helpers
//-----------------------------------------------------------------------------
static float RcSpeed( const float *v )
{
	if ( rc_speedometer_3d->value )
		return sqrt( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );

	return sqrt( v[0] * v[0] + v[1] * v[1] );
}

static void RcDrawCentered( int x, int y, const char *text, const float *color )
{
	int w, h;

	gEngfuncs.pfnDrawConsoleStringLen( text, &w, &h );
	gEngfuncs.pfnDrawSetTextColor( color[0], color[1], color[2] );
	gEngfuncs.pfnDrawConsoleString( x - w / 2, y, (char *)text );
}

//-----------------------------------------------------------------------------
// called from CHud::Redraw: the movement mode tag and the speedometer
//-----------------------------------------------------------------------------
void RcClient_Redraw( float flTime, int intermission )
{
	if ( intermission || !rc_speedometer )
		return;

	// the tag keeps runs in Half-Life movement visible
	if ( RcHLMovement() )
	{
		gEngfuncs.pfnDrawSetTextColor( 1.0, 0.7, 0.2 );
		gEngfuncs.pfnDrawConsoleString( 8, 8, (char *)"HL movement" );
	}

	if ( !rc_speedometer->value || !pmove )
		return;

	const float *v = pmove->velocity;

	// the takeoff speed, measured as RebuiltSimon does: when the vertical speed leaves 0 or turns upward
	if ( ( v[2] != 0 && s_vecLastVelocity[2] == 0 ) || ( v[2] > 0 && s_vecLastVelocity[2] < 0 ) )
		s_iJumpSpeed = (int)RcSpeed( s_vecLastVelocity );

	VectorCopy( v, s_vecLastVelocity );

	float x = 0.5, y = 0.9;
	float color[3] = { 255, 255, 255 };

	sscanf( rc_speedometer_pos->string, "%f %f", &x, &y );
	sscanf( rc_speedometer_color->string, "%f %f %f", &color[0], &color[1], &color[2] );

	for ( int i = 0; i < 3; i++ )
		color[i] /= 255.0;

	char text[32];
	int px = (int)( x * ScreenWidth );
	int py = (int)( y * ScreenHeight );

	sprintf( text, "%d", (int)RcSpeed( v ) );
	RcDrawCentered( px, py, text, color );

	if ( rc_speedometer_jump->value )
	{
		int w, h;
		gEngfuncs.pfnDrawConsoleStringLen( text, &w, &h );

		sprintf( text, "%d", s_iJumpSpeed );
		RcDrawCentered( px, py + h + 2, text, color );
	}
}
