

struct DynamicLight
{
	vec3_t	origin;
	float	radius;
	vec3_t	color; // ignored for spotlights, they have a texture
	float	die;				// stop lighting after this time
	float	decay;				// drop this each second
	int		key;

	// spotlight specific:
	vec3_t	angles;	
	float	cone_hor;
	float	cone_ver;
	char	spot_texture[64];
};


DynamicLight* MY_AllocDlight (int key);

// ReCoF (B198): the key of the night vision light, so it keeps one slot (no entity index reaches it)
#define RC_DLIGHT_NIGHTVISION	-4000

int GetDlightsForPoint(vec3_t point, vec3_t mins, vec3_t maxs, DynamicLight **pOutArray, int alwaysFlashlight);