// ReCoF: the server list behind Join Server. Cry of Fear's own server browser (platform/servers) never shows a
// server any more: its LAN search drops Cry of Fear's servers (their replies carry the app id 223710 only in the
// extended part) and its Internet list gets no answers. This one reads Steam's master list for Cry of Fear
// servers through the game's steam_api.dll, finds the ones on the local network itself, lists the ReCoF ones
// and has a box to join an address.

#ifndef RCSERVERBROWSER_H
#define RCSERVERBROWSER_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Frame.h>

namespace vgui = vgui2;

namespace vgui2
{
class Button;
class CheckButton;
class ComboBox;
class Label;
class ListPanel;
class TextEntry;
};

class CRcServerListResponse;
class CRcServerList;
class KeyValues;

//-----------------------------------------------------------------------------
// Purpose: the Servers window
//-----------------------------------------------------------------------------
class CRcServerBrowserDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CRcServerBrowserDialog, vgui::Frame );

public:
	CRcServerBrowserDialog( vgui::Panel *parent );
	~CRcServerBrowserDialog();

	virtual void Activate();
	virtual void OnClose();

	// from the Steam reply object
	void ServerResponded( void *hRequest, int iServer );
	void RefreshComplete( void *hRequest, int response );

private:
	virtual void OnCommand( const char *command );
	virtual void OnTick();

	MESSAGE_FUNC( OnServerDoubleClicked, "ServerDoubleClicked" );
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel );

	void	StartRefresh( void );
	void	StopRefresh( void );
	void	JoinSelected( void );
	void	JoinAddress( const char *address );
	void	UpdateStatus( void );
	bool	IsLan( void );
	float	Now( void );

	// Internet: Steam's master list
	void	StartInternet( void );
	void	RebuildInternetList( void );

	// local network: a broadcast of the server info query, answered by the servers themselves
	void	StartLan( void );
	void	PollLan( void );
	void	StopLan( void );

	void	AddServer( const char *address, const char *name, const char *map, int players, int maxPlayers, int ping, bool password );

	CRcServerList			*m_pList;
	vgui::ComboBox			*m_pSource;		// Internet or the local network
	vgui::CheckButton		*m_pRecofOnly;
	vgui::Label				*m_pStatus;
	vgui::TextEntry			*m_pAddress;
	vgui::Button			*m_pJoin;

	CRcServerListResponse	*m_pResponse;
	void					*m_hRequest;		// the Steam server list request being filled, or NULL
	int						m_nLanSocket;		// the LAN query socket, or -1
	float					m_flLanLastSend;	// when the last LAN query left, for the ping
	bool					m_bRefreshing;
	int						m_nListed;
	float					m_flRefreshStart;
	float					m_flLastRebuild;
	bool					m_bQueryLan;		// what the running query asked for
	bool					m_bQueryRecof;
};

#endif // RCSERVERBROWSER_H
