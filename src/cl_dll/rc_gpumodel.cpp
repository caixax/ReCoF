// ReCoF (B287 step 4): studio models moved by their bones and lit in a vertex shader (rc_gpumodel.h)

#include "hud.h"
#include "cl_util.h"
#include "com_model.h"
#include "studio.h"
#include "rc_glsl.h"
#include "rc_gpumodel.h"
#include "rc_prof.h"

#include <stdlib.h>
#include <string.h>

extern cvar_t *rc_gpumodels;	// gl_renderer.cpp

#define RC_GPU_CACHE	1024	// submodels with a vertex buffer, until the next map
#define RC_GPU_UNIFORMS	1700	// vertex uniform components the shader needs: 128 bones of three rows, and the lights

// one vertex as GL_DrawPoints sends it, before the bones move it: the position and its bone, the normal and
// its bone, and the texture coordinate in texels
struct RcGpuVert_t
{
	float	pos[4];		// w: the bone of the position
	float	norm[4];	// w: the bone of the normal
	float	st[2];
};

#define RC_GPU_ATTR_ST		6	// generic attributes no fixed function array shares on any driver
#define RC_GPU_ATTR_NORM	7

struct RcGpuMesh_t
{
	int		first;		// first vertex in the buffer
	int		count;		// three vertices per triangle
	int		lastNorm;	// the normal of the mesh's last vertex, -1 when the mesh has none
	int		lastS, lastT;
};

struct RcGpuSub_t
{
	const studiohdr_t		*hdr;		// NULL: free slot
	const mstudiomodel_t	*sub;
	int						length;		// checks that the model at this address is still the same
	int						numverts;
	float					firstVert[3];
	GLuint					vbo;
	int						nummesh;
	RcGpuMesh_t				*mesh;
};

static RcGpuSub_t	s_RcGpuCache[RC_GPU_CACHE];
static int			s_iRcGpuUsed;
static RcGpuSub_t	*s_pRcGpuSub;		// the submodel of the last RcGpuPrepare

static int			s_iRcGpuState;		// 0 not tried yet on this map, 1 ready, -1 not available
static GLuint		s_iRcGpuProgram;
static int			s_iRcGpuBonesKey = -1;
static const void	*s_pRcGpuBonesHdr;
static float		s_flRcGpuBone0[12];	// the first bone matrix loaded, a last check that the bones are the same

static GLint		s_iULBones, s_iULAmbient, s_iULAddLight, s_iULLightDir, s_iULLambert, s_iULNumDl;
static GLint		s_iULDlOrg, s_iULDlSpot, s_iULDlColor, s_iULMesh, s_iULStScale;

// the vertex shader does what GL_DrawPoints and CStudioModelRenderer::Lighting do on the CPU, in the same
// order; the comments name the CPU steps. The normal is turned into world space and dotted with the world
// light vectors, which gives the same numbers as the CPU's normal dotted with the light vectors turned into
// the bone's space (VectorIRotate), since both are the same product of the bone matrix
static const char *s_szRcGpuVertex =
	"#version 110\n"
	"uniform vec4 rc_bones[384];\n"			// three rows of the bone matrix per bone
	"uniform vec3 rc_ambient;\n"
	"uniform vec3 rc_addlight;\n"
	"uniform vec3 rc_lightdir;\n"
	"uniform float rc_lambert;\n"
	"uniform int rc_numdl;\n"
	"uniform vec4 rc_dlorg[4];\n"
	"uniform vec4 rc_dlspot[4];\n"
	"uniform vec3 rc_dlcolor[4];\n"
	"uniform vec4 rc_mesh;\n"				// mode, brightness, hand fade divisor, alpha
	"uniform vec2 rc_stscale;\n"
	"attribute vec4 rc_norm;\n"
	"attribute vec2 rc_st;\n"
	"void main()\n"
	"{\n"
	// VectorTransform of the vertex by its bone
	"	int vb = int( gl_Vertex.w ) * 3;\n"
	"	vec4 v = vec4( gl_Vertex.xyz, 1.0 );\n"
	"	vec4 wp = vec4( dot( rc_bones[vb], v ), dot( rc_bones[vb + 1], v ), dot( rc_bones[vb + 2], v ), 1.0 );\n"
	"	vec4 ep = gl_ModelViewMatrix * wp;\n"
	"	gl_Position = gl_ProjectionMatrix * ep;\n"
	"	gl_ClipVertex = ep;\n"
	"	gl_FogFragCoord = abs( ep.z );\n"
	// glTexCoord2f( s * 1/width, t * 1/height ); the other units keep their current coordinates
	"	gl_TexCoord[0] = gl_TextureMatrix[0] * vec4( rc_st * rc_stscale, 0.0, 1.0 );\n"
	"	gl_TexCoord[1] = gl_TextureMatrix[1] * gl_MultiTexCoord1;\n"
	"	gl_TexCoord[2] = gl_TextureMatrix[2] * gl_MultiTexCoord2;\n"
	"	gl_TexCoord[3] = gl_TextureMatrix[3] * gl_MultiTexCoord3;\n"
	"	gl_FrontSecondaryColor = gl_SecondaryColor;\n"
	"	vec3 illum;\n"
	"	if ( rc_mesh.x > 1.5 )\n"
	"		illum = vec3( rc_mesh.y );\n"		// fullbright: cof_fullbrighttextures
	"	else\n"
	"	{\n"
	"		if ( rc_mesh.x > 0.5 )\n"
	"			illum = rc_ambient + 0.8 * rc_addlight;\n"	// STUDIO_NF_FLATSHADE
	"		else\n"
	"		{\n"
	"			int nb = int( rc_norm.w ) * 3;\n"
	"			vec3 n = vec3( dot( rc_bones[nb].xyz, rc_norm.xyz ), dot( rc_bones[nb + 1].xyz, rc_norm.xyz ), dot( rc_bones[nb + 2].xyz, rc_norm.xyz ) );\n"
	"			float lightcos = min( dot( n, rc_lightdir ), 1.0 );\n"
	"			illum = rc_ambient + rc_addlight;\n"
	"			lightcos = ( lightcos + ( rc_lambert - 1.0 ) ) / rc_lambert;\n"	// modified hemispherical lighting
	"			if ( lightcos > 0.0 )\n"
	"				illum -= lightcos * rc_addlight;\n"
	"			illum = max( illum, 0.0 );\n"
	// GL_SetupLighting's light vector per bone: from the light to the bone's origin, normalized, scaled by the
	// distance attenuation and the spot cone
	"			vec3 bone = vec3( rc_bones[nb].w, rc_bones[nb + 1].w, rc_bones[nb + 2].w );\n"
	"			for ( int i = 0; i < 4; i++ )\n"
	"			{\n"
	"				if ( i < rc_numdl )\n"
	"				{\n"
	"					vec3 d = bone - rc_dlorg[i].xyz;\n"
	"					float dist = dot( d, d );\n"
	"					float atten = max( 1.0 - dist / rc_dlorg[i].w, 0.0 );\n"
	"					if ( dist > 0.0 )\n"
	"						d *= 1.0 / sqrt( dist );\n"
	"					if ( rc_dlspot[i].w < 1.5 )\n"
	"					{\n"
	"						float cone = dot( rc_dlspot[i].xyz, d );\n"
	"						atten = cone < rc_dlspot[i].w ? 0.0 : atten * ( cone - rc_dlspot[i].w ) / ( 1.0 - rc_dlspot[i].w );\n"
	"					}\n"
	"					float c = -dot( n, d ) * atten;\n"
	"					if ( c > 0.0 )\n"
	"						illum += c * rc_dlcolor[i];\n"
	"				}\n"
	"			}\n"
	"		}\n"
	"		float m = max( illum.x, max( illum.y, illum.z ) );\n"
	"		if ( m > 1.0 )\n"
	"			illum *= 1.0 / m;\n"
	"		illum /= rc_mesh.z;\n"
	"	}\n"
	"	gl_FrontColor = vec4( illum, rc_mesh.w );\n"
	"}\n";

static void RcGpuFreeCache( void )
{
	for ( int i = 0; i < RC_GPU_CACHE; i++ )
	{
		RcGpuSub_t *c = &s_RcGpuCache[i];
		if ( !c->hdr )
			continue;

		if ( c->vbo )
			gl.glDeleteBuffersARB( 1, &c->vbo );
		free( c->mesh );
		memset( c, 0, sizeof( *c ) );
	}

	s_iRcGpuUsed = 0;
	s_pRcGpuSub = NULL;
}

void RcGpuModelsNewMap( void )
{
	if ( s_iRcGpuState > 0 )
	{
		RcGpuFreeCache();
		rcgl.glDeleteProgram( s_iRcGpuProgram );
	}

	s_iRcGpuProgram = 0;
	s_iRcGpuState = 0;
	s_iRcGpuBonesKey = -1;
	s_pRcGpuBonesHdr = NULL;
}

bool RcGpuModelsReady( void )
{
	if ( !rc_gpumodels || !rc_gpumodels->value )
		return false;

	if ( !s_iRcGpuState )
	{
		s_iRcGpuState = -1;

		if ( !RcGlslInit() )
			return false;

		if ( rcgl.maxVertexUniforms < RC_GPU_UNIFORMS )
		{
			ConLog( "ReCoF shaders: %d vertex uniform components are too few for the model shader, models stay on the CPU\n", rcgl.maxVertexUniforms );
			return false;
		}

		static const int index[] = { RC_GPU_ATTR_NORM, RC_GPU_ATTR_ST };
		static const char *name[] = { "rc_norm", "rc_st", NULL };
		s_iRcGpuProgram = RcGlslProgram( "studio model", s_szRcGpuVertex, NULL, index, name );
		if ( !s_iRcGpuProgram )
			return false;

		s_iULBones = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_bones" );
		s_iULAmbient = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_ambient" );
		s_iULAddLight = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_addlight" );
		s_iULLightDir = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_lightdir" );
		s_iULLambert = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_lambert" );
		s_iULNumDl = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_numdl" );
		s_iULDlOrg = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_dlorg" );
		s_iULDlSpot = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_dlspot" );
		s_iULDlColor = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_dlcolor" );
		s_iULMesh = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_mesh" );
		s_iULStScale = rcgl.glGetUniformLocation( s_iRcGpuProgram, "rc_stscale" );

		s_iRcGpuState = 1;
	}

	return s_iRcGpuState > 0;
}

static void RcGpuPutVert( RcGpuVert_t *out, const short *cmd, const vec3_t *verts, const byte *vertbone, const vec3_t *norms, const byte *normbone )
{
	out->pos[0] = verts[cmd[0]][0];
	out->pos[1] = verts[cmd[0]][1];
	out->pos[2] = verts[cmd[0]][2];
	out->pos[3] = vertbone[cmd[0]];
	out->norm[0] = norms[cmd[1]][0];
	out->norm[1] = norms[cmd[1]][1];
	out->norm[2] = norms[cmd[1]][2];
	out->norm[3] = normbone[cmd[1]];
	out->st[0] = cmd[2];
	out->st[1] = cmd[3];
}

// the triangles GL draws from a mesh's strips and fans, in the same order and with the same winding as
// GL_DrawPoints' glBegin strips (RcEnd in gl_studiomodel.cpp does the same conversion); out NULL only counts
static int RcGpuMeshTriangles( const studiohdr_t *hdr, const mstudiomodel_t *sub, const mstudiomesh_t *mesh, RcGpuVert_t *out, RcGpuMesh_t *info )
{
	const vec3_t *verts = (const vec3_t *)( (const byte *)hdr + sub->vertindex );
	const byte *vertbone = (const byte *)hdr + sub->vertinfoindex;
	const vec3_t *norms = (const vec3_t *)( (const byte *)hdr + sub->normindex );
	const byte *normbone = (const byte *)hdr + sub->norminfoindex;
	const short *cmd = (const short *)( (const byte *)hdr + mesh->triindex );
	int written = 0;
	int n;

	if ( info )
		info->lastNorm = -1;

	while ( ( n = *( cmd++ ) ) != 0 )
	{
		bool fan = n < 0;
		if ( fan )
			n = -n;

		for ( int k = 0; k < n - 2; k++ )
		{
			if ( out )
			{
				int a, b;
				if ( fan )
					a = 0, b = k + 1;
				else if ( k & 1 )
					a = k + 1, b = k;
				else
					a = k, b = k + 1;

				RcGpuPutVert( out++, cmd + a * 4, verts, vertbone, norms, normbone );
				RcGpuPutVert( out++, cmd + b * 4, verts, vertbone, norms, normbone );
				RcGpuPutVert( out++, cmd + ( k + 2 ) * 4, verts, vertbone, norms, normbone );
			}
			written += 3;
		}

		if ( info )
		{
			const short *last = cmd + ( n - 1 ) * 4;
			info->lastNorm = last[1];
			info->lastS = last[2];
			info->lastT = last[3];
		}

		cmd += n * 4;
	}

	return written;
}

static bool RcGpuSame( const RcGpuSub_t *c, const studiohdr_t *hdr, const mstudiomodel_t *sub )
{
	if ( c->hdr != hdr || c->sub != sub || c->length != hdr->length || c->numverts != sub->numverts || c->nummesh != sub->nummesh )
		return false;

	if ( sub->numverts )
	{
		const vec3_t *verts = (const vec3_t *)( (const byte *)hdr + sub->vertindex );
		if ( c->firstVert[0] != verts[0][0] || c->firstVert[1] != verts[0][1] || c->firstVert[2] != verts[0][2] )
			return false;
	}

	return true;
}

bool RcGpuPrepare( const void *pHdr, const void *pSub )
{
	const studiohdr_t *hdr = (const studiohdr_t *)pHdr;
	const mstudiomodel_t *sub = (const mstudiomodel_t *)pSub;

	unsigned int slot = ( (unsigned int)(size_t)sub >> 4 ) % RC_GPU_CACHE;
	RcGpuSub_t *c = NULL;

	for ( int probe = 0; probe < RC_GPU_CACHE; probe++ )
	{
		RcGpuSub_t *e = &s_RcGpuCache[( slot + probe ) % RC_GPU_CACHE];
		if ( !e->hdr )
		{
			c = e;
			break;
		}
		if ( e->hdr == hdr && e->sub == sub )
		{
			if ( RcGpuSame( e, hdr, sub ) )
			{
				s_pRcGpuSub = e;
				return true;
			}

			// the model memory was given to another model: build the buffer again in this slot
			if ( e->vbo )
				gl.glDeleteBuffersARB( 1, &e->vbo );
			free( e->mesh );
			memset( e, 0, sizeof( *e ) );
			s_iRcGpuUsed--;
			c = e;
			break;
		}
	}

	if ( !c || s_iRcGpuUsed >= RC_GPU_CACHE * 3 / 4 )
	{
		RcGpuFreeCache();
		c = &s_RcGpuCache[slot];
	}

	const mstudiomesh_t *pmesh = (const mstudiomesh_t *)( (const byte *)hdr + sub->meshindex );
	RcGpuMesh_t *info = (RcGpuMesh_t *)calloc( sub->nummesh > 0 ? sub->nummesh : 1, sizeof( RcGpuMesh_t ) );
	if ( !info )
		return false;

	int total = 0;
	for ( int j = 0; j < sub->nummesh; j++ )
	{
		info[j].first = total;
		info[j].count = RcGpuMeshTriangles( hdr, sub, &pmesh[j], NULL, &info[j] );
		total += info[j].count;
	}

	GLuint vbo = 0;
	if ( total )
	{
		RcGpuVert_t *data = (RcGpuVert_t *)malloc( total * sizeof( RcGpuVert_t ) );
		if ( !data )
		{
			free( info );
			return false;
		}

		for ( int j = 0; j < sub->nummesh; j++ )
			RcGpuMeshTriangles( hdr, sub, &pmesh[j], data + info[j].first, NULL );

		// models draw outside the world pass, where no buffer is bound (GL_DrawPoints checks use_vertex_array)
		gl.glGenBuffersARB( 1, &vbo );
		gl.glBindBufferARB( GL_ARRAY_BUFFER_ARB, vbo );
		gl.glBufferDataARB( GL_ARRAY_BUFFER_ARB, total * sizeof( RcGpuVert_t ), data, GL_STATIC_DRAW_ARB );
		gl.glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		free( data );

		if ( !vbo )
		{
			free( info );
			return false;
		}
	}

	c->hdr = hdr;
	c->sub = sub;
	c->length = hdr->length;
	c->numverts = sub->numverts;
	if ( sub->numverts )
	{
		const vec3_t *verts = (const vec3_t *)( (const byte *)hdr + sub->vertindex );
		c->firstVert[0] = verts[0][0];
		c->firstVert[1] = verts[0][1];
		c->firstVert[2] = verts[0][2];
	}
	c->vbo = vbo;
	c->nummesh = sub->nummesh;
	c->mesh = info;
	s_iRcGpuUsed++;

	s_pRcGpuSub = c;
	return true;
}

#define RC_GPU_OFFSET( member )	( (const void *)&( ( (RcGpuVert_t *)NULL )->member ) )

void RcGpuBegin( const float *bones, int numbones, int bonesKey, const void *hdr, const RcGpuLight_t *light )
{
	rcgl.glUseProgram( s_iRcGpuProgram );

	// one entity draws all its submodels with the same bones (the model count changes per entity; a weapon
	// model merged onto a player keeps the count but has its own header)
	if ( bonesKey != s_iRcGpuBonesKey || hdr != s_pRcGpuBonesHdr || memcmp( bones, s_flRcGpuBone0, sizeof( s_flRcGpuBone0 ) ) )
	{
		rcgl.glUniform4fv( s_iULBones, numbones * 3, bones );
		s_iRcGpuBonesKey = bonesKey;
		s_pRcGpuBonesHdr = hdr;
		memcpy( s_flRcGpuBone0, bones, sizeof( s_flRcGpuBone0 ) );
	}

	rcgl.glUniform3fv( s_iULAmbient, 1, light->ambient );
	rcgl.glUniform3fv( s_iULAddLight, 1, light->addlight );
	rcgl.glUniform3fv( s_iULLightDir, 1, light->lightdir );
	rcgl.glUniform1f( s_iULLambert, light->lambert );
	rcgl.glUniform1i( s_iULNumDl, light->numdl );
	if ( light->numdl )
	{
		rcgl.glUniform4fv( s_iULDlOrg, light->numdl, &light->dlorg[0][0] );
		rcgl.glUniform4fv( s_iULDlSpot, light->numdl, &light->dlspot[0][0] );
		rcgl.glUniform3fv( s_iULDlColor, light->numdl, &light->dlcolor[0][0] );
	}

	if ( !s_pRcGpuSub->vbo )
		return;

	// the same client array state RcMeshFlush uses: unit 0's texture coordinate and colour arrays are off, the
	// vertex array carries the position and its bone
	if ( gl.glClientActiveTextureARB )
		gl.glClientActiveTextureARB( GL_TEXTURE0_ARB );
	gl.glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	gl.glDisableClientState( GL_COLOR_ARRAY );

	gl.glBindBufferARB( GL_ARRAY_BUFFER_ARB, s_pRcGpuSub->vbo );
	gl.glVertexPointer( 4, GL_FLOAT, sizeof( RcGpuVert_t ), RC_GPU_OFFSET( pos ) );
	gl.glEnableClientState( GL_VERTEX_ARRAY );
	rcgl.glVertexAttribPointer( RC_GPU_ATTR_NORM, 4, GL_FLOAT, GL_FALSE, sizeof( RcGpuVert_t ), RC_GPU_OFFSET( norm ) );
	rcgl.glEnableVertexAttribArray( RC_GPU_ATTR_NORM );
	rcgl.glVertexAttribPointer( RC_GPU_ATTR_ST, 2, GL_FLOAT, GL_FALSE, sizeof( RcGpuVert_t ), RC_GPU_OFFSET( st ) );
	rcgl.glEnableVertexAttribArray( RC_GPU_ATTR_ST );
}

void RcGpuDrawMesh( int mesh, int mode, float bright, float handScale, float alpha, float sscale, float tscale )
{
	const RcGpuMesh_t *m = &s_pRcGpuSub->mesh[mesh];
	if ( !m->count )
		return;

	rcgl.glUniform4f( s_iULMesh, (float)mode, bright, handScale, alpha );
	rcgl.glUniform2f( s_iULStScale, sscale, tscale );
	gl.glDrawArrays( GL_TRIANGLES, m->first, m->count );
	RcProf_Count( RCC_GPUMESHES );
}

bool RcGpuMeshLast( int mesh, int *norm, int *s, int *t )
{
	const RcGpuMesh_t *m = &s_pRcGpuSub->mesh[mesh];
	if ( m->lastNorm < 0 )
		return false;

	*norm = m->lastNorm;
	*s = m->lastS;
	*t = m->lastT;
	return true;
}

void RcGpuEnd( void )
{
	if ( s_pRcGpuSub->vbo )
	{
		rcgl.glDisableVertexAttribArray( RC_GPU_ATTR_ST );
		rcgl.glDisableVertexAttribArray( RC_GPU_ATTR_NORM );
		gl.glDisableClientState( GL_VERTEX_ARRAY );
		gl.glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	}

	rcgl.glUseProgram( 0 );
}
