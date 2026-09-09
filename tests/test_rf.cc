// Tests for the RF model.
//
// These pin down the properties a scenario actually leans on, not exact
// numbers: the medium is mac80211_hwsim, which fabricates what a driver would
// measure, so an absolute figure here would be false precision. What has to
// hold is the shape -- further is worse, wider is noisier, a beacon outlives a
// data frame, and a wall is the same wall from both sides.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "crf.h"
#include "cwifi.h"
#include "cwifiserver.h"

static int Failures=0;

#define CHECK(what) do {                                                      \
	if( !(what) ) {                                                           \
		fprintf(stderr,"FAIL %s:%d : %s\n",__FILE__,__LINE__,#what);          \
		Failures++;                                                           \
	}                                                                         \
} while(0)

// Same, for a value that is a calculation rather than a decision: the model is
// allowed to move by a rounding step without failing a test.
#define CHECK_NEAR(what,expected,tolerance) do {                              \
	double got_=(what);                                                       \
	double want_=(expected);                                                  \
	double slack_=(tolerance);                                                \
	double diff_=got_>want_ ? got_-want_ : want_-got_;                        \
	if( diff_ > slack_ ) {                                                    \
		fprintf(stderr,"FAIL %s:%d : %s = %g, expected %g +/- %g\n",          \
		        __FILE__,__LINE__,#what,got_,want_,slack_);                   \
		Failures++;                                                           \
	}                                                                         \
} while(0)

// 2.4 GHz channels 1, 3 and 11; 5 GHz channel 36.
static const CChannel CH1(2412,20);
static const CChannel CH3(2422,20);
static const CChannel CH11(2462,20);
static const CChannel CH36(5180,20);
static const CChannel CH36_80(5210,80);
static const CChannel UNKNOWN(0,20);

static void TestSpectrum()
{
	// Same channel : the whole passband.
	CHECK( SpectralOverlap(CH1,CH1) == 20 );

	// Channels 1 and 3 are 10 MHz apart and 20 MHz wide, so they share half.
	CHECK( SpectralOverlap(CH1,CH3) == 10 );

	// 1 and 11 are 50 MHz apart : nothing in common.
	CHECK( SpectralOverlap(CH1,CH11) == 0 );

	// Different bands never touch.
	CHECK( SpectralOverlap(CH1,CH36) == 0 );

	// A 20 MHz channel sitting inside an 80 MHz one is wholly inside it.
	CHECK( SpectralOverlap(CH36,CH36_80) == 20 );

	// An unknown channel overlaps nothing -- it is a missing fact, and the
	// callers that must not drop on a missing fact ask ChannelsCanCarry.
	CHECK( SpectralOverlap(UNKNOWN,CH1) == 0 );

	// Delivery follows the driver, which compares centre frequencies for
	// equality : a half-overlapping frame is not a weak frame, it is discarded.
	CHECK(   ChannelsCanCarry(CH1,CH1) );
	CHECK( ! ChannelsCanCarry(CH1,CH3) );
	CHECK( ! ChannelsCanCarry(CH1,CH36) );

	// Unknown on either side means "not reported yet", never "unreachable".
	CHECK( ChannelsCanCarry(UNKNOWN,CH1) );
	CHECK( ChannelsCanCarry(CH1,UNKNOWN) );

	// Occupancy is against the transmission's own width, so a narrow frame
	// inside a wide receiver is all of that frame.
	CHECK( OverlapFraction(CH1,CH1) == 1.0 );
	CHECK( OverlapFraction(CH1,CH3) == 0.5 );
	CHECK( OverlapFraction(CH1,CH11) == 0.0 );
	CHECK( OverlapFraction(CH36,CH36_80) == 1.0 );
}

static void TestAirtime()
{
	// A longer frame occupies the medium for longer.
	CHECK( FrameAirtimeUs(1500,CH1) > FrameAirtimeUs(100,CH1) );

	// Even a zero-length frame costs the fixed preamble/SIFS/ACK exchange,
	// which is what keeps a channel of small frames from reading as idle.
	CHECK( FrameAirtimeUs(0,CH1) > 0 );

	// 5 GHz carries the same frame faster than 2.4 GHz, and a wider channel
	// faster still. This is the whole reason a band-steering decision has an
	// airtime consequence.
	CHECK( FrameAirtimeUs(1500,CH36) < FrameAirtimeUs(1500,CH1) );
	CHECK( FrameAirtimeUs(1500,CH36_80) < FrameAirtimeUs(1500,CH36) );
}

static void TestPathLoss()
{
	// ITU-R P.1238 Table 2, Residential: the two anchors the model is built on.
	CHECK_NEAR(ResidentialPathLossCoefficient(2400), 28.0, 0.01);
	CHECK_NEAR(ResidentialPathLossCoefficient(5200), 30.0, 0.01);

	// Between them it interpolates, above them it extends the same slope.
	// 6135 MHz is an extrapolation, flagged as such in crf.h.
	CHECK(ResidentialPathLossCoefficient(5180) > 28.0);
	CHECK(ResidentialPathLossCoefficient(5180) < 30.0);
	CHECK(ResidentialPathLossCoefficient(6135) > 30.0);

	// Never below free space: a house does not focus radio.
	CHECK(ResidentialPathLossCoefficient(2412) >= 20.0);
	CHECK(ResidentialPathLossCoefficient(900)  >= 20.0);

	// At one metre the model agrees with free space to within a dB, which is
	// what makes it safe to use from zero distance upwards.
	//   FSPL(1 m, 2412 MHz) = 20log10(2412) + 20log10(1) - 27.55 = 40.1 dB
	CHECK_NEAR(IndoorPathLossDb(1.0, 2412), 40, 1);

	// Below a metre it clamps rather than going negative and handing out gain.
	CHECK(IndoorPathLossDb(0.01, 2412) == IndoorPathLossDb(1.0, 2412));

	// Monotonic in both arguments.
	CHECK(IndoorPathLossDb(10.0, 2412) > IndoorPathLossDb(1.0, 2412));
	CHECK(IndoorPathLossDb(10.0, 6135) > IndoorPathLossDb(10.0, 2412));

	// The property the whole change exists for: the gap between a low band and
	// a high one widens with distance. Under free space it is constant, and a
	// station could never be made to prefer 2.4 GHz by walking away from the AP.
	int gapNear=IndoorPathLossDb(2.0,  6135) - IndoorPathLossDb(2.0,  2412);
	int gapFar =IndoorPathLossDb(30.0, 6135) - IndoorPathLossDb(30.0, 2412);
	CHECK(gapFar > gapNear + 2);

	// Same shape for 5 GHz, and 6 GHz always falls off at least as fast as 5.
	int gap5Near=IndoorPathLossDb(2.0,  5180) - IndoorPathLossDb(2.0,  2412);
	int gap5Far =IndoorPathLossDb(30.0, 5180) - IndoorPathLossDb(30.0, 2412);
	CHECK(gap5Far > gap5Near);
	CHECK(gapFar >= gap5Far);

	// A sanity anchor against the Recommendation, computed by hand:
	//   2412 MHz, 10 m: 20log10(2412) + 28*log10(10) - 28 = 67.6 + 28 - 28 = 67.6
	CHECK_NEAR(IndoorPathLossDb(10.0, 2412), 68, 1);
	//   5180 MHz, 10 m: 20log10(5180) + 29.99*1 - 28      = 74.3 + 30.0 - 28 = 76.3
	CHECK_NEAR(IndoorPathLossDb(10.0, 5180), 76, 1);
}

static void TestNoise()
{
	CHECK( NoiseFloorForWidth(-92,20) == -92 );

	// +3 dB per doubling of bandwidth.
	CHECK( NoiseFloorForWidth(-92,40) == -89 );
	CHECK( NoiseFloorForWidth(-92,80) == -86 );
	CHECK( NoiseFloorForWidth(-92,160) == -83 );
}

static void TestErrorCurve()
{
	// Bounded, and monotonically better as the signal improves.
	for(int snr=-10; snr<40; snr++)
	{
		double per=PacketErrorRate(snr,false);
		CHECK( per >= 0.0 && per <= 1.0 );
		CHECK( per <= PacketErrorRate(snr-1,false) );
	}

	// A strong link is essentially lossless and a dead one essentially total,
	// with the transition in between rather than at either end.
	CHECK( PacketErrorRate(30,false) < 0.01 );
	CHECK( PacketErrorRate(-10,false) > 0.99 );

	// Management frames go out at the lowest basic rate, so they survive where
	// data does not. This is what lets a station keep hearing beacons after its
	// data link has become unusable -- and, inverted, what makes a beacon
	// blackhole a different test from a link cut.
	for(int snr=0; snr<10; snr++)
		CHECK( PacketErrorRate(snr,true) < PacketErrorRate(snr,false) );
}

static void TestFrameClassification()
{
	// Frame control byte : type in bits 2-3, subtype in bits 4-7.
	const char beacon[]      = { (char)0x80, 0x00 };
	const char probeRequest[]= { (char)0x40, 0x00 };
	const char data[]        = { (char)0x08, 0x00 };
	const char ack[]         = { (char)0xd4, 0x00 };

	CHECK(   FrameIsBeacon(beacon,sizeof(beacon)) );
	CHECK(   FrameIsManagement(beacon,sizeof(beacon)) );

	CHECK( ! FrameIsBeacon(probeRequest,sizeof(probeRequest)) );
	CHECK(   FrameIsManagement(probeRequest,sizeof(probeRequest)) );

	CHECK( ! FrameIsBeacon(data,sizeof(data)) );
	CHECK( ! FrameIsManagement(data,sizeof(data)) );

	// A control frame is not management : it does not get the robust curve.
	CHECK( ! FrameIsManagement(ack,sizeof(ack)) );

	// What a beacon blackhole has to swallow is both halves of "this BSS is
	// still here" : the beacon and the probe response that answers a station
	// which has stopped hearing them.
	const char probeResponse[] = { (char)0x50, 0x00 };
	CHECK(   FrameIsBssPresence(beacon,sizeof(beacon)) );
	CHECK(   FrameIsBssPresence(probeResponse,sizeof(probeResponse)) );
	CHECK( ! FrameIsBssPresence(probeRequest,sizeof(probeRequest)) );
	CHECK( ! FrameIsBssPresence(data,sizeof(data)) );

	// Nothing at all is not a beacon, and must not read past the buffer.
	CHECK( ! FrameIsBeacon(NULL,0) );
	CHECK( ! FrameIsManagement(beacon,0) );
}

static void TestWalls()
{
	ResetWalls();

	// Everything starts in one room : no household costs anything, on any band.
	CHECK( WallAttenuationBetween(0,0,2412) == 0 );
	CHECK( WallAttenuationBetween(0,1,2412) == 0 );
	CHECK( WallAttenuationBetween(0,1,5180) == 0 );

	// A flat wall is B = 0 : the same loss whatever crosses it. This is what a
	// plain number on the command line means, and what walls used to be.
	SetDefaultWall(CWall(30,0,1));
	CHECK( WallAttenuationBetween(0,1,2412) == 30 );
	CHECK( WallAttenuationBetween(2,7,5180) == 30 );
	CHECK( WallAttenuationBetween(0,1,6135) == 30 );

	// A node is never behind a wall from itself, whatever the default says.
	CHECK( WallAttenuationBetween(3,3,2412) == 0 );

	// An explicit pair overrides the default, and a wall is the same wall from
	// either side.
	SetWall(0,1,CWall(12,0,1));
	CHECK( WallAttenuationBetween(0,1,2412) == 12 );
	CHECK( WallAttenuationBetween(1,0,2412) == 12 );
	CHECK( WallAttenuationBetween(0,2,2412) == 30 );

	ResetWalls();
	CHECK( WallAttenuationBetween(0,1,2412) == 0 );
}

static void TestWallMaterials()
{
	CWall wall;

	CHECK( ! WallMaterialByName("brick",wall) );
	CHECK( ! WallMaterialByName(NULL,wall) );

	// Case-insensitive, like every other name this tool takes.
	CHECK( WallMaterialByName("CONCRETE",wall) );

	// 3GPP TR 38.901 Table 7.4.3-1 : concrete is 5 + 4f, f in GHz.
	CHECK( WallMaterialByName("concrete",wall) );
	CHECK( wall.Loss(2412) == 15 );   // 5 + 4*2.412 = 14.6
	CHECK( wall.Loss(5180) == 26 );   // 5 + 4*5.180 = 25.7
	CHECK( wall.Loss(6135) == 30 );   // 5 + 4*6.135 = 29.5
	CHECK( ! wall.IsFlat() );

	// The point of the whole exercise: the band gap across a real wall is large
	// enough to steer on. Roughly 11 dB between 2.4 and 5 GHz through concrete.
	CHECK( wall.Loss(5180) - wall.Loss(2412) >= 10 );

	// Wood is nearly frequency-flat, so a light partition does not separate the
	// bands the way a structural wall does. A scenario that needs the two to
	// fail together should say so with wood or a plain number, not by accident.
	CHECK( WallMaterialByName("wood",wall) );
	CHECK( wall.Loss(5180) - wall.Loss(2412) <= 1 );

	// Every material must get worse with frequency, never better.
	const char* names[] = { "glass", "wood", "concrete", "irr-glass" };
	for(unsigned i=0; i<4; i++)
	{
		CHECK( WallMaterialByName(names[i],wall) );
		CHECK( wall.Loss(2412) <= wall.Loss(5180) );
		CHECK( wall.Loss(5180) <= wall.Loss(6135) );
	}

	// Walls stack. Note the rounding happens once, after multiplying, so three
	// concrete walls are 77 dB (3 * 25.72) and not 78 (3 * 26) -- which is the
	// more accurate order and the reason this is not simply 3x the single-wall
	// figure.
	CHECK( WallMaterialByName("concrete",wall) );
	CHECK( wall.Loss(5180) == 26 );
	wall.Count=3;
	CHECK( wall.Loss(5180) == 77 );

	// A radio that has not reported a frequency gets the frequency-independent
	// part alone rather than a loss invented from a frequency of zero.
	CHECK( WallMaterialByName("concrete",wall) );
	CHECK( wall.Loss(0) == 5 );

	// Default-constructed is no wall at all, which is what an unset household
	// pair has to be.
	CWall none;
	CHECK( none.Loss(2412) == 0 );
	CHECK( none.IsFlat() );
}

static void TestRadioStateCodec()
{
	VwifiRadioEntry out[3];
	memset(out,0,sizeof(out));
	out[0].radio_id=0; out[0].frequency=2412; out[0].channel_width=20; out[0].tx_power=20;
	out[1].radio_id=1; out[1].frequency=5180; out[1].channel_width=80; out[1].tx_power=23;
	out[2].radio_id=7; out[2].frequency=0;    out[2].channel_width=20; out[2].tx_power=0;

	char buffer[512];
	ssize_t size=VwifiWriteRadioState(buffer,sizeof(buffer),out,3);
	CHECK( size == VwifiRadioStateSize(3) );

	VwifiRadioEntry in[VWIFI_MAX_RADIOS_PER_CLIENT];
	u32 count=0;
	CHECK( VwifiReadRadioState(buffer,size,in,count) );
	CHECK( count == 3 );
	CHECK( in[1].radio_id == 1 && in[1].frequency == 5180 && in[1].channel_width == 80 );
	CHECK( in[2].frequency == 0 );

	// A frame must not be mistaken for a report. The discriminator is the
	// generic netlink command, which is what the client already dispatches on.
	char frame[64];
	memset(frame,0,sizeof(frame));
	count=99;
	CHECK( ! VwifiReadRadioState(frame,sizeof(frame),in,count) );
	CHECK( count == 0 );

	// Truncation must be refused rather than read past the end : the count
	// comes off a socket, so it is not to be trusted against the bytes present.
	CHECK( ! VwifiReadRadioState(buffer,size-1,in,count) );

	// Nothing to report is not a report.
	CHECK( VwifiWriteRadioState(buffer,sizeof(buffer),out,0) == 0 );

	// More radios than the cap allows, and a buffer too small, are both refused
	// rather than truncated.
	CHECK( VwifiWriteRadioState(buffer,sizeof(buffer),out,VWIFI_MAX_RADIOS_PER_CLIENT+1) == 0 );
	CHECK( VwifiWriteRadioState(buffer,4,out,3) == 0 );
}

static void TestRadioState()
{
	CRadioState radio(2);

	CHECK( radio.RadioId == 2 );
	CHECK( ! radio.Channel.IsKnown() );
	CHECK( radio.NoiseFloor == DEFAULT_NOISE_FLOOR_DBM );
	CHECK( radio.BusyUs() == 0 );

	radio.TxUs=100; radio.RxUs=20; radio.ExtUs=3;
	CHECK( radio.BusyUs() == 123 );
}

int main()
{
	TestSpectrum();
	TestAirtime();
	TestPathLoss();
	TestNoise();
	TestErrorCurve();
	TestFrameClassification();
	TestWalls();
	TestWallMaterials();
	TestRadioStateCodec();
	TestRadioState();

	if( Failures )
	{
		fprintf(stderr,"%d check(s) failed\n",Failures);
		return 1;
	}

	printf("all RF model checks passed\n");
	return 0;
}
