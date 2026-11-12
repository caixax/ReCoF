// ReCoF: the options window's own pages (see RcOptions.h)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "RcOptions.h"
#include "EngineInterface.h"
#include "PanelListPanel.h"
#include "../cl_dll/rc_version.h"

#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/KeyValues.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Slider.h>
#include <vgui_controls/TextEntry.h>

#undef PostMessage	// windows.h, through the engine headers, renames vgui's

using namespace vgui2;

enum
{
	RC_BOOL,		// a check box: 1 ticked, 0 not
	RC_BOOLINV,		// a check box that is ticked at 0 (the cvar turns the thing off)
	RC_LIST,		// a list of choices, "Text=value|Text=value"
	RC_INT,			// a slider of whole numbers
	RC_FLOAT,		// a slider with two decimals
	RC_TEXT,		// a text box (colors, positions, the name)
};

struct RcOption
{
	const char	*tab;
	const char	*cvar;
	int			type;
	const char	*label;
	const char	*def;
	float		min;
	float		max;
	const char	*choices;
	const char	*help;
};

//-----------------------------------------------------------------------------
// Every option of ReCoF's tabs, in the order they are shown. The launcher's options (recof-cvars.json) use
// the same names; the ones only the launcher has start the game (-novid, -noborder, crash reports).
//-----------------------------------------------------------------------------
static const RcOption s_RcOptions[] =
{
	// Game, General
	{ "game_general", "ironsights_toggle", RC_LIST, "Iron sights", "1", 0, 0, "Hold the key=0|Press to aim, again to stop=1",
		"How the iron sights key works: aim while you hold it, or press it once to aim and again to stop." },
	{ "game_general", "cl_subtitles", RC_BOOL, "Subtitles", "1", 0, 0, NULL,
		"Shows what is said at the bottom of the screen. The language is picked in Language, in the main menu." },
	{ "game_general", "sv_disablehints", RC_BOOLINV, "Hints", "0", 0, 0, NULL,
		"The hints the game shows the first time you meet something new." },
	{ "game_general", "cl_nodoubletapdodge", RC_BOOLINV, "Dodge by double-tapping", "0", 0, 0, NULL,
		"Double-tapping a direction key dodges. The Dodge key works either way." },
	{ "game_general", "rc_endstats", RC_BOOL, "Readable end statistics", "1", 0, 0, NULL,
		"The statistics and grade at the end of the game get a dark backdrop, and a click before they are fully shown is ignored. Off is the original." },

	// Game, Gameplay
	{ "game_gameplay", "rc_retail_gameplay", RC_BOOL, "Original balance", "0", 0, 0, NULL,
		"Brings back the original game's behavior of every balance fix at once: monster damage and health, AI, ladder climbs and collision." },
	{ "game_gameplay", "rc_infinite_stamina", RC_BOOL, "Infinite stamina", "0", 0, 0, NULL,
		"Stamina never runs out. The stamina bar still shows." },
	{ "game_gameplay", "rc_door_skip", RC_BOOL, "Skip the door fades", "0", 0, 0, NULL,
		"The doors that take you to the other side move you at once, without the fade to black and back. For fun: runs with it do not compare with the original game." },

	// Game, Movement
	{ "game_movement", "rc_hlmovement", RC_BOOL, "Half-Life movement", "0", 0, 0, NULL,
		"Half-Life 1 physics as in its first version: run speed 320, no sprint, dodge or stamina cost for jumps, no movement camera punches, with bunny hop, ducktap and autojump." },
	{ "game_movement", "rc_bhopcap", RC_BOOL, "Bunny hop speed cap", "1", 0, 0, NULL,
		"On as in the original game. Off removes the cap, which a separate speedrun category uses." },
	{ "game_movement", "rc_ducktap", RC_BOOL, "Ducktap", "0", 0, 0, NULL,
		"Brings back the Half-Life ducktap that Cry of Fear removed." },
	{ "game_movement", "rc_autojump", RC_BOOL, "Autojump", "0", 0, 0, NULL,
		"Holding jump jumps again as you land." },
	{ "game_movement", "rc_ducktap_priority", RC_BOOL, "Ducktap before autojump", "1", 0, 0, NULL,
		"While the ducktap key is held, autojump pauses." },
	{ "game_movement", "rc_jump_priority", RC_BOOL, "Jump before ducktap", "0", 0, 0, NULL,
		"While jump is held, the ducktap key pauses." },

	// Controls, Mouse (a row on the mouse page)
	{ "controls_mouse", "rc_rawinput", RC_LIST, "Raw mouse input", "2", 0, 0, "Off (the Windows mouse)=0|On the game window=1|On a thread of its own, as RInput=2",
		"Reads the mouse directly, without Windows acceleration." },

	// Graphics, Effects
	{ "gfx_effects", "gl_posteffects", RC_BOOL, "Black and white effect", "1", 0, 0, NULL,
		"The game's black and white and color effects." },
	{ "gfx_effects", "gl_screenblur", RC_BOOL, "Screen blur", "1", 0, 0, NULL,
		"Blurs the screen when you are hurt or on morphine." },
	{ "gfx_effects", "cl_vignette", RC_BOOL, "Vignette", "1", 0, 0, NULL,
		"Darkens the corners of the screen." },
	{ "gfx_effects", "cl_noiseeffect", RC_BOOL, "Film grain", "0", 0, 0, NULL,
		"A grain over the picture, as on old film." },
	{ "gfx_effects", "cl_noiseamount", RC_FLOAT, "Film grain amount", "0.5", 0, 1, NULL,
		"How strong the film grain is." },
	{ "gfx_effects", "cl_cameraeffect", RC_BOOL, "Camera and scope effect", "0", 0, 0, NULL,
		"The effect over the picture when you look through the camera or a scope." },
	{ "gfx_effects", "cl_bloodeffects", RC_BOOL, "Blood particles", "1", 0, 0, NULL,
		"Blood when something is hit." },
	{ "gfx_effects", "r_weather", RC_LIST, "Weather", "2", 0, 0, "Off=0|Low quality=1|High quality=2",
		"Rain and snow." },
	{ "gfx_effects", "cl_ripples", RC_BOOL, "Water ripples", "1", 0, 0, NULL,
		"Rings on water where rain falls and things touch it." },
	{ "gfx_effects", "cg_water", RC_BOOL, "Water shader", "1", 0, 0, NULL,
		"Water that reflects and moves." },
	{ "gfx_effects", "cl_cheapweaponsmoke", RC_BOOL, "Cheap gun smoke", "0", 0, 0, NULL,
		"A lighter gun smoke effect, for slower computers." },
	{ "gfx_effects", "cl_smokeparticlelimit", RC_INT, "Gun smoke particles", "5", 0, 20, NULL,
		"The most gun smoke particles at a time." },

	// Graphics, Quality
	{ "gfx_quality", "gl_brightness", RC_FLOAT, "Brightness boost", "0", 0, 0.3f, NULL,
		"Lights up the picture on top of the Brightness of the Display tab." },
	{ "gfx_quality", "gl_contrast", RC_FLOAT, "Contrast", "1", 0, 20, NULL,
		"1 is normal." },
	{ "gfx_quality", "gl_bump", RC_BOOL, "Bump maps", "1", 0, 0, NULL,
		"Surfaces with depth under the light." },
	{ "gfx_quality", "gl_specular", RC_BOOL, "Specular maps", "1", 0, 0, NULL,
		"Shine on wet and metal surfaces." },
	{ "gfx_quality", "gl_highspecular", RC_BOOL, "High quality specular", "1", 0, 0, NULL,
		"Finer shine, for faster graphics cards." },
	{ "gfx_quality", "cl_propdistance", RC_BOOL, "Fade distant props", "0", 0, 0, NULL,
		"Props far away are not drawn, which can help a slow computer." },

	// Graphics, Performance
	{ "gfx_performance", "rc_worldbatch", RC_BOOL, "Faster world drawing", "1", 0, 0, NULL,
		"Sends the world to the graphics card in fewer calls. The picture is the same; off is the original way, for comparison." },
	{ "gfx_performance", "rc_lightshader", RC_BOOL, "Faster dynamic lights", "1", 0, 0, NULL,
		"Draws the flashlight and the other lights with a shader, in far fewer calls. The picture is the same." },
	{ "gfx_performance", "rc_gpumodels", RC_BOOL, "Models on the graphics card", "1", 0, 0, NULL,
		"Moves and lights the models on the graphics card instead of the processor. The picture is the same." },
	{ "gfx_performance", "rc_modelarrays", RC_BOOL, "Faster model drawing", "1", 0, 0, NULL,
		"Sends each model mesh to the graphics card in one call. The picture is the same." },
	{ "gfx_performance", "rc_dlightcull", RC_BOOL, "Skip lights off screen", "1", 0, 0, NULL,
		"A light that cannot reach anything on screen is not drawn. The picture is the same." },
	{ "gfx_performance", "rc_smoothframes", RC_BOOL, "Smooth frames (experimental)", "0", 0, 0, NULL,
		"On monitors faster than 100 Hz, draws extra pictures between the game's 100 frames a second, turned with the mouse." },
	{ "gfx_performance", "rc_smoothframes_fps", RC_INT, "Smooth frames rate", "0", 0, 500, NULL,
		"The pictures a second smooth frames aim for. 0 follows the monitor's refresh rate." },
	{ "gfx_performance", "rc_prof", RC_LIST, "Frame profiler", "0", 0, 0, "Off=0|Frame times=1|Frame and graphics card times=2",
		"Shows how long each part of a frame takes." },

	// Graphics, Weapon view
	{ "gfx_weapon", "rc_viewmodel_fov", RC_INT, "Weapon field of view", "0", 0, 170, NULL,
		"Draws the weapon with its own field of view. 0 keeps the view's." },
	{ "gfx_weapon", "rc_viewmodel_ofs_forward", RC_FLOAT, "Weapon forward", "0", -10, 10, NULL,
		"Moves the weapon forward, or back below 0, in units. Iron sights ignore it." },
	{ "gfx_weapon", "rc_viewmodel_ofs_right", RC_FLOAT, "Weapon right", "0", -10, 10, NULL,
		"Moves the weapon right, or left below 0, in units. Iron sights ignore it." },
	{ "gfx_weapon", "rc_viewmodel_ofs_up", RC_FLOAT, "Weapon up", "0", -10, 10, NULL,
		"Moves the weapon up, or down below 0, in units. Iron sights ignore it." },

	// HUD, Crosshair
	{ "hud_crosshair", "rc_crosshair", RC_BOOL, "Cry of Fear crosshair", "1", 0, 0, NULL,
		"The game's red crosshair. Off hides it and keeps the ammo display." },
	{ "hud_crosshair", "rc_cross", RC_BOOL, "Custom crosshair", "0", 0, 0, NULL,
		"A crosshair set up by the options below. It shows with or without a weapon." },
	{ "hud_crosshair", "rc_cross_color", RC_TEXT, "Color (red green blue)", "0 255 0", 0, 0, NULL,
		"Three numbers from 0 to 255, for example 0 255 0 for green." },
	{ "hud_crosshair", "rc_cross_alpha", RC_INT, "Opacity", "255", 0, 255, NULL,
		"0 is invisible, 255 solid." },
	{ "hud_crosshair", "rc_cross_size", RC_INT, "Line length", "10", 0, 100, NULL,
		"In pixels." },
	{ "hud_crosshair", "rc_cross_gap", RC_INT, "Gap", "3", 0, 50, NULL,
		"From the middle of the screen to each line, in pixels." },
	{ "hud_crosshair", "rc_cross_thickness", RC_INT, "Thickness", "2", 0, 20, NULL,
		"Of the lines and the circle, in pixels." },
	{ "hud_crosshair", "rc_cross_outline", RC_INT, "Outline", "0", 0, 10, NULL,
		"A black border of this width, in pixels. 0 has none." },
	{ "hud_crosshair", "rc_cross_dot_size", RC_INT, "Dot", "0", 0, 20, NULL,
		"A square dot in the middle, in pixels. 0 has none." },
	{ "hud_crosshair", "rc_cross_dot_color", RC_TEXT, "Dot color", "", 0, 0, NULL,
		"Three numbers from 0 to 255. Empty uses the lines' color." },
	{ "hud_crosshair", "rc_cross_circle_radius", RC_INT, "Circle", "0", 0, 100, NULL,
		"A circle of this radius, in pixels. 0 has none." },
	{ "hud_crosshair", "rc_cross_top_line", RC_BOOL, "Top line", "1", 0, 0, NULL, "The line above the middle." },
	{ "hud_crosshair", "rc_cross_bottom_line", RC_BOOL, "Bottom line", "1", 0, 0, NULL, "The line below the middle." },
	{ "hud_crosshair", "rc_cross_left_line", RC_BOOL, "Left line", "1", 0, 0, NULL, "The line left of the middle." },
	{ "hud_crosshair", "rc_cross_right_line", RC_BOOL, "Right line", "1", 0, 0, NULL, "The line right of the middle." },

	// HUD, Information panel
	{ "hud_info", "rc_info", RC_BOOL, "Information panel", "0", 0, 0, NULL,
		"RebuiltSimon's panel at the top right: ReCoF and its version, Simon's health, stamina and speed, and a clock." },
	{ "hud_info", "rc_info_health", RC_BOOL, "Health", "1", 0, 0, NULL, "Simon's health in the panel." },
	{ "hud_info", "rc_info_stamina", RC_BOOL, "Stamina", "1", 0, 0, NULL, "Simon's stamina in the panel." },
	{ "hud_info", "rc_info_speed", RC_BOOL, "Speed", "1", 0, 0, NULL, "Simon's speed in the panel." },
	{ "hud_info", "rc_info_stats", RC_BOOL, "Statistics", "1", 0, 0, NULL, "The run's statistics in the panel." },
	{ "hud_info", "rc_info_color", RC_TEXT, "Text color (red green blue)", "255 255 255", 0, 0, NULL,
		"Three numbers from 0 to 255." },
	{ "hud_info", "rc_info_background", RC_BOOL, "Background", "1", 0, 0, NULL, "A box behind the panel's text." },
	{ "hud_info", "rc_info_background_color", RC_TEXT, "Background color (red green blue alpha)", "20 20 20 160", 0, 0, NULL,
		"Four numbers from 0 to 255; the last is the opacity." },
	{ "hud_info", "rc_info_offset", RC_INT, "First line", "4", 0, 40, NULL,
		"Where the panel starts, in text lines from the top." },

	// HUD, Speedrun
	{ "hud_speedrun", "rc_hud_timer", RC_BOOL, "Run timer", "0", 0, 0, NULL,
		"A run timer on the left that counts as the LiveSplit autosplitter does: it starts with a new game and stops for pauses, loads, menus and cutscenes." },
	{ "hud_speedrun", "rc_hud_timer_color", RC_TEXT, "Timer color", "255 255 255", 0, 0, NULL, "Three numbers from 0 to 255." },
	{ "hud_speedrun", "rc_hud_timer_pos", RC_TEXT, "Timer position (x y)", "0 .5", 0, 0, NULL,
		"Two numbers from 0 to 1: 0 0 is the top left of the screen, 1 1 the bottom right." },
	{ "hud_speedrun", "rc_speedometer", RC_BOOL, "Speedometer", "0", 0, 0, NULL, "Shows your horizontal speed on screen." },
	{ "hud_speedrun", "rc_speedometer_jump", RC_BOOL, "Takeoff speed", "1", 0, 0, NULL, "Shows the speed at your last jump under the speedometer." },
	{ "hud_speedrun", "rc_speedometer_3d", RC_BOOL, "3D speed", "0", 0, 0, NULL, "The speedometer counts vertical speed too." },
	{ "hud_speedrun", "rc_speedometer_color", RC_TEXT, "Speedometer color", "255 255 255", 0, 0, NULL, "Three numbers from 0 to 255." },
	{ "hud_speedrun", "rc_speedometer_pos", RC_TEXT, "Speedometer position (x y)", ".5 .9", 0, 0, NULL,
		"Two numbers from 0 to 1: 0 0 is the top left of the screen, 1 1 the bottom right." },
	{ "hud_speedrun", "rc_speedometer_jump_pos", RC_TEXT, "Takeoff speed position (x y)", ".5 .95", 0, 0, NULL,
		"Two numbers from 0 to 1: 0 0 is the top left of the screen, 1 1 the bottom right." },
	{ "hud_speedrun", "rc_hud_health", RC_BOOL, "Health number", "0", 0, 0, NULL, "Simon's health in HUD digits, as RebuiltSimon shows it." },
	{ "hud_speedrun", "rc_hud_health_pos", RC_TEXT, "Health number position (x y)", ".02 .73", 0, 0, NULL,
		"Two numbers from 0 to 1: 0 0 is the top left of the screen, 1 1 the bottom right." },
	{ "hud_speedrun", "rc_hud_stamina", RC_BOOL, "Stamina number", "0", 0, 0, NULL, "Simon's stamina in HUD digits, as RebuiltSimon shows it." },
	{ "hud_speedrun", "rc_hud_stamina_pos", RC_TEXT, "Stamina number position (x y)", ".07 .73", 0, 0, NULL,
		"Two numbers from 0 to 1: 0 0 is the top left of the screen, 1 1 the bottom right." },
	{ "hud_speedrun", "rc_hud_centered", RC_BOOL, "Numbers centered on their position", "1", 0, 0, NULL,
		"Off, a number starts at its position instead." },

	// Audio, Sound (a row on the audio page)
	{ "audio_sound", "rc_omgbruh_sound", RC_BOOL, "Same pitch on every shot", "0", 0, 0, NULL,
		"Plays your weapon sounds at their recorded pitch. The original game changes it a little at every shot." },

	// Multiplayer, Player
	{ "mp_player", "name", RC_TEXT, "Co-op name", "James", 0, 0, NULL,
		"Your name in co-op games." },
	{ "mp_player", "cl_coop_language", RC_LIST, "Co-op language", "1", 0, 0,
		"Default (English)=0|English=1|Dutch=2|French=3|German=4|Norwegian=5|Spanish=6|Swedish=7",
		"The language of the co-op texts." },

	// ReCoF
	{ "about", "rc_discord", RC_BOOL, "Discord status", "1", 0, 0, NULL,
		"While Discord is open, your profile shows that you are playing ReCoF: the map, the version and whether you are playing or in the menu." },
};

#define RC_NUM_OPTIONS ( sizeof( s_RcOptions ) / sizeof( s_RcOptions[0] ) )

// the choices of a list option: the n-th "Text=value"; false past the last
static bool RcGetChoice( const RcOption *option, int n, char *text, int textSize, char *value, int valueSize )
{
	const char *p = option->choices;
	for ( int i = 0; p && *p; i++ )
	{
		const char *end = strchr( p, '|' );
		int len = end ? (int)( end - p ) : (int)strlen( p );

		if ( i == n )
		{
			const char *eq = (const char *)memchr( p, '=', len );
			int textLen = eq ? (int)( eq - p ) : len;
			_snprintf( text, textSize, "%.*s", textLen, p );
			text[textSize - 1] = 0;
			if ( eq )
				_snprintf( value, valueSize, "%.*s", len - textLen - 1, eq + 1 );
			else
				_snprintf( value, valueSize, "%d", i );
			value[valueSize - 1] = 0;
			return true;
		}

		p = end ? end + 1 : NULL;
	}

	return false;
}

// the cvar's value; empty when the cvar does not exist
static void RcGetCvar( const char *cvar, char *out, int size )
{
	const char *value = engine->pfnGetCvarString( (char *)cvar );
	_snprintf( out, size, "%s", value ? value : "" );
	out[size - 1] = 0;
}

static bool RcSameNumber( const char *a, const char *b )
{
	return fabs( atof( a ) - atof( b ) ) < 0.0001;
}

//-----------------------------------------------------------------------------
// The row of one option
//-----------------------------------------------------------------------------
#define RC_ROW_TALL		28
#define RC_FLOAT_SCALE	100.0f

CRcOptionRow::CRcOptionRow( vgui::Panel *parent, const RcOption *option, vgui::Panel *target ) : BaseClass( parent, option->cvar )
{
	m_pOption = option;
	m_pTarget = target;
	m_pCheck = NULL;
	m_pCombo = NULL;
	m_pSlider = NULL;
	m_pValue = NULL;
	m_pText = NULL;
	m_bLoading = false;
	m_szLoaded[0] = 0;

	SetSize( 400, RC_ROW_TALL );
	SetPaintBackgroundEnabled( false );

	m_pLabel = new Label( this, "Label", option->label );
	m_pLabel->SetPaintBackgroundEnabled( false );

	switch ( option->type )
	{
	case RC_BOOL:
	case RC_BOOLINV:
		m_pCheck = new CheckButton( this, "Check", "" );
		m_pCheck->AddActionSignalTarget( this );
		break;

	case RC_LIST:
	{
		char text[96], value[32];
		int count = 0;
		while ( RcGetChoice( option, count, text, sizeof( text ), value, sizeof( value ) ) )
			count++;

		m_pCombo = new ComboBox( this, "Choices", count, false );
		for ( int i = 0; i < count; i++ )
		{
			RcGetChoice( option, i, text, sizeof( text ), value, sizeof( value ) );
			m_pCombo->AddItem( text, NULL );
		}
		m_pCombo->AddActionSignalTarget( this );
		break;
	}

	case RC_INT:
	case RC_FLOAT:
	{
		float scale = option->type == RC_FLOAT ? RC_FLOAT_SCALE : 1.0f;
		m_pSlider = new Slider( this, "Slider" );
		m_pSlider->SetRange( (int)floor( option->min * scale + 0.5f ), (int)floor( option->max * scale + 0.5f ) );
		m_pSlider->AddActionSignalTarget( this );
		m_pValue = new Label( this, "Value", "" );
		break;
	}

	case RC_TEXT:
		m_pText = new TextEntry( this, "Text" );
		m_pText->AddActionSignalTarget( this );
		break;
	}
}

void CRcOptionRow::PerformLayout()
{
	BaseClass::PerformLayout();

	int wide, tall;
	GetSize( wide, tall );

	int labelWide = wide / 2;
	int x = labelWide;
	int controlWide = wide - labelWide - 8;
	if ( controlWide > 240 )
		controlWide = 240;

	m_pLabel->SetBounds( 0, 2, labelWide - 8, 24 );

	if ( m_pCheck )
		m_pCheck->SetBounds( x, 2, 24, 24 );
	if ( m_pCombo )
		m_pCombo->SetBounds( x, 2, controlWide, 24 );
	if ( m_pText )
		m_pText->SetBounds( x, 2, controlWide, 24 );
	if ( m_pSlider )
	{
		m_pSlider->SetBounds( x, 0, controlWide - 56, 26 );
		m_pValue->SetBounds( x + controlWide - 52, 2, 52, 24 );
	}
}

void CRcOptionRow::SetValue( const char *value )
{
	const RcOption *o = m_pOption;

	switch ( o->type )
	{
	case RC_BOOL:
		m_pCheck->SetSelected( atof( value ) != 0.0f );
		break;

	case RC_BOOLINV:
		m_pCheck->SetSelected( atof( value ) == 0.0f );
		break;

	case RC_LIST:
	{
		char text[96], choice[32];
		int row = 0;
		for ( int i = 0; RcGetChoice( o, i, text, sizeof( text ), choice, sizeof( choice ) ); i++ )
		{
			if ( RcSameNumber( choice, value ) )
				row = i;
		}
		m_pCombo->ActivateItemByRow( row );
		break;
	}

	case RC_INT:
	case RC_FLOAT:
	{
		float scale = o->type == RC_FLOAT ? RC_FLOAT_SCALE : 1.0f;
		float v = (float)atof( value );
		if ( v < o->min )
			v = o->min;
		if ( v > o->max )
			v = o->max;
		m_pSlider->SetValue( (int)floor( v * scale + 0.5f ) );
		UpdateValueLabel();
		break;
	}

	case RC_TEXT:
		m_pText->SetText( value );
		break;
	}
}

void CRcOptionRow::GetValue( char *out, int size )
{
	const RcOption *o = m_pOption;
	out[0] = 0;

	switch ( o->type )
	{
	case RC_BOOL:
		_snprintf( out, size, "%d", m_pCheck->IsSelected() ? 1 : 0 );
		break;

	case RC_BOOLINV:
		_snprintf( out, size, "%d", m_pCheck->IsSelected() ? 0 : 1 );
		break;

	case RC_LIST:
	{
		char text[96];
		if ( !RcGetChoice( o, m_pCombo->GetActiveItem(), text, sizeof( text ), out, size ) )
			_snprintf( out, size, "%s", o->def );
		break;
	}

	case RC_INT:
		_snprintf( out, size, "%d", m_pSlider->GetValue() );
		break;

	case RC_FLOAT:
		_snprintf( out, size, "%.2f", (float)m_pSlider->GetValue() / RC_FLOAT_SCALE );
		break;

	case RC_TEXT:
		m_pText->GetText( out, size );
		break;
	}

	out[size - 1] = 0;
}

float CRcOptionRow::GetFloat( void )
{
	char value[64];
	GetValue( value, sizeof( value ) );
	return (float)atof( value );
}

void CRcOptionRow::UpdateValueLabel( void )
{
	if ( !m_pValue )
		return;

	char value[32];
	GetValue( value, sizeof( value ) );
	m_pValue->SetText( value );
}

void CRcOptionRow::Load( void )
{
	m_bLoading = true;
	RcGetCvar( m_pOption->cvar, m_szLoaded, sizeof( m_szLoaded ) );
	SetValue( m_szLoaded[0] ? m_szLoaded : m_pOption->def );
	m_bLoading = false;
}

void CRcOptionRow::SetDefault( void )
{
	SetValue( m_pOption->def );
	Changed();
}

void CRcOptionRow::Apply( void )
{
	char value[64];
	GetValue( value, sizeof( value ) );

	bool same = m_pOption->type == RC_TEXT ? !strcmp( value, m_szLoaded ) : RcSameNumber( value, m_szLoaded );
	if ( same && m_szLoaded[0] )
		return;

	// only what a value may hold: no quotes or command separators reach the console
	for ( char *p = value; *p; p++ )
	{
		if ( *p == '"' || *p == ';' || *p == '\n' || *p == '\r' )
			*p = ' ';
	}

	char command[160];
	_snprintf( command, sizeof( command ), "%s \"%s\"\n", m_pOption->cvar, value );
	command[sizeof( command ) - 1] = 0;
	engine->pfnClientCmd( command );

	strncpy( m_szLoaded, value, sizeof( m_szLoaded ) - 1 );
	m_szLoaded[sizeof( m_szLoaded ) - 1] = 0;
}

void CRcOptionRow::Changed( void )
{
	if ( !m_bLoading && m_pTarget )
		PostMessage( m_pTarget, new KeyValues( "RcOptionChanged" ) );
}

void CRcOptionRow::OnCheckButtonChecked( Panel *panel )
{
	Changed();
}

void CRcOptionRow::OnTextChanged( Panel *panel )
{
	Changed();
}

void CRcOptionRow::OnSliderMoved( Panel *panel )
{
	UpdateValueLabel();
	Changed();
}

static const RcOption *RcFindOption( const char *cvar )
{
	for ( unsigned int i = 0; i < RC_NUM_OPTIONS; i++ )
	{
		if ( !stricmp( s_RcOptions[i].cvar, cvar ) )
			return &s_RcOptions[i];
	}

	return NULL;
}

CRcOptionRow *RcCreateOptionRow( vgui::Panel *parent, const char *cvar, vgui::Panel *target )
{
	const RcOption *option = RcFindOption( cvar );
	return option ? new CRcOptionRow( parent, option, target ) : NULL;
}

//-----------------------------------------------------------------------------
// The crosshair tab's preview: the custom crosshair as the options set it, before Apply
//-----------------------------------------------------------------------------
class CRcCrosshairPreview : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CRcCrosshairPreview, vgui::Panel );

public:
	CRcCrosshairPreview( CRcOptionsPage *page ) : BaseClass( page, "CrosshairPreview" ), m_pPage( page ) {}

	virtual void Paint()
	{
		int wide, tall;
		GetSize( wide, tall );

		surface()->DrawSetColor( 40, 40, 40, 255 );
		surface()->DrawFilledRect( 0, 0, wide, tall );
		surface()->DrawSetColor( 90, 90, 90, 255 );
		surface()->DrawOutlinedRect( 0, 0, wide, tall );

		int color[4] = { 0, 255, 0, 255 };
		Color( "rc_cross_color", color );
		color[3] = (int)Value( "rc_cross_alpha" );

		// dimmed while the custom crosshair is off: what it would look like
		if ( Value( "rc_cross" ) == 0 )
			color[3] /= 3;

		int dot[4] = { color[0], color[1], color[2], color[3] };
		Color( "rc_cross_dot_color", dot );

		int cx = wide / 2, cy = tall / 2;
		int size = (int)Value( "rc_cross_size" );
		int gap = (int)Value( "rc_cross_gap" );
		int thick = (int)Value( "rc_cross_thickness" );
		int outline = (int)Value( "rc_cross_outline" );
		int dotSize = (int)Value( "rc_cross_dot_size" );
		int radius = (int)Value( "rc_cross_circle_radius" );
		int half = thick / 2;

		// the lines: an outline of black around each, then the line
		for ( int pass = outline > 0 ? 0 : 1; pass < 2; pass++ )
		{
			int o = pass == 0 ? outline : 0;
			if ( pass == 0 )
				surface()->DrawSetColor( 0, 0, 0, color[3] );
			else
				surface()->DrawSetColor( color[0], color[1], color[2], color[3] );

			if ( thick > 0 && size > 0 )
			{
				if ( Value( "rc_cross_top_line" ) )
					surface()->DrawFilledRect( cx - half - o, cy - gap - size - o, cx - half + thick + o, cy - gap + o );
				if ( Value( "rc_cross_bottom_line" ) )
					surface()->DrawFilledRect( cx - half - o, cy + gap - o, cx - half + thick + o, cy + gap + size + o );
				if ( Value( "rc_cross_left_line" ) )
					surface()->DrawFilledRect( cx - gap - size - o, cy - half - o, cx - gap + o, cy - half + thick + o );
				if ( Value( "rc_cross_right_line" ) )
					surface()->DrawFilledRect( cx + gap - o, cy - half - o, cx + gap + size + o, cy - half + thick + o );
			}
		}

		if ( dotSize > 0 )
		{
			surface()->DrawSetColor( dot[0], dot[1], dot[2], dot[3] );
			surface()->DrawFilledRect( cx - dotSize / 2, cy - dotSize / 2, cx - dotSize / 2 + dotSize, cy - dotSize / 2 + dotSize );
		}

		if ( radius > 0 && thick > 0 )
		{
			surface()->DrawSetColor( color[0], color[1], color[2], color[3] );
			for ( int i = 0; i < 96; i++ )
			{
				float a = (float)i / 96.0f * 6.2831853f;
				int x = cx + (int)floor( cos( a ) * radius + 0.5f );
				int y = cy + (int)floor( sin( a ) * radius + 0.5f );
				surface()->DrawFilledRect( x - half, y - half, x - half + thick, y - half + thick );
			}
		}
	}

private:
	float Value( const char *cvar )
	{
		CRcOptionRow *row = m_pPage->FindRow( cvar );
		return row ? row->GetFloat() : 0.0f;
	}

	void Color( const char *cvar, int *rgb )
	{
		CRcOptionRow *row = m_pPage->FindRow( cvar );
		if ( !row )
			return;

		char value[64];
		row->GetValue( value, sizeof( value ) );
		int r, g, b;
		if ( sscanf( value, "%d %d %d", &r, &g, &b ) == 3 )
		{
			rgb[0] = r;
			rgb[1] = g;
			rgb[2] = b;
		}
	}

	CRcOptionsPage *m_pPage;
};

//-----------------------------------------------------------------------------
// The page of one tab
//-----------------------------------------------------------------------------
CRcOptionsPage::CRcOptionsPage( vgui::Panel *parent, const char *tab, const char *intro ) : BaseClass( parent, tab )
{
	m_nRows = 0;
	m_pszIntro = intro;
	m_pShownHelp = NULL;
	m_pPreview = NULL;

	m_pList = new CPanelListPanel( this, "Options" );

	for ( unsigned int i = 0; i < RC_NUM_OPTIONS && m_nRows < 40; i++ )
	{
		if ( stricmp( s_RcOptions[i].tab, tab ) )
			continue;

		CRcOptionRow *row = new CRcOptionRow( m_pList, &s_RcOptions[i], this );
		m_pList->AddItem( row );
		m_pRows[m_nRows++] = row;
	}

	if ( !stricmp( tab, "hud_crosshair" ) )
		m_pPreview = new CRcCrosshairPreview( this );

	m_pHelp = new Label( this, "Help", intro ? intro : "" );
	m_pHelp->SetWrap( true );
	m_pHelp->SetContentAlignment( Label::a_northwest );

	m_pDefaults = new Button( this, "Defaults", "Defaults" );
	m_pDefaults->SetCommand( "Defaults" );

	ivgui()->AddTickSignal( GetVPanel(), 100 );
}

void CRcOptionsPage::PerformLayout()
{
	BaseClass::PerformLayout();

	int wide, tall;
	GetSize( wide, tall );

	int listWide = wide - 16;
	if ( m_pPreview )
	{
		listWide -= 168;
		m_pPreview->SetBounds( wide - 168, 8, 160, 160 );
	}

	m_pList->SetBounds( 8, 8, listWide, tall - 64 );
	m_pHelp->SetBounds( 10, tall - 52, wide - 120, 46 );
	m_pDefaults->SetBounds( wide - 100, tall - 34, 90, 24 );

	// wrapped again at this width: a label keeps the wrap of the size it had when its text was set, and a page
	// is first laid out tiny, before its tab is shown
	m_pHelp->InvalidateLayout( true );
	m_pHelp->SetText( m_pShownHelp ? m_pShownHelp->help : ( m_pszIntro ? m_pszIntro : "" ) );
}

CRcOptionRow *CRcOptionsPage::FindRow( const char *cvar )
{
	for ( int i = 0; i < m_nRows; i++ )
	{
		if ( !stricmp( m_pRows[i]->GetOption()->cvar, cvar ) )
			return m_pRows[i];
	}

	return NULL;
}

// an opaque page: the window is see-through, and the menu's text behind it showed between the options
void CRcOptionsPage::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	Color bg = GetBgColor();
	SetBgColor( Color( bg.r(), bg.g(), bg.b(), 255 ) );
	SetPaintBackgroundEnabled( true );
}

void CRcOptionsPage::OnResetData()
{
	for ( int i = 0; i < m_nRows; i++ )
		m_pRows[i]->Load();

	if ( m_pPreview )
		m_pPreview->Repaint();
}

void CRcOptionsPage::OnApplyChanges()
{
	for ( int i = 0; i < m_nRows; i++ )
		m_pRows[i]->Apply();
}

void CRcOptionsPage::OnRcOptionChanged()
{
	if ( m_pPreview )
		m_pPreview->Repaint();

	PostActionSignal( new KeyValues( "ApplyButtonEnable" ) );
}

void CRcOptionsPage::OnCommand( const char *command )
{
	if ( !stricmp( command, "Defaults" ) )
	{
		for ( int i = 0; i < m_nRows; i++ )
			m_pRows[i]->SetDefault();

		if ( m_pPreview )
			m_pPreview->Repaint();
		return;
	}

	BaseClass::OnCommand( command );
}

// the help of the option under the mouse
void CRcOptionsPage::OnTick()
{
	BaseClass::OnTick();

	if ( !IsVisible() )
		return;

	int mx, my;
	input()->GetCursorPos( mx, my );

	// only the rows the list shows
	int lx = 0, ly = 0, lw, lt;
	m_pList->LocalToScreen( lx, ly );
	m_pList->GetSize( lw, lt );

	const RcOption *hovered = NULL;
	if ( mx >= lx && mx < lx + lw && my >= ly && my < ly + lt )
	{
		for ( int i = 0; i < m_nRows; i++ )
		{
			int x = 0, y = 0, w, t;
			m_pRows[i]->LocalToScreen( x, y );
			m_pRows[i]->GetSize( w, t );
			if ( mx >= x && mx < x + w && my >= y && my < y + t )
				hovered = m_pRows[i]->GetOption();
		}
	}

	if ( hovered == m_pShownHelp )
		return;

	m_pShownHelp = hovered;
	m_pHelp->SetText( hovered ? hovered->help : ( m_pszIntro ? m_pszIntro : "" ) );
}

//-----------------------------------------------------------------------------
// The sheet: passes the window's load and apply on to its pages
//-----------------------------------------------------------------------------
CRcOptionsSheet::CRcOptionsSheet( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	// the size it will have in the window, so its pages are first laid out at it: a slider first laid out tiny
	// keeps its captions cut short
	SetSize( 620, 400 );
	SetTabWidth( 92 );
}

// a page shown in a sub-tab gets its scheme and layout again: the original pages' sliders were laid out
// before their caption font came, which cut the captions short ("D..." for Dark)
void CRcOptionsSheet::ChangeActiveTab( int index )
{
	BaseClass::ChangeActiveTab( index );

	Panel *page = GetActivePage();
	if ( page && !dynamic_cast<CRcOptionsPage *>( page ) )
		page->InvalidateLayout( false, true );
}

void CRcOptionsSheet::OnResetData()
{
	ResetAllData();
}

void CRcOptionsSheet::OnApplyChanges()
{
	ApplyChanges();
}

//-----------------------------------------------------------------------------
// The ReCoF tab
//-----------------------------------------------------------------------------
static const char s_szRcAbout[] =
	"ReCoF " RECOF_VERSION "\n\n"
	"Cry of Fear 1.6a rebuilt from the DeCoF source, with bug fixes and improvements. Speedrun techniques are "
	"kept; the balance fixes go back to the original with Original balance (Game, Gameplay).\n\n"
	"github.com/caixax/ReCoF\n"
	"The ReCoF Launcher installs and updates it: github.com/caixax/ReCoF-Launcher\n\n"
	"Cry of Fear is by Team Psykskallar. Thanks to cd_n0 (RebuiltSimon), YaLTeR (BunnymodXT) and Valve.";

CRcAboutPage::CRcAboutPage( vgui::Panel *parent ) : BaseClass( parent, "about", "Point at an option to see what it does." )
{
	m_pText = new Label( this, "About", s_szRcAbout );
	m_pText->SetWrap( true );
	m_pText->SetContentAlignment( Label::a_northwest );
}

void CRcAboutPage::PerformLayout()
{
	BaseClass::PerformLayout();

	int wide, tall;
	GetSize( wide, tall );

	// the text above, the option below it
	m_pText->SetBounds( 12, 10, wide - 24, 170 );
	m_pText->InvalidateLayout( true );
	m_pText->SetText( s_szRcAbout );	// wrapped again at this width
	m_pList->SetBounds( 8, 186, wide - 16, tall - 186 - 64 );
}
