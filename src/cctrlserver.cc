#include <cstring> // strcpy

#include <net/ethernet.h> // ETH_ALEN

#include "cctrlserver.h"
#include "config.h" // MAX_SIZE_NAME


CCTRLServer::CCTRLServer(CWifiServer* wifiServerVTCP, CWifiServer* wifiServerITCP, CWifiServer* wifiServerSPY, CSelect* scheduler) : CSocketServer()
{
	WifiServerVTCP=wifiServerVTCP;
	WifiServerITCP=wifiServerITCP;
	WifiServerSPY=wifiServerSPY;
	Scheduler=scheduler;
}

CCTRLServer::~CCTRLServer()
{
	// Close any open connections
	CloseAllClient();
}

bool CCTRLServer::_Listen(TDescriptor& master, TPort port)
{
	return CSocketServerFunctionITCP::_Listen(master, port);
}

TDescriptor CCTRLServer::_Accept(TDescriptor master, TCID& cid)
{
	return CSocketServerFunctionITCP::_Accept(master, cid);
}

ssize_t CCTRLServer::Read(char* data, ssize_t sizeOfData)
{
		return CSocket::Read(GetSocketClient(0),data, sizeOfData);
}

ssize_t CCTRLServer::Send(char* data, ssize_t sizeOfData)
{
		return CSocket::Send(GetSocketClient(0),data, sizeOfData);
}

TOrder CCTRLServer::GetOrder()
{
	TOrder order;

	if( GetNumberClient() != 1 )
		return TORDER_NO;

	if( Read(reinterpret_cast<char*>(&order), sizeof(TOrder)) == SOCKET_ERROR )
		return TORDER_NO;

	return order;
}

bool CCTRLServer::SendCInfoWifi(CInfoWifi* infoWifi)
{
	TCID cid=infoWifi->GetCid();
	if( Send(reinterpret_cast<char*>(&cid),sizeof(cid)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendCInfoWifi : cid : "<<infoWifi->GetCid()<<endl;
		return false;
	}

	CCoordinate coo=(*infoWifi);
	if( Send(reinterpret_cast<char*>(&coo),sizeof(coo)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendCInfoWifi : CCoordinate : "<<infoWifi->GetCid()<<endl;
		return false;
	}

	int sizeName=infoWifi->GetSizeName();
	if( Send(reinterpret_cast<char*>(&sizeName),sizeof(sizeName)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendCInfoWifi : size of name : "<<infoWifi->GetCid()<<endl;
		return false;
	}
	if( sizeName > 0 )
	{
		char name[MAX_SIZE_NAME+1]; // +1 : \0
		strcpy(name,(infoWifi->GetName()).c_str());
		if( Send(name,sizeName+1) == SOCKET_ERROR ) // +1 : \0
		{
			cerr<<"Error : SendCInfoWifi : name : "<<infoWifi->GetCid()<<endl;
			return false;
		}
	}

	return true;
}

void CCTRLServer::SendList()
{
	// because the same List is shared by WifiServerVTCP and WifiServerITCP

	CInfoWifi* infoWifi;

	// Spies :

	TIndex number=WifiServerSPY->GetNumberClient();

	if( Send(reinterpret_cast<char*>(&number), sizeof(number)) == SOCKET_ERROR )
		return;

	for(TIndex i=0; i<number;i++)
	{
		if( WifiServerSPY->IsEnable(i) )
		{
			infoWifi=WifiServerSPY->GetReferenceOnInfoWifiByIndex(i);
			if( ! SendCInfoWifi(infoWifi) )
			{
				cerr<<"Error : SendList : Send : Spies : CInfoWifi : "<<*infoWifi<<endl;
				return;
			}
		}
	}

	// Clients

	number=WifiServerITCP->GetNumberClient();

	if( Send(reinterpret_cast<char*>(&number), sizeof(number)) == SOCKET_ERROR )
		return;

	for(TIndex i=0; i<number;i++)
	{
		if( WifiServerITCP->IsEnable(i) )
		{
			infoWifi=WifiServerITCP->GetReferenceOnInfoWifiByIndex(i);
			if( ! SendCInfoWifi(infoWifi) )
			{
				cerr<<"Error : SendList : Send : Clients : CInfoWifi : "<<*infoWifi<<endl;
				return;
			}
		}
	}
}

void CCTRLServer::ChangeCoordinate()
{
	TCID cid;

	if( Read(reinterpret_cast<char*>(&cid), sizeof(TCID)) == SOCKET_ERROR )
		return;

	CCoordinate coo;

	if( Read(reinterpret_cast<char*>(&coo), sizeof(coo)) == SOCKET_ERROR )
		return;

	if( cid < TCID_GUEST_MIN )
		return;

	// because the same List is shared by WifiServerVTCP and WifiServerITCP

	CInfoWifi* infoWifi;

	infoWifi=WifiServerITCP->GetReferenceOnInfoWifiByCID(cid);
	if( infoWifi != NULL )
	{
		infoWifi->Set(coo);
		return;
	}

	infoWifi=WifiServerITCP->GetReferenceOnInfoWifiDeconnectedByCID(cid);
	if( infoWifi != NULL )
	{
		infoWifi->Set(coo);
		return;
	}

	CInfoWifi infoNewWifi(cid,coo);
	WifiServerITCP->AddInfoWifiDeconnected(infoNewWifi);
}

void CCTRLServer::SetName()
{
	TCID cid;

	if( Read(reinterpret_cast<char*>(&cid), sizeof(TCID)) == SOCKET_ERROR )
		return;

	int sizeName;
	char strName[MAX_SIZE_NAME+1]; // +1 : \0

	if( Read(reinterpret_cast<char*>(&sizeName), sizeof(sizeName)) == SOCKET_ERROR )
		return;

	if( Read(reinterpret_cast<char*>(strName), sizeof(strName)) == SOCKET_ERROR )
		return;

	if( cid < TCID_GUEST_MIN )
		return;

	// because the same List is shared by WifiServerVTCP and WifiServerITCP

	string name(strName);

	CInfoWifi* infoWifi;

	infoWifi=WifiServerITCP->GetReferenceOnInfoWifiByCID(cid);
	if( infoWifi != NULL )
	{
		infoWifi->SetName(name);
		return;
	}

	infoWifi=WifiServerITCP->GetReferenceOnInfoWifiDeconnectedByCID(cid);
	if( infoWifi != NULL )
	{
		infoWifi->SetName(name);
		return;
	}

	infoWifi=WifiServerSPY->GetReferenceOnInfoWifiByCID(cid);
	if( infoWifi != NULL )
	{
		infoWifi->SetName(name);
		return;
	}
}

void CCTRLServer::ChangePacketLoss()
{
	int value;

	if( Read(reinterpret_cast<char*>(&value), sizeof(value)) == SOCKET_ERROR )
		return;

	if ( value )
	{
		#ifdef _DEBUG
			cout<<"Packet loss : Enable"<<endl;
		#endif
		CanLostPackets=true;
	}
	else
	{
		#ifdef _DEBUG
			cout<<"Packet loss : Disable"<<endl;
		#endif
		CanLostPackets=false;
	}
}

void CCTRLServer::SetLinkState()
{
	TByte mac[ETH_ALEN];

	if( Read(reinterpret_cast<char*>(mac), sizeof(mac)) == SOCKET_ERROR )
		return;

	int up;

	if( Read(reinterpret_cast<char*>(&up), sizeof(up)) == SOCKET_ERROR )
		return;

	// Spies are not on the air, so only real clients can have their link cut.
	int codeError = WifiServerITCP->SetLinkStateByMac(VwifiMacToString(mac), up != 0) ? 0 : -1;

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetLinkState : Send : code"<<endl;
}

void CCTRLServer::SetHousehold()
{
	TByte mac[ETH_ALEN];

	if( Read(reinterpret_cast<char*>(mac), sizeof(mac)) == SOCKET_ERROR )
		return;

	u32 household;

	if( Read(reinterpret_cast<char*>(&household), sizeof(household)) == SOCKET_ERROR )
		return;

	int codeError = WifiServerITCP->SetHouseholdByMac(VwifiMacToString(mac), household) ? 0 : -1;

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetHousehold : Send : code"<<endl;
}

void CCTRLServer::SetPosition()
{
	TByte mac[ETH_ALEN];

	if( Read(reinterpret_cast<char*>(mac), sizeof(mac)) == SOCKET_ERROR )
		return;

	TValue xyz[3];

	if( Read(reinterpret_cast<char*>(xyz), sizeof(xyz)) == SOCKET_ERROR )
		return;

	int codeError = WifiServerITCP->SetPositionByMac(VwifiMacToString(mac),
			xyz[0], xyz[1], xyz[2]) ? 0 : -1;

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetPosition : Send : code"<<endl;
}

void CCTRLServer::SetWall()
{
	u32 householdA;
	u32 householdB;
	double a;
	double b;
	u32 count;

	if( Read(reinterpret_cast<char*>(&householdA), sizeof(householdA)) == SOCKET_ERROR )
		return;

	if( Read(reinterpret_cast<char*>(&householdB), sizeof(householdB)) == SOCKET_ERROR )
		return;

	// The wall arrives as its two coefficients rather than as a loss, because
	// the loss depends on the frequency of the frame crossing it, and that is
	// not known until one does.
	if( Read(reinterpret_cast<char*>(&a), sizeof(a)) == SOCKET_ERROR )
		return;

	if( Read(reinterpret_cast<char*>(&b), sizeof(b)) == SOCKET_ERROR )
		return;

	if( Read(reinterpret_cast<char*>(&count), sizeof(count)) == SOCKET_ERROR )
		return;

	int codeError=0;

	CWall wall(a,b,count);

	// The same household on both sides means "every pair that has no wall of
	// its own", which is the knob a two-household scenario actually wants : one
	// wall, rather than an entry for each pair.
	// Qualified : CCTRLServer has a SetWall of its own -- this one -- and the
	// unqualified name finds that instead of the medium's.
	if( householdA == householdB )
		::SetDefaultWall(wall);
	else
		::SetWall(householdA,householdB,wall);

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetWall : Send : code"<<endl;
}

void CCTRLServer::SetNoiseFloor()
{
	TByte mac[ETH_ALEN];

	if( Read(reinterpret_cast<char*>(mac), sizeof(mac)) == SOCKET_ERROR )
		return;

	u32 radioId;
	int noiseFloor;

	if( Read(reinterpret_cast<char*>(&radioId), sizeof(radioId)) == SOCKET_ERROR )
		return;

	if( Read(reinterpret_cast<char*>(&noiseFloor), sizeof(noiseFloor)) == SOCKET_ERROR )
		return;

	int codeError = WifiServerITCP->SetNoiseFloorByMac(VwifiMacToString(mac), radioId, noiseFloor) ? 0 : -1;

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetNoiseFloor : Send : code"<<endl;
}

void CCTRLServer::SetBeaconState()
{
	TByte mac[ETH_ALEN];

	if( Read(reinterpret_cast<char*>(mac), sizeof(mac)) == SOCKET_ERROR )
		return;

	int relayed;

	if( Read(reinterpret_cast<char*>(&relayed), sizeof(relayed)) == SOCKET_ERROR )
		return;

	int codeError = WifiServerITCP->SetBeaconsRelayedByMac(VwifiMacToString(mac), relayed != 0) ? 0 : -1;

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetBeaconState : Send : code"<<endl;
}

void CCTRLServer::SetAckState()
{
	TByte mac[ETH_ALEN];

	if( Read(reinterpret_cast<char*>(mac), sizeof(mac)) == SOCKET_ERROR )
		return;

	int faking;

	if( Read(reinterpret_cast<char*>(&faking), sizeof(faking)) == SOCKET_ERROR )
		return;

	int codeError = WifiServerITCP->SetAckFakingByMac(VwifiMacToString(mac), faking != 0) ? 0 : -1;

	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
		cerr<<"Error : SetAckState : Send : code"<<endl;
}

void CCTRLServer::SendRadios()
{
	TIndex number=WifiServerITCP->GetNumberClient();

	if( Send(reinterpret_cast<char*>(&number), sizeof(number)) == SOCKET_ERROR )
		return;

	for(TIndex i=0; i<number; i++)
	{
		CInfoWifi* infoWifi=WifiServerITCP->GetReferenceOnInfoWifiByIndex(i);

		TCID cid=( infoWifi ? infoWifi->GetCid() : 0 );
		if( Send(reinterpret_cast<char*>(&cid),sizeof(cid)) == SOCKET_ERROR )
			return;

		u32 household=( infoWifi ? infoWifi->GetHousehold() : 0 );
		if( Send(reinterpret_cast<char*>(&household),sizeof(household)) == SOCKET_ERROR )
			return;

		u32 count=0;
		if( infoWifi )
			count=infoWifi->GetRadios().size();

		if( Send(reinterpret_cast<char*>(&count),sizeof(count)) == SOCKET_ERROR )
			return;

		if( ! infoWifi )
			continue;

		const map<u32,CRadioState>& radios=infoWifi->GetRadios();
		for(map<u32,CRadioState>::const_iterator it=radios.begin(); it != radios.end(); ++it)
		{
			// A flat record rather than the struct : CRadioState is the
			// server's own and is free to grow, and this is a wire format.
			u32 fields[3];
			fields[0]=it->second.RadioId;
			fields[1]=it->second.Channel.Centre;
			fields[2]=it->second.Channel.Width;

			if( Send(reinterpret_cast<char*>(fields),sizeof(fields)) == SOCKET_ERROR )
				return;

			int scalars[2];
			scalars[0]=it->second.TxPower;
			scalars[1]=it->second.NoiseFloor;

			if( Send(reinterpret_cast<char*>(scalars),sizeof(scalars)) == SOCKET_ERROR )
				return;

			u64 airtime[3];
			airtime[0]=it->second.TxUs;
			airtime[1]=it->second.RxUs;
			airtime[2]=it->second.ExtUs;

			if( Send(reinterpret_cast<char*>(airtime),sizeof(airtime)) == SOCKET_ERROR )
				return;
		}
	}
}

void CCTRLServer::SendStatus()
{
	if( Send(reinterpret_cast<char*>(&CanLostPackets),sizeof(CanLostPackets)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendStatus : Send : PacketLoss"<<endl;
		return;
	}

	if( Send(reinterpret_cast<char*>(&Scale),sizeof(Scale)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendStatus : Send : Scale"<<endl;
		return;
	}

#ifdef ENABLE_VHOST
	// VHOST
	if( Send(reinterpret_cast<char*>(&WifiServerVTCP->Port),sizeof(WifiServerVTCP->Port)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendStatus : Send : Port VHOST"<<endl;
		return;
	}
#endif

	// INET

	if( Send(reinterpret_cast<char*>(&WifiServerITCP->Port),sizeof(WifiServerITCP->Port)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendStatus : Send : Port INET"<<endl;
		return;
	}

	// SizeOfDisconnected
	// be careful : the same List is shared by WifiServerVTCP and WifiServerITCP

	if( Send(reinterpret_cast<char*>(&WifiServerITCP->MaxClientDeconnected),sizeof(WifiServerITCP->MaxClientDeconnected)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendStatus : Send : Size MaxClientDeconnected"<<endl;
		return;
	}

	// SPY

	bool spyIsConnected=( WifiServerSPY->GetNumberClient() > 0 );
	if( Send(reinterpret_cast<char*>(&spyIsConnected),sizeof(spyIsConnected)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendStatus : Send : spyIsConnected"<<endl;
		return;
	}
}

void CCTRLServer::SendShow()
{
	if( Send(reinterpret_cast<char*>(&CanLostPackets),sizeof(CanLostPackets)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendShow : Send : PacketLoss"<<endl;
		return;
	}

	if( Send(reinterpret_cast<char*>(&Scale),sizeof(Scale)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendShow : Send : Scale"<<endl;
		return;
	}

	bool spyIsConnected=( WifiServerSPY->GetNumberClient() > 0 );
	if( Send(reinterpret_cast<char*>(&spyIsConnected),sizeof(spyIsConnected)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendShow : Send : spyIsConnected"<<endl;
		return;
	}
}

void CCTRLServer::SendDistance()
{
	TCID cid1, cid2;

	if( Read(reinterpret_cast<char*>(&cid1), sizeof(TCID)) == SOCKET_ERROR )
		return;

	if( Read(reinterpret_cast<char*>(&cid2), sizeof(TCID)) == SOCKET_ERROR )
		return;

	int codeError;

	// because the same List is shared by WifiServerVTCP and WifiServerITCP

	CCoordinate* coo1;

	coo1=WifiServerITCP->GetReferenceOnInfoWifiByCID(cid1);
	if( coo1 == NULL )
	{
		coo1=WifiServerITCP->GetReferenceOnInfoWifiDeconnectedByCID(cid1);
		if( coo1 == NULL )
		{
			codeError=-1;
			if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
				cerr<<"Error : SendDistance : Send : unknown cid1"<<endl;
			return ;
		}
	}

	CCoordinate* coo2;

	coo2=WifiServerITCP->GetReferenceOnInfoWifiByCID(cid2);
	if( coo2 == NULL )
	{
		coo2=WifiServerITCP->GetReferenceOnInfoWifiDeconnectedByCID(cid2);
		if( coo2 == NULL )
		{
			codeError=-2;
			if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
				cerr<<"Error : SendDistance : Send : unknown cid2"<<endl;
			return ;
		}
	}

	codeError=0;
	if( Send(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendDistance : Send : no error"<<endl;
		return ;
	}

	TDistance distance=coo1->DistanceWith(*coo2);

	if( Send(reinterpret_cast<char*>(&distance),sizeof(distance)) == SOCKET_ERROR )
	{
		cerr<<"Error : SendDistance : Send : distance"<<endl;
		return;
	}
}

void CCTRLServer::SetScale()
{
	TScale new_scale;

	if( Read(reinterpret_cast<char*>(&new_scale), sizeof(new_scale)) == SOCKET_ERROR )
		return;

	Scale=new_scale;
}

void CCTRLServer::CloseAllClient()
{
	// because the same List is shared by WifiServerVTCP and WifiServerITCP

	// Clients :

	// be careful : In the Scheduler, i must delete only the nodes of WifiServer, not the node of the CTRLServer
	for (TIndex i = 0; i < WifiServerITCP->GetNumberClient(); i++)
		Scheduler->DelNode((*WifiServerITCP)[i]);

	WifiServerITCP->CloseAllClient();

	// Spies :

	for (TIndex i = 0; i < WifiServerSPY->GetNumberClient(); i++)
		Scheduler->DelNode((*WifiServerSPY)[i]);

	WifiServerSPY->CloseAllClient();
}

void CCTRLServer::ReceiveOrder()
{
	if ( Accept() == SOCKET_ERROR )
		return;

	TOrder order=GetOrder();

	switch( order )
	{
			case TORDER_NO : break ;

			case TORDER_LIST :
				SendList();
				break;

			case TORDER_CHANGE_COORDINATE :
				ChangeCoordinate();
				break;

			case TORDER_SETNAME :
				SetName();
				break;

			case TORDER_PACKET_LOSS :
				ChangePacketLoss();
				break;

			case TORDER_LINK :
				SetLinkState();
				break;

			case TORDER_HOUSEHOLD :
				SetHousehold();
				break;

			case TORDER_POSITION :
				SetPosition();
				break;

			case TORDER_WALL :
				SetWall();
				break;

			case TORDER_NOISE :
				SetNoiseFloor();
				break;

			case TORDER_BEACON :
				SetBeaconState();
				break;

			case TORDER_RADIOS :
				SendRadios();
				break;

			case TORDER_ACK :
				SetAckState();
				break;

			case TORDER_STATUS :
				SendStatus();
				break;

			case TORDER_SHOW :
				SendShow();
				break;

			case TORDER_DISTANCE_BETWEEN_CID :
				SendDistance();
				break;

			case TORDER_SET_SCALE :
				SetScale();
				break;

			case TORDER_CLOSE_ALL_CLIENT :
				CloseAllClient();
				break;
	}

	CloseClient(0);
}
