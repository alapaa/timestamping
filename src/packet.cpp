#include <memory>
#include <type_traits>
#include <iostream>

#include <cstring>
#include <cassert>

#include <arpa/inet.h>

#include "packet.h"

using std::cout;

//using Netrounds::SenderPacket;
//using Netrounds::PacketType;

using std::shared_ptr;
using std::tuple;

template<class T1> T1 serialize(T1 val)
{
    if (sizeof(T1) == sizeof(uint32_t))
    {
        return static_cast<T1>(htonl(val));
    }
    else if (sizeof(T1) == sizeof(uint16_t))
    {
        return static_cast<T1>(htons(val));
    }
    else
    {
        assert(0);
    }

}

template<class T1> T1 deserialize(uint32_t val)
{
    if (sizeof(T1) == sizeof(uint32_t))
    {
        return static_cast<T1>(ntohl(val));
    }
}

namespace Netrounds
{
void prepare_packet(char* buf, size_t buflen, uint32_t seq)
{
    SenderPacket pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.type = serialize(PacketType::FROM_SENDER);
    pkt.sender_seq = htonl(seq);
    assert(sizeof(pkt) <= buflen);
    memcpy(buf, &pkt, sizeof(pkt));
}

shared_ptr<SenderPacket> decode_packet(char *data, size_t datalen)
{
    assert(datalen >= sizeof(SenderPacket));

    shared_ptr<SenderPacket> pkt(new SenderPacket);
    memcpy(pkt.get(), data, sizeof(*pkt));
    pkt->type = deserialize<PacketType>(pkt->type);
    assert(pkt->type == FROM_SENDER);
    pkt->sender_seq = ntohl(pkt->sender_seq);

    cout << "Packet type:" << pkt->type << '\n';
    cout << "Packet sender_seq: " << pkt->sender_seq << '\n';

    return pkt;
}

tuple<shared_ptr<char>, size_t> serialize_reflector_packet(shared_ptr<ReflectorPacket>& pkt)
{
    size_t BUFLEN = 1472;
    pkt->type = serialize(pkt->type);
    pkt->sender_seq = htonl(pkt->sender_seq);
    pkt->refl_seq = htonl(pkt->refl_seq);

    shared_ptr<char> data(new char[BUFLEN]);
    memset(data.get(), 0, BUFLEN); // TODO: Remove? Not really necessary, but convenient to zero out buffer at start.
    memcpy(data.get(), pkt.get(), sizeof(*pkt));

    return tuple<shared_ptr<char>, size_t>(data, BUFLEN);
}
};
