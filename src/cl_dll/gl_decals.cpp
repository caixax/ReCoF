//
// written by BUzer for HL: Paranoia modification
//
//		2006

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "cdll_int.h"
#include "com_model.h"
#include "glmanager.h"
#include "entity_types.h"
#include "gl_texloader.h"

#include "studio_event.h" // def. of mstudioevent_t
#include "r_efx.h"
#include "event_api.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "parsemsg.h"
#include "gl_renderer.h"
#include "gl_dlight.h"


#define MAX_DECALTEXTURES	128
#define MAX_CUSTOMDECALS	1024
#define MAX_STATICDECALS	256
#define MAX_GROUPENTRIES	64

#define MAX_DECAL_MSG_CACHE	256


class DecalGroupEntry
{
public:
	int gl_texid;
	int xsize, ysize;
	float overlay;
};

class DecalGroup
{
public:
	DecalGroup(const char *_name, int numelems, DecalGroupEntry *source);
	~DecalGroup();

	DecalGroupEntry *GetEntry(int num);
	const DecalGroupEntry *GetRandomDecal();

	char name[16];

// private:
	DecalGroup *pnext;
	DecalGroupEntry *pEntryArray;
	int size;
};

DecalGroup *pDecalGroupList = NULL;


DecalGroup::DecalGroup(const char *_name, int numelems, DecalGroupEntry *source)
{
	strcpy(name, _name);
	pEntryArray = new DecalGroupEntry[numelems];
	size = numelems;
	memcpy(pEntryArray, source, sizeof(DecalGroupEntry)*size);
	pnext = pDecalGroupList;
	pDecalGroupList = this;
}

DecalGroup::~DecalGroup()
{
	delete[] pEntryArray;
	if (pnext) delete pnext;
}

DecalGroupEntry* DecalGroup::GetEntry(int num)
{
	return &pEntryArray[num];

}

const DecalGroupEntry* DecalGroup::GetRandomDecal()
{
	if (size == 0)
		return NULL;

	if (size == 1)
		return &pEntryArray[0];

	int idx = gEngfuncs.pfnRandomLong( 0, size-1 );
	return &pEntryArray[idx];
}



DecalGroup* FindGroup(const char *_name)
{
	DecalGroup *plist = pDecalGroupList;
	while(plist)
	{
		if (!strcmp(plist->name, _name))
			return plist;

		plist = plist->pnext;
	}
	return NULL; // nothing found
}



typedef struct decaltexture_s {
	char name[16];
	int gl_texid;
} decaltexture;

typedef struct customdecal_s {
	msurface_t *surface;
	vec3_t point;
	vec3_t normal;
	const DecalGroupEntry *texinfo;
} customdecal;


struct decal_msg_cache
{
	vec3_t	pos;
	vec3_t	normal;
	char	name[16];
	int		persistent;
};



decaltexture decaltextures[MAX_DECALTEXTURES];
customdecal decals[MAX_CUSTOMDECALS];
customdecal staticdecals[MAX_STATICDECALS];
decal_msg_cache msgcache[MAX_DECAL_MSG_CACHE];

int numdecaltextures = 0;
int numdecals = 0;
int numstaticdecals = 0;
int curdecal = 0;
int cachedecals = 0;

int decalrendercounter = 0;
int staticdecalrendercounter = 0;

cvar_t *cv_customdecals;
cvar_t *cv_decalsdebug;



// ===========================
// Math
// ===========================

// пересечение линии с плоскостью
void FindIntersectionPoint( const vec3_t &p1, const vec3_t &p2, const vec3_t &normal, const vec3_t &planepoint, vec3_t &newpoint )
{
	vec3_t planevec;
	vec3_t linevec;
	float planedist, linedist;

	VectorSubtract( planepoint, p1, planevec );
	VectorSubtract( p2, p1, linevec );
	planedist = DotProduct( normal, planevec );
	linedist = DotProduct( normal, linevec );

	if (linedist != 0)
	{
		VectorMA( p1, planedist/linedist, linevec, newpoint );
		return;
	}
	VectorClear( newpoint ); // error - прямая параллельна плоскости
}



// нормаль смотрит в сторону неотсекаемой части полигона
int ClipPolygonByPlane (const vec3_t *arrIn, int numpoints, vec3_t normal, vec3_t planepoint, vec3_t *arrOut)
{
	int i, cur, prev;
	int first = -1;
	int outCur = 0;
	float dots[64];
	for (i = 0; i < numpoints; i++)
	{
		vec3_t vecDir;
		VectorSubtract( arrIn[i], planepoint, vecDir );
		dots[i] = DotProduct( vecDir, normal );
		if (dots[i] > 0) first = i;
	}
	if (first == -1) return 0; // плоскость отсекла весь многоугольник

	VectorCopy( arrIn[first], arrOut[outCur] );
	outCur++;

	cur = first + 1;
	if (cur == numpoints) cur = 0;
	while (cur != first)
	{
		if (dots[cur] > 0)
		{
			VectorCopy( arrIn[cur], arrOut[outCur] );
			cur++; outCur++;
			if (cur == numpoints) cur = 0;
		}
		else
			break;
	}

	if (cur == first) return outCur; // ничего не отсекается этой плоскостью

	if (dots[cur] < 0)
	{
		vec3_t newpoint;
		if (cur > 0) prev = cur-1;
		else prev = numpoints - 1;
		FindIntersectionPoint( arrIn[prev], arrIn[cur], normal, planepoint, newpoint );
		VectorCopy( newpoint, arrOut[outCur] );
	}
	else
	{
		VectorCopy( arrIn[cur], arrOut[outCur] );
	}
	outCur++;
	cur++;
	if (cur == numpoints) cur = 0;

	while (dots[cur] < 0)
	{
		cur++;
		if (cur == numpoints) cur = 0;
	}

	if (cur > 0) prev = cur-1;
	else prev = numpoints - 1;
	if (dots[cur] > 0 && dots[prev] < 0)
	{
		vec3_t newpoint;
		FindIntersectionPoint( arrIn[prev], arrIn[cur], normal, planepoint, newpoint );
		VectorCopy( newpoint, arrOut[outCur] );
		outCur++;
	}

	while (cur != first)
	{
		VectorCopy( arrIn[cur], arrOut[outCur] );
		cur++; outCur++;
		if (cur == numpoints) cur = 0;
	}
	return outCur;
}

void GetUpRight(vec3_t forward, vec3_t &up, vec3_t &right)
{
	VectorClear(up);
	if (forward.x || forward.y)
		up.z = 1;
	else
		up.x = 1;

	right = CrossProduct(forward, up);
	VectorNormalize(right);
	up = CrossProduct(forward, right);
	VectorNormalize(up);
}



// ===========================
// Decals loading
// ===========================

int LoadDecalTexture(const char *texname)
{
	for (int i = 0; i < numdecaltextures; i++)
	{
		if (!strcmp(decaltextures[i].name, texname))
			return decaltextures[i].gl_texid;
	}

	if (numdecaltextures >= MAX_DECALTEXTURES)
	{
		ConLog("Too many entries in decal info file (%d max)\n", MAX_DECALTEXTURES);
		return 0;
	}

	char path[256];	
	sprintf(path, "gfx/decals/%s.tga", texname);
	int newid = CreateTexture(path, MIPS_YES);
	if (!newid)
	{
		ConLog("Missing decal texture %s!\n", path);
		return 0;
	}

//	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
//	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );

	strcpy(decaltextures[numdecaltextures].name, texname);
	decaltextures[numdecaltextures].gl_texid = newid;
	numdecaltextures++;
	return newid;
}

void LoadDecals()
{
	if (!cv_customdecals->value)
		return;

	if (!gl.IsGLAllowed())
		return;

	// only once
	static int called = 0;
	if (called) return;
	called = 1;

	Log("\n>> Loading decals\n");

	char *pfile = (char *)gEngfuncs.COM_LoadFile("gfx/decals/decalinfo.txt", 5, NULL);
	if (!pfile)
	{
		ConLog("Cannot open file \"gfx/decals/decalinfo.txt\"\n");
		return;
	}

	int counter = 0;
	char *ptext = pfile;
	while(1)
	{
		// store position where group names recorded
		char *groupnames = ptext;

		// loop until we'll find decal names
		int numgroups = 0;
		char token[256];
		while(1)
		{
			ptext = gEngfuncs.COM_ParseFile(ptext, token);
			if (!ptext) goto getout;
			if (token[0] == '{') break;
			numgroups++;
		}

		DecalGroupEntry tempentries[MAX_GROUPENTRIES];
		int numtemp = 0;
		while(1)
		{
			char sz_xsize[64];
			char sz_ysize[64];
			char sz_overlay[64];
			ptext = gEngfuncs.COM_ParseFile(ptext, token);
			if (!ptext) goto getout;
			if (token[0] == '}') break;

			if (numtemp >= MAX_GROUPENTRIES)
			{
				ConLog("Too many decals in group (%d max) - skipping %s\n", MAX_GROUPENTRIES, token);
				continue;
			}

			ptext = gEngfuncs.COM_ParseFile(ptext, sz_xsize);
			if (!ptext) goto getout;
			ptext = gEngfuncs.COM_ParseFile(ptext, sz_ysize);
			if (!ptext) goto getout;
			ptext = gEngfuncs.COM_ParseFile(ptext, sz_overlay);
			if (!ptext) goto getout;

			if (strlen(token) > 16)
			{
				ConLog("%s - got too large token when parsing decal info file\n", token);
				continue;
			}

			int id = LoadDecalTexture(token);
			if (!id)
				continue;

			tempentries[numtemp].gl_texid = id;
			tempentries[numtemp].xsize = atof(sz_xsize) / 2;
			tempentries[numtemp].ysize = atof(sz_ysize) / 2;
			tempentries[numtemp].overlay = atof(sz_overlay) * 2;
			numtemp++;
		}

		// get back to group names

		for (int i = 0; i < numgroups; i++)
		{
			groupnames = gEngfuncs.COM_ParseFile(groupnames, token);
			if (!numtemp)
			{
				ConLog("warning: got empty decal group: %s\n", token);
				continue;
			}

			new DecalGroup(token, numtemp, tempentries);
			counter++;
		}
	}

getout:

	gEngfuncs.COM_FreeFile( pfile );
	ConLog("%d decal groups created\n", counter);
}



// ===========================
// Decals creation
// ===========================
customdecal *AllocDecal()
{
	customdecal *ret = &decals[curdecal];

	if (numdecals < MAX_CUSTOMDECALS)
		numdecals++;

	curdecal++;
	if (curdecal == MAX_CUSTOMDECALS)
		curdecal = 0; // get decals from tail

	return ret;
}


// static decals, used for level detalization, placed by mapper
customdecal *AllocStaticDecal()
{
	if (numstaticdecals < MAX_STATICDECALS)
	{
		customdecal *ret = &staticdecals[numstaticdecals];
		numstaticdecals++;
		return ret;
	}
	return NULL;
}



void CreateDecal(vec3_t endpos, vec3_t pnormal, const char *name, int persistent = 0)
{
	if (!cv_customdecals->value)
		return;

	LoadDecals();

	model_t *world = gEngfuncs.GetEntityByIndex(0)->model;
	if (!world)
	{
		// CreateDecal often being called from server messages. Messages about static
		// decals are sent during initialization, and in some cases i cant access
		// world model when message received (this happens during level transitions).
		// So instead of creating decal now, i will add them to cache -
		// they will be created when drawing starts.

		if (cachedecals >= MAX_DECAL_MSG_CACHE)
		{
			ConLog("Too many decals in cache!\n");
			return;
		}

		strcpy(msgcache[cachedecals].name, name);
		msgcache[cachedecals].normal = pnormal;
		msgcache[cachedecals].pos = endpos;
		msgcache[cachedecals].persistent = persistent;
		cachedecals++;
	//	CONPRINT("decal cached\n");
		return;
	}

	DecalGroup *group = FindGroup(name);
	if (!group)
	{
	//	gEngfuncs.Con_Printf("decal group %s not found\n", name);
		// buz: this msg will flood in sw and d3d video modes
		return;
	}

	const DecalGroupEntry *texptr = group->GetRandomDecal();
	if (!texptr)
	{
	//	gEngfuncs.Con_Printf("no decals in group %s!\n", name);
		return;
	}

	int xsize = texptr->xsize;
	int ysize = texptr->ysize;

	vec3_t right, up;
	GetUpRight(pnormal, up, right);
	float radius = sqrt((float)(xsize*xsize + ysize*ysize));

	float texc_orig_x = DotProduct(endpos, right);
	float texc_orig_y = DotProduct(endpos, up);

//	msurface_t* surfaces = world->surfaces;
	msurface_t* surfaces = &world->surfaces[world->firstmodelsurface];
//	for (int i = 0; i < world->numsurfaces; i++)
	for (int i = 0; i < world->nummodelsurfaces; i++)
	{
		vec3_t norm;
		float ndist = surfaces[i].plane->dist;
		VectorCopy(surfaces[i].plane->normal, norm);
		if (surfaces[i].flags & SURF_PLANEBACK)
		{
			VectorInverse(norm);
			ndist *= -1;
		}

		float normdot = DotProduct(pnormal, norm);
		if (normdot < 0.7)
			continue;

		float dist = DotProduct(norm, endpos);
		dist = dist - ndist;
		if (dist < 0) dist *= -1;
		if (dist > radius)
			continue;

		if (surfaces[i].flags & (SURF_DRAWSKY|SURF_DRAWTURB))
			continue;

		if (!surfaces[i].samples)
			continue;

		// execute part of drawing process to see if polygon completely clipped

		glpoly_t	*p = surfaces[i].polys;
		float		*v = p->verts[0];

		vec3_t array1[64];
		vec3_t array2[64];
		for (int j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
		{
			VectorCopy( v, array1[j] );
		}

		int nv;
		vec3_t planepoint;
		VectorMA(endpos, -xsize, right, planepoint);
		nv = ClipPolygonByPlane (array1, p->numverts, right, planepoint, array2);

		VectorMA(endpos, xsize, right, planepoint);
		nv = ClipPolygonByPlane (array2, nv, right*-1, planepoint, array1);

		VectorMA(endpos, -ysize, up, planepoint);
		nv = ClipPolygonByPlane (array1, nv, up, planepoint, array2);

		VectorMA(endpos, ysize, up, planepoint);
		nv = ClipPolygonByPlane (array2, nv, up*-1, planepoint, array1);

		if (!nv)
			continue; // no vertexes left after clipping

		customdecal *newdecal = NULL;

		if (persistent)
		{
			newdecal = AllocStaticDecal();
			if (!newdecal)
			{
				CONPRINT("Warning: too many static decals on level!\n");
				return;
			}
		}
		else
		{
			// look other decals on this surface - is someone too close?
			// if so, clear him

			for (int k = 0; k < numdecals; k++)
			{
				if ((decals[k].surface == &surfaces[i]) &&
					(decals[k].texinfo->xsize == texptr->xsize) &&
					(decals[k].texinfo->ysize == texptr->ysize))
				{
					float texc_x = DotProduct(decals[k].point, right) - texc_orig_x;
					float texc_y = DotProduct(decals[k].point, up) - texc_orig_y;
					if (texc_x < 0) texc_x *= -1;
					if (texc_y < 0) texc_y *= -1;
					if (texc_x < (float)texptr->xsize*texptr->overlay &&
						texc_y < (float)texptr->ysize*texptr->overlay)
					{
						newdecal = &decals[k];
					//	gEngfuncs.Con_Printf("paste on others place\n");
						break;
					}
				}
			}

			if (!newdecal)
				newdecal = AllocDecal();
		}

		newdecal->surface = &surfaces[i];
		VectorCopy(endpos, newdecal->point);
		VectorCopy(pnormal, newdecal->normal);
		newdecal->texinfo = texptr;
	//	gEngfuncs.Con_Printf("pasted decal %s\n", name);
	}
}


void CreateDecal(pmtrace_t &tr, const char *name, int persistent = 0)
{
	if (tr.ent)	return; // not on entites for now
	if (tr.allsolid || tr.fraction == 1.0) return;

	CreateDecal(tr.endpos, tr.plane.normal, name, persistent);
}

void CreateDecalTrace(vec3_t start, vec3_t end, const char *name, int persistent = 0)
{
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( start, end, PM_NORMAL, -1, &tr );
	CreateDecal(tr, name, persistent);
}


void CreateCachedDecals()
{
	if (!gEngfuncs.GetEntityByIndex(0)->model)
		return; // world model is inaccesible!
	// (heh, if it will happen during rendering, the paranoia renderer will crash much earlier :)

	for (int i = 0; i < cachedecals; i++)
	{
		CreateDecal(msgcache[i].pos, msgcache[i].normal, msgcache[i].name, msgcache[i].persistent);
	//	CONPRINT("decal created from cache\n");
	}

	cachedecals = 0;
}


// debugging feature
extern vec3_t	render_origin;
extern vec3_t	render_angles;

void PasteViewDecal()
{
	if ( gEngfuncs.Cmd_Argc() <= 1 )
	{
		gEngfuncs.Con_Printf( "usage: pastedecal <decal name>\n" );
		return;
	}

	vec3_t dir;
	AngleVectors(render_angles, dir, NULL, NULL);
	CreateDecalTrace(render_origin, render_origin + (dir * 1024), gEngfuncs.Cmd_Argv(1));
}



// ===========================
// Decals drawing
//
//		Uses surface visframe information from custom renderer's pass
// ===========================

int DrawSingleDecal(customdecal *decal)
{
	if (decal->surface->visframe != framecount)
		return FALSE;

	vec3_t right, up;
	GetUpRight(decal->normal, up, right);

	float texc_orig_x = DotProduct(decal->point, right);
	float texc_orig_y = DotProduct(decal->point, up);
	int xsize = decal->texinfo->xsize;
	int ysize = decal->texinfo->ysize;

	glpoly_t	*p = decal->surface->polys;
	float		*v = p->verts[0];

	vec3_t array1[64];
	vec3_t array2[64];
	for (int j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
	{
		VectorCopy( v, array1[j] );
	}

	int nv;
	vec3_t planepoint;
	VectorMA(decal->point, -xsize, right, planepoint);
	nv = ClipPolygonByPlane (array1, p->numverts, right, planepoint, array2);

	VectorMA(decal->point, xsize, right, planepoint);
	nv = ClipPolygonByPlane (array2, nv, right*-1, planepoint, array1);

	VectorMA(decal->point, -ysize, up, planepoint);
	nv = ClipPolygonByPlane (array1, nv, up, planepoint, array2);

	VectorMA(decal->point, ysize, up, planepoint);
	nv = ClipPolygonByPlane (array2, nv, up*-1, planepoint, array1);

	if (!nv)
		return FALSE; // shouldn't happen..
	
	Bind2DTexture(GL_TEXTURE0_ARB,  decal->texinfo->gl_texid);
	gl.glBegin(GL_POLYGON);
	for (int k=0; k<nv; k++, v+= VERTEXSIZE)
	{
		float texc_x = (DotProduct(array1[k], right) - texc_orig_x)/xsize;
		float texc_y = (DotProduct(array1[k], up) - texc_orig_y)/ysize;
		texc_x = (texc_x + 1) / 2;
		texc_y = (texc_y + 1) / 2;

		gl.glMultiTexCoord2fARB (GL_TEXTURE0_ARB, texc_x, texc_y);
		gl.glVertex3fv (array1[k]);
	}
	gl.glEnd ();
	return TRUE;
}


static int RcSprayCount( void );		// ReCoF (B302), below
static void RcSprayInit( void );
static void RcDrawSprays( void );
static void RcSpray( vec3_t pos, vec3_t normal, int player );
static void RcSpraysClear( void );

void DrawDecals()
{
	decalrendercounter = 0;
	staticdecalrendercounter = 0;

	CreateCachedDecals();

	if ((!numdecals && !numstaticdecals && !RcSprayCount()) || !cv_customdecals->value)
		return;

	gl.glEnable(GL_BLEND);
	gl.glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	gl.glDepthMask(GL_FALSE);

	gl.glPolygonOffset ( -1, -1 );
	gl.glEnable ( GL_POLYGON_OFFSET_FILL );

	// disable texturing on all units except first
/*	gl.glActiveTextureARB( GL_TEXTURE0_ARB );
	gl.glEnable(GL_TEXTURE_2D);
	gl.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
	gl.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE);
	gl.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
	
	gl.glActiveTextureARB( GL_TEXTURE1_ARB );
	gl.glDisable(GL_TEXTURE_2D);
	gl.glActiveTextureARB( GL_TEXTURE2_ARB );
	gl.glDisable(GL_TEXTURE_2D);
	gl.glActiveTextureARB( GL_TEXTURE3_ARB );
	gl.glDisable(GL_TEXTURE_2D);
	gl.glActiveTextureARB( GL_TEXTURE0_ARB );*/

	SetTexEnvs(ENVSTATE_REPLACE);

	int i;
	for (i = 0; i < numdecals; i++)
	{
		if (DrawSingleDecal(&decals[i]))
			decalrendercounter++;
	}

	for (i = 0; i < numstaticdecals; i++)
	{
		if (DrawSingleDecal(&staticdecals[i]))
			staticdecalrendercounter++;
	}

	RcDrawSprays();	// ReCoF (B302)

	gl.glDisable ( GL_POLYGON_OFFSET_FILL );
}


int MsgCustomDecal(const char *pszName, int iSize, void *pbuf)
{
	BEGIN_READ( pbuf, iSize );

	vec3_t pos, normal;
	pos.x = READ_COORD();
	pos.y = READ_COORD();
	pos.z = READ_COORD();
	normal.x = READ_COORD();
	normal.y = READ_COORD();
	normal.z = READ_COORD();
	int persistent = READ_BYTE();
//	CONPRINT("fgdsfgdsd %d\n", persistent);

	const char *name = READ_STRING();
	if (!strncmp(name, "rcspray", 7))	// ReCoF (B302): a player's spray (CSprayCan::Think)
	{
		RcSpray(pos, normal, atoi(name + 7));
		return 1;
	}

	CreateDecal(pos, normal, name, persistent);
	return 1;
}


void DeleteDecals()
{
	numdecals = 0;
	curdecal = 0;
	numstaticdecals = 0;
	RcSpraysClear();	// ReCoF (B302)
}
/*
void HowManyDecals()
{
	gEngfuncs.Con_Printf("%d decals in memory, %d rendered last time\n", numdecals, decalrendercounter);
}*/

void InitDecals()
{
	gEngfuncs.pfnAddCommand ("pastedecal", PasteViewDecal );
	gEngfuncs.pfnAddCommand ("decalsclear", DeleteDecals );
//	gEngfuncs.pfnAddCommand ("howmanydecals", HowManyDecals );
	cv_customdecals = gEngfuncs.pfnRegisterVariable("gl_customdecals","1",0);
	cv_decalsdebug = gEngfuncs.pfnRegisterVariable("gl_decalstatus","0",0);
	gEngfuncs.pfnHookUserMsg("customdecal", MsgCustomDecal);
	RcSprayInit();	// ReCoF (B302)
}

void DecalsShutdown()
{
	if (pDecalGroupList) delete pDecalGroupList;
}

void DecalsPrintDebugInfo()
{
	if (cv_decalsdebug->value)
	{
		char msg[256];
		sprintf(msg, "%d decals in memory, %d rendered\n", numdecals, decalrendercounter);
		DrawConsoleString( XRES(10), YRES(100), msg );
		sprintf(msg, "%d static decals in memory, %d rendered\n", numstaticdecals, staticdecalrendercounter);
		DrawConsoleString( XRES(10), YRES(115), msg );
	}
}

//-----------------------------------------------------------------------------
// ReCoF (B302): player sprays. The engine paints a spray (TE_PLAYERDECAL) as one of its own decals,
// and this renderer never draws those, so no spray ever showed. CSprayCan::Think also sends a
// customdecal message named rcspray<player>; for the local player the spray is drawn here from the
// tempdecal.wad the engine sent (the first one the file system finds, as CL_CreateResourceList
// reads it). Other players' pictures do not reach the client, so their sprays still do not show.
// The picture is read again at every spray, so a new one from the Spray tab shows on the next spray.
// rc_spray_size is the length of its longer side on the wall, in units; 0 keeps a unit a pixel, as
// in Half-Life. It is drawn over the wall with its see-through parts, lit by the wall's own lightmap
// at that point and by the dynamic lights that reach it, the flashlight among them, so it is as dark
// as the wall around it.
//-----------------------------------------------------------------------------
#define RC_MAX_SPRAYS		128		// pieces: a spray over a corner or a seam is one per surface
#define RC_SPRAY_MAX_SIZE	512		// the largest rc_spray_size, in units

struct rcspray_t
{
	msurface_t	*surface;
	vec3_t		point;
	vec3_t		normal;
	float		xsize, ysize;	// half the width and the height on the wall, in units
	float		light[3];		// the wall's lightmap at the point, 1 = full
};

static cvar_t			*rc_spray_size;
static rcspray_t		s_Sprays[RC_MAX_SPRAYS];
static int				s_iNumSprays;
static int				s_iNextSpray;
static int				s_iSprayTexture;			// gl texture id, taken the first time
static int				s_iSprayWide, s_iSprayTall;	// the picture, 0 until one was read
static unsigned char	*s_pSprayPixels;			// the picture waiting for its texture upload
static unsigned char	*s_pSprayFile;				// the tempdecal.wad the picture came from
static int				s_iSprayFileSize;

extern int current_ext_texture_id;	// gl_texloader.cpp

static int RcSprayCount( void )
{
	return s_iNumSprays;
}

static void RcSprayInit( void )
{
	rc_spray_size = gEngfuncs.pfnRegisterVariable("rc_spray_size", "0", FCVAR_ARCHIVE);
}

static void RcSpraysClear( void )
{
	s_iNumSprays = 0;
	s_iNextSpray = 0;
}

// the wall's own light at point, from the surface's first light map, 1 at 128 (the drawing caps it)
static void RcSpraySurfaceLight( msurface_t *surf, const vec3_t &point, float *light )
{
	mtexinfo_t *tex = surf->texinfo;
	int s, t, smax;
	color24 *lm;

	light[0] = light[1] = light[2] = 1.0f;
	if (!surf->samples || !tex || surf->styles[0] == 255)
		return;

	s = (int)(point.x * tex->vecs[0][0] + point.y * tex->vecs[0][1] + point.z * tex->vecs[0][2] + tex->vecs[0][3]) - surf->texturemins[0];
	t = (int)(point.x * tex->vecs[1][0] + point.y * tex->vecs[1][1] + point.z * tex->vecs[1][2] + tex->vecs[1][3]) - surf->texturemins[1];
	if (s < 0) s = 0;
	if (s > surf->extents[0]) s = surf->extents[0];
	if (t < 0) t = 0;
	if (t > surf->extents[1]) t = surf->extents[1];

	smax = (surf->extents[0] >> 4) + 1;
	lm = surf->samples + (t >> 4) * smax + (s >> 4);

	light[0] = lm->r / 128.0f;
	light[1] = lm->g / 128.0f;
	light[2] = lm->b / 128.0f;
}

// reads tempdecal.wad when it changed since the last spray; the sprays of the old picture go, as
// they share its texture. False when there is no usable picture
static int RcSprayReadPicture( void )
{
	int size = 0;
	unsigned char *file = (unsigned char *)gEngfuncs.COM_LoadFile("tempdecal.wad", 5, &size);

	if (!file)
		return s_iSprayWide != 0;

	if (s_pSprayFile && size == s_iSprayFileSize && !memcmp(file, s_pSprayFile, size))
	{
		gEngfuncs.COM_FreeFile(file);
		return s_iSprayWide != 0;
	}

	// one WAD3 lump: miptex header, four mip levels, a 256 color palette (WAD3 as the engine checks it,
	// Draw_CustomCacheWadInit and Draw_ValidateCustomLogo)
	int ok = 0, wide = 0, tall = 0;
	unsigned char *rgba = NULL;

	if (size >= 12 && !memcmp(file, "WAD3", 4) && *(int *)(file + 4) == 1)
	{
		int info = *(int *)(file + 8);
		int filepos = (info >= 12 && info + 32 <= size) ? *(int *)(file + info) : 0;

		if (filepos >= 12 && filepos + 40 <= size)
		{
			unsigned char *lump = file + filepos;
			wide = *(int *)(lump + 16);
			tall = *(int *)(lump + 20);
			int offset = *(int *)(lump + 24);
			int pixels = wide * tall;
			int mipBytes = pixels + pixels / 4 + pixels / 16 + pixels / 64;

			if (wide > 0 && tall > 0 && wide <= 256 && tall <= 256 && offset >= 40 && filepos + offset + mipBytes + 2 + 768 <= size)
			{
				unsigned char *index = lump + offset;
				unsigned char *pal = index + mipBytes + 2;
				int solid = (pal[765] == 0 && pal[766] == 0 && pal[767] == 255);

				rgba = new unsigned char[pixels * 4];
				for (int i = 0; i < pixels; i++)
				{
					int c = index[i];
					int a, r, g, b;

					if (solid)
					{
						// '{' logo: index 255 is see-through
						a = (c == 255) ? 0 : 255;
						r = pal[c * 3 + 0]; g = pal[c * 3 + 1]; b = pal[c * 3 + 2];
					}
					else
					{
						// one color logo: the last palette color, as solid as the index (GL_Upload8)
						a = c;
						r = pal[765]; g = pal[766]; b = pal[767];
					}

					// premultiplied, so the filtering at the edges of the see-through parts stays clean
					rgba[i * 4 + 0] = (unsigned char)(r * a / 255);
					rgba[i * 4 + 1] = (unsigned char)(g * a / 255);
					rgba[i * 4 + 2] = (unsigned char)(b * a / 255);
					rgba[i * 4 + 3] = (unsigned char)a;
				}
				ok = 1;
			}
		}
	}

	if (!ok)
	{
		gEngfuncs.COM_FreeFile(file);
		return s_iSprayWide != 0;
	}

	delete[] s_pSprayPixels;
	s_pSprayPixels = rgba;	// uploaded by the next draw, which has the GL context
	s_iSprayWide = wide;
	s_iSprayTall = tall;

	delete[] s_pSprayFile;
	s_pSprayFile = new unsigned char[size];
	memcpy(s_pSprayFile, file, size);
	s_iSprayFileSize = size;

	gEngfuncs.COM_FreeFile(file);
	RcSpraysClear();
	return 1;
}

// puts a picture read by RcSprayReadPicture into the spray texture
static void RcSprayUpload( void )
{
	if (!s_pSprayPixels)
		return;

	if (!s_iSprayTexture)
		s_iSprayTexture = current_ext_texture_id++;

	// binding 0 first makes the second call select unit 0 whatever the bind cache held
	Bind2DTexture(GL_TEXTURE0_ARB, 0);
	Bind2DTexture(GL_TEXTURE0_ARB, s_iSprayTexture);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s_iSprayWide, s_iSprayTall, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_pSprayPixels);

	delete[] s_pSprayPixels;
	s_pSprayPixels = NULL;
}

// a spray landed on the world; as CreateDecal, every world surface it covers gets a piece
static void RcSpray( vec3_t pos, vec3_t normal, int player )
{
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	model_t *world = gEngfuncs.GetEntityByIndex(0)->model;

	if (!cv_customdecals->value || !world || !local || local->index != player)
		return;

	if (!RcSprayReadPicture())
		return;

	// the message sends the normal as coordinates, in eighths
	VectorNormalize(normal);
	if (normal.Length() < 0.5f)
		return;

	float scale = 1.0f;
	if (rc_spray_size && rc_spray_size->value > 0)
	{
		float size = rc_spray_size->value;
		if (size > RC_SPRAY_MAX_SIZE)
			size = RC_SPRAY_MAX_SIZE;
		if (size < 4)
			size = 4;
		scale = size / (float)((s_iSprayWide > s_iSprayTall) ? s_iSprayWide : s_iSprayTall);
	}

	float xsize = s_iSprayWide * scale / 2.0f;
	float ysize = s_iSprayTall * scale / 2.0f;
	float radius = sqrt(xsize * xsize + ysize * ysize);

	vec3_t right, up;
	GetUpRight(normal, up, right);

	msurface_t *surfaces = &world->surfaces[world->firstmodelsurface];
	for (int i = 0; i < world->nummodelsurfaces; i++)
	{
		vec3_t norm;
		float ndist = surfaces[i].plane->dist;
		VectorCopy(surfaces[i].plane->normal, norm);
		if (surfaces[i].flags & SURF_PLANEBACK)
		{
			VectorInverse(norm);
			ndist *= -1;
		}

		if (DotProduct(normal, norm) < 0.7)
			continue;

		float dist = DotProduct(norm, pos) - ndist;
		if (dist < 0) dist *= -1;
		if (dist > radius)
			continue;

		if (surfaces[i].flags & (SURF_DRAWSKY|SURF_DRAWTURB))
			continue;

		if (!surfaces[i].samples)
			continue;

		// keep it only if some of the surface is left inside the spray
		glpoly_t *p = surfaces[i].polys;
		float *v = p->verts[0];
		vec3_t array1[64];
		vec3_t array2[64];
		for (int j = 0; j < p->numverts; j++, v += VERTEXSIZE)
			VectorCopy(v, array1[j]);

		int nv;
		vec3_t planepoint;
		VectorMA(pos, -xsize, right, planepoint);
		nv = ClipPolygonByPlane(array1, p->numverts, right, planepoint, array2);
		VectorMA(pos, xsize, right, planepoint);
		nv = ClipPolygonByPlane(array2, nv, right*-1, planepoint, array1);
		VectorMA(pos, -ysize, up, planepoint);
		nv = ClipPolygonByPlane(array1, nv, up, planepoint, array2);
		VectorMA(pos, ysize, up, planepoint);
		nv = ClipPolygonByPlane(array2, nv, up*-1, planepoint, array1);
		if (!nv)
			continue;

		rcspray_t *spray = &s_Sprays[s_iNextSpray];
		s_iNextSpray = (s_iNextSpray + 1) % RC_MAX_SPRAYS;
		if (s_iNumSprays < RC_MAX_SPRAYS)
			s_iNumSprays++;

		spray->surface = &surfaces[i];
		VectorCopy(pos, spray->point);
		VectorCopy(normal, spray->normal);
		spray->xsize = xsize;
		spray->ysize = ysize;
		RcSpraySurfaceLight(&surfaces[i], pos, spray->light);
	}
}

static void RcDrawSprays( void )
{
	if (!s_iNumSprays)
		return;

	RcSprayUpload();
	if (!s_iSprayTexture)
		return;

	gl.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	SetTexEnvs(ENVSTATE_MUL);
	Bind2DTexture(GL_TEXTURE0_ARB, s_iSprayTexture);

	for (int i = 0; i < s_iNumSprays; i++)
	{
		rcspray_t *spray = &s_Sprays[i];

		if (spray->surface->visframe != framecount)
			continue;

		float xsize = spray->xsize;
		float ysize = spray->ysize;

		vec3_t right, up;
		GetUpRight(spray->normal, up, right);
		float texc_orig_x = DotProduct(spray->point, right);
		float texc_orig_y = DotProduct(spray->point, up);

		glpoly_t *p = spray->surface->polys;
		float *v = p->verts[0];
		vec3_t array1[64];
		vec3_t array2[64];
		for (int j = 0; j < p->numverts; j++, v += VERTEXSIZE)
			VectorCopy(v, array1[j]);

		int nv;
		vec3_t planepoint;
		VectorMA(spray->point, -xsize, right, planepoint);
		nv = ClipPolygonByPlane(array1, p->numverts, right, planepoint, array2);
		VectorMA(spray->point, xsize, right, planepoint);
		nv = ClipPolygonByPlane(array2, nv, right*-1, planepoint, array1);
		VectorMA(spray->point, -ysize, up, planepoint);
		nv = ClipPolygonByPlane(array1, nv, up, planepoint, array2);
		VectorMA(spray->point, ysize, up, planepoint);
		nv = ClipPolygonByPlane(array2, nv, up*-1, planepoint, array1);
		if (!nv)
			continue;

		// the wall's light plus the dynamic lights that reach the spray, as the models take them
		float light[3] = { spray->light[0], spray->light[1], spray->light[2] };
		DynamicLight *dlights[32];	// MAX_DLIGHTS of gl_light_dynamic.cpp
		vec3_t lightpoint = spray->point + spray->normal * 2;
		int numdlights = GetDlightsForPoint(lightpoint, Vector(-2, -2, -2), Vector(2, 2, 2), dlights, 0);
		int k;
		for (k = 0; k < numdlights; k++)
		{
			DynamicLight *dl = dlights[k];
			float frac = 1.0f - (lightpoint - dl->origin).Length() / dl->radius;
			if (frac <= 0)
				continue;

			if (dl->spot_texture[0])
			{
				// spot lights (the flashlight) carry their color in a texture: white
				light[0] += frac; light[1] += frac; light[2] += frac;
			}
			else
			{
				light[0] += frac * dl->color[0]; light[1] += frac * dl->color[1]; light[2] += frac * dl->color[2];
			}
		}
		for (k = 0; k < 3; k++)
		{
			if (light[k] > 1.0f)
				light[k] = 1.0f;
		}
		gl.glColor4f(light[0], light[1], light[2], 1.0f);

		gl.glBegin(GL_POLYGON);
		for (k = 0; k < nv; k++)
		{
			// CreateDecal's right points to the left of someone facing the wall: mirror the picture
			float texc_x = (DotProduct(array1[k], right) - texc_orig_x) / xsize;
			float texc_y = (DotProduct(array1[k], up) - texc_orig_y) / ysize;
			texc_x = 1.0f - (texc_x + 1) / 2;
			texc_y = (texc_y + 1) / 2;

			gl.glMultiTexCoord2fARB(GL_TEXTURE0_ARB, texc_x, texc_y);
			gl.glVertex3fv(array1[k]);
		}
		gl.glEnd();
	}

	gl.glColor4f(1, 1, 1, 1);
	gl.glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
}
