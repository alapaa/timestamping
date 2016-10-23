#ifndef _PACKET_H_
#define _PACKET_H_

#include <memory>
#include <tuple>

#include <cstdint>

namespace Netrounds
{
enum PacketType
{
    FROM_SENDER,
    FROM_REFLECTOR,
    FROM_REFLECTOR_ONLY_TIMESTAMPS // Used for packets that are 'unsolicited', i.e. not a reflected pkt.
};

typedef uint32_t timestamp_t;

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

void prepare_packet(char* buf, size_t buflen, uint32_t seq);
std::shared_ptr<SenderPacket> decode_packet(char *data, size_t datalen);
std::tuple<std::shared_ptr<char>, size_t> serialize_reflector_packet(std::shared_ptr<ReflectorPacket>& pkt);
bool check_seqnr(std::shared_ptr<char> data, int datalen, uint32_t seqnr);
};

#endif
