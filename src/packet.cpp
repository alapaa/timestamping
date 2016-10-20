#include <cstring>
#include <cassert>

#include <arpa/inet.h>

#include "nr_timestamp.h"
#include "packet.h"

using Netrounds::Packet;
using Netrounds::PacketType;

void prepare_packet(char* buf, size_t buflen, uint32_t seq)
{
    Packet pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.type = PacketType::from_sender;
    pkt.sender_seq = htonl(seq);
    assert(sizeof(pkt) <= buflen);
    memcpy(buf, &pkt, sizeof(pkt));
}
