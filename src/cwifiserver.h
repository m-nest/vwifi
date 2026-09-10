#ifndef _CWIFISERVER_H_
#define _CWIFISERVER_H_

#include "crf.h" // CWall
#include "csocketserver.h"
#include "cinfowifi.h"
#include "cwifi.h"

extern bool CanLostPackets;

// Attenuation in dB between two different households, and the exceptions to it.
//
// These are free functions over globals for the same reason CanLostPackets and
// Scale are : a CWifiServer is copied, and the VHOST and TCP servers are two
// objects sharing one medium. A per-object matrix would give the two of them
// different walls.
//
// WallAttenuationBetween() is the only reader. It returns 0 dB within a
// household -- same room, nothing in the way -- an explicit pairwise wall if
// one has been set, and the default otherwise. It takes the frequency because
// a wall is not one number: see CWall.
void SetDefaultWall(const CWall& wall);
CWall GetDefaultWall();
void SetWall(u32 householdA, u32 householdB, const CWall& wall);
int  WallAttenuationBetween(u32 householdA, u32 householdB, TFrequency frequencyMHz);
void ResetWalls();

class CWifiServer : public CSocketServer, public CWifi
{
		friend class CCTRLServer;

		TIndex MaxClientDeconnected;

		CListInfo<CInfoWifi>* InfoWifis;
		CListInfo<CInfoWifi>* InfoWifisDeconnected;

		// Restore what is known about a client that has connected before, into
		// `recovered`. Both search by CID; the first also erases the entry it
		// found, and the second disables the stale still-connected one.
		//
		// These used to hand back a coordinate and a name, and everything else
		// a client had accumulated was silently dropped on reconnect. That was
		// invisible while a coordinate was all there was to lose, and stopped
		// being so once a client also carried a household, a noise floor and a
		// beacon blackhole: a gateway whose vwifi-client is restarted -- which
		// the keeper does on every wld restart -- came back in household 0 with
		// its walls gone, still at the coordinates it was placed at, and
		// nothing said so.
		bool RecoverInfosOfInfoWifiDeconnected(TCID cid, CInfoWifi& recovered);

		bool RecoverInfosOfInfoWifi(TCID cid, CInfoWifi& recovered);

		void DefaultValues();

	public :

		CWifiServer();

		CWifiServer(CListInfo<CInfoSocket>* infoSockets, CListInfo<CInfoWifi>* infoWifis, CListInfo<CInfoWifi>* infoWifisDeconnected);

		CWifiServer( const CWifiServer & wifiServer );

		~CWifiServer();

		CWifiServer& operator=(const CWifiServer& wifiServer);

		bool Listen(TIndex maxClientDeconnected);

		TDescriptor Accept();

		void ShowInfoWifi(TIndex index);

		void CloseClient(TIndex index);

		void CloseAllClient();

		ssize_t SendSignal(TDescriptor descriptor, VwifiRadioInfo* radio_info, const char* buffer, int sizeOfBuffer);

		ssize_t RecvSignal(TDescriptor descriptor, VwifiRadioInfo* radio_info, CDynBuffer* buffer);

		// Relays one frame to every client that could actually hear it.
		//
		// Three things decide that, and they are asked in this order because
		// each is cheaper and more absolute than the next : whether the
		// receiver is tuned to a channel that can carry the frame at all,
		// whether the frame is a beacon the sender has been told to swallow,
		// and whether the link budget survives the roll against the error
		// curve. Airtime is charged before any of them, because a frame that
		// nobody could decode still occupied the medium.
		void SendAllOtherClients(TIndex index, VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData);

		// Records what one client just said about its own radios. Returns false
		// when the buffer is not a radio-state report, in which case it is a
		// frame and the caller should go on relaying it.
		bool LearnRadioState(TIndex index, const char* data, ssize_t sizeOfData);

		// Push each client the channel occupancy its own radios have seen, so
		// that its driver can report a survey instead of inventing one.
		//
		// Called from the main loop rather than on a timer: the loop runs
		// whenever anything crosses the medium, and a medium with an AP in it
		// is never quiet for a second. Rates are what gets sent, so a push
		// that does not happen costs nothing -- the driver keeps applying the
		// last rate it was given rather than freezing.
		void PushSurvey(u32 minimumIntervalMs);

		// Sets the household of the client transmitting from mac, or whether
		// the beacons sent from that one address are relayed, or the noise
		// floor of one of its radios. Each
		// returns false when no connected client has ever transmitted from
		// that address -- the same contract, and the same reason, as
		// SetLinkStateByMac().
		bool SetHouseholdByMac(const string& mac, u32 household);

		// Moves the client transmitting from mac to (x,y,z), in the same
		// coordinates "set CID" uses.
		//
		// By MAC rather than by CID because a scenario knows a station by its
		// address and nothing else: the CID is assigned by this server when the
		// client connects, is not derivable from anything on the client side,
		// and is not reported alongside the MAC anywhere. Every other lever a
		// scenario reaches for -- household, noise, beacon, ack, link -- is
		// addressed the same way, and distance was the one that was not.
		bool SetPositionByMac(const string& mac, TValue x, TValue y, TValue z);

		// Reads back what SetPositionByMac() set. Same addressing, and the same
		// false when no connected client has transmitted from that address.
		bool GetPositionByMac(const string& mac, TValue& x, TValue& y, TValue& z);
		bool SetBeaconsRelayedByMac(const string& mac, bool relayed);
		bool SetNoiseFloorByMac(const string& mac, u32 radioId, int noiseFloorDbm);

		// Pins the transmit power of a node's radio. Keyed by CID rather than
		// by MAC, because this is set per radio and "vwifi-ctrl radios" -- the
		// only place radio ids are visible -- lists CIDs, not addresses.
		bool SetTxPowerByCid(TCID cid, u32 radioId, int txPowerDbm);

		// Stops or restores the fabricated acknowledgement of a client's own
		// transmissions, without touching whether its frames are relayed.
		bool SetAckFakingByMac(const string& mac, bool faking);

		bool ClientLinkIsUp(TIndex index) const;

		// Records the address a client transmits from, so that it can later be
		// named by MAC from vwifi-ctrl.
		void LearnTransmitter(TIndex index, char* data, ssize_t sizeOfData);

		// Cuts or restores the RF link of the client transmitting from mac.
		// Returns false when no connected client has ever transmitted from it.
		bool SetLinkStateByMac(const string& mac, bool up);

		void SendAllOtherClientsWithoutLoss(TIndex index, VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData);

		void SendAllClientsWithoutLoss(VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData);

		CInfoWifi* GetReferenceOnInfoWifiByCID(TCID cid) const;

		CInfoWifi* GetReferenceOnInfoWifiDeconnectedByCID(TCID cid) const;

		CInfoWifi* GetReferenceOnInfoWifiByIndex(TIndex index) const;

		void AddInfoWifiDeconnected(CInfoWifi infoWifi);

};

#endif
