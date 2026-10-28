//========= Copyright (c) 1996-2003, Valve LLC, All rights reserved. ============
//
// Purpose: the C interface of the engine to VGUI1: the root panel the client
//			builds its VGUI1 panels on and the VGUI1 surface of the engine
//
// $NoKeywords: $
//=============================================================================

#include <windows.h>

#include "interface.h"
#include "VGUI_EngineSurfaceWrap.h"
#include "EngineSurface.h"

#include <VGUI.h>
#include <VGUI_App.h>
#include <VGUI_Panel.h>
#include <VGUI_Scheme.h>

extern "C"
{
#include "quakedef.h"
#include "cvar.h"
#include "APIProxy.h"
#include "vgui_int.h"

void	ClientDLL_ActivateMouse( void );
void	ClientDLL_DeactivateMouse( void );
void	SetMouseEnable( qboolean fState );
void	VGui_ViewportPaintBackground( int *extents );

HWND *pmainwindow;
}

using namespace vgui;

class CDummyApp : public App
{
public:
	virtual void main( int argc, char *argv[] )
	{
	}

	// ReCoF (B298)
	virtual void getCursorPos( int &x, int &y );
	virtual void setCursorPos( int x, int y );

protected:
	virtual void platTick( void )
	{
	}
};

cvar_t vgui_emulatemouse = { "vgui_emulatemouse", "0" };

static Panel *staticPanel;
static EngineSurfaceWrap *staticEngineSurface;

//-----------------------------------------------------------------------------
// Purpose: ReCoF (B298): the cursor position from the game window's mouse messages. App took it from
//			GetCursorPos, and on every tick it gives the mouse to the panel under that position.
//			RInput and the tools like it replace GetCursorPos with the raw mouse counts the aim is read
//			from, which never follow the cursor on screen, so the inventory, the padlock, the keypad and
//			the other menus answered to a cursor somewhere else
//-----------------------------------------------------------------------------
void CDummyApp::getCursorPos( int &x, int &y )
{
	if ( staticPanel && staticEngineSurface && staticEngineSurface->getWindowCursorPos( x, y ) )
	{
		staticPanel->localToScreen( x, y );	// as App::internalCursorMoved does with the same position
		return;
	}

	App::getCursorPos( x, y );
}

//-----------------------------------------------------------------------------
// Purpose: ReCoF (B298): moves the cursor as App did, and keeps the position getCursorPos returns right
//			until the mouse message of the move comes in
//-----------------------------------------------------------------------------
void CDummyApp::setCursorPos( int x, int y )
{
	App::setCursorPos( x, y );

	if ( staticPanel && staticEngineSurface )
	{
		staticPanel->screenToLocal( x, y );
		staticEngineSurface->setWindowCursorPos( x, y );
	}
}

//-----------------------------------------------------------------------------
// Purpose: ReCoF (B298): the same position for the VGUI2 surface (BaseUISurface::SurfaceGetCursorPos)
//-----------------------------------------------------------------------------
bool VGuiWrap_GetWindowCursorPos( int &x, int &y )
{
	return staticEngineSurface && staticEngineSurface->getWindowCursorPos( x, y );
}

void VGuiWrap_SetWindowCursorPos( int x, int y )
{
	if ( staticEngineSurface )
		staticEngineSurface->setWindowCursorPos( x, y );
}

void VGuiWrap_SetRootPanelSize( void )
{
	Panel *panel = (Panel *)VGuiWrap_GetPanel();
	if ( !panel )
		return;

	POINT pnt;
	RECT rect;

	pnt.x = 0;
	pnt.y = 0;
	ClientToScreen( *pmainwindow, &pnt );
	GetClientRect( *pmainwindow, &rect );

	panel->setBounds( pnt.x, pnt.y, rect.right, rect.bottom );
}

static CDummyApp theApp;

void VGuiWrap_Startup( void )
{
	if ( staticEngineSurface )
		return;

	App::getInstance()->reset();

	staticPanel = new Panel( 0, 0, 320, 240 );
	staticPanel->setPaintBorderEnabled( false );
	staticPanel->setPaintBackgroundEnabled( false );
	staticPanel->setPaintEnabled( false );
	staticPanel->setCursor( App::getInstance()->getScheme()->getCursor( Scheme::scu_none ) );

	CreateInterfaceFn engineFactory = Sys_GetFactoryThis();
	staticEngineSurface = new EngineSurfaceWrap( staticPanel, (IEngineSurface *)engineFactory( ENGINESURFACE_INTERFACE_VERSION, NULL ) );

	if ( !Cvar_FindVar( "vgui_emulatemouse" ) )
		Cvar_RegisterVariable( &vgui_emulatemouse );

	VGuiWrap_SetRootPanelSize();
}

void VGuiWrap_Shutdown( void )
{
	delete staticPanel;
	staticPanel = NULL;

	delete staticEngineSurface;
	staticEngineSurface = NULL;
}

int VGuiWrap_CallEngineSurfaceAppHandler( void *hwnd, unsigned int msg, unsigned int wparam, long lparam )
{
	if ( !staticEngineSurface )
		return 0;

	staticEngineSurface->AppHandler( hwnd, msg, wparam, lparam );
	return 0;
}

void *VGuiWrap_GetPanel( void )
{
	g_engdstAddrs.VGui_GetPanel();

	return staticPanel;
}

void VGuiWrap_ReleaseMouse( void )
{
	if ( App::getInstance() && staticEngineSurface )
	{
		int iEmulateMouse_guess = (int)vgui_emulatemouse.value;
		vgui_emulatemouse.value = 0;

		if ( VGuiWrap2_UseVGUI1() )
		{
			staticEngineSurface->setCursor( App::getInstance()->getScheme()->getCursor( Scheme::scu_arrow ) );
			staticEngineSurface->lockCursor();
		}

		ClientDLL_DeactivateMouse();
		SetMouseEnable( false );

		vgui_emulatemouse.value = iEmulateMouse_guess;
	}
}

void VGuiWrap_GetMouse( void )
{
	if ( staticEngineSurface )
	{
		if ( VGuiWrap2_UseVGUI1() )
		{
			staticEngineSurface->unlockCursor();
		}

		ClientDLL_ActivateMouse();
		SetMouseEnable( true );
	}
}

void VGuiWrap_SetVisible( int state )
{
	if ( !staticPanel )
		return;

	if ( state )
		staticPanel->setVisible( true );
	else
		staticPanel->setVisible( false );
}

void VGuiWrap_Paint( int paintAll )
{
	staticEngineSurface->setEmulatedCursorVisible( vgui_emulatemouse.value ? true : false );

	Panel *panel = (Panel *)VGuiWrap_GetPanel();
	if ( !panel )
		return;

	VGuiWrap_SetRootPanelSize();

	panel->repaint();
	App::getInstance()->externalTick();

	if ( paintAll )
	{
		panel->paintTraverse();
	}
	else
	{
		int extents[4];
		panel->getAbsExtents( extents[0], extents[1], extents[2], extents[3] );
		VGui_ViewportPaintBackground( extents );
	}
}
