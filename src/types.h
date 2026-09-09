#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint32_t u32;

// int
typedef s32 TValue;

// unsigned int
typedef u32 TCID;

// appended to, never reordered : the value goes on the wire between
// vwifi-ctrl and vwifi-server
enum TOrder {
	TORDER_NO, TORDER_LIST, TORDER_SHOW, TORDER_CHANGE_COORDINATE, TORDER_SETNAME, TORDER_PACKET_LOSS, TORDER_STATUS, TORDER_DISTANCE_BETWEEN_CID, TORDER_SET_SCALE, TORDER_CLOSE_ALL_CLIENT, TORDER_LINK,
	TORDER_HOUSEHOLD, TORDER_WALL, TORDER_NOISE, TORDER_BEACON, TORDER_RADIOS, TORDER_ACK,
	TORDER_POSITION
};

// int
typedef s32 TDescriptor;

// unsigned int
typedef u32 TIndex;

// int
typedef s32 TSocket;
// AF_INET : use IP
// AF_VSOCK : use vsock

// unsigned short
typedef u16 TPort;

// char
typedef s8 TPower; // empirical observed values with int : [-123,20]
// The strongest signal any receiver in this medium is allowed to report.
//
// -10 dBm was the original value and is not a level a domestic radio produces:
// it is roughly what you would measure with the antennas touching, close enough
// to a receiver's damage threshold that no real deployment sees it. Every node
// in the default topology shares one coordinate, so the distance term is zero
// and every link lands on this ceiling -- which made the ceiling, rather than
// any transmit power or path loss, the number the whole medium reported.
//
// -30 dBm is what a station a metre or two from a home gateway actually sees,
// and it leaves the useful range of the model -- roughly -30 down to the -92
// noise floor -- spread across the distances and materials a house contains.
const TPower TPower_MAX=-30;
const TPower TPower_MIN=INT8_MIN;

// double
typedef double TDistance; // in meters
typedef double TScale;

// u32
typedef u32 TFrequency; // MHz

// unsigned short
typedef u16 TMinimalSize;

typedef u8 TByte;

struct VwifiRadioInfo
{
    uint32_t radio_id;
    uint32_t frequency;
    uint32_t channel_width;
    int32_t  tx_power;
};

// One radio, as its own client describes it. Sent up periodically rather than
// with a frame : what the server is missing is not the transmitter's channel --
// that already rides on every frame in VwifiRadioInfo -- but every potential
// receiver's, and a radio that is not transmitting never tells anyone anything.
struct VwifiRadioEntry
{
    uint32_t radio_id;
    uint32_t frequency;     // MHz, 0 when the interface is down or unknown
    uint32_t channel_width; // MHz
    int32_t  tx_power;      // dBm
};

// One radio's channel occupancy, as the server has measured it, on its way
// down to the driver.
//
// Rates in permille of airtime rather than counters, because the driver
// accumulates them against jiffies : that is what lets a consumer sampling at
// any instant see a counter that has moved, without the server having to push
// on that consumer's schedule. It also means a dropped push costs nothing --
// the previous rate simply keeps applying.
struct VwifiSurveyEntry
{
    uint32_t radio_id;
    uint32_t frequency;      // MHz, which channel this describes
    uint32_t busy_permille;  // all of it: own tx, own rx, everyone else
    uint32_t rx_permille;    // received from this radio's own household
    uint32_t ext_permille;   // received from any other household
    uint32_t tx_permille;
    int32_t  noise;          // dBm
};

// How many radios one report may describe. A node with more than this has
// bigger problems than a truncated report.
const uint32_t VWIFI_MAX_RADIOS_PER_CLIENT = 16;

#endif
