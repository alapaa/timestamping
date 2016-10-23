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

shared_ptr<SenderPacket> deserialize_packet(char *data, size_t datalen)
{
    assert(datalen >= sizeof(SenderPacket));

    shared_ptr<SenderPacket> pkt(new SenderPacket);
    memcpy(pkt.get(), data, sizeof(*pkt));
    pkt->type = (PacketType)ntohl(pkt->type);
    assert(pkt->type == FROM_SENDER);
    pkt->sender_seq = ntohl(pkt->sender_seq);

    cout << "Packet type:" << pkt->type << '\n';
    cout << "Packet sender_seq: " << pkt->sender_seq << '\n';

    return pkt;
}

tuple<shared_ptr<char>, size_t> serialize_reflector_packet(shared_ptr<ReflectorPacket>& pkt)
{
    static_assert(sizeof(timespec) == 4*sizeof(uint32_t), "ouch1");
    static_assert(sizeof(timespec::tv_sec) == 2*sizeof(uint32_t), "ouch2");
    static_assert(sizeof(timespec::tv_nsec) == 2*sizeof(uint32_t), "ouch3");
    size_t BUFLEN = 1472;
    pkt->type = serialize(pkt->type);
    pkt->sender_seq = htonl(pkt->sender_seq);
    pkt->refl_seq = htonl(pkt->refl_seq);

    pkt->t2_sec = htonl(pkt->t2_sec);
    pkt->t2_nsec = htonl(pkt->t2_nsec);
    pkt->t3_sec = htonl(pkt->t3_sec);
    pkt->t3_nsec = htonl(pkt->t3_nsec);

    shared_ptr<char> data(new char[BUFLEN]);
    memset(data.get(), 0, BUFLEN); // TODO: Remove? Not really necessary, but convenient to zero out buffer at start.
    memcpy(data.get(), pkt.get(), sizeof(*pkt));

    return tuple<shared_ptr<char>, size_t>(data, BUFLEN);
}

shared_ptr<ReflectorPacket> deserialize_reflector_packet(shared_ptr<char> data, int datalen)
{
    // Below code should be efficient, but may break strict aliasing optimizations. If necessary, compile this
    // source file with strict aliasing optimization turned off.

    ReflectorPacket *p = reinterpret_cast<ReflectorPacket *>(data.get());
    shared_ptr<ReflectorPacket> rp(new ReflectorPacket);

    assert(datalen >= (int)sizeof(*rp));
    memset(rp.get(), 0, sizeof(rp));
    rp->type = (PacketType)ntohl(p->type);
    rp->sender_seq = ntohl(p->sender_seq);
    rp->refl_seq = ntohl(p->refl_seq);

    rp->t2_sec = ntohl(p->t2_sec);
    rp->t2_nsec = ntohl(p->t2_nsec);
    rp->t3_sec = ntohl(p->t3_sec);
    rp->t3_nsec = ntohl(p->t3_nsec);

// TODO: FIX SERIALIZATION ALSO SO IT COPIES IN T2 AND T3
    return rp;
}

bool check_seqnr(shared_ptr<char> data, int datalen, uint32_t seqnr)
{
    assert(datalen = 1514);
    const int offset = 42;
    uint32_t *p = (uint32_t *)(data.get() + offset);
    if (ntohl(*p) != FROM_REFLECTOR)
    {
        cout << "Wrong packet type, expected " << FROM_REFLECTOR << " got " << ntohl(*p) << '\n';
        return false;
    }
    p += sizeof(ReflectorPacket::type)/sizeof(uint32_t);
    if (ntohl(*p) != seqnr)
    {
        cout << "Expected seqnr " << seqnr << ", got " << ntohl(*p) << "!\n";
        return false;
    }
    else
    {
        return true;
    }
}
};
