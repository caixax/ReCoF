// ReCoF practice commands (B317): BunnymodXT's bxt_ch_* commands and bxt_set_angles for Cry of Fear, with rc_ in
// place of bxt_: health, position, velocity and view angles. They are planning tools, so they need sv_cheats 1 as
// BunnymodXT's do. The client sends a command it does not know to the server, and ClientCommand (client.cpp) hands
// it here for the player who typed it. bxt_ch_set_armor is left out: Cry of Fear has no armor. rc_ch_give (B346) is
// ReCoF's own, as Cry of Fear has no give command: tests and practice of a fight need the weapons.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"

#include <stdlib.h>
#include <string.h>

static bool RcCheatsOn( CBasePlayer *pPlayer )
{
	if ( CVAR_GET_FLOAT( "sv_cheats" ) )
		return true;

	ClientPrint( pPlayer->pev, HUD_PRINTCONSOLE, "This command needs sv_cheats 1\n" );
	return false;
}

static void RcUsage( CBasePlayer *pPlayer, const char *pszUsage )
{
	ClientPrint( pPlayer->pev, HUD_PRINTCONSOLE, UTIL_VarArgs( "Usage: %s\n", pszUsage ) );
}

static float RcArg( int i )
{
	return (float)atof( CMD_ARGV( i ) );
}

// the velocity of a speed in the direction of view angles (pitch up is negative, as the view's)
static Vector RcVelocityAlong( const Vector &angles, float flSpeed )
{
	UTIL_MakeVectors( Vector( angles.x, angles.y, 0 ) );
	return gpGlobals->v_forward * flSpeed;
}

// true when pcmd is one of these commands (run or refused), false to let ClientCommand go on
bool RcCheatCommand( edict_t *pEntity, const char *pcmd )
{
	if ( strncmp( pcmd, "rc_ch_", 6 ) && strcmp( pcmd, "rc_set_angles" ) )
		return false;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)&pEntity->v );
	if ( !pPlayer )
		return false;

	int argc = CMD_ARGC();
	entvars_t *pev = pPlayer->pev;

	if ( FStrEq( pcmd, "rc_ch_set_health" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc != 2 )
		{
			RcUsage( pPlayer, "rc_ch_set_health <health>" );
			return true;
		}
		pev->health = RcArg( 1 );
		return true;
	}

	if ( FStrEq( pcmd, "rc_ch_set_pos" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc != 4 )
		{
			RcUsage( pPlayer, "rc_ch_set_pos <x> <y> <z>" );
			return true;
		}
		UTIL_SetOrigin( pPlayer, Vector( RcArg( 1 ), RcArg( 2 ), RcArg( 3 ) ) );
		return true;
	}

	if ( FStrEq( pcmd, "rc_ch_set_pos_offset" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc != 4 )
		{
			RcUsage( pPlayer, "rc_ch_set_pos_offset <dx> <dy> <dz>" );
			return true;
		}
		UTIL_SetOrigin( pPlayer, pev->origin + Vector( RcArg( 1 ), RcArg( 2 ), RcArg( 3 ) ) );
		return true;
	}

	if ( FStrEq( pcmd, "rc_ch_get_pos" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		// the position and the view, and a line that puts them back
		ClientPrint( pev, HUD_PRINTCONSOLE, UTIL_VarArgs( "pos %.6f %.6f %.6f  angles %.6f %.6f %.6f\n",
			pev->origin.x, pev->origin.y, pev->origin.z, pev->v_angle.x, pev->v_angle.y, pev->v_angle.z ) );
		ClientPrint( pev, HUD_PRINTCONSOLE, UTIL_VarArgs( "rc_ch_set_pos %.6f %.6f %.6f; rc_set_angles %.6f %.6f %.6f\n",
			pev->origin.x, pev->origin.y, pev->origin.z, pev->v_angle.x, pev->v_angle.y, pev->v_angle.z ) );
		return true;
	}

	if ( FStrEq( pcmd, "rc_ch_set_vel" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc != 4 )
		{
			RcUsage( pPlayer, "rc_ch_set_vel <vx> <vy> <vz>" );
			return true;
		}
		pev->velocity = Vector( RcArg( 1 ), RcArg( 2 ), RcArg( 3 ) );
		return true;
	}

	if ( FStrEq( pcmd, "rc_ch_set_vel_angles" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc == 2 )
			pev->velocity = RcVelocityAlong( pev->v_angle, RcArg( 1 ) );
		else if ( argc == 4 )
			pev->velocity = RcVelocityAlong( Vector( RcArg( 1 ), RcArg( 2 ), 0 ), RcArg( 3 ) );
		else
			RcUsage( pPlayer, "rc_ch_set_vel_angles <magnitude>, or rc_ch_set_vel_angles <pitch> <yaw> <magnitude>" );
		return true;
	}

	// rc_ch_set_angles: the name with ch_ as the other practice commands have (BunnymodXT's has none)
	if ( FStrEq( pcmd, "rc_set_angles" ) || FStrEq( pcmd, "rc_ch_set_angles" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc != 3 && argc != 4 )
		{
			RcUsage( pPlayer, "rc_set_angles <pitch> <yaw> [<roll>]" );
			return true;
		}
		// the engine sends pev->angles to the client as its view angles when fixangle is set
		pev->angles = Vector( RcArg( 1 ), RcArg( 2 ), argc == 4 ? RcArg( 3 ) : 0 );
		pev->v_angle = pev->angles;
		pev->fixangle = TRUE;
		return true;
	}

	// B346: a weapon, ammo or item given at the player's feet, as if picked up there; the unlockable weapons keep
	// their own tests (GiveNamedItem)
	if ( FStrEq( pcmd, "rc_ch_give" ) )
	{
		if ( !RcCheatsOn( pPlayer ) )
			return true;
		if ( argc != 2 && argc != 3 )
		{
			RcUsage( pPlayer, "rc_ch_give <weapon_, ammo_ or item_ name> [<count>]" );
			return true;
		}
		const char *pszName = CMD_ARGV( 1 );
		if ( strncmp( pszName, "weapon_", 7 ) && strncmp( pszName, "ammo_", 5 ) && strncmp( pszName, "item_", 5 ) )
		{
			RcUsage( pPlayer, "rc_ch_give <weapon_, ammo_ or item_ name> [<count>]" );
			return true;
		}
		int iCount = argc == 3 ? atoi( CMD_ARGV( 2 ) ) : 1;
		if ( iCount < 1 )
			iCount = 1;
		if ( iCount > 20 )
			iCount = 20;
		// the entity keeps the name as its classname, so it must outlive the command line
		const char *pszKept = STRING( ALLOC_STRING( pszName ) );
		for ( int i = 0; i < iCount; i++ )
			pPlayer->GiveNamedItem( pszKept, false, true );
		return true;
	}

	ClientPrint( pev, HUD_PRINTCONSOLE, UTIL_VarArgs( "Unknown command: %s\n", pcmd ) );
	return true;
}
