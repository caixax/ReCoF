// ======================================
// Cry of Fear screen noise effect
// (class name from the RTTI of client.dll, file name guessed)
// ======================================

#include "hud.h"
#include "cl_util.h"

extern cvar_t *cl_noiseeffect;
extern cvar_t *cl_noiseamount;

int CNeffect::Init( void )
{
	m_iFlags |= HUD_ACTIVE;
	gHUD.AddHudElem( this );

	return 1;
}

int CNeffect::VidInit( void )
{
	m_HUD_snoise_guess = gHUD.GetSpriteIndex( "snoise" );
	m_HUD_arghnoise_guess = gHUD.GetSpriteIndex( "arghnoise" );
	m_HUD_tv_scanlines_guess = gHUD.GetSpriteIndex( "tv_scanlines" );

	return 1;
}

int CNeffect::Draw( float flTime )
{
	int x, y;
	int iWidth, iHeight;
	int a;

	if ( !cl_noiseeffect->value )
		return 1;

	iWidth = gHUD.GetSpriteRect( m_HUD_snoise_guess ).right - gHUD.GetSpriteRect( m_HUD_snoise_guess ).left;
	iHeight = gHUD.GetSpriteRect( m_HUD_snoise_guess ).bottom - gHUD.GetSpriteRect( m_HUD_snoise_guess ).top;

	// tiles the noise sprite over the screen (x steps by the height, y by the width)
	for ( y = 0; y < ScreenHeight; y += iWidth )
	{
		for ( x = 0; x < ScreenWidth; x += iHeight )
		{
			a = cl_noiseamount->value * 255;
			SPR_Set( gHUD.GetSprite( m_HUD_snoise_guess ), a, a, a );
			SPR_DrawAdditive( gEngfuncs.pfnRandomLong( 1, 19 ), x, y, &gHUD.GetSpriteRect( m_HUD_snoise_guess ) );
		}
	}

	return 1;
}
