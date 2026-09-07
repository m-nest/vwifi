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
	TORDER_HOUSEHOLD, TORDER_WALL, TORDER_NOISE, TORDER_BEACON, TORDER_RADIOS, TORDER_ACK
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
const TPower TPower_MAX=-10; // dBm is always negative. -10 is an empirical value
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

// How many radios one report may describe. A node with more than this has
// bigger problems than a truncated report.
const uint32_t VWIFI_MAX_RADIOS_PER_CLIENT = 16;

#endif
