//========= Copyright (c) 1996-2003, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

#include "OptionsDialog.h"
#include "OptionsSubKeyboard.h"
#include "OptionsSubMouse.h"
#include "OptionsSubAudio.h"
#include "OptionsSubVideo.h"
#include "OptionsSubVoice.h"
#include "OptionsSubSpray.h"
#include "RcOptions.h"
#include "Taskbar.h"

#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/PropertySheet.h>

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Basic help dialog
//-----------------------------------------------------------------------------
COptionsDialog::COptionsDialog( vgui::Panel *parent ) : PropertyDialog( parent, "OptionsDialog" )
{
	// ReCoF (B332): seven tabs, each with a second row of tabs, and every ReCoF option in them; retail had seven
	// pages in a 512 x 406 window, with Cry of Fear's own options in one long Advanced list
	SetBounds( 0, 0, 660, 500 );
	SetSizeable( false );

	g_pTaskbar->AddTask( GetVPanel() );

	SetTitle( "#GameUI_Options", true );

	CRcOptionsSheet *sheet;

	sheet = new CRcOptionsSheet( this, "GameSheet" );
	sheet->AddPage( new CRcOptionsPage( sheet, "game_general", "Point at an option to see what it does." ), "General" );
	sheet->AddPage( new CRcOptionsPage( sheet, "game_gameplay", "Point at an option to see what it does." ), "Gameplay" );
	sheet->AddPage( new CRcOptionsPage( sheet, "game_movement", "Point at an option to see what it does." ), "Movement" );
	AddPage( sheet, "Game" );

	sheet = new CRcOptionsSheet( this, "ControlsSheet" );
	sheet->AddPage( new COptionsSubKeyboard( sheet ), "#GameUI_Keyboard" );
	sheet->AddPage( new COptionsSubMouse( sheet ), "#GameUI_Mouse" );
	AddPage( sheet, "Controls" );

	sheet = new CRcOptionsSheet( this, "GraphicsSheet" );
	sheet->AddPage( new COptionsSubVideo( sheet ), "Display" );
	sheet->AddPage( new CRcOptionsPage( sheet, "gfx_effects", "Point at an option to see what it does." ), "Effects" );
	sheet->AddPage( new CRcOptionsPage( sheet, "gfx_quality", "Point at an option to see what it does." ), "Quality" );
	sheet->AddPage( new CRcOptionsPage( sheet, "gfx_performance", "Point at an option to see what it does." ), "Performance" );
	sheet->AddPage( new CRcOptionsPage( sheet, "gfx_weapon", "Point at an option to see what it does." ), "Weapon view" );
	AddPage( sheet, "Graphics" );

	sheet = new CRcOptionsSheet( this, "HudSheet" );
	sheet->AddPage( new CRcOptionsPage( sheet, "hud_crosshair", "Point at an option to see what it does." ), "Crosshair" );
	sheet->AddPage( new CRcOptionsPage( sheet, "hud_info", "Point at an option to see what it does." ), "Info panel" );
	sheet->AddPage( new CRcOptionsPage( sheet, "hud_speedrun", "Point at an option to see what it does." ), "Speedrun" );
	AddPage( sheet, "HUD" );

	sheet = new CRcOptionsSheet( this, "AudioSheet" );
	sheet->AddPage( new COptionsSubAudio( sheet ), "Sound" );
	sheet->AddPage( new COptionsSubVoice( sheet ), "#GameUI_Voice" );
	AddPage( sheet, "Audio" );

	sheet = new CRcOptionsSheet( this, "MultiplayerSheet" );
	sheet->AddPage( new CRcOptionsPage( sheet, "mp_player", "Point at an option to see what it does." ), "Player" );
	sheet->AddPage( new COptionsSubSpray( sheet ), "Spray" );
	AddPage( sheet, "Multiplayer" );

	AddPage( new CRcAboutPage( this ), "ReCoF" );

	SetApplyButtonVisible( true );
	GetPropertySheet()->SetTabWidth( 84 );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
COptionsDialog::~COptionsDialog()
{
}

//-----------------------------------------------------------------------------
// Purpose: sets the title in the dialog and in the taskbar
//-----------------------------------------------------------------------------
void COptionsDialog::SetTitle( const char *title, bool surfaceTitle )
{
	BaseClass::SetTitle( title, surfaceTitle );

	if ( g_pTaskbar )
	{
		wchar_t *wTitle;
		wchar_t w_szTitle[ 1024 ];

		wTitle = localize()->Find( title );

		if ( !wTitle )
		{
			localize()->ConvertANSIToUnicode( title, w_szTitle, sizeof( w_szTitle ) );
			wTitle = w_szTitle;
		}

		g_pTaskbar->SetTitle( GetVPanel(), wTitle );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Brings the dialog to the fore
//-----------------------------------------------------------------------------
void COptionsDialog::Activate()
{
	BaseClass::Activate();
	EnableApplyButton( false );
}

//-----------------------------------------------------------------------------
// Purpose: Opens the dialog
//-----------------------------------------------------------------------------
void COptionsDialog::Run()
{
	SetTitle( "#GameUI_Options", true );
	Activate();
}

//-----------------------------------------------------------------------------
// Purpose: Called when the GameUI is hidden
//-----------------------------------------------------------------------------
void COptionsDialog::OnClose()
{
	BaseClass::OnClose();
	MarkForDeletion();
}
