// ReCoF: the options window's own pages. Every option is one entry of a table (RcOptions.cpp): its tab, cvar,
// kind of control, text, help and default. A page lists the options of its tab as rows, shows the help of the
// row under the mouse and puts its tab back to the defaults; a sheet holds a group of pages as a second row of
// tabs inside the window's tabs, as BugfixedHL's options do.

#ifndef RCOPTIONS_H
#define RCOPTIONS_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertySheet.h>

namespace vgui = vgui2;

namespace vgui2
{
class Button;
class CheckButton;
class ComboBox;
class Label;
class Slider;
class TextEntry;
};

class CPanelListPanel;
class CRcCrosshairPreview;
struct RcOption;

//-----------------------------------------------------------------------------
// Purpose: one option: its name on the left, its control on the right
//-----------------------------------------------------------------------------
class CRcOptionRow : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CRcOptionRow, vgui::Panel );

public:
	// target gets "RcOptionChanged" when the player changes the value
	CRcOptionRow( vgui::Panel *parent, const RcOption *option, vgui::Panel *target );

	void		Load( void );			// from the cvar
	void		Apply( void );			// to the cvar, when it changed
	void		SetDefault( void );		// the control to the default, applied with the page
	void		GetValue( char *out, int size );
	float		GetFloat( void );
	const RcOption *GetOption( void ) { return m_pOption; }

	virtual void PerformLayout();

private:
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel );
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnSliderMoved, "SliderMoved", panel );

	void		SetValue( const char *value );
	void		UpdateValueLabel( void );
	void		Changed( void );

	const RcOption		*m_pOption;
	vgui::Panel			*m_pTarget;
	vgui::Label			*m_pLabel;
	vgui::CheckButton	*m_pCheck;
	vgui::ComboBox		*m_pCombo;
	vgui::Slider		*m_pSlider;
	vgui::Label			*m_pValue;		// the slider's value
	vgui::TextEntry		*m_pText;
	bool				m_bLoading;		// the control is being set, not changed by the player
	char				m_szLoaded[64];	// the cvar's value when loaded
};

// a row for the option of this cvar, for a page of its own (the mouse and audio pages)
CRcOptionRow *RcCreateOptionRow( vgui::Panel *parent, const char *cvar, vgui::Panel *target );

//-----------------------------------------------------------------------------
// Purpose: the options of one tab
//-----------------------------------------------------------------------------
class CRcOptionsPage : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( CRcOptionsPage, vgui::PropertyPage );

public:
	CRcOptionsPage( vgui::Panel *parent, const char *tab, const char *intro );

	virtual void OnResetData();
	virtual void OnApplyChanges();

	CRcOptionRow *FindRow( const char *cvar );

protected:
	virtual void PerformLayout();
	virtual void OnTick();
	virtual void OnCommand( const char *command );
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );

	MESSAGE_FUNC( OnRcOptionChanged, "RcOptionChanged" );

	CPanelListPanel		*m_pList;
	vgui::Label			*m_pHelp;
	vgui::Button		*m_pDefaults;
	CRcCrosshairPreview	*m_pPreview;	// the crosshair tab's
	CRcOptionRow		*m_pRows[40];
	int					m_nRows;
	const char			*m_pszIntro;	// the help when the mouse is on no option
	const RcOption		*m_pShownHelp;
};

//-----------------------------------------------------------------------------
// Purpose: a group of pages as a second row of tabs; passes the window's load and apply on to its pages
//-----------------------------------------------------------------------------
class CRcOptionsSheet : public vgui::PropertySheet
{
	DECLARE_CLASS_SIMPLE( CRcOptionsSheet, vgui::PropertySheet );

public:
	CRcOptionsSheet( vgui::Panel *parent, const char *name );

protected:
	virtual void ChangeActiveTab( int index );

private:
	MESSAGE_FUNC( OnResetData, "ResetData" );
	MESSAGE_FUNC( OnApplyChanges, "ApplyChanges" );
};

//-----------------------------------------------------------------------------
// Purpose: the ReCoF tab: the version, the Discord status option, where it comes from
//-----------------------------------------------------------------------------
class CRcAboutPage : public CRcOptionsPage
{
	DECLARE_CLASS_SIMPLE( CRcAboutPage, CRcOptionsPage );

public:
	CRcAboutPage( vgui::Panel *parent );

protected:
	virtual void PerformLayout();

private:
	vgui::Label *m_pText;
};

#endif // RCOPTIONS_H
