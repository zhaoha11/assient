#ifndef FLVAVCPACKET_H
#define FLVAVCPACKET_H
#include <cstddef>
#include <cstdint>
#include <vector>

// Convert one complete Annex-B access unit into a FLV AVC NALU payload.
bool BuildFlvAvcPacket(const uint8_t* data, size_t size,
                       std::vector<uint8_t>& output, bool& keyFrame);
#endif
