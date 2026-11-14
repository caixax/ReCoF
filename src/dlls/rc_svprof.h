// ReCoF server profiler (rc_svprof.cpp, B337)

#ifndef RC_SVPROF_H
#define RC_SVPROF_H

// The scopes. Each moment the server spends in hl.dll belongs to the innermost scope open at that moment, so the
// exclusive times add up without counting anything twice; each scope also has its inclusive time and its calls.
enum
{
	RCSV_OTHER = 0,		// hl.dll code the engine called outside the scopes below
	RCSV_PLAYER,		// PlayerPreThink and PlayerPostThink, without CheckDesiredList
	RCSV_DESIRED,		// CheckDesiredList: the thinks of monsters and of entities that move with others
	RCSV_STARTFRAME,	// StartFrame: the game rules and CheckAssistList
	RCSV_THINK,			// DispatchThink: the thinks the engine runs (entities that are not monsters)
	RCSV_TOUCH,			// DispatchTouch and DispatchBlocked
	RCSV_FULLPACK,		// AddToFullPack: deciding what the client is sent
	RCSV_MTHINK,		// CBaseMonster::MonsterThink
	RCSV_RUNAI,			// CBaseMonster::RunAI: sensing and the schedule's choices
	RCSV_LOOK,			// CBaseMonster::Look
	RCSV_LISTEN,		// CBaseMonster::Listen
	RCSV_ENTSINBOX,		// UTIL_EntitiesInBox and UTIL_MonstersInSphere (the scan of every slot, B107)
	RCSV_LOCALMOVE,		// CBaseMonster::CheckLocalMove
	RCSV_ROUTE,			// BuildRoute, BuildNearestRoute and FRefreshRoute (the node graph)
	RCSV_COVER,			// FindCover
	RCSV_MOVE,			// CBaseMonster::Move
	RCSV_MOVEEXEC,		// CBaseMonster::MoveExecute
	// the engine functions hl.dll calls, while rc_svprof is on (RcSvHook)
	RCSV_E_TRACELINE,
	RCSV_E_TRACEHULL,
	RCSV_E_MONSTERHULL,
	RCSV_E_TRACEMODEL,
	RCSV_E_TRACETOSS,
	RCSV_E_TEXTURE,
	RCSV_E_WALKMOVE,
	RCSV_E_MOVETO,
	RCSV_E_DROPTOFLOOR,
	RCSV_E_SETORIGIN,
	RCSV_E_SPHERE,		// FindEntityInSphere
	RCSV_E_FINDSTRING,	// FindEntityByString
	RCSV_E_PVS,			// FindClientInPVS, EntitiesInPVS, CheckVisibility
	RCSV_E_CONTENTS,	// PointContents
	RCSV_E_SOUND,		// EmitSound
	RCSV_E_BONES,		// GetBonePosition, GetAttachment
	RCSV_COUNT
};

extern int g_iRcSvProf;		// rc_svprof as read at the last StartFrame

void RcSvProf_Init( void );		// cvar and commands (GameDLLInit)
void RcSvProf_Frame( void );	// the frame boundary, at the start of StartFrame
void RcSvProf_Push( int scope );
void RcSvProf_Pop( void );

// opens a scope for the rest of the block; does nothing while rc_svprof is 0
struct RcSvScope
{
	int m_bOn;
	RcSvScope( int scope ) { m_bOn = g_iRcSvProf; if ( m_bOn ) RcSvProf_Push( scope ); }
	~RcSvScope() { if ( m_bOn ) RcSvProf_Pop(); }
};

#endif // RC_SVPROF_H
