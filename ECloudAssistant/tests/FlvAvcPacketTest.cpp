#include "FlvAvcPacket.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using Bytes = std::vector<uint8_t>;

static void check(bool ok, const char* name)
{
    if(!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}

static void sample(const Bytes& input, const Bytes& expected, bool key)
{
    Bytes output;
    bool idr = false;
    check(BuildFlvAvcPacket(input.data(), input.size(), output, idr), "valid input");
    check(output == expected, "exact FLV payload");
    check(idr == key, "IDR classification");
    size_t offset = 5;
    while(offset < output.size())
    {
        check(output.size() - offset >= 4, "complete length prefix");
        uint32_t length = 0;
        for(int i = 0; i < 4; ++i) length = (length << 8) | output[offset++];
        check(length > 0 && length <= output.size() - offset, "NAL length boundary");
        offset += length;
    }
    check(offset == output.size(), "lengths reach packet end");
}

int main(int argc, char**)
{
    const Bytes multi{0,0,0,1,0x67,0x11,0,0,1,0x68,0x22,
                      0,0,0,1,0x06,0x33,0,0,1,0x65,0x44};
    const Bytes expected{0x17,1,0,0,0,0,0,0,2,0x67,0x11,
                         0,0,0,2,0x68,0x22,0,0,0,2,0x06,0x33,
                         0,0,0,2,0x65,0x44};
    if(argc > 1)
    {
        // Reproduce the original fixed-four-byte removal and single length.
        Bytes old{0x17,1,0,0,0};
        const uint32_t size = static_cast<uint32_t>(multi.size() - 4);
        for(int shift = 24; shift >= 0; shift -= 8) old.push_back(size >> shift);
        old.insert(old.end(), multi.begin() + 4, multi.end());
        check(old == expected, "legacy multi-NAL conversion");
    }
    sample(multi, expected, true);
    sample({0,0,1,0x41,0x11}, {0x27,1,0,0,0,0,0,0,2,0x41,0x11}, false);
    sample({0,0,0,1,0x65,0x11}, {0x17,1,0,0,0,0,0,0,2,0x65,0x11}, true);
    sample({0,0,1,0x67,0x11}, {0x27,1,0,0,0,0,0,0,2,0x67,0x11}, false);
    sample({0,0,1,0x06,0x11,0,0,0,1,0x65,0x22,0,0,1,0x65,0x33},
           {0x17,1,0,0,0,0,0,0,2,0x06,0x11,0,0,0,2,0x65,0x22,0,0,0,2,0x65,0x33}, true);
    sample({0,0,0,0,1,0x41,0,0,3,1,0x22,0,0},
           {0x27,1,0,0,0,0,0,0,6,0x41,0,0,3,1,0x22}, false);
    Bytes largeInput{0,0,0,1,0x65};
    largeInput.insert(largeInput.end(), 65536, 0x55);
    Bytes largeExpected{0x17,1,0,0,0,0,1,0,1,0x65};
    largeExpected.insert(largeExpected.end(), 65536, 0x55);
    sample(largeInput, largeExpected, true);
    for(const Bytes& input : {Bytes{}, Bytes{0x65,0x11}, Bytes{0,0,1},
                            Bytes{0,0,1,0,0,1,0x65}, Bytes{0,0,1,0x65,0,0,1},
                            Bytes{9,0,0,1,0x65}})
    {
        Bytes output{9}; bool idr = true;
        check(!BuildFlvAvcPacket(input.data(), input.size(), output, idr), "invalid input");
        check(output.empty() && !idr, "failure clears output");
    }
    Bytes output; bool idr = true;
    check(!BuildFlvAvcPacket(nullptr, 10, output, idr), "null input");
    std::cout << "PASS: FLV AVC byte samples and invalid inputs\n";
}
