#ifndef _TIMESTAMP_H_
#define _TIMESTAMP_H_

#include <cstdint>

namespace Netrounds
{
enum class PacketType
{
    from_sender,
    from_reflector,
    from_reflector_only_timestamps // Used for packets that are 'unsolicited', i.e. not a reflected pkt.
};

typedef uint64_t timestamp_t;

struct Packet
{
    PacketType type;
    uint32_t sender_seq;

    // Filled in by receiver in returned packet The _prime are SW timestamps from userspace, and are optional. With
    // them, we can compute approximate time spent between HW receive/'return send' timestamp and SW userspace
    // receive/send.
    timestamp_t t2;
    timestamp_t t2_prime;
    timestamp_t t3;
    timestamp_t t3_prime;
};
};
#endif
