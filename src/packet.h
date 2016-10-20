#ifndef _PACKET_H_
#define _PACKET_H_

#include <stdint.h>

void prepare_packet(char* buf, size_t buflen, uint32_t seq);

#endif
