#ifndef _TIMESTAMP_H_
#define _TIMESTAMP_H_

#include <cstdint>

namespace Netrounds
{
typedef uint64_t timestamp_t;

struct Packet
{
    uint32_t packet_nr;

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
