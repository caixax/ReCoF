// ReCoF: the server list behind Join Server (see RcServerBrowser.h)

#include <windows.h>
#undef PostMessage
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "RcServerBrowser.h"
#include "EngineInterface.h"

#include <vgui/IVGui.h>
#include <vgui/KeyValues.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/ListPanel.h>
#include <vgui_controls/TextEntry.h>

using namespace vgui2;

#define RC_COF_APPID		223710		// Cry of Fear on Steam
#define RC_SERVER_TAG		"recof"		// set by ReCoF Server (SetGameTags), also in its info replies
#define RC_LAN_FIRST_PORT	27015		// the ports a LAN search asks, as Steam's
#define RC_LAN_LAST_PORT	27020
#define RC_LAN_SECONDS		2.5f		// how long the LAN search waits for answers
#define RC_NO_PING			99999		// sorts the servers that did not answer last

//-----------------------------------------------------------------------------
// The parts of Steam's ISteamMatchmakingServers (SteamMatchMakingServers002, the version the game's
// steam_api.dll returns) this list uses, with the layouts of the Steamworks headers
//-----------------------------------------------------------------------------
struct RcServerNetAdr
{
	unsigned short	m_usConnectionPort;	// host byte order
	unsigned short	m_usQueryPort;
	unsigned int	m_unIP;
};

class RcGameServerItem
{
public:
	RcServerNetAdr	m_NetAdr;
	int				m_nPing;
	bool			m_bHadSuccessfulResponse;
	bool			m_bDoNotRefresh;
	char			m_szGameDir[32];
	char			m_szMap[32];
	char			m_szGameDescription[64];
	unsigned int	m_nAppID;
	int				m_nPlayers;
	int				m_nMaxPlayers;
	int				m_nBotPlayers;
	bool			m_bPassword;
	bool			m_bSecure;
	unsigned int	m_ulTimeLastPlayed;
	int				m_nServerVersion;
	char			m_szServerName[64];
	char			m_szGameTags[128];
	unsigned __int64 m_steamID;
};

struct RcMatchKeyValue
{
	char m_szKey[256];
	char m_szValue[256];
};

class IRcServerListResponse
{
public:
	virtual void ServerResponded( void *hRequest, int iServer ) = 0;
	virtual void ServerFailedToRespond( void *hRequest, int iServer ) = 0;
	virtual void RefreshComplete( void *hRequest, int response ) = 0;
};

class IRcMatchmakingServers
{
public:
	virtual void *RequestInternetServerList( unsigned int iApp, RcMatchKeyValue **ppchFilters, unsigned int nFilters, IRcServerListResponse *pResponse ) = 0;
	virtual void *RequestLANServerList( unsigned int iApp, IRcServerListResponse *pResponse ) = 0;
	virtual void *RequestFriendsServerList( unsigned int iApp, RcMatchKeyValue **ppchFilters, unsigned int nFilters, IRcServerListResponse *pResponse ) = 0;
	virtual void *RequestFavoritesServerList( unsigned int iApp, RcMatchKeyValue **ppchFilters, unsigned int nFilters, IRcServerListResponse *pResponse ) = 0;
	virtual void *RequestHistoryServerList( unsigned int iApp, RcMatchKeyValue **ppchFilters, unsigned int nFilters, IRcServerListResponse *pResponse ) = 0;
	virtual void *RequestSpectatorServerList( unsigned int iApp, RcMatchKeyValue **ppchFilters, unsigned int nFilters, IRcServerListResponse *pResponse ) = 0;
	virtual void ReleaseRequest( void *hRequest ) = 0;
	virtual RcGameServerItem *GetServerDetails( void *hRequest, int iServer ) = 0;
	virtual void CancelQuery( void *hRequest ) = 0;
	virtual void RefreshQuery( void *hRequest ) = 0;
	virtual bool IsRefreshing( void *hRequest ) = 0;
	virtual int GetServerCount( void *hRequest ) = 0;
};

typedef IRcMatchmakingServers *( *SteamMatchmakingServers_t )( void );
typedef void ( *SteamAPI_RunCallbacks_t )( void );

static IRcMatchmakingServers *RcMatchmakingServers( void )
{
	HMODULE hSteamApi = GetModuleHandleA( "steam_api.dll" );
	if ( !hSteamApi )
		return NULL;

	SteamMatchmakingServers_t pfn = (SteamMatchmakingServers_t)GetProcAddress( hSteamApi, "SteamMatchmakingServers" );
	return pfn ? pfn() : NULL;
}

// The engine reads Steam's callbacks itself (Steam_BGetCallback), never through SteamAPI_RunCallbacks, which is
// what delivers a server list query's results: the list runs it while its query is open
static void RcRunSteamCallbacks( void )
{
	HMODULE hSteamApi = GetModuleHandleA( "steam_api.dll" );
	if ( !hSteamApi )
		return;

	SteamAPI_RunCallbacks_t pfn = (SteamAPI_RunCallbacks_t)GetProcAddress( hSteamApi, "SteamAPI_RunCallbacks" );
	if ( pfn )
		pfn();
}

//-----------------------------------------------------------------------------
// Purpose: receives Steam's replies and passes them to the window
//-----------------------------------------------------------------------------
class CRcServerListResponse : public IRcServerListResponse
{
public:
	CRcServerListResponse( CRcServerBrowserDialog *pDialog ) : m_pDialog( pDialog ) {}

	virtual void ServerResponded( void *hRequest, int iServer ) { m_pDialog->ServerResponded( hRequest, iServer ); }
	virtual void ServerFailedToRespond( void *hRequest, int iServer ) {}
	virtual void RefreshComplete( void *hRequest, int response ) { m_pDialog->RefreshComplete( hRequest, response ); }

private:
	CRcServerBrowserDialog *m_pDialog;
};

//-----------------------------------------------------------------------------
// Purpose: the list, which reports a double click on a server
//-----------------------------------------------------------------------------
class CRcServerList : public ListPanel
{
	DECLARE_CLASS_SIMPLE( CRcServerList, ListPanel );

public:
	CRcServerList( Panel *parent, const char *name ) : BaseClass( parent, name ) {}

	virtual void OnMouseDoublePressed( MouseCode code )
	{
		BaseClass::OnMousePressed( code );

		if ( code == MOUSE_LEFT && GetSelectedItemsCount() > 0 )
			PostActionSignal( new KeyValues( "ServerDoubleClicked" ) );
	}
};

static int RcCompareInt( const ListPanelItem &item1, const ListPanelItem &item2, const char *key )
{
	int a = item1.kv->GetInt( key );
	int b = item2.kv->GetInt( key );

	if ( a != b )
		return a < b ? -1 : 1;

	return stricmp( item1.kv->GetString( "name" ), item2.kv->GetString( "name" ) );
}

static int __cdecl RcSortByPing( ListPanel *pPanel, const ListPanelItem &item1, const ListPanelItem &item2 )
{
	return RcCompareInt( item1, item2, "pingvalue" );
}

static int __cdecl RcSortByPlayers( ListPanel *pPanel, const ListPanelItem &item1, const ListPanelItem &item2 )
{
	// most players first
	return -RcCompareInt( item1, item2, "playercount" );
}

// what an address may hold: a host name or IP and a port; nothing that could add a console command
static bool RcValidAddress( const char *address )
{
	if ( !address || !address[0] )
		return false;

	for ( const char *p = address; *p; p++ )
	{
		char c = *p;
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '.' || c == '-' || c == ':' ) )
			return false;
	}

	return true;
}

// reads a NUL-terminated string of an info reply; false when the reply ends first
static bool RcReadString( const char *&p, const char *end, char *out, int outSize )
{
	int n = 0;
	while ( p < end && *p )
	{
		if ( n < outSize - 1 )
			out[n++] = *p;
		p++;
	}
	out[n] = 0;

	if ( p >= end )
		return false;

	p++;	// the NUL
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CRcServerBrowserDialog::CRcServerBrowserDialog( vgui::Panel *parent ) : BaseClass( parent, "RcServerBrowser" )
{
	m_pResponse = new CRcServerListResponse( this );
	m_hRequest = NULL;
	m_nLanSocket = -1;
	m_flLanLastSend = 0;
	m_bRefreshing = false;
	m_nListed = 0;
	m_flRefreshStart = 0;
	m_flLastRebuild = 0;
	m_bQueryLan = false;
	m_bQueryRecof = true;

	SetSize( 640, 440 );
	SetSizeable( false );
	SetTitle( "Servers", true );

	m_pSource = new ComboBox( this, "Source", 2, false );
	m_pSource->AddItem( "Internet", NULL );
	m_pSource->AddItem( "Local network (LAN)", NULL );
	m_pSource->ActivateItemByRow( 0 );
	m_pSource->SetBounds( 16, 34, 200, 24 );
	m_pSource->AddActionSignalTarget( this );

	m_pRecofOnly = new CheckButton( this, "RecofOnly", "ReCoF servers only" );
	m_pRecofOnly->SetSelected( true );
	m_pRecofOnly->SetBounds( 228, 34, 200, 24 );
	m_pRecofOnly->AddActionSignalTarget( this );

	Button *button = new Button( this, "Refresh", "Refresh" );
	button->SetCommand( "Refresh" );
	button->SetBounds( 530, 34, 94, 24 );

	m_pList = new CRcServerList( this, "ServerList" );
	m_pList->AddColumnHeader( 0, "name", "Server", 300, 120, 600 );
	m_pList->AddColumnHeader( 1, "map", "Map", 130, 60, 300 );
	m_pList->AddColumnHeader( 2, "players", "Players", 70, 50, 120 );
	m_pList->AddColumnHeader( 3, "ping", "Ping", 60, 40, 120 );
	m_pList->SetSortFunc( 2, RcSortByPlayers );
	m_pList->SetSortFunc( 3, RcSortByPing );
	m_pList->SetSortColumn( 3 );
	m_pList->SetEmptyListText( "No servers yet." );
	m_pList->SetBounds( 16, 66, 608, 270 );
	m_pList->AddActionSignalTarget( this );

	m_pStatus = new Label( this, "Status", "" );
	m_pStatus->SetBounds( 16, 340, 608, 20 );

	Label *label = new Label( this, "AddressLabel", "Address:" );
	label->SetBounds( 16, 368, 64, 24 );

	m_pAddress = new TextEntry( this, "Address" );
	m_pAddress->SetBounds( 80, 368, 300, 24 );

	button = new Button( this, "JoinAddress", "Join address" );
	button->SetCommand( "JoinAddress" );
	button->SetBounds( 388, 368, 110, 24 );

	m_pJoin = new Button( this, "Join", "Join server" );
	m_pJoin->SetCommand( "Join" );
	m_pJoin->SetBounds( 420, 404, 100, 24 );

	button = new Button( this, "Close", "Close" );
	button->SetCommand( "Close" );
	button->SetBounds( 530, 404, 94, 24 );

	// often enough for a LAN ping; the tick does nothing while no search runs
	ivgui()->AddTickSignal( GetVPanel(), 15 );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CRcServerBrowserDialog::~CRcServerBrowserDialog()
{
	StopRefresh();
	delete m_pResponse;
}

//-----------------------------------------------------------------------------
// Purpose: shows the window and looks for servers
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::Activate()
{
	BaseClass::Activate();
	StartRefresh();
}

void CRcServerBrowserDialog::OnClose()
{
	StopRefresh();
	BaseClass::OnClose();
}

bool CRcServerBrowserDialog::IsLan( void )
{
	return m_pSource->GetActiveItem() == 1;
}

float CRcServerBrowserDialog::Now( void )
{
	return (float)GetTickCount() / 1000.0f;
}

//-----------------------------------------------------------------------------
// Purpose: a new search
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::StartRefresh( void )
{
	StopRefresh();
	m_pList->DeleteAllItems();
	m_nListed = 0;
	m_bQueryLan = IsLan();
	m_bQueryRecof = m_pRecofOnly->IsSelected();
	m_flRefreshStart = Now();
	m_flLastRebuild = 0;

	if ( m_bQueryLan )
		StartLan();
	else
		StartInternet();

	UpdateStatus();
}

void CRcServerBrowserDialog::StopRefresh( void )
{
	if ( m_hRequest )
	{
		IRcMatchmakingServers *pServers = RcMatchmakingServers();
		if ( pServers )
		{
			pServers->CancelQuery( m_hRequest );
			pServers->ReleaseRequest( m_hRequest );
		}

		m_hRequest = NULL;
	}

	StopLan();
	m_bRefreshing = false;
}

//-----------------------------------------------------------------------------
// Purpose: adds a row; ping -1 for a server Steam lists that did not answer
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::AddServer( const char *address, const char *name, const char *map, int players, int maxPlayers, int ping, bool password )
{
	char shownName[128];
	if ( ping < 0 )
		_snprintf( shownName, sizeof( shownName ), "(no answer) %s", name[0] ? name : address );
	else
		_snprintf( shownName, sizeof( shownName ), "%s%s", password ? "(password) " : "", name[0] ? name : address );
	shownName[sizeof( shownName ) - 1] = 0;

	char playerText[32];
	if ( maxPlayers > 0 )
		sprintf( playerText, "%d / %d", players, maxPlayers );
	else
		strcpy( playerText, "-" );

	char pingText[16];
	if ( ping < 0 )
		strcpy( pingText, "-" );
	else
		sprintf( pingText, "%d", ping );

	KeyValues *data = new KeyValues( "server" );
	data->SetString( "name", shownName );
	data->SetString( "map", map );
	data->SetString( "players", playerText );
	data->SetInt( "playercount", players );
	data->SetString( "ping", pingText );
	data->SetInt( "pingvalue", ping < 0 ? RC_NO_PING : ping );
	data->SetString( "address", address );
	m_pList->AddItem( data, 0, false, true );
	data->deleteThis();

	m_nListed++;
}

//-----------------------------------------------------------------------------
// Purpose: Internet: Steam's master list of Cry of Fear servers, the ReCoF ones when the box is ticked
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::StartInternet( void )
{
	IRcMatchmakingServers *pServers = RcMatchmakingServers();
	if ( !pServers )
	{
		m_pStatus->SetText( "Steam is not available: start Steam, then Refresh. Or join an address below." );
		return;
	}

	static RcMatchKeyValue filters[2];
	unsigned int nFilters = 0;

	strcpy( filters[nFilters].m_szKey, "gamedir" );
	strcpy( filters[nFilters].m_szValue, "cryoffear" );
	nFilters++;

	if ( m_bQueryRecof )
	{
		strcpy( filters[nFilters].m_szKey, "gametagsand" );
		strcpy( filters[nFilters].m_szValue, RC_SERVER_TAG );
		nFilters++;
	}

	RcMatchKeyValue *pFilters = filters;
	m_hRequest = pServers->RequestInternetServerList( RC_COF_APPID, &pFilters, nFilters, m_pResponse );
	m_bRefreshing = m_hRequest != NULL;
}

void CRcServerBrowserDialog::ServerResponded( void *hRequest, int iServer )
{
	if ( hRequest == m_hRequest )
		RebuildInternetList();
}

void CRcServerBrowserDialog::RefreshComplete( void *hRequest, int response )
{
	if ( hRequest != m_hRequest )
		return;

	m_bRefreshing = false;
	RebuildInternetList();
}

//-----------------------------------------------------------------------------
// Purpose: lists the servers of the query, read from Steam's results since the per-server replies do not
// always come. A server Steam lists that did not answer stays, marked: from inside the network a server
// runs on, its public address often does not answer (the router does not loop it back).
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::RebuildInternetList( void )
{
	IRcMatchmakingServers *pServers = RcMatchmakingServers();
	if ( !pServers || !m_hRequest )
		return;

	// keep the selected server selected
	char selected[32];
	selected[0] = 0;
	if ( m_pList->GetSelectedItemsCount() > 0 )
	{
		KeyValues *sel = m_pList->GetItem( m_pList->GetSelectedItem( 0 ) );
		if ( sel )
		{
			strncpy( selected, sel->GetString( "address" ), sizeof( selected ) - 1 );
			selected[sizeof( selected ) - 1] = 0;
		}
	}

	m_pList->DeleteAllItems();
	m_nListed = 0;
	m_flLastRebuild = Now();

	int count = pServers->GetServerCount( m_hRequest );
	for ( int i = 0; i < count; i++ )
	{
		RcGameServerItem *pItem = pServers->GetServerDetails( m_hRequest, i );
		if ( !pItem )
			continue;

		if ( stricmp( pItem->m_szGameDir, "cryoffear" ) )
			continue;

		if ( m_bQueryRecof && !strstr( pItem->m_szGameTags, RC_SERVER_TAG ) )
			continue;

		unsigned int ip = pItem->m_NetAdr.m_unIP;
		char address[32];
		sprintf( address, "%u.%u.%u.%u:%u", ( ip >> 24 ) & 0xFF, ( ip >> 16 ) & 0xFF, ( ip >> 8 ) & 0xFF, ip & 0xFF, pItem->m_NetAdr.m_usConnectionPort );

		// still waiting for its answer while the query runs
		if ( !pItem->m_bHadSuccessfulResponse && m_bRefreshing )
			continue;

		if ( pItem->m_bHadSuccessfulResponse )
			AddServer( address, pItem->m_szServerName, pItem->m_szMap, pItem->m_nPlayers - pItem->m_nBotPlayers, pItem->m_nMaxPlayers, pItem->m_nPing, pItem->m_bPassword );
		else
			AddServer( address, "", pItem->m_szMap, 0, 0, -1, false );

		if ( selected[0] && !strcmp( selected, address ) )
			m_pList->SetSingleSelectedItem( m_pList->GetItemIDFromRow( m_pList->GetItemCount() - 1 ) );
	}

	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose: local network: the info query broadcast on the ports servers use. Steam's own LAN search drops
// Cry of Fear's servers, whose replies carry the app id 223710 only in their extended part.
//-----------------------------------------------------------------------------
static const char s_szInfoQuery[] = "\xFF\xFF\xFF\xFFTSource Engine Query";	// sent with its NUL

// GetIpAddrTable (iphlpapi.dll), declared here as the old SDK this builds with has no header for it
struct RcIpAddrRow
{
	DWORD			dwAddr;
	DWORD			dwIndex;
	DWORD			dwMask;
	DWORD			dwBCastAddr;
	DWORD			dwReasmSize;
	unsigned short	unused1;
	unsigned short	wType;
};

typedef DWORD ( WINAPI *GetIpAddrTable_t )( void *pIpAddrTable, ULONG *pdwSize, BOOL bOrder );

// the limited broadcast and the broadcast address of each local IPv4 network, in network byte order
static int RcLanBroadcastAddresses( unsigned int *out, int max )
{
	int n = 0;
	out[n++] = INADDR_BROADCAST;

	HMODULE hIpHlp = LoadLibraryA( "iphlpapi.dll" );
	if ( !hIpHlp )
		return n;

	GetIpAddrTable_t pGetIpAddrTable = (GetIpAddrTable_t)GetProcAddress( hIpHlp, "GetIpAddrTable" );
	if ( pGetIpAddrTable )
	{
		ULONG size = 0;
		pGetIpAddrTable( NULL, &size, FALSE );
		if ( size )
		{
			unsigned char *table = (unsigned char *)malloc( size );
			if ( table && pGetIpAddrTable( table, &size, FALSE ) == NO_ERROR )
			{
				DWORD count = *(DWORD *)table;
				RcIpAddrRow *rows = (RcIpAddrRow *)( table + sizeof( DWORD ) );
				for ( DWORD i = 0; i < count && n < max; i++ )
				{
					DWORD addr = rows[i].dwAddr;
					DWORD mask = rows[i].dwMask;
					if ( !addr || ( ntohl( addr ) >> 24 ) == 127 || !mask || mask == 0xFFFFFFFF )
						continue;

					out[n++] = ( addr & mask ) | ~mask;
				}
			}
			free( table );
		}
	}

	FreeLibrary( hIpHlp );
	return n;
}

void CRcServerBrowserDialog::StartLan( void )
{
	SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( s == INVALID_SOCKET )
	{
		m_pStatus->SetText( "The local network search could not start. Join an address below." );
		return;
	}

	BOOL yes = TRUE;
	setsockopt( s, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof( yes ) );
	u_long nonBlocking = 1;
	ioctlsocket( s, FIONBIO, &nonBlocking );

	// 255.255.255.255 leaves through one adapter only, which on a PC with WSL, Hyper-V or a VPN is often not
	// the local network's: the query also goes to the broadcast address of each local network
	unsigned int targets[16];
	int nTargets = RcLanBroadcastAddresses( targets, 16 );

	sockaddr_in to;
	memset( &to, 0, sizeof( to ) );
	to.sin_family = AF_INET;
	for ( int t = 0; t < nTargets; t++ )
	{
		to.sin_addr.s_addr = targets[t];
		for ( int port = RC_LAN_FIRST_PORT; port <= RC_LAN_LAST_PORT; port++ )
		{
			to.sin_port = htons( (u_short)port );
			sendto( s, s_szInfoQuery, sizeof( s_szInfoQuery ), 0, (sockaddr *)&to, sizeof( to ) );
		}
	}

	m_nLanSocket = (int)s;
	m_flLanLastSend = Now();
	m_bRefreshing = true;
}

void CRcServerBrowserDialog::StopLan( void )
{
	if ( m_nLanSocket == -1 )
		return;

	closesocket( (SOCKET)m_nLanSocket );
	m_nLanSocket = -1;
}

//-----------------------------------------------------------------------------
// Purpose: reads the answers: a challenge is asked again with it, a server info reply is listed
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::PollLan( void )
{
	if ( m_nLanSocket == -1 )
		return;

	SOCKET s = (SOCKET)m_nLanSocket;
	char buf[1400];
	sockaddr_in from;
	int fromLen = sizeof( from );
	int n;

	while ( ( n = recvfrom( s, buf, sizeof( buf ), 0, (sockaddr *)&from, &fromLen ) ) > 0 )
	{
		fromLen = sizeof( from );

		if ( n < 6 || *(int *)buf != -1 )
			continue;

		// a challenge: the same query with the challenge after it
		if ( buf[4] == 'A' && n >= 9 )
		{
			char query[sizeof( s_szInfoQuery ) + 4];
			memcpy( query, s_szInfoQuery, sizeof( s_szInfoQuery ) );
			memcpy( query + sizeof( s_szInfoQuery ), buf + 5, 4 );
			sendto( s, query, sizeof( query ), 0, (sockaddr *)&from, sizeof( from ) );
			m_flLanLastSend = Now();
			continue;
		}

		if ( buf[4] != 'I' )
			continue;

		const char *p = buf + 6;	// after the header and the protocol byte
		const char *end = buf + n;
		char name[64], map[32], folder[32], game[64], version[32];
		if ( !RcReadString( p, end, name, sizeof( name ) ) || !RcReadString( p, end, map, sizeof( map ) ) ||
			!RcReadString( p, end, folder, sizeof( folder ) ) || !RcReadString( p, end, game, sizeof( game ) ) )
			continue;

		if ( end - p < 9 )
			continue;

		p += 2;	// app id (0 for Cry of Fear: it does not fit)
		int players = (unsigned char)p[0];
		int maxPlayers = (unsigned char)p[1];
		int bots = (unsigned char)p[2];
		bool password = p[5] != 0;
		p += 7;	// players, max, bots, type, environment, visibility, VAC

		if ( !RcReadString( p, end, version, sizeof( version ) ) )
			version[0] = 0;

		// the extended part: the game port and the tags
		int gamePort = ntohs( from.sin_port );
		char tags[128];
		tags[0] = 0;
		if ( p < end )
		{
			unsigned char edf = (unsigned char)*p++;
			if ( ( edf & 0x80 ) && end - p >= 2 )
			{
				gamePort = (unsigned char)p[0] | ( (unsigned char)p[1] << 8 );
				p += 2;
			}
			if ( edf & 0x10 )
				p += 8;
			if ( ( edf & 0x40 ) && end - p >= 2 )
			{
				char spectatorName[64];
				p += 2;
				RcReadString( p, end, spectatorName, sizeof( spectatorName ) );
			}
			if ( ( edf & 0x20 ) && p < end )
				RcReadString( p, end, tags, sizeof( tags ) );
		}

		if ( stricmp( folder, "cryoffear" ) )
			continue;

		if ( m_bQueryRecof && !strstr( tags, RC_SERVER_TAG ) )
			continue;

		unsigned int ip = ntohl( from.sin_addr.s_addr );
		char address[32];
		sprintf( address, "%u.%u.%u.%u:%d", ( ip >> 24 ) & 0xFF, ( ip >> 16 ) & 0xFF, ( ip >> 8 ) & 0xFF, ip & 0xFF, gamePort );

		// one row per server, even when it answers twice
		bool listed = false;
		for ( int row = 0; row < m_pList->GetItemCount(); row++ )
		{
			KeyValues *data = m_pList->GetItem( m_pList->GetItemIDFromRow( row ) );
			if ( data && !strcmp( data->GetString( "address" ), address ) )
				listed = true;
		}

		if ( !listed )
		{
			int ping = (int)( ( Now() - m_flLanLastSend ) * 1000.0f );
			AddServer( address, name, map, players - bots, maxPlayers, ping, password );
			UpdateStatus();
		}
	}
}

void CRcServerBrowserDialog::UpdateStatus( void )
{
	char text[256];

	if ( m_bRefreshing )
		sprintf( text, "Looking for servers... %d found", m_nListed );
	else if ( m_nListed )
		sprintf( text, "%d server%s. Double click one to join it.", m_nListed, m_nListed == 1 ? "" : "s" );
	else if ( m_bQueryLan )
		strcpy( text, m_bQueryRecof ? "No ReCoF server on the local network." : "No Cry of Fear server on the local network." );
	else if ( m_bQueryRecof )
		strcpy( text, "No ReCoF server is online right now. Join one by its address below." );
	else
		strcpy( text, "No Cry of Fear server is online right now." );

	m_pStatus->SetText( text );
}

//-----------------------------------------------------------------------------
// Purpose: runs the search while it is open
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::OnTick()
{
	BaseClass::OnTick();

	if ( !m_bRefreshing || !IsVisible() )
		return;

	float elapsed = Now() - m_flRefreshStart;

	if ( m_bQueryLan )
	{
		PollLan();
		if ( elapsed > RC_LAN_SECONDS )
		{
			StopLan();
			m_bRefreshing = false;
			UpdateStatus();
		}
		return;
	}

	if ( !m_hRequest )
		return;

	RcRunSteamCallbacks();

	if ( Now() - m_flLastRebuild > 1.0f )
		RebuildInternetList();

	// Steam's query is done even when no completion came
	IRcMatchmakingServers *pServers = RcMatchmakingServers();
	if ( pServers && !pServers->IsRefreshing( m_hRequest ) && elapsed > 3.0f )
	{
		m_bRefreshing = false;
		RebuildInternetList();
	}
}

//-----------------------------------------------------------------------------
// Purpose: joins
//-----------------------------------------------------------------------------
void CRcServerBrowserDialog::JoinAddress( const char *address )
{
	if ( !RcValidAddress( address ) )
	{
		m_pStatus->SetText( "That is not a server address: an IP or a name, and the port, for example 203.0.113.5:27015." );
		return;
	}

	char command[128];
	_snprintf( command, sizeof( command ), "connect %s\n", address );
	command[sizeof( command ) - 1] = 0;

	OnClose();
	engine->pfnClientCmd( command );
}

void CRcServerBrowserDialog::JoinSelected( void )
{
	if ( m_pList->GetSelectedItemsCount() < 1 )
	{
		m_pStatus->SetText( "Pick a server in the list first." );
		return;
	}

	KeyValues *data = m_pList->GetItem( m_pList->GetSelectedItem( 0 ) );
	if ( data )
		JoinAddress( data->GetString( "address" ) );
}

void CRcServerBrowserDialog::OnServerDoubleClicked()
{
	JoinSelected();
}

void CRcServerBrowserDialog::OnCommand( const char *command )
{
	if ( !stricmp( command, "Refresh" ) )
	{
		StartRefresh();
	}
	else if ( !stricmp( command, "Join" ) )
	{
		JoinSelected();
	}
	else if ( !stricmp( command, "JoinAddress" ) )
	{
		char address[64];
		m_pAddress->GetText( address, sizeof( address ) );
		JoinAddress( address );
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}

void CRcServerBrowserDialog::OnTextChanged( Panel *panel )
{
	// a new source: Internet or the LAN (the window's own setup sends one too, which changes nothing)
	if ( panel == m_pSource && IsVisible() && IsLan() != m_bQueryLan )
		StartRefresh();
}

void CRcServerBrowserDialog::OnCheckButtonChecked( Panel *panel )
{
	if ( panel == m_pRecofOnly && IsVisible() && m_pRecofOnly->IsSelected() != m_bQueryRecof )
		StartRefresh();
}
