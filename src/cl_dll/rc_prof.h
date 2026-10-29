// ReCoF frame profiler (rc_prof.cpp)

#ifndef RC_PROF_H
#define RC_PROF_H

// The phases of a frame. Each moment of the frame belongs to the innermost scope open at that moment, so
// the times add up to the whole frame without counting anything twice.
enum
{
	RCP_OTHER = 0,		// outside every client scope: engine, server game logic, sound, buffer swap
	RCP_VIEW,			// V_CalcRefdef
	RCP_ENTS,			// HUD_CreateEntities and HUD_AddEntity
	RCP_CLFRAME,		// HUD_Frame (music, voice)
	RCP_WORLD,			// DrawBump outside the passes below
	RCP_WORLD_FIRST,	// first pass: world surfaces and the brush entities that do not move
	RCP_WORLD_BUMP,		// additional bump pass
	RCP_WORLD_DLIGHTS,	// one pass per dynamic light
	RCP_WORLD_SECOND,	// second pass
	RCP_WORLD_SPECULAR,	// specular pass
	RCP_WORLD_BRUSH,	// brush entities that move
	RCP_WORLD_DECALS,	// decals
	RCP_SKY,			// sky
	RCP_MODELS,			// studio models and players
	RCP_MDL_VERTS,		// models: the vertices moved by their bones (GL_DrawPoints)
	RCP_MDL_LIGHT,		// models: the normals lit (GL_DrawPoints)
	RCP_MDL_DRAW,		// models: the meshes sent to the driver (GL_DrawPoints)
	RCP_TRANS,			// transparent triangles: fog, particles, objects, grass, weather, glows
	RCP_WATER,			// Cg water
	RCP_POST,			// post effects
	RCP_HUD,			// the HUD
	RCP_COUNT
};

enum
{
	RCC_MODELS = 0,		// studio models drawn
	RCC_DLIGHTS,		// dynamic lights drawn on the world
	RCC_WORLDDRAWS,		// draw calls of world and brush polygons from the vertex buffer (B195)
	RCC_COUNT
};

void RcProf_Init( void );			// cvars and commands (RcClient_Init)
void RcProf_Frame( void );			// frame boundary (start of HUD_Redraw)
void RcProf_Draw( void );			// the overlay (end of CHud::Redraw)
void RcProf_Push( int scope );
void RcProf_Pop( void );
void RcProf_Count( int counter );

// opens a scope for the rest of the block
struct RcProfScope
{
	RcProfScope( int scope ) { RcProf_Push( scope ); }
	~RcProfScope() { RcProf_Pop(); }
};

#endif // RC_PROF_H
