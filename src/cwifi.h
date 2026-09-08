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

// Whether this client should go on reporting its own transmissions as
// acknowledged. Same shape and same reasoning as the link state above, but a
// separate message because the two are separate facts -- see
// SendAckStateWithSocket().
bool VwifiReadAckState(const char* buffer, ssize_t sizeOfBuffer, bool& faking);

// A client's periodic description of its own radios, travelling up the data
// socket in place of a frame. Returns false when the buffer holds anything
// else, so the server can try this first and fall through to relaying.
//
// Writes at most VWIFI_MAX_RADIOS_PER_CLIENT entries into radios and sets
// numberOfRadios to how many it wrote.
bool VwifiReadRadioState(const char* buffer, ssize_t sizeOfBuffer,
		VwifiRadioEntry* radios, u32& numberOfRadios);

// Serialises up to VWIFI_MAX_RADIOS_PER_CLIENT entries into buffer, which must
// hold VwifiRadioStateSize(numberOfRadios) bytes. Returns the number of bytes
// written, or 0 when the report does not fit or describes no radio at all.
ssize_t VwifiWriteRadioState(char* buffer, ssize_t sizeOfBuffer,
		const VwifiRadioEntry* radios, u32 numberOfRadios);

ssize_t VwifiRadioStateSize(u32 numberOfRadios);

// The server's measured channel occupancy, travelling down to a client in
// place of a frame. Same envelope and same reasoning as the radio-state report
// going the other way.
bool VwifiReadSurvey(const char* buffer, ssize_t sizeOfBuffer,
		VwifiSurveyEntry* entries, u32& numberOfEntries);

ssize_t VwifiWriteSurvey(char* buffer, ssize_t sizeOfBuffer,
		const VwifiSurveyEntry* entries, u32 numberOfEntries);

ssize_t VwifiSurveySize(u32 numberOfEntries);

class CWifi
{
	protected :

		TFrequency GetFrequency(struct nlmsghdr* nlh);

		// hwsim address a frame was transmitted from, empty when the message
		// carries none. Frames are all the server ever sees of a client, so
		// this is what lets it be addressed by MAC.
		std::string GetTransmitter(struct nlmsghdr* nlh);

		// The 802.11 frame carried inside a relayed netlink message. The
		// server otherwise only ever handles the netlink envelope, but the
		// frame's own length is what airtime is computed from and its first
		// byte is what says whether it is a beacon.
		//
		// Returns false when the message carries no frame, leaving the outputs
		// untouched. body points into nlh and is valid for as long as it is.
		bool GetFrameBody(struct nlmsghdr* nlh, const char*& body, u32& sizeOfBody);

		// Parses one relayed message once, so that a caller needing more than
		// one attribute out of it does not walk it again per attribute. The
		// forwarding path needs both the frame and, for a beacon, the
		// transmitter, and it is the hottest loop in the server.
		//
		// attrs must have HWSIM_ATTR_MAX + 1 entries. Returns false when the
		// message does not parse, in which case attrs is not to be read.
		bool ParseMessage(struct nlmsghdr* nlh, struct nlattr** attrs);

		// The same two accessors, against an already-parsed message.
		bool GetFrameBodyFrom(struct nlattr* const* attrs, const char*& body, u32& sizeOfBody);
		std::string GetTransmitterFrom(struct nlattr* const* attrs);

		ssize_t SendLinkStateWithSocket(CSocket* socket, TDescriptor descriptor, bool up);

		// Tell one client to stop, or resume, fabricating HWSIM_TX_STAT_ACK for
		// its own transmissions.
		//
		// This is half of what "link down" does: cutting the link stops the
		// relay and the fake ack together, and this stops only the ack.
		//
		// Be careful what a test built on it is allowed to claim. Measured
		// behaviour: with beacons swallowed and the fake ack left on, a station
		// stays associated indefinitely -- mac80211 probes on beacon loss and
		// its own probe is acknowledged, so it concludes the AP is there. Turn
		// the fake ack off and it drops within ten seconds. But it also drops
		// within ten seconds with the fake ack off and every beacon still
		// arriving, because the connection monitor's periodic null-data probe
		// fails for the same reason.
		//
		// So this produces a de-association, and the beacon blackhole produces
		// genuine beacon starvation, but neither makes the first the
		// consequence of the second. Doing that faithfully needs the ack to
		// follow the medium -- the server knows whether a frame was delivered
		// and the client does not -- which is a transmit-status path that does
		// not exist yet.
		ssize_t SendAckStateWithSocket(CSocket* socket, TDescriptor descriptor, bool faking);

		// distance : meter
		int Attenuation(TDistance distance, TFrequency frequency);

		// return power value between [TPower_MIN,TPower_MAX]
		TPower BoundedPower(int power);

		// True when a frame at this received power should be dropped.
		//
		// The old model drew a number in [40,92] and compared it against
		// -signalLevel, which made loss a property of the receiver's power
		// alone : there was no noise floor in it, no bandwidth, and no
		// difference between a beacon and a data frame. This one computes an
		// SNR against the receiver's own floor and rolls against the error
		// curve for the frame's class, so a management frame outlives the data
		// link the way it does on real hardware.
		bool PacketIsLost(TPower signalLevel, int noiseFloorDbm, u32 channelWidth, bool robust);

		ssize_t SendSignalWithSocket(CSocket* socket, TDescriptor descriptor, VwifiRadioInfo* radio_info, const char* buffer, int sizeOfBuffer);
		ssize_t RecvSignalWithSocket(CSocket* socket, TDescriptor descriptor, VwifiRadioInfo* radio_info, CDynBuffer* buffer);
};

#endif
