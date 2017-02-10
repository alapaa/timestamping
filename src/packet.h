#ifndef _PACKET_H_
#define _PACKET_H_

#include <memory>
#include <tuple>

#include <cstdint>

const int BILLION = 1000000000; // Swedish "miljard"

bool operator<(const timespec& t1, const timespec& t2);

inline bool operator<(const timespec& t1, const timespec& t2)
{
//    assert (abs(t1.tv_nsec) < BILLION && abs(t2.tv_nsec) < BILLION);
    if (t1.tv_sec < t2.tv_sec) return true;
    if (t1.tv_sec == t2.tv_sec && t1.tv_nsec < t2.tv_nsec) return true;
    return false;
}

namespace Netrounds
{

const size_t HDR_SZ = 18+20+8; // TODO: IPv4 hardcoded
enum PacketType
{
    FROM_SENDER,
    FROM_REFLECTOR,
    FROM_REFLECTOR_ONLY_TIMESTAMPS // Used for packets that are 'unsolicited', i.e. not a reflected pkt.
};

typedef int32_t timestamp_t;

struct SenderPacket
{
    PacketType type; // FROM_SENDER
    uint32_t sender_seq;
};

struct ReflectorPacket
{
    PacketType type; // FROM_REFLECTOR for normal reflected packet, FROM_REFLECTOR_ONLY_TIMESTAMPS for 'extra' packet.
    uint32_t sender_seq;
    uint32_t refl_seq;

    // Note on timestamps: TWAMP uses 2 32-bit values for the timestamps, one for the integer part of seconds, and
    // one for the fractional part. struct timespec can in general even be defined as floating-point, Posix at least
    // guarantees that tv_sec and tv_nsec are integers. Linux seems to use the 'natural' size for the system, so
    // e.g. x86 Linux uses 32-bit tv_sec and 32-bit tv_nsec, and x86_64 uses 64-bit tv_sec and 64-bit tv_nsec.
    // AFAIK, Linux timestamps are signed (e.g. time_t is long, not unsigned long). Signed values are good when
    // e.g. subtracting time values. (In our case, we know e.g. T3 is larger than T2, so no problem subtracting). In
    // the packet structs defined in this file, we use unsigned values since those are easiest to handle while
    // bit-fiddling/serializing/unserializing network packets.
    //
    timestamp_t t2_sec;
    timestamp_t t2_nsec;
    timestamp_t t3_sec;
    timestamp_t t3_nsec;
};

inline timespec subtract_ts(const timespec& newer, const timespec& older)
{
    timespec result;

    if ((newer.tv_nsec - older.tv_nsec) < 0)
    {
        result.tv_sec = newer.tv_sec - older.tv_sec - 1;
        result.tv_nsec = newer.tv_nsec - older.tv_nsec + BILLION;
    }
    else
    {
        result.tv_sec = newer.tv_sec - older.tv_sec;
        result.tv_nsec = newer.tv_nsec - older.tv_nsec;
    }

    return result;
}

inline timespec add_ts(const timespec& t1, const timespec& t2)
{
    assert(t1.tv_nsec >= 0 && t2.tv_nsec >= 0);

    timespec result {0, 0};
    int tmp = t1.tv_nsec + t2.tv_nsec;
    assert (tmp < BILLION*2);
    if (tmp > BILLION)
    {
        result.tv_sec += t1.tv_sec + t2.tv_sec + 1;
        result.tv_nsec = tmp - BILLION;
    }
    else
    {
        result.tv_sec += t1.tv_sec + t2.tv_sec;
        result.tv_nsec = tmp;
    }

    return result;
}


bool operator==(const timespec& t1, const timespec& t2);
bool operator!=(const timespec& t1, const timespec& t2);
std::ostream& operator<<(std::ostream& os, const timespec& ts);
std::string ts2string_rounding(const timespec& ts);
timespec add_ts(const timespec& lhs, const timespec& rhs);
timespec subtract_ts(const timespec& newer, const timespec& older);
timespec make_timespec(timestamp_t tv_sec, timestamp_t tv_nsec);
void prepare_packet(char* buf, size_t buflen, uint32_t seq);
std::shared_ptr<SenderPacket> deserialize_packet(char *data, size_t datalen);
std::tuple<std::shared_ptr<char>, size_t> serialize_reflector_packet(std::shared_ptr<ReflectorPacket>& pkt);
std::shared_ptr<ReflectorPacket> deserialize_reflector_packet(std::shared_ptr<char> data, int datalen);
bool check_seqnr(std::shared_ptr<char> data, int datalen, uint32_t seqnr);
void dbl2ts(double dbl, timespec& ts);

};

#endif
