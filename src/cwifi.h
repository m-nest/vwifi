#ifndef _WIFI_H_
#define _WIFI_H_

#include <string>

#include "types.h" // TPower
#include "csocket.h" // CSocket

// "aa:bb:cc:dd:ee:ff", lowercase : the canonical form a MAC is stored and
// compared in
std::string VwifiMacToString(const TByte* mac);

// Reads a link state message sent by the server in place of a frame. Returns
// false when the buffer holds anything else, so a caller can simply try this
// first and fall through to its normal frame handling.
bool VwifiReadLinkState(const char* buffer, ssize_t sizeOfBuffer, bool& up);

class CWifi
{
	protected :

		TFrequency GetFrequency(struct nlmsghdr* nlh);

		// hwsim address a frame was transmitted from, empty when the message
		// carries none. Frames are all the server ever sees of a client, so
		// this is what lets it be addressed by MAC.
		std::string GetTransmitter(struct nlmsghdr* nlh);

		ssize_t SendLinkStateWithSocket(CSocket* socket, TDescriptor descriptor, bool up);

		// distance : meter
		int Attenuation(TDistance distance, TFrequency frequency);

		// return power value between [TPower_MIN,TPower_MAX]
		TPower BoundedPower(int power);

		bool PacketIsLost(TPower signalLevel);

		ssize_t SendSignalWithSocket(CSocket* socket, TDescriptor descriptor, VwifiRadioInfo* radio_info, const char* buffer, int sizeOfBuffer);
		ssize_t RecvSignalWithSocket(CSocket* socket, TDescriptor descriptor, VwifiRadioInfo* radio_info, CDynBuffer* buffer);
};

#endif
