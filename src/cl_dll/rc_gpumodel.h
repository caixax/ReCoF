// ReCoF (B287 step 4): studio models moved by their bones and lit in a vertex shader
//
// CStudioModelRenderer::GL_DrawPoints moved every vertex by its bone matrix and lit every normal on the CPU,
// every frame, and sent the result to the driver. Here each submodel's vertices go to a vertex buffer once per
// map, in model space with their bone numbers, and a vertex shader does the same bone transform and the same
// lighting as GL_DrawPoints and Lighting (ambient, the lambert term, up to four dynamic lights per bone, flat
// shaded, fullbright and hand fade meshes). The fragment stage stays the fixed function one, so textures,
// texture environments, blending and alpha test are unchanged. Meshes with chrome coordinates or the random
// brightness of corona textures, and submodels with hidden bones, keep the CPU path.

#ifndef RC_GPUMODEL_H
#define RC_GPUMODEL_H

#define RC_GPU_MAXBONES		128
#define RC_GPU_MAXLIGHTS	4		// MAX_MODEL_DYNLIGHTS

// the lighting GL_SetupLighting worked out for the entity being drawn
struct RcGpuLight_t
{
	float	ambient[3];
	float	addlight[3];
	float	lightdir[3];
	float	lambert;							// cv_lambert, at least 1
	int		numdl;
	float	dlorg[RC_GPU_MAXLIGHTS][4];		// where the light vectors start, and the radius squared
	float	dlspot[RC_GPU_MAXLIGHTS][4];	// the spot direction and cone cosine; w is 2 for a point light
	float	dlcolor[RC_GPU_MAXLIGHTS][3];
};

enum
{
	RC_GPU_LIT = 0,		// Lighting
	RC_GPU_FLAT,		// Lighting with STUDIO_NF_FLATSHADE
	RC_GPU_BRIGHT		// one brightness for every vertex (fullbright textures)
};

// rc_gpumodels is on and the shader works on this driver
bool	RcGpuModelsReady( void );

// frees the vertex buffers and the program, at a map load
void	RcGpuModelsNewMap( void );

// finds or builds the vertex buffer of a submodel (studiohdr_t, mstudiomodel_t); false if it cannot
bool	RcGpuPrepare( const void *hdr, const void *submodel );

// binds the program and the submodel of the last RcGpuPrepare, and loads the bones and the lighting. The bones
// are loaded again only when bonesKey and hdr differ from the last call (the model count of the renderer)
void	RcGpuBegin( const float *bones, int numbones, int bonesKey, const void *hdr, const RcGpuLight_t *light );

// draws one mesh of the submodel: mode RC_GPU_*, bright for RC_GPU_BRIGHT, the hand fade divisor, the alpha
// and the texture coordinate scale (one over the texture size)
void	RcGpuDrawMesh( int mesh, int mode, float bright, float handScale, float alpha, float sscale, float tscale );

// the normal and texture coordinate of a mesh's last vertex; false when the mesh has none
bool	RcGpuMeshLast( int mesh, int *norm, int *s, int *t );

// unbinds everything RcGpuBegin bound
void	RcGpuEnd( void );

#endif // RC_GPUMODEL_H
