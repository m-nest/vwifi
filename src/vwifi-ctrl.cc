#include <iostream> // cout
#include <memory> // unique_ptr

#include <stdlib.h> // atof
#include <string.h> //strlen

#include <net/ethernet.h> // ETH_ALEN

#include "config.h"
#include "tools.h" // isInt isPositiveInt isIntOrFloat
#include "addinterfaces.h" // ParseAddress
#include "crf.h" // CWall, WallMaterialByName
#include "cwifi.h" // VwifiMacToString
#include "csocketclientitcp.h"
#include "types.h"
#include "ccoordinate.h" // CCoordinate
#include "cinfowifi.h"
#include "cwifiserver.h" // SetWall / CWall helpers

using namespace std;

std::string IP_Ctrl = std::string(DEFAULT_ADDRESS_IP);
TPort Port_Ctrl = DEFAULT_CTRL_PORT;

char* NameOfProg;

void Help();

// Every one of these follows the shape "link" established : connect, send the
// order, send its arguments, read one status code back. The code is 0 or -1,
// and -1 always means the same thing -- no connected client has ever
// transmitted from that MAC, so the server has nothing to apply it to.
static int SendOrderWithMacAndValue(const char* what, TOrder order, const TByte* mac,
		const void* value, size_t sizeOfValue)
{
	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : "<<what<<" : socket.Connect error"<<endl;
		return 1;
	}

	if( socket.Send(reinterpret_cast<char*>(&order),sizeof(order)) == SOCKET_ERROR )
	{
		cerr<<"Error : "<<what<<" : socket.Send : order"<<endl;
		return 1;
	}

	if( socket.Send(reinterpret_cast<char*>(const_cast<TByte*>(mac)),ETH_ALEN) == SOCKET_ERROR )
	{
		cerr<<"Error : "<<what<<" : socket.Send : MAC"<<endl;
		return 1;
	}

	if( socket.Send(reinterpret_cast<char*>(const_cast<void*>(value)),sizeOfValue) == SOCKET_ERROR )
	{
		cerr<<"Error : "<<what<<" : socket.Send : value"<<endl;
		return 1;
	}

	int codeError;
	if( socket.Read(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
	{
		cerr<<"Error : "<<what<<" : socket.Read : code"<<endl;
		return 1;
	}

	socket.Close();

	if( codeError )
	{
		cerr<<"Error : "<<what<<" : no connected Client has transmitted from "<<VwifiMacToString(mac)<<endl;
		return 1;
	}

	return 0;
}

int SetHousehold(int argc, char** argv)
{
	if( argc != 3 )
	{
		cerr<<"Error : household : the number of parameter is uncorrect"<<endl;
		Help();
		return 1;
	}

	TByte mac[ETH_ALEN];
	if( ParseAddress(argv[1],mac) != ETH_ALEN )
	{
		cerr<<"Error : household : \""<<argv[1]<<"\" is not a MAC address"<<endl;
		return 1;
	}

	if( ! isPositiveInt(argv[2]) )
	{
		cerr<<"Error : household : \""<<argv[2]<<"\" is not a household number"<<endl;
		return 1;
	}

	u32 household=static_cast<u32>(stoi(argv[2]));

	if( SendOrderWithMacAndValue("household",TORDER_HOUSEHOLD,mac,&household,sizeof(household)) )
		return 1;

	cout<<VwifiMacToString(mac)<<" : household "<<household<<endl;

	return 0;
}

// "position MAC" reads, "position MAC X Y Z" writes.
static int GetPosition(const TByte* mac)
{
	CSocketClientITCP socket;
	TOrder order=TORDER_GET_POSITION;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : position : socket.Connect error"<<endl;
		return 1;
	}

	if( socket.Send(reinterpret_cast<char*>(&order),sizeof(order)) == SOCKET_ERROR ||
	    socket.Send(reinterpret_cast<char*>(const_cast<TByte*>(mac)),ETH_ALEN) == SOCKET_ERROR )
	{
		cerr<<"Error : position : socket.Send"<<endl;
		return 1;
	}

	int codeError;
	if( socket.Read(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
	{
		cerr<<"Error : position : socket.Read : code"<<endl;
		return 1;
	}

	if( codeError )
	{
		cerr<<"Error : position : no connected Client has transmitted from "
		    <<VwifiMacToString(const_cast<TByte*>(mac))<<endl;
		return 1;
	}

	TValue xyz[3];
	if( socket.Read(reinterpret_cast<char*>(xyz),sizeof(xyz)) == SOCKET_ERROR )
	{
		cerr<<"Error : position : socket.Read : coordinates"<<endl;
		return 1;
	}

	socket.Close();

	cout<<VwifiMacToString(const_cast<TByte*>(mac))<<" : position "
	    <<xyz[0]<<" "<<xyz[1]<<" "<<xyz[2]<<endl;

	return 0;
}

int SetPosition(int argc, char** argv)
{
	if( argc != 2 && argc != 5 )
	{
		cerr<<"Error : position : the number of parameter is uncorrect"<<endl;
		Help();
		return 1;
	}

	TByte mac[ETH_ALEN];
	if( ParseAddress(argv[1],mac) != ETH_ALEN )
	{
		cerr<<"Error : position : \""<<argv[1]<<"\" is not a MAC address"<<endl;
		return 1;
	}

	if( argc == 2 )
		return GetPosition(mac);

	// Coordinates may be negative, so isPositiveInt() is not the check here.
	TValue xyz[3];
	for(int i=0; i<3; i++)
	{
		try
		{
			xyz[i]=static_cast<TValue>(stoi(argv[2+i]));
		}
		catch(...)
		{
			cerr<<"Error : position : \""<<argv[2+i]<<"\" is not a coordinate"<<endl;
			return 1;
		}
	}

	if( SendOrderWithMacAndValue("position",TORDER_POSITION,mac,xyz,sizeof(xyz)) )
		return 1;

	cout<<VwifiMacToString(mac)<<" : position "<<xyz[0]<<" "<<xyz[1]<<" "<<xyz[2]<<endl;

	return 0;
}

int SetWall(int argc, char** argv)
{
	if( argc != 4 && argc != 5 )
	{
		cerr<<"Error : wall : the number of parameter is uncorrect"<<endl;
		Help();
		return 1;
	}

	if( ! isPositiveInt(argv[1]) || ! isPositiveInt(argv[2]) )
	{
		cerr<<"Error : wall : the households must be positive numbers"<<endl;
		return 1;
	}

	u32 householdA=static_cast<u32>(stoi(argv[1]));
	u32 householdB=static_cast<u32>(stoi(argv[2]));

	// A plain number is a loss that does not vary with frequency : predictable,
	// and what this command used to mean. A material name is the physical
	// thing, whose loss rises with frequency.
	CWall wall;
	bool isMaterial=false;

	if( isPositiveInt(argv[3]) )
	{
		wall=CWall(atof(argv[3]),0.0,1);
	}
	else if( WallMaterialByName(argv[3],wall) )
	{
		isMaterial=true;
	}
	else
	{
		cerr<<"Error : wall : \""<<argv[3]<<"\" is neither a positive number of dB"
		    <<" nor one of : "<<WallMaterialNames()<<endl;
		return 1;
	}

	if( argc == 5 )
	{
		if( ! isPositiveInt(argv[4]) || stoi(argv[4]) < 1 )
		{
			cerr<<"Error : wall : the count must be a positive number of walls"<<endl;
			return 1;
		}
		wall.Count=static_cast<u32>(stoi(argv[4]));
	}

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : wall : socket.Connect error"<<endl;
		return 1;
	}

	TOrder order=TORDER_WALL;
	if( socket.Send(reinterpret_cast<char*>(&order),sizeof(order)) == SOCKET_ERROR ||
	    socket.Send(reinterpret_cast<char*>(&householdA),sizeof(householdA)) == SOCKET_ERROR ||
	    socket.Send(reinterpret_cast<char*>(&householdB),sizeof(householdB)) == SOCKET_ERROR ||
	    socket.Send(reinterpret_cast<char*>(&wall.A),sizeof(wall.A)) == SOCKET_ERROR ||
	    socket.Send(reinterpret_cast<char*>(&wall.B),sizeof(wall.B)) == SOCKET_ERROR ||
	    socket.Send(reinterpret_cast<char*>(&wall.Count),sizeof(wall.Count)) == SOCKET_ERROR )
	{
		cerr<<"Error : wall : socket.Send"<<endl;
		return 1;
	}

	int codeError;
	if( socket.Read(reinterpret_cast<char*>(&codeError),sizeof(codeError)) == SOCKET_ERROR )
	{
		cerr<<"Error : wall : socket.Read : code"<<endl;
		return 1;
	}

	socket.Close();

	if( householdA == householdB )
		cout<<"wall between any two households : ";
	else
		cout<<"wall between household "<<householdA<<" and "<<householdB<<" : ";

	if( isMaterial )
		cout<<argv[3];
	else
		cout<<wall.A<<" dB";

	if( wall.Count > 1 )
		cout<<" x"<<wall.Count;

	// Print what it comes to on each band. A material is a pair of
	// coefficients, and nobody reads dB out of those at a glance -- while the
	// gap between the bands is the whole reason for setting one.
	cout<<" -> "<<wall.Loss(2412)<<" dB @2.4GHz, "
	    <<wall.Loss(5180)<<" dB @5GHz, "
	    <<wall.Loss(6135)<<" dB @6GHz"<<endl;

	return 0;
}

int SetNoiseFloor(int argc, char** argv)
{
	if( argc != 3 && argc != 4 )
	{
		cerr<<"Error : noise : the number of parameter is uncorrect"<<endl;
		Help();
		return 1;
	}

	TByte mac[ETH_ALEN];
	if( ParseAddress(argv[1],mac) != ETH_ALEN )
	{
		cerr<<"Error : noise : \""<<argv[1]<<"\" is not a MAC address"<<endl;
		return 1;
	}

	int noiseFloor=atoi(argv[2]);
	if( noiseFloor >= 0 )
	{
		cerr<<"Error : noise : a noise floor is a negative dBm value"<<endl;
		return 1;
	}

	// Without a radio id the floor goes on every radio the node has, which is
	// what a single-radio station wants and what a "make this whole node deaf"
	// scenario wants too.
	u32 radioId=RADIO_ID_ALL;
	if( argc == 4 )
	{
		if( ! isPositiveInt(argv[3]) )
		{
			cerr<<"Error : noise : \""<<argv[3]<<"\" is not a radio id"<<endl;
			return 1;
		}
		radioId=static_cast<u32>(stoi(argv[3]));
	}

	struct { u32 radioId; int noiseFloor; } payload;
	payload.radioId=radioId;
	payload.noiseFloor=noiseFloor;

	if( SendOrderWithMacAndValue("noise",TORDER_NOISE,mac,&payload,sizeof(payload)) )
		return 1;

	cout<<VwifiMacToString(mac)<<" : noise floor "<<noiseFloor<<" dBm";
	if( radioId != RADIO_ID_ALL )
		cout<<" on radio "<<radioId;
	cout<<endl;

	return 0;
}

int SetBeaconState(int argc, char** argv)
{
	if( argc != 3 )
	{
		cerr<<"Error : beacon : the number of parameter is uncorrect"<<endl;
		Help();
		return 1;
	}

	TByte mac[ETH_ALEN];
	if( ParseAddress(argv[1],mac) != ETH_ALEN )
	{
		cerr<<"Error : beacon : \""<<argv[1]<<"\" is not a MAC address"<<endl;
		return 1;
	}

	int relayed;
	if( ! strcasecmp(argv[2],"on") )
		relayed=1;
	else if( ! strcasecmp(argv[2],"off") )
		relayed=0;
	else
	{
		cerr<<"Error : beacon : the state can only be \"on\" or \"off\""<<endl;
		return 1;
	}

	if( SendOrderWithMacAndValue("beacon",TORDER_BEACON,mac,&relayed,sizeof(relayed)) )
		return 1;

	cout<<VwifiMacToString(mac)<<" : beacons "<<( relayed ? "relayed" : "swallowed" )<<endl;

	return 0;
}

int SetAckState(int argc, char** argv)
{
	if( argc != 3 )
	{
		cerr<<"Error : ack : the number of parameter is uncorrect"<<endl;
		Help();
		return 1;
	}

	TByte mac[ETH_ALEN];
	if( ParseAddress(argv[1],mac) != ETH_ALEN )
	{
		cerr<<"Error : ack : \""<<argv[1]<<"\" is not a MAC address"<<endl;
		return 1;
	}

	int faking;
	if( ! strcasecmp(argv[2],"on") )
		faking=1;
	else if( ! strcasecmp(argv[2],"off") )
		faking=0;
	else
	{
		cerr<<"Error : ack : the state can only be \"on\" or \"off\""<<endl;
		return 1;
	}

	if( SendOrderWithMacAndValue("ack",TORDER_ACK,mac,&faking,sizeof(faking)) )
		return 1;

	cout<<VwifiMacToString(mac)<<" : fake ack "<<( faking ? "on" : "off" )<<endl;

	return 0;
}

int AskRadios()
{
	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : radios : socket.Connect error"<<endl;
		return 1;
	}

	TOrder order=TORDER_RADIOS;
	if( socket.Send(reinterpret_cast<char*>(&order),sizeof(order)) == SOCKET_ERROR )
	{
		cerr<<"Error : radios : socket.Send : order"<<endl;
		return 1;
	}

	TIndex number;
	if( socket.Read(reinterpret_cast<char*>(&number),sizeof(number)) == SOCKET_ERROR )
	{
		cerr<<"Error : radios : socket.Read : number"<<endl;
		return 1;
	}

	for(TIndex i=0; i<number; i++)
	{
		TCID cid;
		u32 household;
		u32 count;

		if( socket.Read(reinterpret_cast<char*>(&cid),sizeof(cid)) == SOCKET_ERROR ||
		    socket.Read(reinterpret_cast<char*>(&household),sizeof(household)) == SOCKET_ERROR ||
		    socket.Read(reinterpret_cast<char*>(&count),sizeof(count)) == SOCKET_ERROR )
		{
			cerr<<"Error : radios : socket.Read : client"<<endl;
			return 1;
		}

		cout<<"CID "<<cid<<" household "<<household;
		if( count == 0 )
		{
			cout<<" : no radio reported yet"<<endl;
			continue;
		}
		cout<<endl;

		for(u32 r=0; r<count; r++)
		{
			u32 fields[3];
			int scalars[2];
			u64 airtime[3];

			if( socket.Read(reinterpret_cast<char*>(fields),sizeof(fields)) == SOCKET_ERROR ||
			    socket.Read(reinterpret_cast<char*>(scalars),sizeof(scalars)) == SOCKET_ERROR ||
			    socket.Read(reinterpret_cast<char*>(airtime),sizeof(airtime)) == SOCKET_ERROR )
			{
				cerr<<"Error : radios : socket.Read : radio"<<endl;
				return 1;
			}

			cout<<"  radio "<<fields[0]<<" : ";
			if( fields[1] )
				cout<<fields[1]<<" MHz / "<<fields[2]<<" MHz wide";
			else
				cout<<"no channel";

			cout<<", tx "<<scalars[0]<<" dBm, noise "<<scalars[1]<<" dBm"<<endl;

			// Microseconds, monotonic since the client connected. Two samples
			// and the interval between them is what gives a utilisation; a
			// single sample on its own says only that the radio has been busy
			// for some of its life.
			cout<<"    airtime us : tx "<<airtime[0]
			    <<"  rx "<<airtime[1]
			    <<"  other households "<<airtime[2]<<endl;
		}
	}

	socket.Close();

	return 0;
}

void Help()
{
	cout<<NameOfProg<<" [order]"<<endl;
	cout<<" with [order] :"<<endl;
	cout<<"	ls"<<endl;
	cout<<"		- List the Clients"<<endl;
	cout<<"	set CID X Y Z"<<endl;
	cout<<"		- Change the coordinate of the Client with CID"<<endl;
	cout<<"	setname CID NAME"<<endl;
	cout<<"		- Set the NAME of the Client with CID"<<endl;
	cout<<"	loss yes/no"<<endl;
	cout<<"		- loss yes : packets can be lost"<<endl;
	cout<<"		- loss no : no packets can be lost"<<endl;
	cout<<"	link MAC up/down"<<endl;
	cout<<"		- link MAC down : cut the RF link of the Client transmitting from MAC :"<<endl;
	cout<<"		                  nothing it sends is relayed, nothing reaches it, and it"<<endl;
	cout<<"		                  stops acknowledging its own frames, so a station behind"<<endl;
	cout<<"		                  it loses its beacons and de-associates"<<endl;
	cout<<"		- link MAC up : put it back on the air"<<endl;
	cout<<"	show"<<endl;
	cout<<"		- Display the status of loss and list of Clients"<<endl;
	cout<<"	status"<<endl;
	cout<<"		- Display the status of the configuration of vwifi-server"<<endl;
	cout<<"	distance CID1 CID2"<<endl;
	cout<<"		- Display the distance in meters between the Client with CID1 and the Client with CID2"<<endl;
	cout<<"	scale VALUE"<<endl;
	cout<<"		- Set the scale of the distances between the clients to VALUE"<<endl;
	cout<<"		- VALUE can be a decimal number"<<endl;
	cout<<"	close"<<endl;
	cout<<"		- Close all the connections with Wifi Clients"<<endl;
	cout<<"	household MAC N"<<endl;
	cout<<"		- Put the Client transmitting from MAC into household N."<<endl;
	cout<<"		  Everything starts in household 0. Two nodes in the same household"<<endl;
	cout<<"		  hear each other with nothing in the way; between two different ones"<<endl;
	cout<<"		  the wall below applies."<<endl;
	cout<<"	position MAC"<<endl;
	cout<<"		- Report where the Client transmitting from MAC currently is."<<endl;
	cout<<"	position MAC X Y Z"<<endl;
	cout<<"		- Move the Client transmitting from MAC to (X,Y,Z), the same"<<endl;
	cout<<"		  coordinates \"set CID\" uses, addressed by MAC instead."<<endl;
	cout<<"		- Distance is the other way to separate the bands, and unlike a"<<endl;
	cout<<"		  wall it needs no second household: indoor path loss rises faster"<<endl;
	cout<<"		  with frequency (ITU-R P.1238 residential), so walking a station"<<endl;
	cout<<"		  away costs 6GHz more than 5GHz and 5GHz more than 2.4GHz."<<endl;
	cout<<"	wall N M DB|MATERIAL [COUNT]"<<endl;
	cout<<"		- Put a wall between household N and household M"<<endl;
	cout<<"		- DB : a loss in dB that does not vary with frequency"<<endl;
	cout<<"		- MATERIAL : "<<WallMaterialNames()<<endl;
	cout<<"		             a real material, whose loss rises with frequency"<<endl;
	cout<<"		             (3GPP TR 38.901). Concrete costs ~15 dB at 2.4GHz and"<<endl;
	cout<<"		             ~26 dB at 5GHz, which is why a station loses 5GHz first."<<endl;
	cout<<"		- COUNT : how many such walls are in the way (default 1)"<<endl;
	cout<<"		- wall N N ... : set the wall used between any two households that"<<endl;
	cout<<"		                 have no wall of their own"<<endl;
	cout<<"	noise MAC DBM [RADIO]"<<endl;
	cout<<"		- Set the noise floor of the Client transmitting from MAC to DBM,"<<endl;
	cout<<"		  which is negative. Raising it is how a link is made marginal without"<<endl;
	cout<<"		  moving anything. Applies to every radio of that node unless RADIO"<<endl;
	cout<<"		  names one."<<endl;
	cout<<"	beacon MAC on/off"<<endl;
	cout<<"		- beacon MAC off : stop relaying the beacons that Client transmits,"<<endl;
	cout<<"		                   and nothing else. It goes on believing it beacons;"<<endl;
	cout<<"		                   a station behind it counts the misses and gives up."<<endl;
	cout<<"		- beacon MAC on : relay them again"<<endl;
	cout<<"	ack MAC on/off"<<endl;
	cout<<"		- ack MAC off : stop the Client transmitting from MAC from reporting its"<<endl;
	cout<<"		                own transmissions as acknowledged. Cutting the link does"<<endl;
	cout<<"		                this too, along with everything else."<<endl;
	cout<<"		                A station drops its association within ~10s of this,"<<endl;
	cout<<"		                whether or not it is still hearing beacons -- so it is"<<endl;
	cout<<"		                the way to force a de-association, but not evidence that"<<endl;
	cout<<"		                beacon loss caused one."<<endl;
	cout<<"		- ack MAC on : acknowledge again"<<endl;
	cout<<"		- note that a later \"link\" command overwrites this, and vice versa"<<endl;
	cout<<"	radios"<<endl;
	cout<<"		- List what each Client has reported about its own radios : channel,"<<endl;
	cout<<"		  power, noise floor, and the airtime each one has accumulated"<<endl;
	cout<<endl;
	cout<<" [-p PORT] or [--port PORT] : Set the port used by the vwifi-server (by default PORT="<< Port_Ctrl <<")"<<endl;
	cout<<" [-i IP] or [--ip IP] : Set the IP used by the vwifi-server (by default IP="<< IP_Ctrl <<")"<<endl;
	cout<<" [-v] or [--version] : Display the version of "<<NameOfProg<<endl;
	cout<<" [-h] or [--help] : this help"<<endl;
}

int GetCInfoWifi(CSocketClientITCP & socket, CInfoWifi* infoWifi)
{
	int err;

	TCID cid;
	err=socket.Read(reinterpret_cast<char*>(&cid),sizeof(cid));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : GetCInfoWifi : socket.Read : cid"<<endl;
		return 1;
	}

	CCoordinate coo;
	err=socket.Read(reinterpret_cast<char*>(&coo),sizeof(coo));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : GetCInfoWifi : socket.Read : CCoordinate (cid:"<<cid<<")"<<endl;
		return 1;
	}

	int sizeName;
	err=socket.Read(reinterpret_cast<char*>(&sizeName),sizeof(sizeName));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : GetCInfoWifi : socket.Read : size of name (cid:"<<cid<<")"<<endl;
		return 1;
	}
	if( sizeName > MAX_SIZE_NAME )
	{
		cerr<<"Error : GetCInfoWifi : size of name > "<<MAX_SIZE_NAME<<" (cid:"<<cid<<")"<<endl;
		return 1;
	}

	char strName[MAX_SIZE_NAME+1]; // +1 : \0
	if( sizeName > 0 )
	{
		err=socket.Read(reinterpret_cast<char*>(strName),sizeName+1); // +1 : \0
		if( err == SOCKET_ERROR )
		{
			cerr<<"Error : GetCInfoWifi : socket.Read : size of name (cid:"<<cid<<")"<<endl;
			return 1;
		}
	}
	else
		strName[0]='\0';

	infoWifi->SetCid(cid);
	infoWifi->Set(coo);

	string name(strName);
	infoWifi->SetName(name);

	return 0;
}

int AskList()
{
	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : ls : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_LIST;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : ls : socket.Send : order"<<endl;
		return 1;
	}

	// Spies :

	TIndex number;
	err=socket.Read(reinterpret_cast<char*>(&number),sizeof(number));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : ls : socket.Read : number"<<endl;
		return 1;
	}

	CInfoWifi info;
	for(TIndex i=0; i<number;i++)
	{
		err=GetCInfoWifi(socket,&info);
		if( err == SOCKET_ERROR )
		{
			cerr<<"Error : ls : socket.Read : CInfoWifi"<<endl;
			return 1;
		}
		cout<<"S:"<<info.GetCid();
		if( info.HasName() )
			cout<<" ("<<info.GetName()<<")";
		cout<<endl;
	}

	// Clients :

	err=socket.Read(reinterpret_cast<char*>(&number),sizeof(number));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : ls : socket.Read : number"<<endl;
		return 1;
	}

	for(TIndex i=0; i<number;i++)
	{
		err=GetCInfoWifi(socket,&info);
		if( err == SOCKET_ERROR )
		{
			cerr<<"Error : ls : socket.Read : CInfoWifi"<<endl;
			return 1;
		}
		cout<<info<<endl;
	}

	socket.Close();

	return 0;
}

int ChangeCoordinate(int argc, char *argv[])
{
	if( argc != 5 )
	{
			cerr<<"Error : set : the number of parameter is uncorrect"<<endl;
			Help();
			return 1;
	}

	if( ! isPositiveInt(argv[1]) )
	{
		cerr<<"Error : set : the CID is not an integer"<<endl;
		return 1;
	}
	TCID cid=atoi(argv[1]);

	if( cid < TCID_GUEST_MIN )
	{
			cerr<<"Error : set : the CID must be greater than or equal to "<<TCID_GUEST_MIN<<endl;
			return 1;
	}

	if( ! isInt(argv[2]) )
	{
		cerr<<"Error : set : the x coordinate is not an integer"<<endl;
		return 1;
	}
	if( ! isInt(argv[3]) )
	{
		cerr<<"Error : set : the y coordinate is not an integer"<<endl;
		return 1;
	}
	if( ! isInt(argv[4]) )
	{
		cerr<<"Error : set : the z coordinate is not an integer"<<endl;
		return 1;
	}
	TValue x=atoi(argv[2]);
	TValue y=atoi(argv[3]);
	TValue z=atoi(argv[4]);
	CCoordinate coo(x,y,z);

	cout<<cid<<" "<<coo<<" "<<endl;

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : set : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_CHANGE_COORDINATE;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : set : socket.Send : order"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&cid),sizeof(cid));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : set : socket.Send : cid"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&coo),sizeof(coo));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : set : socket.Send : "<<coo<<endl;
		return 1;
	}

	socket.Close();

	return 0;
}

int SetName(int argc, char *argv[])
{
	if( argc != 3 )
	{
			cerr<<"Error : setname : the number of parameter is uncorrect"<<endl;
			Help();
			return 1;
	}

	if( ! isPositiveInt(argv[1]) )
	{
		cerr<<"Error : setname : the CID is not an integer"<<endl;
		return 1;
	}
	TCID cid=atoi(argv[1]);

	if( cid < TCID_GUEST_MIN )
	{
			cerr<<"Error : setname : the CID must be greater than or equal to "<<TCID_GUEST_MIN<<endl;
			return 1;
	}

	string name(argv[2]);
	int sizeName=name.size();
	if( name.length() > MAX_SIZE_NAME )
	{
		name.resize(MAX_SIZE_NAME);
		sizeName=MAX_SIZE_NAME;
	}

	cout<<cid<<" "<<name<<" "<<endl;

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : setname : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_SETNAME;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : setname : socket.Send : order"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&cid),sizeof(cid));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : setname : socket.Send : cid"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&sizeName),sizeof(sizeName));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : setname : socket.Send : size of name"<<endl;
		return 1;
	}
	err=socket.Send(const_cast<char*>(name.c_str()),sizeName+1); // +1 : \0
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : setname : socket.Send : name"<<name<<endl;
		return 1;
	}

	socket.Close();

	return 0;
}

int ChangePacketLoss(int argc, char *argv[])
{
	if( argc != 2 )
	{
			cerr<<"Error : loss : the number of parameter is uncorrect"<<endl;
			Help();
			return 1;
	}

	int value;
	if ( ! strcmp(argv[1],"yes") )
		value=1;
	else if ( ! strcmp(argv[1],"no") )
		value=0;
	else
	{
			cerr<<"Error : loss : the value can only be \"yes\" or \"no\""<<endl;
			return 1;
	}

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : loss : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_PACKET_LOSS;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : loss : socket.Send : order"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&value),sizeof(value));
	if( err == SOCKET_ERROR )
	{
		if ( value  )
			cerr<<"Error : loss : socket.Send : yes"<<endl;
		else
			cerr<<"Error : loss : socket.Send : no"<<endl;

		return 1;
	}

	socket.Close();

	return 0;
}

int SetLinkState(int argc, char *argv[])
{
	if( argc != 3 )
	{
			cerr<<"Error : link : the number of parameter is uncorrect"<<endl;
			Help();
			return 1;
	}

	TByte mac[ETH_ALEN];
	if( ParseAddress(argv[1],mac) != ETH_ALEN )
	{
			cerr<<"Error : link : \""<<argv[1]<<"\" is not a MAC address"<<endl;
			return 1;
	}

	int up;
	if ( ! strcasecmp(argv[2],"up") )
		up=1;
	else if ( ! strcasecmp(argv[2],"down") )
		up=0;
	else
	{
			cerr<<"Error : link : the state can only be \"up\" or \"down\""<<endl;
			return 1;
	}

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : link : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_LINK;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : link : socket.Send : order"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(mac),sizeof(mac));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : link : socket.Send : MAC"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&up),sizeof(up));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : link : socket.Send : state"<<endl;
		return 1;
	}

	int codeError;
	err=socket.Read(reinterpret_cast<char*>(&codeError),sizeof(codeError));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : link : socket.Read : code"<<endl;
		return 1;
	}

	socket.Close();

	// A client is only addressable once the server has seen it transmit : the
	// address comes from the frames themselves, nothing else carries it.
	if( codeError )
	{
		cerr<<"Error : link : no connected Client has transmitted from "<<VwifiMacToString(mac)<<endl;
		return 1;
	}

	cout<<VwifiMacToString(mac)<<" : RF link "<<( up ? "up" : "down" )<<endl;

	return 0;
}

int AskStatus()
{
	cout<<"CTRL : IP : "<<IP_Ctrl.c_str()<<endl;
	cout<<"CTRL : Port : "<<Port_Ctrl<<endl;

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : status : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_STATUS;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Send : Order"<<endl;
		return 1;
	}

	bool loss;
	err=socket.Read(reinterpret_cast<char*>(&loss),sizeof(loss));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Read : Loss"<<endl;
		return 1;
	}
	cout<<"SRV : PacketLoss : ";
	if ( loss )
		cout<<"Enable"<<endl;
	else
		cout<<"Disable"<<endl;

	TScale scale;
	err=socket.Read(reinterpret_cast<char*>(&scale),sizeof(scale));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Read : scale"<<endl;
		return 1;
	}
	cout<<"SRV : Scale : "<<scale<<endl;

	// VHOST

	TPort port;
	err=socket.Read(reinterpret_cast<char*>(&port),sizeof(port));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Read : Port VHOST"<<endl;
		return 1;
	}
	cout<<"SRV VHOST : Port : "<<port<<endl;

	// INET

	err=socket.Read(reinterpret_cast<char*>(&port),sizeof(port));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Read : Port INET"<<endl;
		return 1;
	}
	cout<<"SRV INET : Port : "<<port<<endl;

	// SizeOfDisconnected
	// becareful : the same List is shared by WifiServerVTCP and WifiServerITCP

	TIndex size;
	err=socket.Read(reinterpret_cast<char*>(&size),sizeof(size));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Read : Size INET"<<endl;
		return 1;
	}
	cout<<"SRV : SizeOfDisconnected : "<<size<<endl;

	// SPY

	bool spyIsConnected;
	err=socket.Read(reinterpret_cast<char*>(&spyIsConnected),sizeof(spyIsConnected));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : status : socket.Read : spyIsConnected"<<endl;
		return 1;
	}
	cout<<"SPY : ";
	if ( spyIsConnected )
		cout<<"Connected"<<endl;
	else
		cout<<"Disconnected"<<endl;

	socket.Close();

	return 0;
}

int AskShow()
{
	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : show : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_SHOW;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : show : socket.Send : Order"<<endl;
		return 1;
	}

	bool loss;
	err=socket.Read(reinterpret_cast<char*>(&loss),sizeof(loss));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : show : socket.Read : Loss"<<endl;
		return 1;
	}
	cout<<"PacketLoss : ";
	if ( loss )
		cout<<"Enable"<<endl;
	else
		cout<<"Disable"<<endl;

	TScale scale;
	err=socket.Read(reinterpret_cast<char*>(&scale),sizeof(scale));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : show : socket.Read : scale"<<endl;
		return 1;
	}
	cout<<"Scale : "<<scale<<endl;

	bool spyIsConnected;
	err=socket.Read(reinterpret_cast<char*>(&spyIsConnected),sizeof(spyIsConnected));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : show : socket.Read : spyIsConnected"<<endl;
		return 1;
	}
	cout<<"Spy : ";
	if ( spyIsConnected )
		cout<<"Connected"<<endl;
	else
		cout<<"Disconnected"<<endl;

	socket.Close();

	cout<<"----------------"<<endl;

	return AskList();
}

int DistanceBetweenCID(int argc, char *argv[])
{
	if( argc != 3 )
	{
			cerr<<"Error : distance : the number of parameter is uncorrect"<<endl;
			Help();
			return 1;
	}

	if( ! isPositiveInt(argv[1]) )
	{
		cerr<<"Error : distance : the CID 1 is not an integer"<<endl;
		return 1;
	}
	TCID cid1=atoi(argv[1]);

	if( cid1 < TCID_GUEST_MIN )
	{
			cerr<<"Error : distance : the CID 1 must be greater than or equal to "<<TCID_GUEST_MIN<<endl;
			return 1;
	}

	if( ! isPositiveInt(argv[2]) )
	{
		cerr<<"Error : distance : the CID 2 is not an integer"<<endl;
		return 1;
	}
	TCID cid2=atoi(argv[2]);

	if( cid2 < TCID_GUEST_MIN )
	{
			cerr<<"Error : distance : the CID2 must be greater than or equal to "<<TCID_GUEST_MIN<<endl;
			return 1;
	}

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : distance : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_DISTANCE_BETWEEN_CID;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : distance : socket.Send : order"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&cid1),sizeof(cid1));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : distance : socket.Send : cid 1"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&cid2),sizeof(cid2));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : distance : socket.Send : cid 2"<<endl;
		return 1;
	}

	int codeError;
	err=socket.Read(reinterpret_cast<char*>(&codeError),sizeof(codeError));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : distance : socket.Read : codeError"<<endl;
		return 1;
	}

	if ( codeError == -1 )
	{
		cerr<<"Error : distance : unknown cid 1 : "<<cid1<<endl;
		return 1;
	}
	if ( codeError == -2 )
	{
		cerr<<"Error : distance : unknown cid 2 : "<<cid2<<endl;
		return 1;
	}

	TDistance distance;
	err=socket.Read(reinterpret_cast<char*>(&distance),sizeof(distance));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : distance : socket.Read : distance"<<endl;
		return 1;
	}

	cout<<distance<<endl;

	socket.Close();

	return 0;
}

int SetScale(int argc, char *argv[])
{
	if( argc != 2)
	{
			cerr<<"Error : scale : the number of parameter is uncorrect"<<endl;
			Help();
			return 1;
	}

	if( ! isIntOrFloat(argv[1]) )
	{
		cerr<<"Error : scale : the format of the value is uncorrect"<<endl;
		return 1;
	}
	TScale scale=atof(argv[1]);
	if( scale <= 0 )
	{
		cerr<<"Error : scale : the value must be greater than 0"<<endl;
		return 1;
	}

	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : scale : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_SET_SCALE;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : scale : socket.Send : order"<<endl;
		return 1;
	}
	err=socket.Send(reinterpret_cast<char*>(&scale),sizeof(scale));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : scale : socket.Send : scale"<<endl;
		return 1;
	}

	socket.Close();

	return 0;
}

int CloseAllClient()
{
	CSocketClientITCP socket;

	socket.Init(IP_Ctrl.c_str(),Port_Ctrl);

	if( ! socket.ConnectLoop() )
	{
		cerr<<"Error : close : socket.Connect error"<<endl;
		return 1;
	}

	int err;

	TOrder order=TORDER_CLOSE_ALL_CLIENT;
	err=socket.Send(reinterpret_cast<char*>(&order),sizeof(order));
	if( err == SOCKET_ERROR )
	{
		cerr<<"Error : close : socket.Send : order"<<endl;
		return 1;
	}

	socket.Close();

	return 0;
}

int main(int argc , char *argv[])
{
	auto param_cmd = std::make_unique<char*[]>(argc);
	int nbr_param_cmd=0;

	NameOfProg=argv[0];

	int arg_idx = 1;
	while (arg_idx < argc)
	{
		if( ! strcmp("-v", argv[arg_idx]) || ! strcmp("--version", argv[arg_idx]) )
		{
			std::cout<<"Version : "<<VERSION<<std::endl;
			return 0;
		}
		if( ! strcmp("-h", argv[arg_idx]) || ! strcmp("--help", argv[arg_idx]) )
		{
			Help();
			return 1;
		}
		if( ( ! strcmp("-p", argv[arg_idx]) || ! strcmp("--port", argv[arg_idx]) ) && (arg_idx + 1) < argc && isPositiveInt(argv[arg_idx+1]) )
		{
			Port_Ctrl = std::stoi(argv[arg_idx+1]);
			arg_idx++;
		}
		else if( ( ! strcmp("-i", argv[arg_idx]) || ! strcmp("--ip", argv[arg_idx]) ) && (arg_idx + 1) < argc)
		{
			IP_Ctrl = std::string(argv[arg_idx+1]);
			arg_idx++;
		}
		else
		{
			param_cmd[nbr_param_cmd++]=argv[arg_idx];
		}

		arg_idx++;
	}

	if( nbr_param_cmd == 0 )
	{
		Help();
		return 0;
	}

	if( ! strcasecmp(param_cmd[0],"ls") )
		return AskList();

	if( ! strcasecmp(param_cmd[0],"set") )
		return ChangeCoordinate(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"setname") )
		return SetName(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"loss") )
		return ChangePacketLoss(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"link") )
		return SetLinkState(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"show") )
		return AskShow();

	if( ! strcasecmp(param_cmd[0],"status") )
		return AskStatus();

	if( ! strcasecmp(param_cmd[0],"distance") )
		return DistanceBetweenCID(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"scale") )
		return SetScale(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"close") )
		return CloseAllClient();

	if( ! strcasecmp(param_cmd[0],"household") )
		return SetHousehold(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"position") )
		return SetPosition(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"wall") )
		return SetWall(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"noise") )
		return SetNoiseFloor(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"beacon") )
		return SetBeaconState(nbr_param_cmd, param_cmd.get());

	if( ! strcasecmp(param_cmd[0],"radios") )
		return AskRadios();

	if( ! strcasecmp(param_cmd[0],"ack") )
		return SetAckState(nbr_param_cmd, param_cmd.get());

	cerr<<NameOfProg<<" : Error : unknown order : "<<param_cmd[0]<<endl;

	return 1;
}

