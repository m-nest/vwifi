#ifndef _CINFOWIFI_H_
#define _CINFOWIFI_H_

#include <iostream> // ostream
#include <set>
#include <string>

#include <map>

#include "ccoordinate.h"
#include "crf.h" // CRadioState
#include "types.h" // TCID

const TCID TCID_GUEST_MIN=3;

// Stands for "every radio of this node" wherever a radio id is accepted.
const u32 RADIO_ID_ALL=0xffffffffu;

using namespace std;

class CInfoWifi : public CCoordinate
{
		TCID Cid;
		string Name;

		// A frame carries its transmitter's hwsim address and nothing else
		// identifying the client, so this is how a client becomes addressable
		// by MAC. One client can own several radios, hence a set.
		set<string> Macs;

		// false : the client is still connected and still transmitting, but
		// nothing it sends is relayed and nothing reaches it
		bool LinkUp;

		// Which household this node belongs to. Everything starts in household
		// 0, which reproduces the single-room medium exactly : the wall matrix
		// is only consulted between two different households.
		u32 Household;

		// What the node has told us about its own radios, keyed by radio id
		// (the wiphy index). Empty until its first radio-state report, and a
		// client with no entry here is treated as reachable on every channel --
		// see ChannelsCanCarry().
		map<u32,CRadioState> Radios;

		// Transmitter addresses whose beacons and probe responses are not
		// relayed to anyone, while everything else they send still is. This is
		// the surgical version of cutting the link : a station keeps its data
		// path and loses only the evidence that the BSS is still there.
		//
		// Per address rather than per node on purpose. One node here carries
		// six BSSes across two bands, so swallowing every beacon it sends is
		// barely different from cutting its link -- what a scenario wants is
		// one BSS to go quiet while the rest of the AP carries on.
		set<string> BeaconBlackhole;

	public :

		CInfoWifi();
		CInfoWifi(TCID cid, CCoordinate coo);

		void SetCid(TCID cid);

		TCID GetCid() const;

		void SetName(string name);
		string GetName() const;
		int GetSizeName() const;
		bool HasName() const;

		void LearnMac(const string& mac);
		bool OwnsMac(const string& mac) const;

		void SetLinkUp(bool up);
		bool IsLinkUp() const;

		void SetBeaconsRelayed(const string& mac, bool relayed);
		bool AreBeaconsRelayed(const string& mac) const;

		void SetHousehold(u32 household);
		u32 GetHousehold() const;

		// Replaces everything known about one radio's channel and power,
		// keeping the airtime counters it has already accumulated : a radio
		// that changes channel has not stopped having been busy.
		void ReportRadio(const VwifiRadioEntry& entry);

		// NULL when this node has never reported that radio.
		CRadioState* GetRadio(u32 radioId);

		const map<u32,CRadioState>& GetRadios() const;

		// Sets the noise floor of one radio, or of every radio this node has
		// when radioId is RADIO_ID_ALL. Returns how many radios it touched.
		u32 SetNoiseFloor(u32 radioId, int noiseFloorDbm);

		// Pins the transmit power of one radio, or of every radio this node
		// has when radioId is RADIO_ID_ALL, so that the client's own reports
		// stop overwriting it. Returns how many radios it touched.
		u32 SetTxPower(u32 radioId, int txPowerDbm);

		// True when radioId has a pinned transmit power, which is then written
		// to `out`. `out` is left alone otherwise, so a caller can seed it with
		// the reported value and let this override it or not.
		bool PinnedTxPower(u32 radioId, TPower& out) const;

		// True when any radio of this node could receive a transmission sent on
		// `channel`, which is the question the forwarding path actually asks :
		// the relay is per client, and the client clones to all its interfaces.
		bool CanReceiveOn(const CChannel& channel) const;

		// The noise floor of whichever radio would receive a transmission on
		// `channel`, or the default when this node has reported none on it.
		int NoiseFloorOn(const CChannel& channel) const;

		// Credits one transmission of `airtimeUs` microseconds on `channel` to
		// every radio of this node whose passband it touches, scaled by how
		// much of it lands inside that passband. `sameHousehold` picks which
		// counter it lands in : the one a neighbouring household fills is the
		// interference signal, and it is the whole point of the exercise.
		void AccumulateAirtime(const CChannel& channel, u32 airtimeUs, bool sameHousehold);

		// Credits one of this node's own transmissions, and takes the chance to
		// record what the frame said about the radio that sent it -- a frame
		// carries its transmitter's channel, so a radio that is talking needs
		// no separate report.
		void AccountOwnTransmission(const VwifiRadioInfo& info, u32 airtimeUs);

		void Display(ostream& os) const;

		friend ostream& operator<<(ostream& os, const CInfoWifi& infowifi);

};

#endif
