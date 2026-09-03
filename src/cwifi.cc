#include <math.h>    // log10
#include <stdio.h>   // snprintf
#include <stdlib.h>  // rand
#include <string.h>  // memset

#include <net/ethernet.h> // ETH_ALEN

#include <iostream>

#include <netlink/netlink.h> // (struct nlmsghdr *)

#include "hwsim.h" // HWSIM_ATTR_FREQ
#include <netlink/genl/genl.h> // genlmsg_parse

#include "cwifi.h"

//#include "config.h"

const double ConstanteC=92.45;
const TFrequency DEFAULT_FREQUENCY=2412; // Hz

const int MTU=2352; // Maximum Transmission Unit :  2352 (from include/linux/ieee80211.h)

// A link state message travels the data socket in place of a frame, and the
// client already dispatches on genlmsghdr::cmd. 240 sits far above every
// mac80211_hwsim command, so a client built before this reports an unknown
// command and drops it rather than misreading it as a frame.
const u8 VWIFI_CMD_LINK_STATE=240;

struct VwifiLinkState
{
	struct nlmsghdr   nlh;
	struct genlmsghdr gnlh;
	u32               up;
};

std::string VwifiMacToString(const TByte* mac)
{
	char buffer[18]; // 6*2 digits + 5 colons + \0
	snprintf(buffer,sizeof(buffer),"%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	return std::string(buffer);
}

bool VwifiReadLinkState(const char* buffer, ssize_t sizeOfBuffer, bool& up)
{
	if( sizeOfBuffer != static_cast<ssize_t>(sizeof(struct VwifiLinkState)) )
		return false;

	const struct VwifiLinkState* message=reinterpret_cast<const struct VwifiLinkState*>(buffer);

	if( message->gnlh.cmd != VWIFI_CMD_LINK_STATE )
		return false;

	up=( message->up != 0 );

	return true;
}

TFrequency CWifi::GetFrequency(struct nlmsghdr* nlh)
{
	/* we get the attributes*/
	struct nlattr *attrs[HWSIM_ATTR_FREQ + 1];
	genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_FREQ, NULL);

	/* we get frequence */
	if (attrs[HWSIM_ATTR_FREQ])
		return nla_get_u32(attrs[HWSIM_ATTR_FREQ]);
	else
		return DEFAULT_FREQUENCY;
}

// distance : meter
// frequency : Hz
int CWifi::Attenuation(TDistance distance, TFrequency frequency)
{
	if( distance == 0 )
		return 0;

	//     ConstanteC+20*log10(frequency/1000)+20*log10(distance/1000);
	//     ConstanteC+20*(log10(frequency)-log10(1000))+20*(log10(distance)-log10(1000))
	return ConstanteC+20*(log10(frequency)-3)+20*(log10(distance)-3);
}

std::string CWifi::GetTransmitter(struct nlmsghdr* nlh)
{
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];

	if( genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_MAX, NULL) )
		return std::string();

	if( ! attrs[HWSIM_ATTR_ADDR_TRANSMITTER] )
		return std::string();

	if( nla_len(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]) < ETH_ALEN )
		return std::string();

	return VwifiMacToString(reinterpret_cast<const TByte*>(nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER])));
}

ssize_t CWifi::SendLinkStateWithSocket(CSocket* socket, TDescriptor descriptor, bool up)
{
	// The client reads [VwifiRadioInfo][netlink message] whatever the message
	// turns out to be, so the metadata still has to be there.
	VwifiRadioInfo radio_info{};

	struct VwifiLinkState message;
	memset(&message,0,sizeof(message));
	message.nlh.nlmsg_len=sizeof(message);
	message.gnlh.cmd=VWIFI_CMD_LINK_STATE;
	message.up=( up ? 1 : 0 );

	return SendSignalWithSocket(socket, descriptor, &radio_info,
			reinterpret_cast<const char*>(&message), sizeof(message));
}

TPower CWifi::BoundedPower(int power)
{
	if( power < TPower_MIN )
		return TPower_MIN;
	if( power > TPower_MAX )
		return TPower_MAX;
	return power;
}

bool CWifi::PacketIsLost(TPower signalLevel)
{
	//don't forget : signalLevel is negative

	int alea = rand() % 53 + 40; // between 40 and 92
	if( alea > -signalLevel )
		return false;

	return true;
}

ssize_t CWifi::SendSignalWithSocket(CSocket* socket, TDescriptor descriptor, VwifiRadioInfo* radio_info, const char* buffer, int sizeOfBuffer)
{
//	cout<<"send power : "<<radio_info->power<<endl;
	int val=socket->Send(descriptor, reinterpret_cast<const char*>(radio_info), sizeof(VwifiRadioInfo));
	if( val <= 0 )
		return val;
//	std::cout<<"send big data of size : "<<sizeOfBuffer<<std::endl;
	return socket->Send(descriptor, buffer, sizeOfBuffer);
}

ssize_t CWifi::RecvSignalWithSocket(CSocket* socket, TDescriptor descriptor, VwifiRadioInfo* radio_info, CDynBuffer* buffer)
{
	int valread;

	// read the metadata
	valread = socket->ReadEqualSize(descriptor, reinterpret_cast<char*>(radio_info), sizeof(VwifiRadioInfo));
	if ( valread <= 0 )
		return valread;

	// read the signal
	// "nlmsg_len" (type "uint32_t") is the first attribut of the "struct nlmsghdr" in "libnl3/netlink/netlink-kernel.h"
	ssize_t sizeRead = socket->ReadEqualSize(descriptor, buffer, 0, sizeof(struct nlmsghdr));
	if( sizeRead == SOCKET_ERROR  )
		return SOCKET_ERROR;

	int sizeTotal=reinterpret_cast<struct nlmsghdr *>(buffer->GetBuffer())->nlmsg_len;

	if( sizeTotal > MTU ) // to avoid that a error packet overfulls the memory
		return SOCKET_ERROR;

	return socket->ReadEqualSize(descriptor, buffer, sizeRead, sizeTotal);
}
