/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//=========================================================
// cof_ladder_manager - climbing a ladder with a camera model
//=========================================================

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"

extern void UTIL_ProMessage( CBaseEntity *pEntity, const char *pMessage, int colour, bool all );
extern float GetSequenceTimeNamed( void *pmodel, char *sequence );

extern int gmsgAttachCam;
extern int gmsgCameraZoom;

bool wasqueuedtoreduceorigin;

// ReCoF (rc_hlmovement): the movement mode as pm_shared sees it (the physinfo key, not the cvar)
static bool RcHLMovementOn( edict_t *pEdict )
{
	return atoi( g_engfuncs.pfnGetPhysicsKeyValue( pEdict, "hlm" ) ) != 0;
}

//=========================================================
// rc_hl_ladder - ReCoF (rc_hlmovement): in Half-Life movement a cof_ladder_manager is climbed like a
// Half-Life ladder, without the camera animation. The manager adds this box over the ladder face, from its
// bottom floor up to its top floor (RcSetupHLLadder), and pm_shared climbs it while physinfo "hlm" is set
// (PM_InRcLadder). SOLID_NOT with skin CONTENTS_LADDER and a model makes the engine hand it to the movement
// code as a ladder; its shape is in origin, angles and controller (see PM_IsRcLadder). It draws nothing
// (EF_NODRAW), and AddToFullPack sends it all the same for client prediction
//=========================================================
class CRcHLLadder : public CBaseEntity
{
public:
	virtual int ObjectCaps( void ) { return CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void EXPORT WatchThink( void );
	BOOL Holds( CBaseEntity *pPlayer );

	EHANDLE m_hManager;
	EHANDLE m_hUse[2];		// the manager's use brushes
	BOOL m_bClimbing;		// the player was on this ladder at the last think
	float m_flClimbFrom;	// with the feet at this height when getting on
};

TYPEDESCRIPTION CRcHLLadder::m_SaveData[] =
{
	DEFINE_FIELD( CRcHLLadder, m_hManager, FIELD_EHANDLE ),
	DEFINE_ARRAY( CRcHLLadder, m_hUse, FIELD_EHANDLE, 2 ),
	DEFINE_FIELD( CRcHLLadder, m_bClimbing, FIELD_BOOLEAN ),
	DEFINE_FIELD( CRcHLLadder, m_flClimbFrom, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE( CRcHLLadder, CBaseEntity );

LINK_ENTITY_TO_CLASS( rc_hl_ladder, CRcHLLadder );

// the test of PM_InRcLadder
BOOL CRcHLLadder::Holds( CBaseEntity *pPlayer )
{
	Vector vecDelta = pPlayer->pev->origin - pev->origin;
	Vector vecForward, vecRight;
	float flHalfHeight = pev->controller[2] | ( pev->controller[3] << 8 );

	UTIL_MakeVectorsPrivate( pev->angles, vecForward, vecRight, NULL );

	return fabs( DotProduct( vecDelta, vecForward ) ) < pev->controller[0] + pPlayer->pev->maxs.x
		&& fabs( DotProduct( vecDelta, vecRight ) ) < pev->controller[1] + pPlayer->pev->maxs.y
		&& vecDelta.z + pPlayer->pev->maxs.z > -flHalfHeight && vecDelta.z + pPlayer->pev->mins.z < flHalfHeight;
}

void CRcHLLadder::WatchThink( void )
{
	// a map closes a ladder by removing its use brushes (c_down kills the top one); the box goes with them
	if ( !m_hUse[0] && !m_hUse[1] )
	{
		UTIL_Remove( this );
		return;
	}

	SetNextThink( 0.1 );

	// the manager fires its target at the end of every climb (c_lobby's scare): fire it when the player gets
	// off this ladder 48 or more units above or below where they got on
	CBaseEntity *pManager = m_hManager;
	CBaseEntity *pPlayer = UTIL_PlayerByIndex( 1 );

	if ( !pManager || FStringNull( pManager->pev->target ) || !pPlayer )
		return;

	float flFeet = pPlayer->pev->origin.z + pPlayer->pev->mins.z;

	if ( pPlayer->pev->movetype == MOVETYPE_FLY && Holds( pPlayer ) )
	{
		if ( !m_bClimbing )
		{
			m_bClimbing = TRUE;
			m_flClimbFrom = flFeet;
		}
	}
	else if ( m_bClimbing )
	{
		m_bClimbing = FALSE;

		if ( fabs( flFeet - m_flClimbFrom ) >= 48 )
			FireTargets( STRING( pManager->pev->target ), pPlayer, pPlayer, USE_TOGGLE, 0 );
	}
}

class CCoFLadderManager : public CPointEntity
{
	virtual void Spawn( void );
	virtual void Precache( void );
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void EXPORT LadderThink( void );
	void StartNextAnimation( void );
	void StartPreviousAnimation( void );
	virtual void KeyValue( KeyValueData *pkvd );
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	bool m_bFinishedOnLadder;
	bool m_bEnableControl;
	float m_flControlEnableTime;
	bool m_bDescending;
	string_t m_sPreviousAnimation;
	string_t m_sCustomOffAnimation;
	string_t m_sCustomOnAnimation;
	string_t m_sCustomOffDescendAnimation;
	string_t m_sCustomOnDescendAnimation;
	EHANDLE m_eTopEnt;
	EHANDLE m_eBottomEnt;
	string_t m_sTopEnt;
	string_t m_sBottomEnt;
	CBasePlayer *m_pPlayer;
	int m_iDescendOffset;
	float m_flLowerTime;
	bool m_bDoLower;
	bool m_bOriginReductionQueuedPrevious;
	bool m_bOriginReductionQueuedNext;
	static TYPEDESCRIPTION m_SaveData[];
	int m_cAnimation[33];
	bool m_bGotOnButNotDescended;
	int m_iNumAnimations;
	int m_iCurrentAnimation;
public:
	void RcSetupHLLadder( void );
	EHANDLE m_hRcLadder;		// ReCoF (rc_hlmovement): the Half-Life ladder (CRcHLLadder), when the ladder has one
	BOOL m_bRcLadderChecked;	// ReCoF (rc_hlmovement): RcSetupHLLadder ran
};

TYPEDESCRIPTION CCoFLadderManager::m_SaveData[] =
{
	DEFINE_FIELD( CCoFLadderManager, m_iNumAnimations, FIELD_INTEGER ),
	DEFINE_ARRAY( CCoFLadderManager, m_cAnimation, FIELD_STRING, 33 ),
	DEFINE_FIELD( CCoFLadderManager, m_iCurrentAnimation, FIELD_INTEGER ),
	DEFINE_FIELD( CCoFLadderManager, m_pPlayer, FIELD_CLASSPTR ),
	DEFINE_FIELD( CCoFLadderManager, m_bEnableControl, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_bDescending, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_flControlEnableTime, FIELD_TIME ),
	DEFINE_FIELD( CCoFLadderManager, m_sPreviousAnimation, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_sCustomOffAnimation, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_sCustomOnAnimation, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_sCustomOffDescendAnimation, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_sCustomOnDescendAnimation, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_iDescendOffset, FIELD_INTEGER ),
	DEFINE_FIELD( CCoFLadderManager, m_flLowerTime, FIELD_TIME ),
	DEFINE_FIELD( CCoFLadderManager, m_bDoLower, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_bOriginReductionQueuedPrevious, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_bOriginReductionQueuedNext, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_bGotOnButNotDescended, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_bFinishedOnLadder, FIELD_BOOLEAN ),
	DEFINE_FIELD( CCoFLadderManager, m_eTopEnt, FIELD_EHANDLE ),
	DEFINE_FIELD( CCoFLadderManager, m_eBottomEnt, FIELD_EHANDLE ),
	DEFINE_FIELD( CCoFLadderManager, m_sTopEnt, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_sBottomEnt, FIELD_STRING ),
	DEFINE_FIELD( CCoFLadderManager, m_hRcLadder, FIELD_EHANDLE ),
	DEFINE_FIELD( CCoFLadderManager, m_bRcLadderChecked, FIELD_BOOLEAN ),
};

IMPLEMENT_SAVERESTORE( CCoFLadderManager, CPointEntity );

void CCoFLadderManager::KeyValue( KeyValueData *pkvd )
{
	if ( FStrEq( pkvd->szKeyName, "animation1" ) )
	{
		m_cAnimation[1] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation2" ) )
	{
		m_cAnimation[2] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation3" ) )
	{
		m_cAnimation[3] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation4" ) )
	{
		m_cAnimation[4] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation5" ) )
	{
		m_cAnimation[5] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation6" ) )
	{
		m_cAnimation[6] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation7" ) )
	{
		m_cAnimation[7] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation8" ) )
	{
		m_cAnimation[8] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation9" ) )
	{
		m_cAnimation[9] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation10" ) )
	{
		m_cAnimation[10] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation11" ) )
	{
		m_cAnimation[11] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation12" ) )
	{
		m_cAnimation[12] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation13" ) )
	{
		m_cAnimation[13] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation14" ) )
	{
		m_cAnimation[14] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation15" ) )
	{
		m_cAnimation[15] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation16" ) )
	{
		m_cAnimation[16] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation17" ) )
	{
		m_cAnimation[17] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation18" ) )
	{
		m_cAnimation[18] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation19" ) )
	{
		m_cAnimation[19] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation20" ) )
	{
		m_cAnimation[20] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation21" ) )
	{
		m_cAnimation[21] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation22" ) )
	{
		m_cAnimation[22] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation23" ) )
	{
		m_cAnimation[23] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation24" ) )
	{
		m_cAnimation[24] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation25" ) )
	{
		m_cAnimation[25] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation26" ) )
	{
		m_cAnimation[26] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation27" ) )
	{
		m_cAnimation[27] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation28" ) )
	{
		m_cAnimation[28] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation29" ) )
	{
		m_cAnimation[29] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "animation30" ) )
	{
		m_cAnimation[30] = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
		m_iNumAnimations++;
	}
	else if ( FStrEq( pkvd->szKeyName, "customoff" ) )
	{
		m_sCustomOffAnimation = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "customon" ) )
	{
		m_sCustomOnAnimation = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "customoffd" ) )
	{
		m_sCustomOffDescendAnimation = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "customond" ) )
	{
		m_sCustomOnDescendAnimation = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "descendoffset" ) )
	{
		m_iDescendOffset = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "topent" ) )
	{
		m_sTopEnt = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if ( FStrEq( pkvd->szKeyName, "bottoment" ) )
	{
		m_sBottomEnt = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseEntity::KeyValue( pkvd );
}

void CCoFLadderManager::LadderThink( void )
{
	if ( m_bDoLower && m_flLowerTime < gpGlobals->time && m_pPlayer && m_pPlayer->pDeathScene )
	{
		m_bDoLower = false;
		m_pPlayer->pDeathScene->pev->origin.z -= 48;
	}

	if ( m_flControlEnableTime < gpGlobals->time && m_bEnableControl && m_pPlayer )
	{
		m_pPlayer->EnableControl( TRUE );

		if ( m_pPlayer->pDeathScene )
		{
			UTIL_Remove( m_pPlayer->pDeathScene );
			m_pPlayer->pDeathScene = NULL;
			m_pPlayer->m_pAttachedCamera = NULL;
		}

		MESSAGE_BEGIN( MSG_ONE, gmsgAttachCam, NULL, m_pPlayer->pev );
			WRITE_SHORT( 0 );
			WRITE_BYTE( 0 );
		MESSAGE_END();

		m_pPlayer->pev->flags &= ~FL_GODMODE;
		m_pPlayer->pev->flags &= ~FL_NOTARGET;

		CLIENT_COMMAND( m_pPlayer->edict(), "finishladder\n" );

		if ( m_bDescending )
		{
			if ( !m_eBottomEnt )
			{
				CBaseEntity *pTemp = UTIL_FindEntityByTargetname( NULL, STRING( m_sBottomEnt ) );

				if ( pTemp )
					m_eBottomEnt = pTemp;
			}

			if ( m_eBottomEnt )
			{
				Vector tmp = m_eBottomEnt->pev->origin;
				tmp.z += 36;
				tmp.z += 1;

				UTIL_SetOrigin( m_pPlayer, tmp );

				m_pPlayer->pev->angles = m_eBottomEnt->pev->angles;
				m_pPlayer->pev->velocity = m_pPlayer->pev->basevelocity = g_vecZero;

				if ( m_pPlayer->IsPlayer() )
				{
					m_pPlayer->pev->v_angle.y = m_eBottomEnt->pev->angles.y;
					m_pPlayer->pev->fixangle = 1;
				}

				m_pPlayer->pev->flags &= ~FL_ONGROUND;
				m_pPlayer->pev->fixangle = 1;
			}
		}
		else
		{
			if ( !m_eTopEnt )
			{
				CBaseEntity *pTemp = UTIL_FindEntityByTargetname( NULL, STRING( m_sTopEnt ) );

				if ( pTemp )
					m_eTopEnt = pTemp;
			}

			if ( m_eTopEnt )
			{
				Vector tmp = m_eTopEnt->pev->origin;
				tmp.z += 36;
				tmp.z += 1;

				UTIL_SetOrigin( m_pPlayer, tmp );

				m_pPlayer->pev->angles = m_eTopEnt->pev->angles;
				m_pPlayer->pev->velocity = m_pPlayer->pev->basevelocity = g_vecZero;

				if ( m_pPlayer->IsPlayer() )
				{
					m_pPlayer->pev->v_angle.y = m_eTopEnt->pev->angles.y;
					m_pPlayer->pev->fixangle = 1;
				}

				m_pPlayer->pev->flags &= ~FL_ONGROUND;
				m_pPlayer->pev->fixangle = 1;
			}
		}

		MESSAGE_BEGIN( MSG_ONE, gmsgCameraZoom, NULL, m_pPlayer->pev );
			WRITE_COORD( 50 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
		MESSAGE_END();

		m_pPlayer->pev->vuser2.x = 90;

		if ( pev->target )
			FireTargets( STRING( pev->target ), m_pPlayer, m_pPlayer, USE_TOGGLE, 0 );

		m_bEnableControl = false;
		return;
	}

	if ( pev->teleport_time < gpGlobals->time && !m_bFinishedOnLadder )
	{
		if ( m_pPlayer )
		{
			if ( m_pPlayer->pDeathScene && ( m_pPlayer->pDeathScene->pev->button & IN_FORWARD ) )
			{
				m_bDescending = false;
				StartNextAnimation();
			}

			if ( m_pPlayer->pDeathScene && ( m_pPlayer->pDeathScene->pev->button & IN_BACK ) )
			{
				m_bDescending = true;
				StartPreviousAnimation();
			}
		}
	}

	SetNextThink( 0 );
}

void CCoFLadderManager::Precache( void )
{
	if ( pev->model )
		PRECACHE_MODEL( (char *)STRING( pev->model ) );
}

void CCoFLadderManager::Spawn( void )
{
	Precache();
	UTIL_SetOrigin( this, pev->origin );
	pev->solid = SOLID_NOT;
	pev->effects = EF_NODRAW;
	pev->v_angle = g_vecZero;

	if ( m_iCurrentAnimation < 1 )
		m_iCurrentAnimation = 1;
}

void CCoFLadderManager::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( !pActivator )
		return;

	if ( !pev->model )
		return;

	// ReCoF (B098): retail cast any activator to a player, and a second climber took over the ladder
	// while the first one was still frozen in the climb camera
	if ( !pActivator->IsPlayer() )
		return;

	// ReCoF (rc_hlmovement): in Half-Life movement a ladder with a Half-Life ladder is climbed without the animation
	if ( m_hRcLadder && RcHLMovementOn( pActivator->edict() ) )
		return;

	if ( m_pPlayer && m_pPlayer->pDeathScene && m_pPlayer->IsAlive() )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)pActivator->pev );

	if ( pPlayer )
	{
		if ( pPlayer->m_pActiveItem )
		{
			if ( FClassnameIs( pPlayer->m_pActiveItem->edict(), "weapon_flare" ) )
			{
				CBasePlayerWeapon *pFlare = pPlayer->GetPlayerWeapon( 13 );

				if ( pFlare && pFlare->pev->iuser1 )
				{
					UTIL_ProMessage( pPlayer, "I'll need to drop this flare before I can climb", 5, false );
					return;
				}
			}

			if ( FClassnameIs( pPlayer->m_pActiveItem->edict(), "weapon_dualweapon" ) && ( pPlayer->m_iDualWeaponLeftID == 13 || pPlayer->m_iDualWeaponRightID == 13 ) )
			{
				CBasePlayerWeapon *pFlare = pPlayer->GetPlayerWeapon( 13 );

				if ( pFlare && pFlare->pev->iuser1 )
				{
					UTIL_ProMessage( pPlayer, "I'll need to drop this flare before I can climb", 5, false );
					return;
				}
			}
		}
	}

	if ( value != 0 )
		m_bDescending = true;
	else
		m_bDescending = false;

	if ( pPlayer )
	{
		m_pPlayer = pPlayer;
		pPlayer->EnableControl( FALSE );
		pPlayer->pDeathScene = (CBaseAnimating *)CBaseEntity::Create( "camera_model", pev->origin, pev->angles, pPlayer->edict() );

		if ( m_bDescending )
			m_iCurrentAnimation = m_iNumAnimations;
		else
			m_iCurrentAnimation = 1;

		if ( pPlayer->pDeathScene )
		{
			UTIL_SetOrigin( pPlayer->pDeathScene, Vector( pev->origin.x, pev->origin.y, pev->origin.z ) );
			SET_MODEL( ENT( pPlayer->pDeathScene->pev ), STRING( pev->model ) );

			if ( m_bDescending )
				pPlayer->pDeathScene->pev->origin.z += m_iDescendOffset;

			pPlayer->m_pAttachedCamera = pPlayer->pDeathScene;
			pPlayer->m_pClientAttachedCamera = pPlayer->m_pAttachedCamera;

			MESSAGE_BEGIN( MSG_ONE, gmsgAttachCam, NULL, pPlayer->pev );
				WRITE_SHORT( ENTINDEX( pPlayer->m_pAttachedCamera->edict() ) );
				WRITE_BYTE( 1 );
			MESSAGE_END();

			m_pPlayer->pev->flags |= FL_NOTARGET;
			m_pPlayer->pev->flags |= FL_GODMODE;

			pev->owner = pPlayer->edict();
		}

		SetThink( &CCoFLadderManager::LadderThink );
		SetNextThink( 0.1 );

		StartNextAnimation();
	}
}

void CCoFLadderManager::StartNextAnimation( void )
{
	if ( !pev->owner )
		return;

	CBaseEntity *pEntity = CBaseEntity::Instance( pev->owner );

	if ( !pEntity )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)pEntity->pev );

	if ( !pPlayer )
		return;

	if ( !pPlayer->pDeathScene )
		return;

	if ( m_bOriginReductionQueuedNext )
	{
		m_bOriginReductionQueuedNext = false;
		wasqueuedtoreduceorigin = true;
		pPlayer->pDeathScene->pev->origin.z -= 48;
	}

	if ( m_iCurrentAnimation > 1 && !m_bDescending && !strstr( STRING( m_sPreviousAnimation ), "climb_loop_down" ) )
	{
		if ( wasqueuedtoreduceorigin )
			wasqueuedtoreduceorigin = false;
		else if ( !m_bGotOnButNotDescended )
			pPlayer->pDeathScene->pev->origin.z += 48;
	}

	int animationtoplay = m_cAnimation[m_iCurrentAnimation];

	if ( m_bDescending )
	{
		if ( strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "climb_end" ) && strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "up" ) && m_sCustomOnAnimation && !m_bDescending )
			animationtoplay = m_sCustomOnAnimation;

		if ( strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "climb_end" ) && strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "up" ) && m_sCustomOnDescendAnimation && m_bDescending == true )
			animationtoplay = m_sCustomOnDescendAnimation;
	}
	else
	{
		if ( strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "climb_end" ) && strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "up" ) && m_sCustomOffAnimation && !m_bDescending )
			animationtoplay = m_sCustomOffAnimation;
	}

	((CBaseMonster *)pPlayer->pDeathScene)->SetSequenceByName( (char *)STRING( animationtoplay ) );
	m_sPreviousAnimation = animationtoplay;

	bool wasdescending = false;

	if ( m_bDescending )
		wasdescending = true;

	float delay = 0;
	delay = GetSequenceTimeNamed( GET_MODEL_PTR( ENT( pPlayer->pDeathScene->pev ) ), (char *)STRING( animationtoplay ) );

	if ( m_bDescending )
	{
		m_bGotOnButNotDescended = true;
		m_bDescending = false;
		m_bOriginReductionQueuedPrevious = true;
	}

	if ( delay != 0 )
	{
		if ( m_iCurrentAnimation == m_iNumAnimations && !wasdescending )
		{
			m_bEnableControl = true;
			m_flControlEnableTime = delay + gpGlobals->time;
		}

		pev->teleport_time = delay + gpGlobals->time;
	}
	else
		ALERT( at_console, "Unable to find delay\n" );

	if ( !wasdescending )
		m_iCurrentAnimation++;
}

void CCoFLadderManager::StartPreviousAnimation( void )
{
	if ( !pev->owner )
		return;

	CBaseEntity *pEntity = CBaseEntity::Instance( pev->owner );

	if ( !pEntity )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)pEntity->pev );

	if ( !pPlayer )
		return;

	if ( !pPlayer->pDeathScene )
		return;

	m_bGotOnButNotDescended = false;

	m_iCurrentAnimation--;

	if ( m_bOriginReductionQueuedPrevious )
	{
		m_bOriginReductionQueuedPrevious = false;
		pPlayer->pDeathScene->pev->origin.z -= 48;
	}

	if ( m_bDescending )
	{
		if ( strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "climb_end" ) && m_sCustomOffDescendAnimation )
			m_cAnimation[m_iCurrentAnimation] = m_sCustomOffDescendAnimation;
	}
	else
	{
		if ( strstr( STRING( m_cAnimation[m_iCurrentAnimation] ), "climb_end" ) && m_sCustomOffAnimation )
			m_cAnimation[m_iCurrentAnimation] = m_sCustomOffAnimation;
	}

	if ( m_iCurrentAnimation <= 1 )
	{
		if ( strstr( STRING( m_sPreviousAnimation ), "climb_loop_down" ) )
			pPlayer->pDeathScene->pev->origin.z -= 48;

		if ( m_bDescending )
		{
			if ( m_sCustomOffDescendAnimation )
				((CBaseMonster *)pPlayer->pDeathScene)->SetSequenceByName( (char *)STRING( m_sCustomOffDescendAnimation ) );
			else
				((CBaseMonster *)pPlayer->pDeathScene)->SetSequenceByName( "climb_end_down" );
		}
		else
		{
			if ( m_sCustomOffAnimation )
				((CBaseMonster *)pPlayer->pDeathScene)->SetSequenceByName( (char *)STRING( m_sCustomOffAnimation ) );
			else
				((CBaseMonster *)pPlayer->pDeathScene)->SetSequenceByName( "climb_end_down" );
		}

		float delay = 0;

		if ( m_bDescending )
		{
			if ( m_sCustomOffDescendAnimation )
			{
				delay = GetSequenceTimeNamed( GET_MODEL_PTR( ENT( pPlayer->pDeathScene->pev ) ), (char *)STRING( m_sCustomOffDescendAnimation ) );
				m_sPreviousAnimation = m_sCustomOffDescendAnimation;
			}
			else
			{
				delay = GetSequenceTimeNamed( GET_MODEL_PTR( ENT( pPlayer->pDeathScene->pev ) ), "climb_end_down" );
				m_sPreviousAnimation = MAKE_STRING( "climb_end_down" );
			}
		}
		else
		{
			if ( m_sCustomOffAnimation )
			{
				delay = GetSequenceTimeNamed( GET_MODEL_PTR( ENT( pPlayer->pDeathScene->pev ) ), (char *)STRING( m_sCustomOffAnimation ) );
				m_sPreviousAnimation = m_sCustomOffAnimation;
			}
			else
			{
				delay = GetSequenceTimeNamed( GET_MODEL_PTR( ENT( pPlayer->pDeathScene->pev ) ), "climb_end_down" );
				m_sPreviousAnimation = MAKE_STRING( "climb_end_down" );
			}
		}

		if ( delay != 0 )
		{
			if ( m_iCurrentAnimation == m_iNumAnimations )
			{
				m_bEnableControl = true;
				m_flControlEnableTime = delay + gpGlobals->time;
			}

			pev->teleport_time = delay + gpGlobals->time;
		}
		else
			ALERT( at_console, "Unable to find delay\n" );

		m_flControlEnableTime = delay + gpGlobals->time;
		m_bEnableControl = true;
	}
	else
	{
		if ( strstr( STRING( m_sPreviousAnimation ), "climb_loop_down" ) )
			pPlayer->pDeathScene->pev->origin.z -= 48;

		((CBaseMonster *)pPlayer->pDeathScene)->SetSequenceByName( "climb_loop_down" );

		float delay = GetSequenceTimeNamed( GET_MODEL_PTR( ENT( pPlayer->pDeathScene->pev ) ), "climb_loop_down" );
		m_sPreviousAnimation = MAKE_STRING( "climb_loop_down" );

		if ( delay != 0 )
		{
			if ( m_iCurrentAnimation == m_iNumAnimations )
			{
				m_bEnableControl = true;
				m_flControlEnableTime = delay + gpGlobals->time;
			}

			pev->teleport_time = delay + gpGlobals->time;
		}
		else
			ALERT( at_console, "Unable to find delay\n" );
	}
}

LINK_ENTITY_TO_CLASS( cof_ladder_manager, CCoFLadderManager );

// ReCoF (rc_hlmovement): the height of the floor under a standing player's origin
static float RcFloorBelow( const Vector &vecOrigin, edict_t *pentIgnore )
{
	TraceResult tr;

	UTIL_TraceHull( vecOrigin, vecOrigin - Vector( 0, 0, 64 ), ignore_monsters, human_hull, pentIgnore, &tr );

	return tr.vecEndPos.z - 36;
}

// ReCoF (rc_hlmovement): give this ladder its Half-Life ladder (CRcHLLadder) when a standing player fits up
// the ladder face to a little above the top floor and from there to the top teleport target, the way the
// animation takes the player. The use brushes stand on the face: they give its depth and width
void CCoFLadderManager::RcSetupHLLadder( void )
{
	m_bRcLadderChecked = TRUE;

	if ( !pev->model || !pev->targetname )
		return;

	Vector vecForward, vecRight;

	UTIL_MakeVectorsPrivate( Vector( 0, pev->angles.y, 0 ), vecForward, vecRight, NULL );

	CBaseEntity *pUse[2] = { NULL, NULL };
	float flNear = 99999, flFar = -99999, flLeft = 99999, flRight = -99999, flUseTop = -99999;
	CBaseEntity *pEnt = NULL;

	while ( ( pEnt = UTIL_FindEntityByString( pEnt, "target", STRING( pev->targetname ) ) ) != NULL )
	{
		if ( !FClassnameIs( pEnt->pev, "cof_ladder_manager_use" ) )
			continue;

		if ( !pUse[0] )
			pUse[0] = pEnt;
		else if ( !pUse[1] )
			pUse[1] = pEnt;

		for ( int i = 0; i < 8; i++ )
		{
			Vector vecCorner = pEnt->pev->origin - pev->origin;

			vecCorner.x += ( i & 1 ) ? pEnt->pev->maxs.x : pEnt->pev->mins.x;
			vecCorner.y += ( i & 2 ) ? pEnt->pev->maxs.y : pEnt->pev->mins.y;
			vecCorner.z += ( i & 4 ) ? pEnt->pev->maxs.z : pEnt->pev->mins.z;

			flNear = min( flNear, DotProduct( vecCorner, vecForward ) );
			flFar = max( flFar, DotProduct( vecCorner, vecForward ) );
			flLeft = min( flLeft, DotProduct( vecCorner, vecRight ) );
			flRight = max( flRight, DotProduct( vecCorner, vecRight ) );
		}

		flUseTop = max( flUseTop, pEnt->pev->origin.z + pEnt->pev->maxs.z );
	}

	if ( !pUse[0] )
	{
		ALERT( at_console, "ReCoF: ladder %s keeps the climb animation in Half-Life movement: no use brush\n", STRING( pev->targetname ) );
		return;
	}

	// the floors at both ends: the manager stands on the bottom one (a ladder that ends above the floor keeps
	// its drop), the top teleport target on the top one; without a target the top use brush ends 2 above it
	CBaseEntity *pTop = m_sTopEnt ? UTIL_FindEntityByTargetname( NULL, STRING( m_sTopEnt ) ) : NULL;
	float flBottom = pev->origin.z;
	float flTop = pTop ? RcFloorBelow( pTop->pev->origin + Vector( 0, 0, 37 ), edict() ) : flUseTop - 2;

	// the player climbs centred on the use brushes, 1 unit before the face, and the climb carries them about
	// 25 above the top floor, over its edge
	float flSide = ( flLeft + flRight ) / 2;
	float flDepth = flNear - 17;
	Vector vecColumn = pev->origin + vecForward * flDepth + vecRight * flSide;
	Vector vecOver = Vector( vecColumn.x, vecColumn.y, flTop + 18 + 37 );
	TraceResult tr;

	UTIL_TraceHull( Vector( vecColumn.x, vecColumn.y, flBottom + 37 ), vecOver, ignore_monsters, human_hull, edict(), &tr );

	if ( !tr.fStartSolid && !tr.fAllSolid && tr.flFraction == 1.0 && pTop )
		UTIL_TraceHull( vecOver, Vector( pTop->pev->origin.x, pTop->pev->origin.y, vecOver.z ), ignore_monsters, human_hull, edict(), &tr );

	if ( tr.fStartSolid || tr.fAllSolid || tr.flFraction < 1.0 )
	{
		ALERT( at_console, "ReCoF: ladder %s keeps the climb animation in Half-Life movement: blocked at %.0f %.0f %.0f\n", STRING( pev->targetname ), tr.vecEndPos.x, tr.vecEndPos.y, tr.vecEndPos.z );
		return;
	}

	// the box: from 8 units past the climber's column to the back of the use brushes, as wide as they are, and
	// from the bottom floor to the top floor (the half height rounds down, so it never ends above the floor):
	// the climber leaves it as the feet pass the top floor, as on a Half-Life ladder
	int iHalfDepth = (int)ceil( ( flFar - ( flDepth + 8 ) ) / 2 );
	int iHalfWidth = (int)ceil( ( flRight - flLeft ) / 2 );
	int iHalfHeight = (int)( ( flTop - flBottom ) / 2 );

	if ( iHalfDepth < 1 || iHalfDepth > 255 || iHalfWidth < 1 || iHalfWidth > 255 || iHalfHeight < 16 )
	{
		ALERT( at_console, "ReCoF: ladder %s keeps the climb animation in Half-Life movement: size %d %d %d\n", STRING( pev->targetname ), iHalfDepth, iHalfWidth, iHalfHeight );
		return;
	}

	// pm_shared finds the column again 8 units before the near side (PM_RcLadderHold); the centre is in whole
	// units, which reach the client exactly
	Vector vecCentre = pev->origin + vecForward * ( flDepth + 8 + iHalfDepth ) + vecRight * flSide;
	vecCentre.x = floor( vecCentre.x + 0.5 );
	vecCentre.y = floor( vecCentre.y + 0.5 );
	vecCentre.z = floor( flBottom ) + iHalfHeight;

	Vector vecExtent;
	vecExtent.x = fabs( vecForward.x ) * iHalfDepth + fabs( vecRight.x ) * iHalfWidth;
	vecExtent.y = fabs( vecForward.y ) * iHalfDepth + fabs( vecRight.y ) * iHalfWidth;
	vecExtent.z = iHalfHeight;

	CRcHLLadder *pLadder = (CRcHLLadder *)CBaseEntity::Create( "rc_hl_ladder", vecCentre, Vector( 0, pev->angles.y, 0 ) );

	if ( !pLadder )
		return;

	pLadder->pev->solid = SOLID_NOT;
	pLadder->pev->movetype = MOVETYPE_NONE;
	pLadder->pev->skin = CONTENTS_LADDER;
	pLadder->pev->effects = EF_NODRAW;
	pLadder->pev->controller[0] = iHalfDepth;
	pLadder->pev->controller[1] = iHalfWidth;
	pLadder->pev->controller[2] = iHalfHeight & 255;
	pLadder->pev->controller[3] = iHalfHeight >> 8;

	SET_MODEL( pLadder->edict(), STRING( pev->model ) );
	UTIL_SetSize( pLadder->pev, -vecExtent, vecExtent );
	UTIL_SetOrigin( pLadder, vecCentre );

	pLadder->m_hManager = this;
	pLadder->m_hUse[0] = pUse[0];
	pLadder->m_hUse[1] = pUse[1];
	pLadder->SetThink( &CRcHLLadder::WatchThink );
	pLadder->SetNextThink( 0.1 );

	m_hRcLadder = pLadder;

	ALERT( at_console, "ReCoF: ladder %s climbs as a Half-Life ladder in Half-Life movement, %.0f to %.0f\n", STRING( pev->targetname ), flBottom, vecCentre.z + iHalfHeight );
}

// ReCoF (rc_hlmovement): called by the player in Half-Life movement; sets up the ladders of the level that were
// not looked at yet (a new level, a retail save, or a save from before Half-Life movement was on)
void RcSetupHLLadders( void )
{
	CBaseEntity *pEnt = NULL;

	while ( ( pEnt = UTIL_FindEntityByClassname( pEnt, "cof_ladder_manager" ) ) != NULL )
	{
		CCoFLadderManager *pManager = (CCoFLadderManager *)pEnt;

		if ( !pManager->m_bRcLadderChecked )
			pManager->RcSetupHLLadder();
	}
}


class CCoFLadderManagerUse : public CBaseToggle
{
public:
	virtual void Spawn( void );
	virtual void Precache( void );
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual int ObjectCaps( void ) { return (CBaseEntity :: ObjectCaps() | FCAP_IMPULSE_USE) & ~FCAP_ACROSS_TRANSITION; }
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	float m_flNextUse;
};

TYPEDESCRIPTION CCoFLadderManagerUse::m_SaveData[] =
{
	DEFINE_FIELD( CCoFLadderManagerUse, m_flNextUse, FIELD_TIME ),
	DEFINE_FIELD( CCoFLadderManagerUse, m_sMaster, FIELD_STRING ),
};

IMPLEMENT_SAVERESTORE_RC( CCoFLadderManagerUse, CBaseToggle );	// ReCoF (B086): retail named CBaseEntity, so the CBaseToggle chain was not saved

LINK_ENTITY_TO_CLASS( cof_ladder_manager_use, CCoFLadderManagerUse );

void CCoFLadderManagerUse::Spawn( void )
{
	Precache();

	pev->solid = SOLID_BSP;
	pev->movetype = MOVETYPE_PUSH;

	UTIL_SetOrigin( this, pev->origin );
	UTIL_SetSize( pev, pev->mins, pev->maxs );
	SET_MODEL( ENT( pev ), STRING( pev->model ) );
	m_flNextUse = 0;

	pev->frame = 0;
}

void CCoFLadderManagerUse::Precache( void )
{
	if ( pev->noise )
		PRECACHE_SOUND( (char *)STRING( pev->noise ) );
}

void CCoFLadderManagerUse::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( !pActivator )
		return;

	if ( !pActivator->IsPlayer() )
		return;

	if ( !UTIL_IsMasterTriggered( m_sMaster, pActivator ) )
		return;

	if ( m_flNextUse > gpGlobals->time )
		return;

	CBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)pActivator->pev );

	if ( !pPlayer )
		return;

	// only climb from the ground
	if ( !( pPlayer->pev->flags & FL_ONGROUND ) )
		return;

	if ( pev->noise )
		EMIT_SOUND( ENT( pActivator->pev ), CHAN_AUTO, STRING( pev->noise ), 1, ATTN_NORM );

	if ( ( pev->spawnflags & 1 ) && pev->target )
	{
		CBaseEntity *pTarget = UTIL_FindEntityByTargetname( NULL, STRING( pev->target ) );

		if ( pTarget )
		{
			int distance = pPlayer->pev->origin.z - pTarget->pev->origin.z;

			if ( pev->target )
			{
				if ( distance <= 40 )
					FireTargets( STRING( pev->target ), pPlayer, pPlayer, USE_TOGGLE, 0 );
			}
		}
	}
	else
	{
		if ( pev->target )
			FireTargets( STRING( pev->target ), pPlayer, pPlayer, USE_TOGGLE, pev->iuser1 );
	}

	m_flNextUse = gpGlobals->time + 1;
}

class CCoFLadderEndTop : public CPointEntity
{
	void Spawn( void );
};

void CCoFLadderEndTop::Spawn( void )
{
	UTIL_SetOrigin( this, pev->origin );
	pev->solid = SOLID_NOT;
	pev->effects = EF_NODRAW;
	pev->v_angle = g_vecZero;
}

LINK_ENTITY_TO_CLASS( cof_ladderend_top, CCoFLadderEndTop );

class CCoFLadderEndBottom : public CPointEntity
{
	void Spawn( void );
};

void CCoFLadderEndBottom::Spawn( void )
{
	UTIL_SetOrigin( this, pev->origin );
	pev->solid = SOLID_NOT;
	pev->effects = EF_NODRAW;
	pev->v_angle = g_vecZero;
}

LINK_ENTITY_TO_CLASS( cof_ladderend_bottom, CCoFLadderEndBottom );
