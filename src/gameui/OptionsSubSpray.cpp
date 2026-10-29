// ReCoF: the Spray page of the options dialog.
//
// Cry of Fear kept Half-Life's spray: impulse 201 paints the logo in tempdecal.wad, which the engine
// sends to the server whenever a map or a save loads (CL_CreateResourceList). It left out the page
// that makes the logo. This one makes a full color logo, with see-through parts, from a PNG, JPG, BMP,
// GIF or TIFF picture in the logos folder, at the size the player picks, and writes tempdecal.wad on
// Apply. GDI+ reads and scales the picture; it is loaded at run time, as the VC6 SDK this DLL builds
// with has no GDI+ headers.

#include <windows.h>
#undef PostMessage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OptionsSubSpray.h"
#include "EngineInterface.h"

#include <FileSystem.h>
#include <vgui/ISurface.h>
#include <vgui/KeyValues.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Label.h>

using namespace vgui;

#define SPRAY_TRANSPARENT	255		// the palette index of the see-through pixels; its color is pure blue

// the longer side of the logo, in pixels, which is also its size in world units; the engine refuses a
// logo wider or taller than 256 (Draw_ValidateCustomLogo), and Half-Life's logos are 64
static const int s_SpraySizes[] = { 64, 96, 128, 160, 192, 256 };
static int s_iSpraySize = 128;		// the size picked last, for the next picture

//-----------------------------------------------------------------------------
// GDI+ flat API
//-----------------------------------------------------------------------------
struct GdipStartupInput_t
{
	unsigned int	GdiplusVersion;
	void			*DebugEventCallback;
	BOOL			SuppressBackgroundThread;
	BOOL			SuppressExternalCodecs;
};

#define GDIP_PIXELFORMAT_32BPP_ARGB		0x0026200A	// B, G, R, A in memory
#define GDIP_INTERPOLATION_HQ_BICUBIC	7
#define GDIP_PIXELOFFSET_HALF			4
#define GDIP_WRAP_TILE_FLIP_XY			3			// the edges are not faded into nothing
#define GDIP_UNIT_PIXEL					2

typedef int ( WINAPI *GdipStartup_t )( unsigned long *token, const GdipStartupInput_t *input, void *output );
typedef void ( WINAPI *GdipShutdown_t )( unsigned long token );
typedef int ( WINAPI *GdipCreateBitmapFromFile_t )( const wchar_t *filename, void **bitmap );
typedef int ( WINAPI *GdipCreateBitmapFromScan0_t )( int width, int height, int stride, int format, unsigned char *scan0, void **bitmap );
typedef int ( WINAPI *GdipGetImageSide_t )( void *image, unsigned int *side );
typedef int ( WINAPI *GdipGetImageGraphicsContext_t )( void *image, void **graphics );
typedef int ( WINAPI *GdipSetGraphicsMode_t )( void *graphics, int mode );
typedef int ( WINAPI *GdipCreateImageAttributes_t )( void **attributes );
typedef int ( WINAPI *GdipSetImageAttributesWrapMode_t )( void *attributes, int wrap, unsigned int argb, BOOL clamp );
typedef int ( WINAPI *GdipDrawImageRectRectI_t )( void *graphics, void *image, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int unit, void *attributes, void *callback, void *callbackData );
typedef int ( WINAPI *GdipDelete_t )( void *object );

//-----------------------------------------------------------------------------
// Purpose: reads a picture and draws it, scaled to maxSide on its longer side and centered on a canvas
//			whose sides are multiples of 16, which a mipmapped texture needs. *pBGRA gets B, G, R, A per
//			pixel, see-through outside the picture; the caller frees it. On failure *pError says why
//-----------------------------------------------------------------------------
static bool LoadSprayPicture( const char *pszPath, int maxSide, unsigned char **pBGRA, int *pWide, int *pTall, const char **pError )
{
	HMODULE hGdip;
	GdipStartup_t pStartup;
	GdipShutdown_t pShutdown;
	GdipCreateBitmapFromFile_t pCreateBitmapFromFile;
	GdipCreateBitmapFromScan0_t pCreateBitmapFromScan0;
	GdipGetImageSide_t pGetImageWidth, pGetImageHeight;
	GdipGetImageGraphicsContext_t pGetImageGraphicsContext;
	GdipSetGraphicsMode_t pSetInterpolationMode, pSetPixelOffsetMode;
	GdipCreateImageAttributes_t pCreateImageAttributes;
	GdipSetImageAttributesWrapMode_t pSetImageAttributesWrapMode;
	GdipDrawImageRectRectI_t pDrawImageRectRectI;
	GdipDelete_t pDeleteGraphics, pDisposeImage, pDisposeImageAttributes;
	GdipStartupInput_t input;
	unsigned long token;
	wchar_t wszPath[MAX_PATH];
	void *source = NULL, *canvas = NULL, *graphics = NULL, *attributes = NULL;
	unsigned int srcWide = 0, srcTall = 0;
	int drawWide, drawTall, wide, tall;
	unsigned char *bgra;
	bool ok = false;

	*pBGRA = NULL;
	*pError = "Windows cannot read pictures here (no GDI+)";

	hGdip = LoadLibraryA( "gdiplus.dll" );
	if ( !hGdip )
		return false;

	pStartup = (GdipStartup_t)GetProcAddress( hGdip, "GdiplusStartup" );
	pShutdown = (GdipShutdown_t)GetProcAddress( hGdip, "GdiplusShutdown" );
	pCreateBitmapFromFile = (GdipCreateBitmapFromFile_t)GetProcAddress( hGdip, "GdipCreateBitmapFromFile" );
	pCreateBitmapFromScan0 = (GdipCreateBitmapFromScan0_t)GetProcAddress( hGdip, "GdipCreateBitmapFromScan0" );
	pGetImageWidth = (GdipGetImageSide_t)GetProcAddress( hGdip, "GdipGetImageWidth" );
	pGetImageHeight = (GdipGetImageSide_t)GetProcAddress( hGdip, "GdipGetImageHeight" );
	pGetImageGraphicsContext = (GdipGetImageGraphicsContext_t)GetProcAddress( hGdip, "GdipGetImageGraphicsContext" );
	pSetInterpolationMode = (GdipSetGraphicsMode_t)GetProcAddress( hGdip, "GdipSetInterpolationMode" );
	pSetPixelOffsetMode = (GdipSetGraphicsMode_t)GetProcAddress( hGdip, "GdipSetPixelOffsetMode" );
	pCreateImageAttributes = (GdipCreateImageAttributes_t)GetProcAddress( hGdip, "GdipCreateImageAttributes" );
	pSetImageAttributesWrapMode = (GdipSetImageAttributesWrapMode_t)GetProcAddress( hGdip, "GdipSetImageAttributesWrapMode" );
	pDrawImageRectRectI = (GdipDrawImageRectRectI_t)GetProcAddress( hGdip, "GdipDrawImageRectRectI" );
	pDeleteGraphics = (GdipDelete_t)GetProcAddress( hGdip, "GdipDeleteGraphics" );
	pDisposeImage = (GdipDelete_t)GetProcAddress( hGdip, "GdipDisposeImage" );
	pDisposeImageAttributes = (GdipDelete_t)GetProcAddress( hGdip, "GdipDisposeImageAttributes" );

	if ( !pStartup || !pShutdown || !pCreateBitmapFromFile || !pCreateBitmapFromScan0 || !pGetImageWidth ||
		 !pGetImageHeight || !pGetImageGraphicsContext || !pSetInterpolationMode || !pSetPixelOffsetMode ||
		 !pCreateImageAttributes || !pSetImageAttributesWrapMode || !pDrawImageRectRectI || !pDeleteGraphics ||
		 !pDisposeImage || !pDisposeImageAttributes )
	{
		FreeLibrary( hGdip );
		return false;
	}

	memset( &input, 0, sizeof( input ) );
	input.GdiplusVersion = 1;
	if ( pStartup( &token, &input, NULL ) != 0 )
	{
		FreeLibrary( hGdip );
		return false;
	}

	MultiByteToWideChar( CP_ACP, 0, pszPath, -1, wszPath, MAX_PATH );
	wszPath[MAX_PATH - 1] = 0;

	*pError = "This picture cannot be read";
	if ( pCreateBitmapFromFile( wszPath, &source ) == 0 && pGetImageWidth( source, &srcWide ) == 0 &&
		 pGetImageHeight( source, &srcTall ) == 0 && srcWide && srcTall )
	{
		// the longer side becomes maxSide; the canvas rounds each side up to 16
		if ( srcWide >= srcTall )
		{
			drawWide = maxSide;
			drawTall = (int)( (double)maxSide * srcTall / srcWide + 0.5 );
		}
		else
		{
			drawTall = maxSide;
			drawWide = (int)( (double)maxSide * srcWide / srcTall + 0.5 );
		}

		if ( drawWide < 1 )
			drawWide = 1;
		if ( drawTall < 1 )
			drawTall = 1;

		wide = ( drawWide + 15 ) & ~15;
		tall = ( drawTall + 15 ) & ~15;

		bgra = (unsigned char *)calloc( wide * tall, 4 );
		if ( bgra && pCreateBitmapFromScan0( wide, tall, wide * 4, GDIP_PIXELFORMAT_32BPP_ARGB, bgra, &canvas ) == 0 &&
			 pGetImageGraphicsContext( canvas, &graphics ) == 0 && pCreateImageAttributes( &attributes ) == 0 )
		{
			pSetInterpolationMode( graphics, GDIP_INTERPOLATION_HQ_BICUBIC );
			pSetPixelOffsetMode( graphics, GDIP_PIXELOFFSET_HALF );
			pSetImageAttributesWrapMode( attributes, GDIP_WRAP_TILE_FLIP_XY, 0, FALSE );

			if ( pDrawImageRectRectI( graphics, source, ( wide - drawWide ) / 2, ( tall - drawTall ) / 2, drawWide, drawTall,
									  0, 0, srcWide, srcTall, GDIP_UNIT_PIXEL, attributes, NULL, NULL ) == 0 )
			{
				ok = true;
			}
		}

		if ( attributes )
			pDisposeImageAttributes( attributes );
		if ( graphics )
			pDeleteGraphics( graphics );
		if ( canvas )
			pDisposeImage( canvas );	// the pixels stay in bgra, which the bitmap only borrowed

		if ( ok )
		{
			*pBGRA = bgra;
			*pWide = wide;
			*pTall = tall;
		}
		else if ( bgra )
		{
			free( bgra );
		}
	}

	if ( source )
		pDisposeImage( source );

	pShutdown( token );
	FreeLibrary( hGdip );
	return ok;
}

//-----------------------------------------------------------------------------
// Median cut: the colors of the visible pixels are split into at most 255 boxes, each pixel takes the
// average color of its box
//-----------------------------------------------------------------------------
struct SprayPixel_t
{
	unsigned char	c[3];		// red, green, blue
	int				index;		// where the pixel sits in the picture
};

struct SprayBox_t
{
	int		first, count;
	int		range, channel;		// the widest spread of a color channel in the box, and that channel
};

static int s_iSortChannel;

static int CompareSprayPixels( const void *a, const void *b )
{
	return ( (const SprayPixel_t *)a )->c[s_iSortChannel] - ( (const SprayPixel_t *)b )->c[s_iSortChannel];
}

static void MeasureSprayBox( SprayBox_t *box, const SprayPixel_t *pixels )
{
	int lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
	int i, k;

	for ( i = box->first; i < box->first + box->count; i++ )
	{
		for ( k = 0; k < 3; k++ )
		{
			if ( pixels[i].c[k] < lo[k] )
				lo[k] = pixels[i].c[k];
			if ( pixels[i].c[k] > hi[k] )
				hi[k] = pixels[i].c[k];
		}
	}

	box->range = -1;
	box->channel = 0;
	for ( k = 0; k < 3; k++ )
	{
		if ( hi[k] - lo[k] > box->range )
		{
			box->range = hi[k] - lo[k];
			box->channel = k;
		}
	}
}

// fills palette (255 colors) and gives each pixel in pixels its color's number in indices; returns
// the number of colors used
static int QuantizeSpray( SprayPixel_t *pixels, int count, unsigned char *palette, unsigned char *indices )
{
	SprayBox_t boxes[SPRAY_TRANSPARENT];
	int nBoxes = 1;
	int i, j, best, half;

	boxes[0].first = 0;
	boxes[0].count = count;
	MeasureSprayBox( &boxes[0], pixels );

	while ( nBoxes < SPRAY_TRANSPARENT )
	{
		best = -1;
		for ( i = 0; i < nBoxes; i++ )
		{
			if ( boxes[i].count > 1 && boxes[i].range > 0 && ( best < 0 || boxes[i].range > boxes[best].range ) )
				best = i;
		}

		if ( best < 0 )
			break;		// every box holds one color

		s_iSortChannel = boxes[best].channel;
		qsort( pixels + boxes[best].first, boxes[best].count, sizeof( SprayPixel_t ), CompareSprayPixels );

		half = boxes[best].count / 2;
		boxes[nBoxes].first = boxes[best].first + half;
		boxes[nBoxes].count = boxes[best].count - half;
		boxes[best].count = half;
		MeasureSprayBox( &boxes[best], pixels );
		MeasureSprayBox( &boxes[nBoxes], pixels );
		nBoxes++;
	}

	for ( i = 0; i < nBoxes; i++ )
	{
		int sum[3] = { 0, 0, 0 };

		for ( j = boxes[i].first; j < boxes[i].first + boxes[i].count; j++ )
		{
			sum[0] += pixels[j].c[0];
			sum[1] += pixels[j].c[1];
			sum[2] += pixels[j].c[2];
			indices[pixels[j].index] = (unsigned char)i;
		}

		palette[i * 3 + 0] = (unsigned char)( ( sum[0] + boxes[i].count / 2 ) / boxes[i].count );
		palette[i * 3 + 1] = (unsigned char)( ( sum[1] + boxes[i].count / 2 ) / boxes[i].count );
		palette[i * 3 + 2] = (unsigned char)( ( sum[2] + boxes[i].count / 2 ) / boxes[i].count );
	}

	return nBoxes;
}

// the palette color closest to r, g, b among the first nColors
static unsigned char NearestSprayColor( const unsigned char *palette, int nColors, int r, int g, int b )
{
	int i, best = 0, bestDist = 0x7fffffff;

	for ( i = 0; i < nColors; i++ )
	{
		int dr = palette[i * 3 + 0] - r;
		int dg = palette[i * 3 + 1] - g;
		int db = palette[i * 3 + 2] - b;
		int dist = dr * dr + dg * dg + db * db;

		if ( dist < bestDist )
		{
			bestDist = dist;
			best = i;
		}
	}

	return (unsigned char)best;
}

//-----------------------------------------------------------------------------
// Purpose: builds the bytes of tempdecal.wad from a B, G, R, A picture (sides multiples of 16): one
//			WAD3 lump holding a miptex with four mip levels and a 256 color palette whose last color is
//			pure blue, which tells the engine that index 255 is see-through (Draw_MiptexTexture in
//			GL_DRAW.C names such a decal '{'). rgbaPreview (wide * tall * 4) gets the result as R, G, B, A.
//			NULL with *pError set when the picture has nothing visible
//-----------------------------------------------------------------------------
static unsigned char *MakeSprayWad( const unsigned char *bgra, int wide, int tall, int *pnSize, unsigned char *rgbaPreview, const char **pError )
{
	int pixels = wide * tall;
	int mipBytes = pixels + pixels / 4 + pixels / 16 + pixels / 64;
	int lumpSize = 40 + mipBytes + 2 + 768;
	int fileSize;
	int i, x, y, xx, yy, level, step, nVisible, nColors;
	SprayPixel_t *visible;
	unsigned char *indices, *wad, *lump, *mip;
	unsigned char palette[768];

	lumpSize = ( lumpSize + 3 ) & ~3;
	fileSize = 12 + lumpSize + 32;

	visible = (SprayPixel_t *)malloc( pixels * sizeof( SprayPixel_t ) );
	indices = (unsigned char *)malloc( pixels );
	wad = (unsigned char *)calloc( fileSize, 1 );
	if ( !visible || !indices || !wad )
	{
		free( visible );
		free( indices );
		free( wad );
		*pError = "Out of memory";
		return NULL;
	}

	// a pixel more than half see-through becomes see-through
	nVisible = 0;
	for ( i = 0; i < pixels; i++ )
	{
		const unsigned char *p = &bgra[i * 4];

		if ( p[3] < 128 )
		{
			indices[i] = SPRAY_TRANSPARENT;
			continue;
		}

		visible[nVisible].c[0] = p[2];
		visible[nVisible].c[1] = p[1];
		visible[nVisible].c[2] = p[0];
		visible[nVisible].index = i;
		nVisible++;
	}

	if ( !nVisible )
	{
		free( visible );
		free( indices );
		free( wad );
		*pError = "This picture is see-through everywhere";
		return NULL;
	}

	memset( palette, 0, sizeof( palette ) );
	nColors = QuantizeSpray( visible, nVisible, palette, indices );
	palette[SPRAY_TRANSPARENT * 3 + 2] = 255;
	free( visible );

	// header, lump and lump info, as UpdateLogoWAD in LogoFile.cpp writes them
	memcpy( wad, "WAD3", 4 );
	*(int *)( wad + 4 ) = 1;
	*(int *)( wad + 8 ) = 12 + lumpSize;

	lump = wad + 12;
	strcpy( (char *)lump, "{LOGO" );
	*(unsigned int *)( lump + 16 ) = wide;
	*(unsigned int *)( lump + 20 ) = tall;
	*(unsigned int *)( lump + 24 ) = 40;
	*(unsigned int *)( lump + 28 ) = 40 + pixels;
	*(unsigned int *)( lump + 32 ) = 40 + pixels + pixels / 4;
	*(unsigned int *)( lump + 36 ) = 40 + pixels + pixels / 4 + pixels / 16;

	mip = lump + 40;
	memcpy( mip, indices, pixels );
	mip += pixels;

	// the smaller levels average the visible pixels of each block; a block mostly see-through stays so
	for ( level = 1; level < 4; level++ )
	{
		step = 1 << level;

		for ( y = 0; y < tall; y += step )
		{
			for ( x = 0; x < wide; x += step )
			{
				int sum[3] = { 0, 0, 0 }, n = 0;

				for ( yy = y; yy < y + step; yy++ )
				{
					for ( xx = x; xx < x + step; xx++ )
					{
						const unsigned char *p = &bgra[( yy * wide + xx ) * 4];

						if ( p[3] >= 128 )
						{
							sum[0] += p[2];
							sum[1] += p[1];
							sum[2] += p[0];
							n++;
						}
					}
				}

				if ( n * 2 < step * step )
					*mip++ = SPRAY_TRANSPARENT;
				else
					*mip++ = NearestSprayColor( palette, nColors, sum[0] / n, sum[1] / n, sum[2] / n );
			}
		}
	}

	*(unsigned short *)mip = 256;
	memcpy( mip + 2, palette, 768 );

	lump = wad + 12 + lumpSize;			// lump info
	*(int *)( lump + 0 ) = 12;
	*(int *)( lump + 4 ) = lumpSize;
	*(int *)( lump + 8 ) = lumpSize;
	lump[12] = 64;						// TYP_LUMPY, as Half-Life's logos
	strcpy( (char *)lump + 16, "{LOGO" );

	for ( i = 0; i < pixels; i++ )
	{
		unsigned char c = indices[i];

		rgbaPreview[i * 4 + 0] = palette[c * 3 + 0];
		rgbaPreview[i * 4 + 1] = palette[c * 3 + 1];
		rgbaPreview[i * 4 + 2] = palette[c * 3 + 2];
		rgbaPreview[i * 4 + 3] = ( c == SPRAY_TRANSPARENT ) ? 0 : 255;
	}

	free( indices );
	*pnSize = fileSize;
	return wad;
}

//-----------------------------------------------------------------------------
// Purpose: the first level of the logo in a tempdecal.wad as R, G, B, A, drawn as the engine draws it
//			(GL_Upload8): with a pure blue last color index 255 is see-through, otherwise the logo is
//			one color, the last one, and each index is how solid it is. The caller frees *pRGBA
//-----------------------------------------------------------------------------
static bool ReadSprayWad( const unsigned char *data, int size, unsigned char **pRGBA, int *pWide, int *pTall )
{
	const unsigned char *lump, *pixels, *palette;
	unsigned int wide, tall, offset;
	int filepos, i, mipBytes;
	unsigned char *rgba;
	bool solid;

	if ( size < 12 || memcmp( data, "WAD", 3 ) || *(const int *)( data + 4 ) < 1 )
		return false;

	i = *(const int *)( data + 8 );
	if ( i < 12 || i + 32 > size )
		return false;

	filepos = *(const int *)( data + i );
	if ( filepos < 12 || filepos + 40 > size )
		return false;

	lump = data + filepos;
	wide = *(const unsigned int *)( lump + 16 );
	tall = *(const unsigned int *)( lump + 20 );
	offset = *(const unsigned int *)( lump + 24 );
	if ( wide < 1 || tall < 1 || wide > 256 || tall > 256 )
		return false;

	mipBytes = wide * tall + wide * tall / 4 + wide * tall / 16 + wide * tall / 64;
	if ( filepos + (int)offset + mipBytes + 2 + 768 > size )
		return false;

	pixels = lump + offset;
	palette = pixels + mipBytes + 2;
	solid = palette[765] == 0 && palette[766] == 0 && palette[767] == 255;

	rgba = (unsigned char *)malloc( wide * tall * 4 );
	if ( !rgba )
		return false;

	for ( i = 0; i < (int)( wide * tall ); i++ )
	{
		unsigned char c = pixels[i];

		if ( solid )
		{
			rgba[i * 4 + 0] = palette[c * 3 + 0];
			rgba[i * 4 + 1] = palette[c * 3 + 1];
			rgba[i * 4 + 2] = palette[c * 3 + 2];
			rgba[i * 4 + 3] = ( c == SPRAY_TRANSPARENT ) ? 0 : 255;
		}
		else
		{
			rgba[i * 4 + 0] = palette[765];
			rgba[i * 4 + 1] = palette[766];
			rgba[i * 4 + 2] = palette[767];
			rgba[i * 4 + 3] = c;
		}
	}

	*pRGBA = rgba;
	*pWide = wide;
	*pTall = tall;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: shows a logo, scaled to fit, on a dark square
//-----------------------------------------------------------------------------
class CSprayPreviewPanel : public Panel
{
	typedef Panel BaseClass;

public:
	CSprayPreviewPanel( Panel *parent, const char *name ) : BaseClass( parent, name )
	{
		m_iTexture = -1;
		m_iWide = m_iTall = 0;
	}

	void SetSpray( const unsigned char *rgba, int wide, int tall )
	{
		if ( !rgba )
		{
			m_iWide = m_iTall = 0;
			return;
		}

		if ( m_iTexture < 0 )
			m_iTexture = surface()->CreateNewTextureID( true );

		surface()->DrawSetTextureRGBA( m_iTexture, rgba, wide, tall, true, true );
		m_iWide = wide;
		m_iTall = tall;
	}

	virtual void Paint()
	{
		int w, h, dw, dh, x, y;

		GetSize( w, h );
		surface()->DrawSetColor( 20, 20, 20, 255 );
		surface()->DrawFilledRect( 0, 0, w, h );

		if ( m_iWide && m_iTall )
		{
			// the largest that fits inside a 4 pixel margin, never more than 2 screen pixels a logo pixel
			dw = w - 8;
			dh = m_iTall * dw / m_iWide;
			if ( dh > h - 8 )
			{
				dh = h - 8;
				dw = m_iWide * dh / m_iTall;
			}
			if ( dw > m_iWide * 2 )
			{
				dw = m_iWide * 2;
				dh = m_iTall * 2;
			}

			x = ( w - dw ) / 2;
			y = ( h - dh ) / 2;
			surface()->DrawSetTexture( m_iTexture );
			surface()->DrawSetColor( 255, 255, 255, 255 );
			surface()->DrawTexturedRect( x, y, x + dw, y + dh );
		}

		surface()->DrawSetColor( 90, 90, 90, 255 );
		surface()->DrawOutlinedRect( 0, 0, w, h );
	}

private:
	int		m_iTexture;
	int		m_iWide, m_iTall;
};

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
COptionsSubSpray::COptionsSubSpray( vgui::Panel *parent ) : vgui::PropertyPage( parent, "OptionsSubSpray" )
{
	Label *label;
	Button *button;
	int i;
	char text[64];

	m_pWad = NULL;
	m_nWadSize = 0;
	m_szWadSource[0] = 0;
	m_bFilling = false;

	label = new Label( this, "ImageLabel", "Spray picture (from the logos folder)" );
	label->SetBounds( 16, 14, 240, 20 );

	m_pImageList = new ComboBox( this, "ImageList", 12, false );
	m_pImageList->SetBounds( 16, 36, 236, 24 );

	label = new Label( this, "SizeLabel", "Size (the longer side, in units)" );
	label->SetBounds( 16, 70, 240, 20 );

	m_pSizeList = new ComboBox( this, "SizeList", 6, false );
	m_pSizeList->SetBounds( 16, 92, 100, 24 );
	for ( i = 0; i < sizeof( s_SpraySizes ) / sizeof( s_SpraySizes[0] ); i++ )
	{
		KeyValues *data = new KeyValues( "size", "size", s_SpraySizes[i] );

		if ( s_SpraySizes[i] == 64 )
			sprintf( text, "%d (Half-Life)", s_SpraySizes[i] );
		else
			sprintf( text, "%d", s_SpraySizes[i] );

		m_pSizeList->AddItem( text, data );
		data->deleteThis();
	}

	button = new Button( this, "OpenFolder", "Open the logos folder" );
	button->SetCommand( "OpenLogos" );
	button->SetBounds( 16, 130, 170, 24 );

	button = new Button( this, "Refresh", "Refresh" );
	button->SetCommand( "Refresh" );
	button->SetBounds( 192, 130, 60, 24 );

	label = new Label( this, "Help1", "PNG, JPG, BMP, GIF or TIFF; see-through parts" );
	label->SetBounds( 16, 170, 250, 18 );
	label = new Label( this, "Help2", "stay see-through. Spray with the Spray logo" );
	label->SetBounds( 16, 188, 250, 18 );
	label = new Label( this, "Help3", "key of the Keyboard tab (impulse 201)." );
	label->SetBounds( 16, 206, 250, 18 );
	label = new Label( this, "Help4", "A new spray shows from the next map or save." );
	label->SetBounds( 16, 224, 250, 18 );

	m_pStatus = new Label( this, "Status", "" );
	m_pStatus->SetBounds( 16, 262, 500, 20 );

	m_pPreview = new CSprayPreviewPanel( this, "Preview" );
	m_pPreview->SetBounds( 276, 14, 224, 224 );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
COptionsSubSpray::~COptionsSubSpray()
{
	free( m_pWad );
}

//-----------------------------------------------------------------------------
// Purpose: the pictures of the logos folder; the first row is the spray in use
//-----------------------------------------------------------------------------
void COptionsSubSpray::FillImageList( void )
{
	static const char *extensions[] = { ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff" };
	FileFindHandle_t handle;
	const char *name;
	KeyValues *data;
	int i, len, sizeRow;

	m_bFilling = true;

	m_pImageList->DeleteAllItems();
	data = new KeyValues( "file", "file", "" );
	m_pImageList->AddItem( "The spray in use", data );
	data->deleteThis();

	name = filesystem()->FindFirst( "logos/*.*", &handle );
	while ( name )
	{
		len = strlen( name );
		if ( !filesystem()->FindIsDirectory( handle ) && len < 60 )
		{
			for ( i = 0; i < sizeof( extensions ) / sizeof( extensions[0] ); i++ )
			{
				int extLen = strlen( extensions[i] );

				if ( len > extLen && !stricmp( name + len - extLen, extensions[i] ) )
				{
					data = new KeyValues( "file", "file", name );
					m_pImageList->AddItem( name, data );
					data->deleteThis();
					break;
				}
			}
		}

		name = filesystem()->FindNext( handle );
	}
	filesystem()->FindClose( handle );

	sizeRow = 2;
	for ( i = 0; i < sizeof( s_SpraySizes ) / sizeof( s_SpraySizes[0] ); i++ )
	{
		if ( s_SpraySizes[i] == s_iSpraySize )
			sizeRow = i;
	}

	m_pImageList->ActivateItemByRow( 0 );
	m_pSizeList->ActivateItemByRow( sizeRow );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: previews the tempdecal.wad in use
//-----------------------------------------------------------------------------
void COptionsSubSpray::ShowCurrentSpray( void )
{
	FileHandle_t file;
	unsigned char *data, *rgba;
	int size, wide, tall;
	char text[128];

	m_pPreview->SetSpray( NULL, 0, 0 );
	m_pSizeList->SetEnabled( false );

	file = filesystem()->Open( "tempdecal.wad", "rb" );
	if ( !file )
	{
		SetStatus( "No spray yet: pick a picture." );
		return;
	}

	size = filesystem()->Size( file );
	data = (unsigned char *)malloc( size > 0 ? size : 1 );
	if ( data && filesystem()->Read( data, size, file ) == size && ReadSprayWad( data, size, &rgba, &wide, &tall ) )
	{
		m_pPreview->SetSpray( rgba, wide, tall );
		free( rgba );
		_snprintf( text, sizeof( text ), "The spray in use: %d x %d.", wide, tall );
		SetStatus( text );
	}
	else
	{
		SetStatus( "The spray in use cannot be shown." );
	}

	free( data );
	filesystem()->Close( file );
}

//-----------------------------------------------------------------------------
// Purpose: makes the spray from the picture and size picked, and shows it
//-----------------------------------------------------------------------------
void COptionsSubSpray::MakeSprayFromSelection( void )
{
	KeyValues *data;
	const char *file, *error;
	char relative[MAX_PATH], local[MAX_PATH], text[160];
	unsigned char *bgra, *rgba;
	int wide, tall;

	free( m_pWad );
	m_pWad = NULL;
	m_szWadSource[0] = 0;

	data = m_pImageList->GetActiveItemUserData();
	file = data ? data->GetString( "file", "" ) : "";
	if ( !file[0] )
	{
		ShowCurrentSpray();
		return;
	}

	m_pSizeList->SetEnabled( true );
	data = m_pSizeList->GetActiveItemUserData();
	if ( data )
		s_iSpraySize = data->GetInt( "size", 128 );

	m_pPreview->SetSpray( NULL, 0, 0 );

	_snprintf( relative, sizeof( relative ), "logos/%s", file );
	relative[sizeof( relative ) - 1] = 0;
	if ( !filesystem()->GetLocalPath( relative, local, sizeof( local ) ) )
	{
		SetStatus( "This picture is gone from the logos folder." );
		return;
	}

	if ( !LoadSprayPicture( local, s_iSpraySize, &bgra, &wide, &tall, &error ) )
	{
		SetStatus( error );
		return;
	}

	rgba = (unsigned char *)malloc( wide * tall * 4 );
	if ( rgba )
		m_pWad = MakeSprayWad( bgra, wide, tall, &m_nWadSize, rgba, &error );
	else
		error = "Out of memory";
	free( bgra );

	if ( !m_pWad )
	{
		free( rgba );
		SetStatus( error );
		return;
	}

	m_pPreview->SetSpray( rgba, wide, tall );
	free( rgba );

	strncpy( m_szWadSource, file, sizeof( m_szWadSource ) - 1 );
	m_szWadSource[sizeof( m_szWadSource ) - 1] = 0;

	_snprintf( text, sizeof( text ), "%d x %d. Press Apply or OK to spray it.", wide, tall );
	SetStatus( text );

	PostActionSignal( new KeyValues( "ApplyButtonEnable" ) );
}

//-----------------------------------------------------------------------------
// Purpose: opens <game>\logos in Explorer, making it first if needed
//-----------------------------------------------------------------------------
void COptionsSubSpray::OpenLogosFolder( void )
{
	typedef HINSTANCE ( WINAPI *ShellExecuteA_t )( HWND, const char *, const char *, const char *, const char *, int );
	char path[MAX_PATH];
	char *slash;
	HMODULE hShell;
	ShellExecuteA_t pShellExecuteA;

	if ( !GetModuleFileNameA( NULL, path, sizeof( path ) ) )
		return;

	slash = strrchr( path, '\\' );
	if ( !slash )
		return;

	_snprintf( slash + 1, path + sizeof( path ) - ( slash + 1 ), "%s\\logos", engine->pfnGetGameDirectory() );
	path[sizeof( path ) - 1] = 0;
	CreateDirectoryA( path, NULL );

	hShell = LoadLibraryA( "shell32.dll" );
	if ( !hShell )
		return;

	pShellExecuteA = (ShellExecuteA_t)GetProcAddress( hShell, "ShellExecuteA" );
	if ( pShellExecuteA )
		pShellExecuteA( NULL, "open", path, NULL, NULL, SW_SHOWNORMAL );

	FreeLibrary( hShell );
}

void COptionsSubSpray::SetStatus( const char *text )
{
	m_pStatus->SetText( text );
}

//-----------------------------------------------------------------------------
// Purpose: the dialog opened: start from the spray in use
//-----------------------------------------------------------------------------
void COptionsSubSpray::OnResetData()
{
	free( m_pWad );
	m_pWad = NULL;
	m_szWadSource[0] = 0;

	FillImageList();
	ShowCurrentSpray();
}

//-----------------------------------------------------------------------------
// Purpose: writes the spray made from the picture picked
//-----------------------------------------------------------------------------
void COptionsSubSpray::OnApplyChanges()
{
	FileHandle_t file;
	char command[128];

	if ( !m_pWad )
		return;

	file = filesystem()->Open( "tempdecal.wad", "wb" );
	if ( !file )
	{
		SetStatus( "tempdecal.wad cannot be written." );
		return;
	}

	filesystem()->Write( m_pWad, m_nWadSize, file );
	filesystem()->Close( file );

	// as Half-Life's multiplayer page, which names the logo picked in the archived cl_logofile
	_snprintf( command, sizeof( command ), "cl_logofile \"%s\"\n", m_szWadSource );
	command[sizeof( command ) - 1] = 0;
	engine->pfnClientCmd( command );

	SetStatus( "Saved. The new spray shows from the next map or save." );
}

void COptionsSubSpray::OnCommand( const char *command )
{
	if ( !stricmp( command, "OpenLogos" ) )
	{
		OpenLogosFolder();
	}
	else if ( !stricmp( command, "Refresh" ) )
	{
		FillImageList();
		ShowCurrentSpray();
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}

//-----------------------------------------------------------------------------
// Purpose: a list changed
//-----------------------------------------------------------------------------
void COptionsSubSpray::OnTextChanged()
{
	if ( !m_bFilling )
		MakeSprayFromSelection();
}
