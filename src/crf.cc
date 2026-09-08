#include <math.h>     // exp, log10
#include <strings.h> // strcasecmp

#include "crf.h"

// ---------------------------------------------------------------------------
// Spectrum
// ---------------------------------------------------------------------------

CChannel::CChannel() : Centre(0), Width(20)
{
}

CChannel::CChannel(TFrequency centre, u32 width) : Centre(centre), Width(width ? width : 20)
{
}

bool CChannel::IsKnown() const
{
	return ( Centre != 0 );
}

TFrequency CChannel::Low() const
{
	return Centre - Width/2;
}

TFrequency CChannel::High() const
{
	return Centre + Width/2;
}

bool CChannel::operator==(const CChannel& channel) const
{
	return ( Centre == channel.Centre ) && ( Width == channel.Width );
}

u32 SpectralOverlap(const CChannel& a, const CChannel& b)
{
	if( ! a.IsKnown() || ! b.IsKnown() )
		return 0;

	TFrequency low  = ( a.Low()  > b.Low()  ? a.Low()  : b.Low()  );
	TFrequency high = ( a.High() < b.High() ? a.High() : b.High() );

	if( high <= low )
		return 0;

	return high-low;
}

bool ChannelsCanCarry(const CChannel& tx, const CChannel& rx)
{
	// An unknown channel on either side means the server has not been told yet,
	// not that the two cannot hear each other. Relay, and let the driver at the
	// far end decide -- which is exactly the behaviour every client had before
	// radio-state reports existed.
	if( ! tx.IsKnown() || ! rx.IsKnown() )
		return true;

	return ( tx.Centre == rx.Centre );
}

double OverlapFraction(const CChannel& tx, const CChannel& rx)
{
	u32 overlap=SpectralOverlap(tx,rx);
	if( overlap == 0 )
		return 0.0;

	// Against the transmission's own width : a 20 MHz frame sitting entirely
	// inside an 80 MHz receiver is all of that frame's energy, not a quarter of
	// it, and it is the frame's energy that occupies the medium.
	return static_cast<double>(overlap) / static_cast<double>(tx.Width);
}

// ---------------------------------------------------------------------------
// Airtime
// ---------------------------------------------------------------------------

u32 NominalRateKbps(const CChannel& channel)
{
	// Top of the legacy rate table for the band, which is what rate_idx 7 means.
	u32 base = ( channel.Centre >= 5000 ? 54000u : 18000u );

	if( ! channel.IsKnown() )
		base=18000u;

	// Doubling the width roughly doubles the symbol rate.
	u32 width=( channel.Width ? channel.Width : 20 );
	return base * width / 20;
}

u32 FrameAirtimeUs(u32 frameBytes, const CChannel& channel)
{
	// Preamble, SIFS and the ACK the frame provokes. A real figure varies with
	// the PHY; this is the order of magnitude for a legacy exchange and it is
	// what keeps a channel of 64-byte ACKs from looking idle.
	const u32 OVERHEAD_US=110;

	u32 rateKbps=NominalRateKbps(channel);
	if( rateKbps == 0 )
		return OVERHEAD_US;

	// bytes * 8 bits / (kbit/s) gives milliseconds; * 1000 gives microseconds.
	u64 payloadUs=( static_cast<u64>(frameBytes) * 8 * 1000 ) / rateKbps;

	return static_cast<u32>(payloadUs) + OVERHEAD_US;
}

// ---------------------------------------------------------------------------
// Link budget
// ---------------------------------------------------------------------------

int NoiseFloorForWidth(int noiseFloor20MHz, u32 width)
{
	if( width <= 20 )
		return noiseFloor20MHz;

	// +3 dB per doubling, rounded : 40 -> +3, 80 -> +6, 160 -> +9.
	double steps=log10(static_cast<double>(width)/20.0)/log10(2.0);
	return noiseFloor20MHz + static_cast<int>(3.0*steps + 0.5);
}

double PacketErrorRate(int snrDb, bool robust)
{
	// Knee of the curve : the SNR at which half the frames fail. 8 dB is about
	// where a mid legacy rate gives up; a management frame goes out at the
	// lowest basic rate and holds on roughly 6 dB further down.
	const double KNEE_DATA=8.0;
	const double KNEE_MANAGEMENT=2.0;

	// Transition sharpness in dB. Small enough that the curve is recognisably a
	// cliff, wide enough that a link sitting on the knee is genuinely marginal
	// rather than a coin flip between perfect and dead.
	const double SHARPNESS=1.5;

	double knee=( robust ? KNEE_MANAGEMENT : KNEE_DATA );

	return 1.0 / ( 1.0 + exp( (static_cast<double>(snrDb) - knee) / SHARPNESS ) );
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

bool FrameIsManagement(const char* data, ssize_t sizeOfData)
{
	if( data == NULL || sizeOfData < 1 )
		return false;

	// Frame control, bits 2-3 : 00 management, 01 control, 10 data.
	return ( ( static_cast<unsigned char>(data[0]) & 0x0c ) == 0x00 );
}

bool FrameIsBeacon(const char* data, ssize_t sizeOfData)
{
	if( ! FrameIsManagement(data,sizeOfData) )
		return false;

	// Subtype 8 in bits 4-7, with the type bits already known to be 00.
	return ( ( static_cast<unsigned char>(data[0]) & 0xf0 ) == 0x80 );
}

bool FrameIsBssPresence(const char* data, ssize_t sizeOfData)
{
	if( ! FrameIsManagement(data,sizeOfData) )
		return false;

	unsigned char subtype=( static_cast<unsigned char>(data[0]) & 0xf0 );

	// 8 : beacon. 5 : probe response.
	return ( subtype == 0x80 ) || ( subtype == 0x50 );
}

// ---------------------------------------------------------------------------
// Walls
// ---------------------------------------------------------------------------

// 3GPP TR 38.901 Table 7.4.3-1, "Material penetration losses", verbatim:
// loss in dB is A + B*f with f in GHz, stated as valid from 0.6 to 100 GHz.
//
// Four materials rather than a long list, because these span the useful range
// and each one is citable. Roughly: "glass" is a boundary that barely exists,
// "wood" a light interior partition, "concrete" a real wall between flats, and
// "irr-glass" -- infrared-reflective coated glazing -- is the near-isolation
// case, which is genuinely how a modern window behaves to a radio.
static const struct
{
	const char* Name;
	double      A;
	double      B;
} WallMaterials[] = {
	{ "glass",     2.00, 0.20 },
	{ "wood",      4.85, 0.12 },
	{ "concrete",  5.00, 4.00 },
	{ "irr-glass", 23.00, 0.30 },
};

static const unsigned WallMaterialCount = sizeof(WallMaterials)/sizeof(WallMaterials[0]);

CWall::CWall() : A(0.0), B(0.0), Count(1)
{
}

CWall::CWall(double a, double b, u32 count) : A(a), B(b), Count(count ? count : 1)
{
}

int CWall::Loss(TFrequency frequencyMHz) const
{
	double frequencyGHz=static_cast<double>(frequencyMHz)/1000.0;

	double loss=static_cast<double>(Count) * ( A + B*frequencyGHz );

	if( loss < 0.0 )
		return 0;

	return static_cast<int>(loss + 0.5);
}

bool CWall::IsFlat() const
{
	return ( B == 0.0 );
}

bool WallMaterialByName(const char* name, CWall& wall)
{
	if( name == NULL )
		return false;

	for(unsigned i=0; i<WallMaterialCount; i++)
	{
		if( strcasecmp(name,WallMaterials[i].Name) == 0 )
		{
			wall=CWall(WallMaterials[i].A,WallMaterials[i].B,1);
			return true;
		}
	}

	return false;
}

const char* WallMaterialNames()
{
	return "glass wood concrete irr-glass";
}

// ---------------------------------------------------------------------------
// Per-radio state
// ---------------------------------------------------------------------------

CRadioState::CRadioState()
	: RadioId(0), Channel(), TxPower(TPower_MAX), NoiseFloor(DEFAULT_NOISE_FLOOR_DBM),
	  TxUs(0), RxUs(0), ExtUs(0),
	  LastTxUs(0), LastRxUs(0), LastExtUs(0), LastPushMs(0)
{
}

CRadioState::CRadioState(u32 radioId)
	: RadioId(radioId), Channel(), TxPower(TPower_MAX), NoiseFloor(DEFAULT_NOISE_FLOOR_DBM),
	  TxUs(0), RxUs(0), ExtUs(0),
	  LastTxUs(0), LastRxUs(0), LastExtUs(0), LastPushMs(0)
{
}

bool CRadioState::TakeRates(u64 nowMs, u32 minimumIntervalMs,
		u32& busyPermille, u32& rxPermille, u32& extPermille, u32& txPermille)
{
	// First time through there is no baseline to measure against, so start one
	// and report nothing rather than attributing the whole run to one interval.
	if( LastPushMs == 0 )
	{
		LastPushMs=nowMs;
		LastTxUs=TxUs;
		LastRxUs=RxUs;
		LastExtUs=ExtUs;
		return false;
	}

	if( nowMs <= LastPushMs || ( nowMs - LastPushMs ) < minimumIntervalMs )
		return false;

	u64 intervalMs=nowMs-LastPushMs;

	// The counters only ever grow, but a client that reconnected brings a
	// fresh radio map with it, so guard the subtraction rather than assume.
	u64 tx =( TxUs  >= LastTxUs  ? TxUs  - LastTxUs  : 0 );
	u64 rx =( RxUs  >= LastRxUs  ? RxUs  - LastRxUs  : 0 );
	u64 ext=( ExtUs >= LastExtUs ? ExtUs - LastExtUs : 0 );

	txPermille =static_cast<u32>( tx  / intervalMs );
	rxPermille =static_cast<u32>( rx  / intervalMs );
	extPermille=static_cast<u32>( ext / intervalMs );

	// Everything that made the channel unavailable. There is no non-WiFi
	// noise source in this medium yet, so the sum is all of it; the driver
	// clamps anyway, and a future noise model only has to add to this.
	busyPermille=txPermille+rxPermille+extPermille;
	if( busyPermille > 1000 )
		busyPermille=1000;

	LastPushMs=nowMs;
	LastTxUs=TxUs;
	LastRxUs=RxUs;
	LastExtUs=ExtUs;

	return true;
}

u64 CRadioState::BusyUs() const
{
	return TxUs + RxUs + ExtUs;
}
