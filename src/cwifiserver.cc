#include <iostream> // cout
#include <cstdio> //perror
#include <cstring> // memcpy
#include <assert.h> // assert

#include <netlink/netlink.h> // struct nlmsghdr
#include <netlink/genl/genl.h> // struct nlattr

#include "hwsim.h" // HWSIM_ATTR_MAX

#include <arpa/inet.h> // struct sockaddr_in
#include <sys/socket.h> // AF_VSOCK / AF_INET
#include <linux/vm_sockets.h> // struct sockaddr_vm

#include <map>
#include <time.h> // clock_gettime

#include "crf.h"
#include "cwifiserver.h"
#include "tools.h"
#include "config.h" // LOST_PACKET_BY_DEFAULT

bool CanLostPackets=LOST_PACKET_BY_DEFAULT;

using namespace std;

// ---------------------------------------------------------------------------
// Walls
// ---------------------------------------------------------------------------

// 0 dB by default, which is the medium every existing scenario already runs
// against : one room, everybody hearing everybody. A household only starts
// costing anything once somebody sets a wall.
static CWall DefaultWall;

// Keyed with the lower household first, so one entry serves both directions --
// a wall is not one-way.
static map< pair<u32,u32>, CWall > Walls;

static pair<u32,u32> WallKey(u32 householdA, u32 householdB)
{
	if( householdA <= householdB )
		return make_pair(householdA,householdB);

	return make_pair(householdB,householdA);
}

void SetDefaultWall(const CWall& wall)
{
	DefaultWall=wall;
}

CWall GetDefaultWall()
{
	return DefaultWall;
}

void SetWall(u32 householdA, u32 householdB, const CWall& wall)
{
	if( householdA == householdB )
		return;

	Walls[WallKey(householdA,householdB)]=wall;
}

int WallAttenuationBetween(u32 householdA, u32 householdB, TFrequency frequencyMHz)
{
	if( householdA == householdB )
		return 0;

	map< pair<u32,u32>, CWall >::const_iterator it=Walls.find(WallKey(householdA,householdB));
	if( it != Walls.end() )
		return it->second.Loss(frequencyMHz);

	return DefaultWall.Loss(frequencyMHz);
}

void ResetWalls()
{
	Walls.clear();
	DefaultWall=CWall();
}

CWifiServer::CWifiServer() : CSocketServer ()
{
	DefaultValues();

	InfoWifis = new CListInfo<CInfoWifi>;
	InfoWifisDeconnected = new CListInfo<CInfoWifi>;
}

CWifiServer::CWifiServer(CListInfo<CInfoSocket>* infoSockets, CListInfo<CInfoWifi>* infoWifis, CListInfo<CInfoWifi>* infoWifisDeconnected) : CSocketServer (infoSockets)
{
	DefaultValues();

	InfoWifis = infoWifis;
	InfoWifisDeconnected = infoWifisDeconnected;
}

CWifiServer::CWifiServer( const CWifiServer & wifiServer ) : CSocketServer(wifiServer), CWifi(wifiServer)
{
	InfoWifis = nullptr;
	InfoWifisDeconnected = nullptr;
	*this=wifiServer;
}

CWifiServer::~CWifiServer()
{
	if( ListInfoSelfManaged )
	{
		delete InfoWifis;
		delete InfoWifisDeconnected;
	}
}

CWifiServer& CWifiServer::operator=(const CWifiServer& wifiServer)
{
	if( this != &wifiServer )
	{
		// protect against invalid self-assignment
		MaxClientDeconnected=wifiServer.MaxClientDeconnected;

		if( ListInfoSelfManaged )
		{
			if( InfoWifis != NULL )
			{
				delete InfoWifis;
				delete InfoWifisDeconnected;
			}
			InfoWifis = new CListInfo<CInfoWifi>(*(wifiServer.InfoWifis));
			InfoWifisDeconnected = new CListInfo<CInfoWifi>(*(wifiServer.InfoWifisDeconnected));
		}
		else
		{
			InfoWifis = wifiServer.InfoWifis;
			InfoWifisDeconnected = wifiServer.InfoWifisDeconnected;
		}
	}

	// by convention, always return *this
	return *this;
}

void CWifiServer::DefaultValues()
{
	MaxClientDeconnected=0;
}

bool CWifiServer::Listen(TIndex maxClientDeconnected)
{
	MaxClientDeconnected=maxClientDeconnected;

	if( ! _Listen(Master, Port) )
		return false;

	return true;
}

bool CWifiServer::RecoverInfosOfInfoWifiDeconnected(TCID cid, CInfoWifi& recovered)
{
	for (auto infoWifiDeconnected = InfoWifisDeconnected->begin(); infoWifiDeconnected != InfoWifisDeconnected->end(); ++infoWifiDeconnected)
	{
		if ( infoWifiDeconnected->GetCid() == cid )
		{
			recovered=*infoWifiDeconnected;

			InfoWifisDeconnected->erase(infoWifiDeconnected);

			return true;
		}
	}

	return false;
}

bool CWifiServer::RecoverInfosOfInfoWifi(TCID cid, CInfoWifi& recovered)
{
	int index=0;
	for (auto& infoWifi : *InfoWifis)
	{
		if( IsEnable(index) )
			if( infoWifi.GetCid() == cid )
			{
				recovered=infoWifi;

				DisableClient(index);

				return true;
			}
		index++;
	}

	return false;
}

TDescriptor CWifiServer::Accept()
{
	TCID cid;

	TDescriptor new_socket = CSocketServer::Accept(cid);
	if( new_socket ==  SOCKET_ERROR )
		return SOCKET_ERROR;

	CInfoWifi infoWifi;

	if( RecoverInfosOfInfoWifiDeconnected(cid,infoWifi) || RecoverInfosOfInfoWifi(cid,infoWifi) )
	{
		// Everything the server can hold on its own comes back : where the node
		// is, what it is called, which household it is in, the walls that
		// implies, its noise floors, the addresses it is known by and the
		// airtime its radios have accumulated. None of that needs the client to
		// agree, and a client that has merely reconnected has not moved house.
		//
		// The link state deliberately does not come back. It is the one piece
		// of state that lives on both sides -- cutting a link stops the relay
		// here and stops the fabricated acknowledgement over there -- and a
		// reconnected client is a new process that starts out acknowledging
		// itself and has been told nothing. Restoring only the server half
		// would leave a node the server refuses to relay for while it believes
		// its own transmissions are landing, which is worse than either state.
		// A link cut is re-applied by whoever cut it, or it is over.
		infoWifi.SetLinkUp(true);
	}

	infoWifi.SetCid(cid);

	InfoWifis->push_back(infoWifi);

	return new_socket;
}

void CWifiServer::ShowInfoWifi(TIndex index)
{
	assert( index < GetNumberClient() );

	cout<<(*InfoWifis)[index];
}

void CWifiServer::CloseClient(TIndex index)
{
	assert( index < GetNumberClient() );

	CSocketServer::CloseClient(index);

	// save the InfoWifi (the coordinate of the cid)
	AddInfoWifiDeconnected( (*InfoWifis)[index] );

	InfoWifis->erase(InfoWifis->begin()+index);
}

void CWifiServer::CloseAllClient()
{
	// ( be careful : "TIndex i" is a **unsigned** int : i=0 ; i-1 != -1 but = 65534 )

	TIndex nbre=GetNumberClient(); // because CloseClient changes the value of GetNumberClient()
	for (TIndex i = 0; i < nbre; i++)
		CloseClient(0); // we can Close the 0 because we use the shift
}

ssize_t CWifiServer::SendSignal(TDescriptor descriptor, VwifiRadioInfo* radio_info, const char* buffer, int sizeOfBuffer)
{
	return SendSignalWithSocket(this, descriptor, radio_info, buffer, sizeOfBuffer);
}

ssize_t CWifiServer::RecvSignal(TDescriptor descriptor, VwifiRadioInfo* radio_info, CDynBuffer* buffer)
{
	return RecvSignalWithSocket(this, descriptor, radio_info, buffer);
}

void CWifiServer::SendAllOtherClients(TIndex index, VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData)
{
    CInfoWifi& source = (*InfoWifis)[index];
    CCoordinate coo = source;

    // The power the medium propagates from, which is not always the one the
    // client reported. A client can only report what its regulatory domain
    // permits -- 12 dBm on 6GHz -- and that is not the EIRP of the device
    // being modelled. It also has to be comparable with the access point's:
    // a station quieter than the AP goes unheard well before it stops
    // hearing, and a controller that decides from what it hears then loses
    // the station exactly when the decision matters. Pinned by
    // "vwifi-ctrl power"; the propagation model itself is untouched.
    TPower txPower = static_cast<TPower>(radio_info->tx_power);
    source.PinnedTxPower(radio_info->radio_id, txPower);

    // What the transmitter is tuned to. This has always been on the wire with
    // every frame; what was missing was any idea of what the receivers were on.
    CChannel txChannel(radio_info->frequency, radio_info->channel_width);

    // Parse the relayed message once. This is the hottest loop in the server --
    // every frame from every client passes through it -- and it needs two
    // things out of the message, so walking it twice would be paying twice.
    struct nlattr* attrs[HWSIM_ATTR_MAX + 1];
    bool parsed = ParseMessage(reinterpret_cast<struct nlmsghdr*>(const_cast<char*>(data)), attrs);

    // The 802.11 frame inside the netlink envelope. Its length is what the
    // medium is occupied for, and its first byte is what says whether it is a
    // beacon. A message carrying no frame is not a transmission -- it costs no
    // airtime and cannot be a beacon -- but it is still relayed, because the
    // client's own dispatcher is what decides what to do with it.
    const char* frame = NULL;
    u32 sizeOfFrame = 0;
    bool hasFrame = parsed && GetFrameBodyFrom(attrs, frame, sizeOfFrame);

    // Beacons and probe responses together : see FrameIsBssPresence().
    bool isPresence = hasFrame && FrameIsBssPresence(frame, sizeOfFrame);
    bool isRobust = hasFrame && FrameIsManagement(frame, sizeOfFrame);

    u32 airtimeUs = hasFrame ? FrameAirtimeUs(sizeOfFrame, txChannel) : 0;

    // Only these need their transmitter resolved, and the message is already
    // parsed, so this is now just an attribute lookup.
    string transmitter;
    if (isPresence)
        transmitter = GetTransmitterFrom(attrs);

    if (airtimeUs)
        source.AccountOwnTransmission(*radio_info, airtimeUs);

    for (TIndex i = 0; i < GetNumberClient(); i++)
    {
        if (i == index || !IsEnable(i) || !ClientLinkIsUp(i))
            continue;

        CInfoWifi& destination = (*InfoWifis)[i];
        bool sameHousehold = (destination.GetHousehold() == source.GetHousehold());

        // Airtime first, and unconditionally. Every radio whose passband this
        // transmission touches loses that time whether or not it could decode
        // the frame, whether or not it is even the same household -- that is
        // what interference is, and accounting it only on delivery would make
        // a neighbouring household look free.
        if (airtimeUs)
            destination.AccumulateAirtime(txChannel, airtimeUs, sameHousehold);

        // hwsim drops a frame whose centre frequency does not match before it
        // looks at any signal metadata, so relaying across channels normally
        // only hands the far end something to discard.
        //
        // Management frames are the exception, and it is not an optimisation
        // detail -- it is the difference between a station that can find
        // another band and one that cannot. A radio's reported channel is its
        // *operating* channel, refreshed once a second; a scanning station
        // tunes away from it for a few tens of milliseconds at a time and
        // never says so. Gating on the reported channel therefore drops every
        // beacon and probe response from every other band, and an associated
        // station can only ever discover the band it is already on. Band
        // steering, roaming and any off-channel scan assertion are impossible
        // under that, and they fail in the worst way: the station looks like
        // it simply preferred to stay.
        //
        // Relaying them costs a little traffic and nothing in correctness --
        // hwsim still drops whatever the receiver was not tuned to when it
        // arrived, which is exactly the filtering a real radio does.
        if (!isRobust && !destination.CanReceiveOn(txChannel))
            continue;

        if (isPresence && !source.AreBeaconsRelayed(transmitter))
            continue;

        VwifiRadioInfo destination_info = *radio_info;

        destination_info.tx_power = BoundedPower(
                txPower
                - Attenuation(coo.DistanceWith(destination), destination_info.frequency)
                - WallAttenuationBetween(source.GetHousehold(), destination.GetHousehold(),
                                         txChannel.Centre));

        if (CanLostPackets
            && PacketIsLost(destination_info.tx_power,
                            destination.NoiseFloorOn(txChannel),
                            txChannel.Width,
                            isRobust))
            continue;

        if (SendSignal((*InfoSockets)[i].GetDescriptor(), &destination_info, data, sizeOfData) < 0)
            (*InfoSockets)[i].DisableIt();
    }
}

bool CWifiServer::LearnRadioState(TIndex index, const char* data, ssize_t sizeOfData)
{
	if( index >= GetNumberClient() )
		return false;

	VwifiRadioEntry radios[VWIFI_MAX_RADIOS_PER_CLIENT];
	u32 numberOfRadios=0;

	if( ! VwifiReadRadioState(data,sizeOfData,radios,numberOfRadios) )
		return false;

	for(u32 r=0; r<numberOfRadios; r++)
		(*InfoWifis)[index].ReportRadio(radios[r]);

	return true;
}

void CWifiServer::PushSurvey(u32 minimumIntervalMs)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC,&now);
	u64 nowMs=static_cast<u64>(now.tv_sec)*1000 + now.tv_nsec/1000000;

	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) )
			continue;

		CInfoWifi& infoWifi=(*InfoWifis)[i];

		VwifiSurveyEntry entries[VWIFI_MAX_RADIOS_PER_CLIENT];
		u32 count=0;

		const map<u32,CRadioState>& radios=infoWifi.GetRadios();
		for(map<u32,CRadioState>::const_iterator it=radios.begin();
		    it != radios.end() && count < VWIFI_MAX_RADIOS_PER_CLIENT; ++it)
		{
			// A radio with no channel cannot have a survey pushed at it : the
			// driver keys the override on the channel, and there is nothing
			// to name.
			if( ! it->second.Channel.IsKnown() )
				continue;

			CRadioState& radio=const_cast<CRadioState&>(it->second);

			u32 busy,rx,ext,tx;
			if( ! radio.TakeRates(nowMs,minimumIntervalMs,busy,rx,ext,tx) )
				continue;

			entries[count].radio_id=radio.RadioId;
			entries[count].frequency=radio.Channel.Centre;
			entries[count].busy_permille=busy;
			entries[count].rx_permille=rx;
			entries[count].ext_permille=ext;
			entries[count].tx_permille=tx;
			entries[count].noise=radio.NoiseFloor;
			count++;
		}

		if( count == 0 )
			continue;

		char buffer[sizeof(struct nlmsghdr) + sizeof(struct genlmsghdr) + sizeof(u32)
				+ VWIFI_MAX_RADIOS_PER_CLIENT*sizeof(VwifiSurveyEntry)];

		ssize_t size=VwifiWriteSurvey(buffer,sizeof(buffer),entries,count);
		if( size <= 0 )
			continue;

		VwifiRadioInfo radio_info{};

		if( SendSignal((*InfoSockets)[i].GetDescriptor(),&radio_info,buffer,size) < 0 )
			(*InfoSockets)[i].DisableIt();
	}
}

bool CWifiServer::SetHouseholdByMac(const string& mac, u32 household)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		(*InfoWifis)[i].SetHousehold(household);
		cout<<"household "<<household<<" : "; ShowInfoWifi(i); cout<<endl;
		return true;
	}

	return false;
}

bool CWifiServer::SetPositionByMac(const string& mac, TValue x, TValue y, TValue z)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		(*InfoWifis)[i].Set(x,y,z);
		cout<<"position : "; ShowInfoWifi(i); cout<<endl;
		return true;
	}

	return false;
}

bool CWifiServer::GetPositionByMac(const string& mac, TValue& x, TValue& y, TValue& z)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		x=(*InfoWifis)[i].GetX();
		y=(*InfoWifis)[i].GetY();
		z=(*InfoWifis)[i].GetZ();
		return true;
	}

	return false;
}

bool CWifiServer::SetBeaconsRelayedByMac(const string& mac, bool relayed)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		(*InfoWifis)[i].SetBeaconsRelayed(mac,relayed);

		// Unlike a link cut, nothing is told to the client. The point of a
		// beacon blackhole is that the transmitter goes on believing it is
		// beaconing normally -- and it is, into a medium that swallows them.
		cout<<"beacons "<<( relayed ? "relayed" : "swallowed" )<<" : "; ShowInfoWifi(i); cout<<endl;
		return true;
	}

	return false;
}

bool CWifiServer::SetAckFakingByMac(const string& mac, bool faking)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		// Nothing is recorded here : this is entirely a fact about the client,
		// and the client is the only thing that acts on it. Note that a later
		// "link up" or "link down" writes the same flag on the far side, so the
		// last of the two commands is the one in force.
		SendAckStateWithSocket(this, (*InfoSockets)[i].GetDescriptor(), faking);

		cout<<"fake ack "<<( faking ? "on" : "off" )<<" : "; ShowInfoWifi(i); cout<<endl;

		return true;
	}

	return false;
}

bool CWifiServer::SetNoiseFloorByMac(const string& mac, u32 radioId, int noiseFloorDbm)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		// A node that has not reported a radio yet has nothing to set the floor
		// on. Report that as a failure rather than silently accepting a value
		// that will be dropped by the next report.
		return ( (*InfoWifis)[i].SetNoiseFloor(radioId,noiseFloorDbm) > 0 );
	}

	return false;
}

bool CWifiServer::SetTxPowerByCid(TCID cid, u32 radioId, int txPowerDbm)
{
	CInfoWifi* infoWifi=GetReferenceOnInfoWifiByCID(cid);

	if( ! infoWifi )
		return false;

	// A node that has not reported a radio yet has nothing to pin the power on.
	return ( infoWifi->SetTxPower(radioId,txPowerDbm) > 0 );
}

void CWifiServer::SendAllOtherClientsWithoutLoss(TIndex index, VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( i != index && IsEnable(i) && ClientLinkIsUp(i) )
			if( SendSignal((*InfoSockets)[i].GetDescriptor(), radio_info, data, sizeOfData) < 0 )
				(*InfoSockets)[i].DisableIt();
	}
}

void CWifiServer::SendAllClientsWithoutLoss(VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
		if( IsEnable(i) && ClientLinkIsUp(i) )
			if( SendSignal((*InfoSockets)[i].GetDescriptor(), radio_info, data, sizeOfData) < 0 )
				(*InfoSockets)[i].DisableIt();
}

bool CWifiServer::ClientLinkIsUp(TIndex index) const
{
	if( index >= GetNumberClient() )
		return false;

	return (*InfoWifis)[index].IsLinkUp();
}

void CWifiServer::LearnTransmitter(TIndex index, char* data, ssize_t sizeOfData)
{
	if( index >= GetNumberClient() )
		return;

	if( sizeOfData < static_cast<ssize_t>(sizeof(struct nlmsghdr)) )
		return;

	string mac=GetTransmitter(reinterpret_cast<struct nlmsghdr*>(data));
	if( mac.empty() )
		return;

	(*InfoWifis)[index].LearnMac(mac);
}

bool CWifiServer::SetLinkStateByMac(const string& mac, bool up)
{
	for (TIndex i = 0; i < GetNumberClient(); i++)
	{
		if( ! IsEnable(i) || ! (*InfoWifis)[i].OwnsMac(mac) )
			continue;

		(*InfoWifis)[i].SetLinkUp(up);

		// Telling the client is the half that matters. Refusing to relay only
		// empties the medium; the station keeps believing it is associated
		// because its own vwifi-client answers every frame it sends with a
		// fabricated HWSIM_TX_STAT_ACK, which resets mac80211's connection
		// monitor. Cutting that off is what lets a beacon loss be noticed.
		SendLinkStateWithSocket(this, (*InfoSockets)[i].GetDescriptor(), up);

		cout<<"RF link "<<( up ? "up" : "down" )<<" : "; ShowInfoWifi(i); cout<<endl;

		return true;
	}

	return false;
}

CInfoWifi* CWifiServer::GetReferenceOnInfoWifiByCID(TCID cid) const
{
	for (auto& infoWifi : *InfoWifis)
	{
		if( infoWifi.GetCid() == cid )
			return &infoWifi;
	}

	return NULL;
}

CInfoWifi* CWifiServer::GetReferenceOnInfoWifiDeconnectedByCID(TCID cid) const
{
	for (auto& infoWifiDeconnected : *InfoWifisDeconnected)
	{
		if( infoWifiDeconnected.GetCid() == cid )
			return &infoWifiDeconnected;
	}

	return NULL;
}

CInfoWifi* CWifiServer::GetReferenceOnInfoWifiByIndex(TIndex index) const
{
	assert( index < GetNumberClient() );

	return &((*InfoWifis)[index]);
}

void CWifiServer::AddInfoWifiDeconnected(CInfoWifi infoWifi)
{
	if( InfoWifisDeconnected->size() >= MaxClientDeconnected )
		InfoWifisDeconnected->erase(InfoWifisDeconnected->begin());

	InfoWifisDeconnected->push_back(infoWifi);
}
