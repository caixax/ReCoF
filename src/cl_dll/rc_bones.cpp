// ReCoF bone setup on worker threads (B339)
//
// rc_threads N (client) sets up the bones of the frame's studio models on N worker threads and the main thread
// together, just before the first model of a picture is drawn: once a game frame, and again for each picture
// smooth frames draws in between (rc_smooth.cpp), which moves the clock the models are animated by. -1, the default, picks N from the logical
// processors the game may run on (RcBonesAuto): none below 8, as on four cores the workers took the time the
// graphics driver's own thread needed and the frame got slower; from 8, a quarter of them, at most 3. 0 is off,
// the game as before. Each model then takes its
// bones from that work when, and only when, everything they are made from is the same as when the work began:
// the entity's state and its interpolation state, its origin and angles, the model, the time, the
// interpolation setting and the rotation matrix the main thread made for it. Any model whose inputs changed, and
// any the work left out, is set up as before on the main thread. The workers run the main thread's own function
// (CStudioModelRenderer::StudioSetupBonesNow) on renderers of their own, from copies of the entities, with the
// main thread's floating point settings, so the bones are the same bytes. rc_threads_verify 1 sets every model
// that took the work's bones up again on the main thread and counts the differences (rc_threads_debug).
//
// Left out of the work: models whose animations are in another file (a seqgroup above 0, which loads through
// the engine's cache), effects that draw random numbers (kRenderFxDistort, kRenderFxHologram), dead players,
// models that follow another one (they take its bones), the view model, players, and models well behind the
// view. While the work runs the main thread only does its share of it, so nothing loads into the engine's cache
// while the workers read models from it.

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "studio.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "dlight.h"
#include "triangleapi.h"

#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "studio_util.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"
#include "rc_bones.h"
#include "rc_prof.h"

#include <windows.h>
#include <intrin.h>
#include <xmmintrin.h>

extern engine_studio_api_t IEngineStudio;
extern CGameStudioModelRenderer g_StudioRenderer;

#define RCB_MAX_JOBS		1024
#define RCB_MAX_WORKERS		7
#define RCB_HASH			2048	// slots of the table that finds an entity's job, a power of two
#define RCB_CONE_COS		0.342f	// cos 70 degrees: models outside this cone round the view, by more than
#define RCB_CONE_RADIUS		256.0f	// this many units, are left to the main thread

// what the bones are made from besides the entity's state, kept from the start of the work and compared when the
// model is drawn; the state is compared with the worker's copy (ent.curstate), which the work does not change
struct RcBoneKey_t
{
	latchedvars_t	latched;
	vec3_t			origin;
	vec3_t			angles;
	mouth_t			mouth;
	model_t			*model;
};

struct RcBoneJob_t
{
	cl_entity_t		*pEnt;				// the entity the engine draws
	RcBoneKey_t		key;
	cl_entity_t		ent;				// the worker's copy of what bone setup reads of it (RcBonesJobFor)
	studiohdr_t		*pHdr;
	int				numbones;
	int				iPrevframeSet;		// the work stored its frame in latched.prevframe
	float			flPrevframe;
	float			rot[3][4];
	float			alias[3][4];
	float			bone[MAXSTUDIOBONES][3][4];
	float			light[MAXSTUDIOBONES][3][4];
};

struct RcBoneStats_t
{
	int		iFrames;		// game frames whose bones were set up by the work
	int		iJobs;
	int		iReplays;		// models that took the work's bones
	int		iNoJob;			// models set up on the main thread: not in the work
	int		iChanged;		// their inputs changed since the work began
	int		iRotation;		// their rotation matrix is another one
	int		iVerified;
	int		iMismatches;
	double	flBatchMs;		// the main thread's time in the work
	double	flWorstMs;
};

__declspec( thread ) int g_iRcBonesPrevframeSet;

static cvar_t		*rc_threads;
static cvar_t		*rc_threads_verify;

static int			s_iOn;				// the workers rc_threads asks for, as read at the last game frame
static int			s_iAuto = -1;		// the workers -1 picks, worked out again every second
static int			s_iVerify;
static int			s_iFrame;			// game frames counted at HUD_Frame
static int			s_iLaunched;		// the game frame the work last ran for (with s_clTime, the picture)

static cl_entity_t	*s_pGather[RCB_MAX_JOBS];
static int			s_iGather;

static RcBoneJob_t	*s_pJobs[RCB_MAX_JOBS];
static int			s_iJobs;			// jobs of the last work, which the models of the frame take from
static RcBoneJob_t	*s_pHash[RCB_HASH];

static double		s_clTime, s_clOldTime;
static int			s_iDoInterp;

// the pool
static int							s_iWorkers;
static HANDLE						s_hThread[RCB_MAX_WORKERS];
static HANDLE						s_hWake[RCB_MAX_WORKERS];
static CGameStudioModelRenderer		*s_pRenderer[RCB_MAX_WORKERS + 1];	// the last one is the main thread's
static volatile LONG				s_lBusy[RCB_MAX_WORKERS];
static volatile LONG				s_lNext, s_lCount, s_lDone;
static volatile LONG				s_lQuit;
static unsigned int					s_uCsr;		// the main thread's SSE and x87 settings, for the workers
static unsigned short				s_usCw;

static RcBoneStats_t	s_Stats, s_LastStats;
static double			s_flStatsStart;
static double			s_flTickMs;

static double RcBonesNowMs( void )
{
	LARGE_INTEGER c;
	QueryPerformanceCounter( &c );
	return (double)c.QuadPart * s_flTickMs;
}

static unsigned int RcBonesHashOf( cl_entity_t *ent )
{
	return ( (unsigned int)(UINT_PTR)ent * 2654435761u ) >> 21;	// the top 11 bits: 0 to RCB_HASH - 1
}

static RcBoneJob_t *RcBonesFind( cl_entity_t *ent )
{
	unsigned int h = RcBonesHashOf( ent );

	for ( int i = 0; i < RCB_HASH; i++ )
	{
		RcBoneJob_t *j = s_pHash[( h + i ) & ( RCB_HASH - 1 )];

		if ( !j )
			return NULL;
		if ( j->pEnt == ent )
			return j;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// one model's bones, on any thread, with that thread's own renderer
//-----------------------------------------------------------------------------
static void RcBonesRun( CGameStudioModelRenderer *r, RcBoneJob_t *j )
{
	r->m_pCurrentEntity = &j->ent;
	r->m_pRenderModel = j->key.model;
	r->m_pStudioHeader = j->pHdr;
	r->m_clTime = s_clTime;
	r->m_clOldTime = s_clOldTime;
	r->m_fDoInterp = s_iDoInterp;
	r->m_pPlayerInfo = NULL;
	r->m_protationmatrix = &j->rot;
	r->m_paliastransform = &j->alias;
	r->m_pbonetransform = &j->bone;
	r->m_plighttransform = &j->light;

	r->StudioSetUpTransform( 0 );
	r->StudioSetupBones();	// not the main renderer: StudioSetupBonesNow

	j->iPrevframeSet = g_iRcBonesPrevframeSet;
	j->flPrevframe = j->ent.latched.prevframe;
}

static void RcBonesWork( CGameStudioModelRenderer *r )
{
	for ( ;; )
	{
		LONG k = InterlockedIncrement( &s_lNext ) - 1;

		if ( k >= s_lCount )
			break;

		RcBonesRun( r, s_pJobs[k] );
		InterlockedIncrement( &s_lDone );
	}
}

static DWORD WINAPI RcBonesWorker( LPVOID pParam )
{
	int w = (int)(INT_PTR)pParam;

	for ( ;; )
	{
		WaitForSingleObject( s_hWake[w], INFINITE );

		if ( s_lQuit )
			break;

		// the main thread's floating point settings, so the results are the same bytes
		_mm_setcsr( s_uCsr );
		unsigned short cw = s_usCw;
		__asm fldcw cw

		RcBonesWork( s_pRenderer[w] );
		InterlockedExchange( &s_lBusy[w], 0 );
	}

	return 0;
}

static void RcBonesStop( void )
{
	if ( !s_iWorkers )
		return;

	InterlockedExchange( &s_lQuit, 1 );
	for ( int w = 0; w < s_iWorkers; w++ )
		SetEvent( s_hWake[w] );
	WaitForMultipleObjects( s_iWorkers, s_hThread, TRUE, 5000 );

	for ( int w = 0; w < s_iWorkers; w++ )
	{
		CloseHandle( s_hThread[w] );
		CloseHandle( s_hWake[w] );
		s_hThread[w] = s_hWake[w] = NULL;
	}
	for ( int w = 0; w <= RCB_MAX_WORKERS; w++ )
	{
		delete s_pRenderer[w];
		s_pRenderer[w] = NULL;
	}

	s_iWorkers = 0;
	InterlockedExchange( &s_lQuit, 0 );
}

static int RcBonesStart( int n )
{
	if ( n > RCB_MAX_WORKERS )
		n = RCB_MAX_WORKERS;

	s_pRenderer[RCB_MAX_WORKERS] = new CGameStudioModelRenderer;

	for ( int w = 0; w < n; w++ )
	{
		s_pRenderer[w] = new CGameStudioModelRenderer;
		s_hWake[w] = CreateEventA( NULL, FALSE, FALSE, NULL );
		s_lBusy[w] = 0;
		s_hThread[w] = CreateThread( NULL, 256 * 1024, RcBonesWorker, (LPVOID)(INT_PTR)w, 0, NULL );

		// below the main thread and the driver's threads: a worker that is not running when the work starts
		// leaves its share to the others, the main thread among them, rather than slow the driver down
		if ( s_hThread[w] )
			SetThreadPriority( s_hThread[w], THREAD_PRIORITY_BELOW_NORMAL );

		if ( !s_hWake[w] || !s_hThread[w] )
		{
			s_iWorkers = w + ( s_hThread[w] ? 1 : 0 );
			RcBonesStop();
			gEngfuncs.Con_Printf( "rc_threads: could not start the worker threads\n" );
			return 0;
		}
	}

	s_iWorkers = n;
	return 1;
}

// rc_threads -1: none below 8 logical processors in the game's affinity, from 8 a quarter of them, at most 3
static int RcBonesAuto( void )
{
	DWORD_PTR process = 0, system = 0;
	int n = 0;

	if ( !GetProcessAffinityMask( GetCurrentProcess(), &process, &system ) )
		return 0;

	for ( ; process; process &= process - 1 )
		n++;

	if ( n < 8 )
		return 0;

	return ( n / 4 < 3 ) ? n / 4 : 3;
}

void RcBones_NewFrame( void )
{
	s_iFrame++;
	s_iGather = 0;
	s_iVerify = rc_threads_verify ? (int)rc_threads_verify->value : 0;

	// the second's numbers for rc_threads_debug, and the workers -1 picks
	double now = RcBonesNowMs();
	if ( now - s_flStatsStart >= 1000.0 || s_iAuto < 0 )
	{
		s_LastStats = s_Stats;
		memset( &s_Stats, 0, sizeof( s_Stats ) );
		s_flStatsStart = now;
		s_iAuto = RcBonesAuto();
	}

	s_iOn = rc_threads ? (int)rc_threads->value : 0;
	if ( s_iOn < 0 )
		s_iOn = s_iAuto;
	if ( s_iOn > RCB_MAX_WORKERS )
		s_iOn = RCB_MAX_WORKERS;
}

void RcBones_Add( cl_entity_t *ent )
{
	if ( !s_iOn || s_iGather >= RCB_MAX_JOBS || !ent->model || ent->model->type != mod_studio )
		return;

	s_pGather[s_iGather++] = ent;
}

// a job for the entity, or NULL when the main thread must set it up itself
static RcBoneJob_t *RcBonesJobFor( cl_entity_t *e, cl_entity_t *view, const vec3_t org, const vec3_t vpn )
{
	if ( e == view || e->player || !e->model || e->model->type != mod_studio )
		return NULL;

	if ( e->curstate.movetype == MOVETYPE_FOLLOW || e->curstate.renderfx == kRenderFxDistort ||
		 e->curstate.renderfx == kRenderFxHologram || e->curstate.renderfx == kRenderFxDeadPlayer )
		return NULL;

	vec3_t d;
	VectorSubtract( e->origin, org, d );
	float len = sqrt( DotProduct( d, d ) );
	if ( DotProduct( d, vpn ) < len * RCB_CONE_COS - RCB_CONE_RADIUS * ( e->curstate.scale > 1 ? e->curstate.scale : 1 ) )
		return NULL;

	studiohdr_t *hdr = (studiohdr_t *)IEngineStudio.Mod_Extradata( e->model );
	if ( !hdr || hdr->numbodyparts == 0 || hdr->numbones > MAXSTUDIOBONES )
		return NULL;

	// animations in another file go through the engine's cache, which only the main thread may touch
	if ( e->curstate.sequence < 0 || e->curstate.sequence >= hdr->numseq )
		return NULL;
	mstudioseqdesc_t *seq = (mstudioseqdesc_t *)( (byte *)hdr + hdr->seqindex );
	if ( seq[e->curstate.sequence].seqgroup != 0 )
		return NULL;
	if ( e->latched.prevsequence >= 0 && e->latched.prevsequence < hdr->numseq && seq[e->latched.prevsequence].seqgroup != 0 )
		return NULL;

	if ( s_iJobs >= RCB_MAX_JOBS )
		return NULL;

	RcBoneJob_t *j = s_pJobs[s_iJobs];
	if ( !j )
	{
		j = s_pJobs[s_iJobs] = (RcBoneJob_t *)malloc( sizeof( RcBoneJob_t ) );
		if ( !j )
			return NULL;
	}

	j->pEnt = e;
	j->pHdr = hdr;
	j->numbones = hdr->numbones;
	memcpy( &j->key.latched, &e->latched, sizeof( j->key.latched ) );
	VectorCopy( e->origin, j->key.origin );
	VectorCopy( e->angles, j->key.angles );
	memcpy( &j->key.mouth, &e->mouth, sizeof( j->key.mouth ) );
	j->key.model = e->model;

	// what StudioSetUpTransform and StudioSetupBonesNow read of the entity; the rest of the copy is left from
	// earlier work and not read (rc_threads_verify would show it if it were)
	memcpy( &j->ent.curstate, &e->curstate, sizeof( j->ent.curstate ) );
	memcpy( &j->ent.latched, &e->latched, sizeof( j->ent.latched ) );
	VectorCopy( e->origin, j->ent.origin );
	VectorCopy( e->angles, j->ent.angles );
	memcpy( &j->ent.mouth, &e->mouth, sizeof( j->ent.mouth ) );
	j->ent.model = e->model;
	j->ent.index = e->index;
	j->ent.player = e->player;

	s_iJobs++;
	return j;
}

//-----------------------------------------------------------------------------
// the work, once a game frame, before the first model is drawn
//-----------------------------------------------------------------------------
void RcBones_Launch( void )
{
	int framecount;
	double clTime, clOldTime;
	IEngineStudio.GetTimes( &framecount, &clTime, &clOldTime );

	// once a picture: a game frame, or a picture smooth frames draws in between at a later time
	if ( s_iLaunched == s_iFrame && clTime == s_clTime && clOldTime == s_clOldTime )
		return;

	s_iLaunched = s_iFrame;
	s_clTime = clTime;
	s_clOldTime = clOldTime;
	s_iJobs = 0;
	memset( s_pHash, 0, sizeof( s_pHash ) );

	if ( !s_iOn )
	{
		if ( s_iWorkers )
			RcBonesStop();
		return;
	}

	if ( !s_iGather )
		return;

	RcProfScope prof( RCP_BONEJOBS );
	double start = RcBonesNowMs();

	if ( s_iWorkers != s_iOn )
	{
		RcBonesStop();
		if ( !RcBonesStart( s_iOn ) )
			return;
	}

	s_iDoInterp = g_StudioRenderer.m_fDoInterp;

	vec3_t org, up, right, vpn;
	IEngineStudio.GetViewInfo( org, up, right, vpn );
	cl_entity_t *view = IEngineStudio.GetViewEntity();

	for ( int i = 0; i < s_iGather; i++ )
	{
		RcBoneJob_t *j = RcBonesJobFor( s_pGather[i], view, org, vpn );

		if ( !j )
			continue;

		unsigned int h = RcBonesHashOf( j->pEnt );
		while ( s_pHash[h] )
			h = ( h + 1 ) & ( RCB_HASH - 1 );
		s_pHash[h] = j;
	}

	if ( s_iJobs )
	{
		s_uCsr = _mm_getcsr();
		__asm fnstcw s_usCw

		s_lNext = 0;
		s_lDone = 0;
		s_lCount = s_iJobs;

		for ( int w = 0; w < s_iWorkers; w++ )
		{
			InterlockedExchange( &s_lBusy[w], 1 );
			SetEvent( s_hWake[w] );
		}

		RcBonesWork( s_pRenderer[RCB_MAX_WORKERS] );

		// the workers finish what they took; then none of them is still looking for work
		for ( ;; )
		{
			int busy = ( s_lDone < s_lCount );
			for ( int w = 0; !busy && w < s_iWorkers; w++ )
				busy = s_lBusy[w];
			if ( !busy )
				break;
			_mm_pause();
		}
	}

	double ms = RcBonesNowMs() - start;
	s_Stats.iFrames++;
	s_Stats.iJobs += s_iJobs;
	s_Stats.flBatchMs += ms;
	if ( ms > s_Stats.flWorstMs )
		s_Stats.flWorstMs = ms;
}

// rc_threads_verify 2: the first input that changed, once a second
static double s_flWhyTime;

static int RcFirstDiff( const void *a, const void *b, int size )
{
	for ( int i = 0; i < size; i++ )
	{
		if ( ( (const unsigned char *)a )[i] != ( (const unsigned char *)b )[i] )
			return i;
	}
	return -1;
}

static void RcBonesWhy( CStudioModelRenderer *r, cl_entity_t *e, RcBoneJob_t *j )
{
	if ( s_iVerify < 2 || RcBonesNowMs() - s_flWhyTime < 1000.0 )
		return;

	s_flWhyTime = RcBonesNowMs();
	gEngfuncs.Con_Printf( "rc_threads_verify: entity %d %s: hdr %d model %d time %d old %d interp %d player %d curstate @%d "
		"origin @%d angles @%d mouth @%d\n", e->index, e->model ? e->model->name : "?", r->m_pStudioHeader != j->pHdr,
		r->m_pRenderModel != j->key.model, r->m_clTime != s_clTime, r->m_clOldTime != s_clOldTime,
		r->m_fDoInterp != s_iDoInterp, r->m_pPlayerInfo != NULL,
		RcFirstDiff( &e->curstate, &j->ent.curstate, sizeof( j->ent.curstate ) ),
		RcFirstDiff( e->origin, j->key.origin, sizeof( j->key.origin ) ),
		RcFirstDiff( e->angles, j->key.angles, sizeof( j->key.angles ) ),
		RcFirstDiff( &e->mouth, &j->key.mouth, sizeof( j->key.mouth ) ) );
	gEngfuncs.Con_Printf( "rc_threads_verify: time %f / %f, old %f / %f\n", r->m_clTime, s_clTime, r->m_clOldTime, s_clOldTime );
}

//-----------------------------------------------------------------------------
// StudioSetupBones of the main renderer: the work's bones, when the model's inputs are the work's
//-----------------------------------------------------------------------------
int RcBones_Replay( CStudioModelRenderer *r )
{
	if ( !s_iJobs )
		return 0;

	cl_entity_t *e = r->m_pCurrentEntity;
	RcBoneJob_t *j = RcBonesFind( e );

	if ( !j )
	{
		s_Stats.iNoJob++;
		return 0;
	}

	if ( r->m_pStudioHeader != j->pHdr || r->m_pRenderModel != j->key.model || r->m_clTime != s_clTime ||
		 r->m_clOldTime != s_clOldTime || r->m_fDoInterp != s_iDoInterp || r->m_pPlayerInfo ||
		 memcmp( &e->curstate, &j->ent.curstate, sizeof( j->ent.curstate ) ) ||
		 memcmp( e->origin, j->key.origin, sizeof( j->key.origin ) ) ||
		 memcmp( e->angles, j->key.angles, sizeof( j->key.angles ) ) ||
		 memcmp( &e->mouth, &j->key.mouth, sizeof( j->key.mouth ) ) )
	{
		s_Stats.iChanged++;
		RcBonesWhy( r, e, j );
		return 0;
	}

	// latched.prevframe is an input only when the work did not store the frame there; when it did, a model drawn
	// twice in a frame finds the frame of its first drawing there
	latchedvars_t latched;
	memcpy( &latched, &e->latched, sizeof( latched ) );	// padding included, which memcmp compares
	if ( j->iPrevframeSet )
		latched.prevframe = j->key.latched.prevframe;
	if ( memcmp( &latched, &j->key.latched, sizeof( latched ) ) )
	{
		s_Stats.iChanged++;
		if ( s_iVerify >= 2 && RcBonesNowMs() - s_flWhyTime >= 1000.0 )
		{
			s_flWhyTime = RcBonesNowMs();
			int off = RcFirstDiff( &latched, &j->key.latched, sizeof( latched ) );
			gEngfuncs.Con_Printf( "rc_threads_verify: entity %d latched @%d of %d (prevframe @%d): %08x / %08x, set %d\n", e->index, off,
				(int)sizeof( latched ), (int)( (char *)&latched.prevframe - (char *)&latched ),
				*(unsigned int *)( (char *)&latched + ( off & ~3 ) ), *(unsigned int *)( (char *)&j->key.latched + ( off & ~3 ) ),
				j->iPrevframeSet );
		}
		return 0;
	}

	if ( memcmp( *r->m_protationmatrix, j->rot, sizeof( j->rot ) ) )
	{
		s_Stats.iRotation++;
		return 0;
	}

	size_t size = j->numbones * sizeof( j->bone[0] );

	if ( s_iVerify )
	{
		// the main thread's own bones, which stay; the work's are only compared with them
		r->StudioSetupBonesNow();

		int same = !memcmp( *r->m_pbonetransform, j->bone, size ) && !memcmp( *r->m_plighttransform, j->light, size ) &&
			g_iRcBonesPrevframeSet == j->iPrevframeSet && ( !j->iPrevframeSet || !memcmp( &e->latched.prevframe, &j->flPrevframe, sizeof( float ) ) );

		s_Stats.iVerified++;
		if ( !same )
		{
			if ( !s_Stats.iMismatches )
				gEngfuncs.Con_Printf( "rc_threads_verify: entity %d (%s) set up differently by the work\n", e->index,
					e->model ? e->model->name : "?" );
			s_Stats.iMismatches++;
		}

		return 1;
	}

	memcpy( *r->m_pbonetransform, j->bone, size );
	memcpy( *r->m_plighttransform, j->light, size );
	if ( j->iPrevframeSet )
		e->latched.prevframe = j->flPrevframe;

	s_Stats.iReplays++;
	return 1;
}

static void RcBones_Debug( void )
{
	RcBoneStats_t s = s_LastStats;

	gEngfuncs.Con_Printf( "rc_threads %d: %d workers asked for (-1 picks %d), %d running, rc_threads_verify %d\n",
		rc_threads ? (int)rc_threads->value : 0, s_iOn, s_iAuto, s_iWorkers, s_iVerify );
	gEngfuncs.Con_Printf( "last second: %d frames with the work, %d models in it, the work %.3f ms a frame (worst %.3f)\n",
		s.iFrames, s.iJobs, s.iFrames ? s.flBatchMs / s.iFrames : 0.0, s.flWorstMs );
	gEngfuncs.Con_Printf( "models drawn with the work's bones %d; set up on the main thread: %d not in the work, %d with "
		"changed inputs, %d with another rotation\n", s.iReplays, s.iNoJob, s.iChanged, s.iRotation );
	if ( s_iVerify )
		gEngfuncs.Con_Printf( "verified %d, different %d\n", s.iVerified, s.iMismatches );
}

void RcBones_Shutdown( void )
{
	RcBonesStop();
	s_iJobs = 0;
}

void RcBones_Init( void )
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	s_flTickMs = 1000.0 / (double)freq.QuadPart;

	rc_threads = gEngfuncs.pfnRegisterVariable( "rc_threads", "-1", FCVAR_ARCHIVE );
	rc_threads_verify = gEngfuncs.pfnRegisterVariable( "rc_threads_verify", "0", 0 );
	gEngfuncs.pfnAddCommand( "rc_threads_debug", RcBones_Debug );
}
