// ReCoF bone setup on worker threads (rc_bones.cpp, B339)

#ifndef RC_BONES_H
#define RC_BONES_H

class CStudioModelRenderer;
struct cl_entity_s;

void RcBones_Init( void );						// cvars and commands (RcClient_Init)
void RcBones_NewFrame( void );					// HUD_Frame: a game frame begins, its entities come next
void RcBones_Add( struct cl_entity_s *ent );	// HUD_AddEntity: an entity the engine draws this frame
void RcBones_Launch( void );					// R_StudioDrawModel, R_StudioDrawPlayer: before the first model
int RcBones_Replay( CStudioModelRenderer *pRenderer );	// StudioSetupBones: 1 when it set the bones
void RcBones_Shutdown( void );					// IN_Shutdown: the workers end before client.dll goes

// set by StudioSetupBonesNow on the thread that ran it: 1 when it stored the frame in latched.prevframe
extern __declspec( thread ) int g_iRcBonesPrevframeSet;

#endif // RC_BONES_H
