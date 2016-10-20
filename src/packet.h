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

typedef uint64_t timestamp_t;

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

    // Filled in by receiver in returned packet The _prime are SW timestamps from userspace, and are optional. With
    // them, we can compute approximate time spent between HW receive/'return send' timestamp and SW userspace
    // receive/send (need to correlate hw tstamp and system clocks, which is difficul to do)
    timestamp_t t2;
    // timestamp_t t2_prime;
    timestamp_t t3;
    // timestamp_t t3_prime;
};

void prepare_packet(char* buf, size_t buflen, uint32_t seq);
std::shared_ptr<SenderPacket> decode_packet(char *data, size_t datalen);
std::tuple<std::shared_ptr<char>, size_t> serialize_reflector_packet(std::shared_ptr<ReflectorPacket>& pkt);
};

#endif
