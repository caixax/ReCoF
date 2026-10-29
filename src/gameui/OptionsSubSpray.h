// ReCoF: the Spray page of the options dialog, which turns a picture from the logos folder into the
// player's spray logo (tempdecal.wad)

#ifndef OPTIONS_SUB_SPRAY_H
#define OPTIONS_SUB_SPRAY_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/PropertyPage.h>

namespace vgui = vgui2;

namespace vgui2
{
class ComboBox;
class Label;
};

class CSprayPreviewPanel;

//-----------------------------------------------------------------------------
// Purpose: the spray logo, part of the options dialog
//-----------------------------------------------------------------------------
class COptionsSubSpray : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( COptionsSubSpray, vgui::PropertyPage );

public:
	COptionsSubSpray( vgui::Panel *parent );
	~COptionsSubSpray();

	virtual void OnResetData();
	virtual void OnApplyChanges();

private:
	virtual void OnCommand( const char *command );

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );

	void	FillImageList( void );
	void	ShowCurrentSpray( void );
	void	MakeSprayFromSelection( void );
	void	OpenLogosFolder( void );
	void	SetStatus( const char *text );

	vgui::ComboBox		*m_pImageList;
	vgui::ComboBox		*m_pSizeList;
	vgui::ComboBox		*m_pWallList;		// rc_spray_size
	vgui::Label			*m_pStatus;
	CSprayPreviewPanel	*m_pPreview;

	unsigned char		*m_pWad;			// the spray made from the selected picture, written on Apply
	int					m_nWadSize;
	char				m_szWadSource[64];	// the picture it was made from
	bool				m_bFilling;			// the lists are being filled: their changes are not the player's
};

#endif // OPTIONS_SUB_SPRAY_H
