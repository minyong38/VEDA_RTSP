// tests/test_rtp.cpp

#include "rtp/packetizer.hpp"
#include "rtp/h264_fua.hpp"

#include <cassert>

int main() {
    using namespace veda::rtp;

    // TODO (Week 3-4): verify RTP header byte layout and FU-A fragmentation
    // Header h;
    // h.payload_type = 96;
    // h.sequence     = 0x1234;
    // h.timestamp    = 0xDEADBEEF;
    // h.ssrc         = 0xCAFEBABE;
    // uint8_t buf[12] = {};
    // write_header(h, buf);
    // assert((buf[0] & 0xC0) == 0x80);   // Version 2
    // assert((buf[1] & 0x7F) == 96);     // PT

    return 0;
}
