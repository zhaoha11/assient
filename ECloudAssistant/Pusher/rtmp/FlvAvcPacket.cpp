#include "FlvAvcPacket.h"
#include <limits>

bool BuildFlvAvcPacket(const uint8_t* data, size_t size,
                       std::vector<uint8_t>& output, bool& keyFrame)
{
    output.clear();
    keyFrame = false;
    if(!data || size == 0) return false;

    std::vector<uint8_t> payload{0x27, 1, 0, 0, 0};
    bool idr = false;
    bool found = false;
    size_t nalBegin = 0;
    size_t zeros = 0;
    // A run of at least two zero bytes followed by 1 is an Annex-B delimiter.
    // Extra zeros are leading_zero_8bits / trailing_zero_8bits, not NAL data.
    auto appendNal = [&](size_t end) {
        if(end <= nalBegin) return false;
        const size_t length = end - nalBegin;
        const size_t maximum = std::numeric_limits<uint32_t>::max();
        if(length > maximum - 4 || payload.size() > maximum - 4 - length) return false;
        const uint32_t nalSize = static_cast<uint32_t>(length);
        for(int shift = 24; shift >= 0; shift -= 8)
            payload.push_back(static_cast<uint8_t>(nalSize >> shift));
        payload.insert(payload.end(), data + nalBegin, data + end);
        idr = idr || ((data[nalBegin] & 0x1f) == 5);
        return true;
    };
    for(size_t i = 0; i < size; ++i)
    {
        if(data[i] == 0) { ++zeros; continue; }
        if(data[i] == 1 && zeros >= 2)
        {
            if(found)
            {
                if(!appendNal(i - zeros)) return false;
            }
            else if(i != zeros) return false;
            found = true;
            nalBegin = i + 1;
        }
        else if(!found) return false;
        zeros = 0;
    }
    if(!found || !appendNal(size - zeros)) return false;
    payload[0] = idr ? 0x17 : 0x27;
    output.swap(payload);
    keyFrame = idr;
    return true;
}
