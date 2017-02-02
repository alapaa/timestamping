#include <memory>
#include <type_traits>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cassert>

#include <arpa/inet.h>

#include "packet.h"
#include "logging.h"

//using std::cout;
using std::ostream;
using std::stringstream;
using std::shared_ptr;
using std::tuple;
using std::string;

const int BILLION = 1000000000; // Swedish "miljard"

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

inline bool operator<(const timespec& t1, const timespec& t2)
{
//    assert (abs(t1.tv_nsec) < BILLION && abs(t2.tv_nsec) < BILLION);
    if (t1.tv_sec < t2.tv_sec) return true;
    if (t1.tv_sec == t2.tv_sec && t1.tv_nsec < t2.tv_nsec) return true;
    return false;
}

namespace Netrounds
{

bool operator==(const timespec& t1, const timespec& t2)
{
    if (t1.tv_sec == t2.tv_sec && t1.tv_nsec == t2.tv_nsec)
    {
        assert((t1 < t2 || t2 < t1) == false);
        return true;
    }
    else
    {
        return false;
    }
}

bool operator!=(const timespec& t1, const timespec& t2)
{
    return (!(t1 == t2));
}

string ts2string_rounding(const timespec& ts)
{
    stringstream ss;

    ss << std::fixed;
    ss << std::setprecision(9);
    ss  << "    " << (ts.tv_sec + (double)ts.tv_nsec/BILLION);

    return ss.str();
}

string ts2string(const timespec& ts)
{
    stringstream ss;

    const size_t SZ = 100;
    char buf[SZ];
    snprintf(buf, SZ, "%ld.%09ld ", ts.tv_sec, ts.tv_nsec);
    ss << "    " << buf;

    if (ts.tv_sec == 0)
    {
        ss << " (" << ts.tv_nsec/1000 << " [microseconds])";
    }

    return ss.str();
}

ostream& operator<<(ostream& os, const timespec& ts)
{
    return os << ts2string(ts);
}

timespec subtract_ts(const timespec& newer, const timespec& older)
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

timespec add_ts(const timespec& t1, const timespec& t2)
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

timespec make_timespec(timestamp_t tv_sec, timestamp_t tv_nsec)
{
    return timespec{tv_sec, tv_nsec};
}

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

    logdebug << "Packet type:" << pkt->type << '\n';
    logdebug << "Packet sender_seq: " << pkt->sender_seq << '\n';

    return pkt;
}

tuple<shared_ptr<char>, size_t> serialize_reflector_packet(shared_ptr<ReflectorPacket>& pkt)
{
    #if __x86_64__
    const int TS_SIZE = 2;
    #else
    const int TS_SIZE = 1;
    #endif
    static_assert(sizeof(timespec) == 2*TS_SIZE*sizeof(uint32_t), "ouch1");
    static_assert(sizeof(timespec::tv_sec) == TS_SIZE*sizeof(uint32_t), "ouch2");
    static_assert(sizeof(timespec::tv_nsec) == TS_SIZE*sizeof(uint32_t), "ouch3");
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
        logerr << "Wrong packet type, expected " << FROM_REFLECTOR << " got " << ntohl(*p) << '\n';
        return false;
    }
    p += sizeof(ReflectorPacket::type)/sizeof(uint32_t);
    if (ntohl(*p) != seqnr)
    {
        logerr << "Expected seqnr " << seqnr << ", got " << ntohl(*p) << "!\n";
        return false;
    }
    else
    {
        return true;
    }
}

void dbl2ts(double dbl, timespec& ts)
{
    assert(dbl >= 0);
    ts.tv_sec = dbl; // Take integer part
    ts.tv_nsec = (dbl - ts.tv_sec) * BILLION;
}
};
