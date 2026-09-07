#ifndef _CRF_H_
#define _CRF_H_

#include <sys/types.h> // ssize_t

#include "types.h" // TFrequency, TPower

// The RF model, kept free of sockets and netlink so that it can be reasoned
// about -- and tested -- on its own. Everything here is a pure function of its
// arguments except CRadioState, which is a plain record.
//
// A note on what these numbers are worth. The medium this models is
// mac80211_hwsim, which fabricates the values a driver would measure: noise is
// hardcoded -92 dBm and busy time is exactly time/8. So nothing below can be
// validated against a real radio, and it is not meant to be. It is meant to be
// monotonic in the right direction and to have the right shape: a station that
// moves further away loses more frames, a household behind a wall is quieter
// than one in the same room, and a channel carrying traffic reports more
// airtime than an idle one. Those are the properties a steering or interference
// scenario asserts on.

// ---------------------------------------------------------------------------
// Spectrum
// ---------------------------------------------------------------------------

// What a radio is tuned to. A radio the server has never had a report from has
// a centre of 0, which every predicate below treats as "unknown" rather than as
// a frequency -- the server has to keep relaying to a client that has not
// introduced itself yet, or a node whose report is late drops off the air.
struct CChannel
{
	TFrequency Centre; // MHz
	u32        Width;  // MHz : 20, 40, 80, 160

	CChannel();
	CChannel(TFrequency centre, u32 width);

	bool IsKnown() const;

	// Passband edges, in MHz.
	TFrequency Low() const;
	TFrequency High() const;

	bool operator==(const CChannel& channel) const;
};

// MHz of spectrum the two passbands share. 0 when they do not touch, which is
// both "different band" and "far enough apart on the same band".
u32 SpectralOverlap(const CChannel& a, const CChannel& b);

// True when a receiver tuned to rx can actually demodulate a frame sent on tx.
//
// This deliberately mirrors what the driver does rather than what physics does.
// hwsim_chans_compat() compares centre frequencies for equality and drops the
// frame before any signal metadata is consulted, so a partially overlapping
// frame is not a weak frame at the far end -- it is not a frame at all. Relaying
// one would only have the receiving client hand hwsim something it discards.
// Partial overlap still costs airtime, and that is accounted separately.
bool ChannelsCanCarry(const CChannel& tx, const CChannel& rx);

// How much of a transmission on tx lands inside rx's passband, as a fraction in
// [0,1]. This is what makes a neighbouring household on an overlapping channel
// cost less airtime than one sitting on the same channel.
double OverlapFraction(const CChannel& tx, const CChannel& rx);

// ---------------------------------------------------------------------------
// Airtime
// ---------------------------------------------------------------------------

// The PHY rate a frame is assumed to have been sent at, in kbps.
//
// vwifi hardcodes rate_idx = 7 on every frame it injects and the netlink RX path
// cannot carry an HT/VHT/HE encoding at all, so there is no real rate to read --
// index 7 is the top of the legacy table, which is 18 Mb/s at 2.4 GHz and
// 54 Mb/s at 5 GHz. Scaling that by channel width is the one honest thing left
// to do with it, and it is what makes a 5 GHz link occupy less airtime than a
// 2.4 GHz one for the same frame. Rich rate reporting is Tier 3.
u32 NominalRateKbps(const CChannel& channel);

// Microseconds of channel time one frame of frameBytes octets occupies,
// including the fixed cost every frame pays whatever its length: preamble,
// SIFS and the ACK it provokes. Short frames are dominated by that overhead,
// which is why a channel full of beacons and ACKs is busier than its byte count
// suggests.
u32 FrameAirtimeUs(u32 frameBytes, const CChannel& channel);

// ---------------------------------------------------------------------------
// Link budget
// ---------------------------------------------------------------------------

// The noise floor a receiver sees over `width` MHz, given its floor over 20 MHz.
// Thermal noise is proportional to bandwidth, so every doubling costs 3 dB --
// which is the reason a wide channel needs more signal for the same SNR.
int NoiseFloorForWidth(int noiseFloor20MHz, u32 width);

// Probability in [0,1] that a frame at this SNR fails.
//
// A logistic in SNR: essentially certain loss well below the knee, essentially
// none well above it, and a few dB of transition in between. `robust` picks the
// knee for a management frame, which goes out at the lowest basic rate and so
// survives roughly 6 dB further down than data does -- the gap that makes a
// station hold onto beacons after its data link has become unusable.
double PacketErrorRate(int snrDb, bool robust);

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

// data points at the 802.11 header, so data[0] is the frame control field:
// bits 2-3 are the type and bits 4-7 the subtype.
bool FrameIsManagement(const char* data, ssize_t sizeOfData);
bool FrameIsBeacon(const char* data, ssize_t sizeOfData);

// The two frames that keep a station believing a BSS is still there: its
// beacons, and the probe responses it sends when asked directly.
//
// Suppressing only beacons is not enough to end an association and it is worth
// being clear why. Losing beacons makes mac80211 probe the AP rather than give
// up on it -- ieee80211_beacon_loss() schedules a probe, and the disconnect
// with reason 4 comes from that probe going unanswered, not from the beacons
// themselves. An AP that still answers probes is an AP that is still there.
bool FrameIsBssPresence(const char* data, ssize_t sizeOfData);

// ---------------------------------------------------------------------------
// Walls
// ---------------------------------------------------------------------------

// What separates two households, as a frequency-dependent loss.
//
// A wall is not a single number, because material penetration loss rises with
// frequency and how fast it rises is a property of the material. 3GPP TR 38.901
// Table 7.4.3-1 gives the losses as L(dB) = A + B*f, with f in GHz, and that
// two-parameter form is what is stored here. It also subsumes the flat wall
// this replaced: B = 0 is a loss that does not care about frequency.
//
// Count is how many such walls are in the way, so "two rooms over" is one
// command rather than arithmetic done by hand.
//
// This matters more than it looks. Concrete costs about 15 dB at 2.4 GHz and
// 26 dB at 5 GHz -- an 11 dB difference, which is most of the reason a station
// that walks into the next room loses 5 GHz before it loses 2.4 GHz. A flat
// wall makes both bands fail together, and a band-steering policy tested
// against one is being tested against a medium that has no opinion about bands.
struct CWall
{
	double A;     // dB, the frequency-independent part
	double B;     // dB per GHz
	u32    Count; // how many of them

	CWall();
	CWall(double a, double b, u32 count);

	// Loss in dB at this frequency. A frequency of 0 -- a radio that has not
	// reported one -- gets the frequency-independent part alone, which is the
	// most this can honestly say without knowing the band.
	int Loss(TFrequency frequencyMHz) const;

	bool IsFlat() const;
};

// Fills `wall` from a material name (case-insensitive). False when the name is
// not one of them. The names are listed by WallMaterialNames(), and the
// coefficients are 3GPP TR 38.901 Table 7.4.3-1 verbatim.
bool WallMaterialByName(const char* name, CWall& wall);

// Space-separated, for help text and error messages.
const char* WallMaterialNames();

// ---------------------------------------------------------------------------
// Per-radio state
// ---------------------------------------------------------------------------

// What the server knows about one radio of one client. Channel and TxPower
// arrive over the air in the periodic radio-state report; the airtime counters
// are accumulated here as frames are relayed.
//
// The counters are monotonic microsecond totals and are never reset. That is
// deliberate: every consumer of airtime -- pwhm's airstats, hostapd's BSS Load
// poller, beerocks' monitor -- diffs a cumulative counter against its own
// previous sample, and a counter that restarts reads as a negative interval or
// as no time having passed at all.
struct CRadioState
{
	u32      RadioId;
	CChannel Channel;
	TPower   TxPower;

	// dBm over 20 MHz. -92 by default, which is what hwsim reports, so the
	// SNR the model computes and the noise the data model shows agree.
	int NoiseFloor;

	// Monotonic microseconds of channel time, split by origin. Own is what
	// this radio transmitted, Rx what it received from its own household, and
	// Ext what reached it from every other household -- the last one being the
	// interference signal, the thing that cannot be computed anywhere but here.
	u64 TxUs;
	u64 RxUs;
	u64 ExtUs;

	CRadioState();
	explicit CRadioState(u32 radioId);

	u64 BusyUs() const;
};

const int DEFAULT_NOISE_FLOOR_DBM = -92;

#endif
