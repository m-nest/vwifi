#include <math.h>    // log10
#include <stdio.h>   // snprintf
#include <stdlib.h>  // rand
#include <string.h>  // memset

#include <net/ethernet.h> // ETH_ALEN

#include <iostream>

#include <netlink/netlink.h> // (struct nlmsghdr *)

#include "hwsim.h" // HWSIM_ATTR_FREQ
#include <netlink/genl/genl.h> // genlmsg_parse

#include "crf.h"
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

// The same trick in the other direction : a client describing its own radios to
// the server. 241 is likewise above every mac80211_hwsim command, so a server
// built before this drops the report instead of relaying it as a frame.
const u8 VWIFI_CMD_RADIO_STATE=241;

// And 242 for the ack state. These three are the only commands vwifi invents;
// they sit above every mac80211_hwsim command so that a peer built before any
// of them drops the message rather than misreading it as a frame.
const u8 VWIFI_CMD_ACK_STATE=242;

struct VwifiLinkState
{
	struct nlmsghdr   nlh;
	struct genlmsghdr gnlh;
	u32               up;
};

struct VwifiAckState
{
	struct nlmsghdr   nlh;
	struct genlmsghdr gnlh;
	u32               faking;
};

// Header of a radio-state report. VwifiRadioEntry[Count] follows immediately,
// which is why this is read back with memcpy rather than by casting the buffer
// to a struct : the entries are not aligned to anything in particular once the
// message has been through a socket.
struct VwifiRadioStateHeader
{
	struct nlmsghdr   nlh;
	struct genlmsghdr gnlh;
	u32               count;
};

ssize_t VwifiRadioStateSize(u32 numberOfRadios)
{
	return static_cast<ssize_t>( sizeof(struct VwifiRadioStateHeader)
			+ numberOfRadios*sizeof(struct VwifiRadioEntry) );
}

ssize_t VwifiWriteRadioState(char* buffer, ssize_t sizeOfBuffer,
		const VwifiRadioEntry* radios, u32 numberOfRadios)
{
	if( buffer == NULL || radios == NULL )
		return 0;

	if( numberOfRadios == 0 || numberOfRadios > VWIFI_MAX_RADIOS_PER_CLIENT )
		return 0;

	ssize_t size=VwifiRadioStateSize(numberOfRadios);
	if( sizeOfBuffer < size )
		return 0;

	struct VwifiRadioStateHeader header;
	memset(&header,0,sizeof(header));
	header.nlh.nlmsg_len=size;
	header.gnlh.cmd=VWIFI_CMD_RADIO_STATE;
	header.count=numberOfRadios;

	memcpy(buffer,&header,sizeof(header));
	memcpy(buffer+sizeof(header),radios,numberOfRadios*sizeof(struct VwifiRadioEntry));

	return size;
}

bool VwifiReadRadioState(const char* buffer, ssize_t sizeOfBuffer,
		VwifiRadioEntry* radios, u32& numberOfRadios)
{
	numberOfRadios=0;

	if( buffer == NULL || radios == NULL )
		return false;

	if( sizeOfBuffer < static_cast<ssize_t>(sizeof(struct VwifiRadioStateHeader)) )
		return false;

	struct VwifiRadioStateHeader header;
	memcpy(&header,buffer,sizeof(header));

	if( header.gnlh.cmd != VWIFI_CMD_RADIO_STATE )
		return false;

	// The count comes off a socket, so it is not to be trusted until it has
	// been checked against both the cap and the bytes actually present.
	if( header.count == 0 || header.count > VWIFI_MAX_RADIOS_PER_CLIENT )
		return false;

	if( sizeOfBuffer < VwifiRadioStateSize(header.count) )
		return false;

	memcpy(radios,buffer+sizeof(header),header.count*sizeof(struct VwifiRadioEntry));
	numberOfRadios=header.count;

	return true;
}

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

bool VwifiReadAckState(const char* buffer, ssize_t sizeOfBuffer, bool& faking)
{
	if( sizeOfBuffer != static_cast<ssize_t>(sizeof(struct VwifiAckState)) )
		return false;

	const struct VwifiAckState* message=reinterpret_cast<const struct VwifiAckState*>(buffer);

	if( message->gnlh.cmd != VWIFI_CMD_ACK_STATE )
		return false;

	faking=( message->faking != 0 );

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

bool CWifi::ParseMessage(struct nlmsghdr* nlh, struct nlattr** attrs)
{
	return ( genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_MAX, NULL) == 0 );
}

std::string CWifi::GetTransmitterFrom(struct nlattr* const* attrs)
{
	if( ! attrs[HWSIM_ATTR_ADDR_TRANSMITTER] )
		return std::string();

	if( nla_len(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]) < ETH_ALEN )
		return std::string();

	return VwifiMacToString(reinterpret_cast<const TByte*>(nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER])));
}

std::string CWifi::GetTransmitter(struct nlmsghdr* nlh)
{
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];

	if( ! ParseMessage(nlh,attrs) )
		return std::string();

	return GetTransmitterFrom(attrs);
}

bool CWifi::GetFrameBodyFrom(struct nlattr* const* attrs, const char*& body, u32& sizeOfBody)
{
	if( ! attrs[HWSIM_ATTR_FRAME] )
		return false;

	int length=nla_len(attrs[HWSIM_ATTR_FRAME]);
	if( length <= 0 )
		return false;

	body=reinterpret_cast<const char*>(nla_data(attrs[HWSIM_ATTR_FRAME]));
	sizeOfBody=static_cast<u32>(length);

	return true;
}

bool CWifi::GetFrameBody(struct nlmsghdr* nlh, const char*& body, u32& sizeOfBody)
{
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];

	if( ! ParseMessage(nlh,attrs) )
		return false;

	return GetFrameBodyFrom(attrs,body,sizeOfBody);
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

ssize_t CWifi::SendAckStateWithSocket(CSocket* socket, TDescriptor descriptor, bool faking)
{
	VwifiRadioInfo radio_info{};

	struct VwifiAckState message;
	memset(&message,0,sizeof(message));
	message.nlh.nlmsg_len=sizeof(message);
	message.gnlh.cmd=VWIFI_CMD_ACK_STATE;
	message.faking=( faking ? 1 : 0 );

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

bool CWifi::PacketIsLost(TPower signalLevel, int noiseFloorDbm, u32 channelWidth, bool robust)
{
	// signalLevel is the power at this receiver, after path loss and any wall
	// between the two households : dBm, and negative.
	int noise=NoiseFloorForWidth(noiseFloorDbm,channelWidth);
	int snr=static_cast<int>(signalLevel) - noise;

	double per=PacketErrorRate(snr,robust);

	// rand() is enough here. This is a coin weighted by the error curve, not a
	// source anything depends on being unpredictable, and keeping it means a
	// run can still be reproduced by seeding srand().
	return ( ( static_cast<double>(rand()) / static_cast<double>(RAND_MAX) ) < per );
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
