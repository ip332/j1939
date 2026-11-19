//
// Known TP / fast-packet reassembly bugs, left for a separate change. Each test describes
// the correct behaviour and FAILS against the current code, so they are DISABLED_ (compiled,
// reported as disabled, not run). Run them with --gtest_also_run_disabled_tests; enable each
// one as its fix lands.
//

#include "transport_protocol.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

J1939_frame frame(uint32_t can_id, std::vector<uint8_t> data) {
    J1939_frame f;
    f.setFrom(CAN_EFF_FLAG | can_id, data.data(), static_cast<uint16_t>(data.size()));
    return f;
}

// Fast-packet frame of PGN `pgn` (PDU2) from `src`.
J1939_frame fastFrame(uint32_t pgn, uint8_t src, std::vector<uint8_t> data) {
    return frame((6u << 26) | (pgn << 8) | src, std::move(data));
}

} // namespace

// transport_protocol.h:19 -- fast-packet state is keyed by SA only, so two fast-packet PGNs
// interleaved by the same node clobber each other. Should be keyed by (SA, PGN).
TEST(TransportProtocolKnownIssues, DISABLED_InterleavedFastPacketPgnsFromOneSourceBothComplete) {
    TransportProtocol tp;
    std::vector<std::unique_ptr<J1939_frame>> done;
    auto feed = [&](const J1939_frame &f) {
        auto r = tp.handleCanFrame(f);
        if (r) {
            done.push_back(std::move(r));
        }
    };
    feed(fastFrame(129029, 0x30, {0x00, 10, 'A', 'B', 'C', 'D', 'E', 'F'}));
    feed(fastFrame(129540, 0x30, {0x20, 10, 'a', 'b', 'c', 'd', 'e', 'f'}));
    feed(fastFrame(129029, 0x30, {0x01, 'G', 'H', 'I', 'J', 0, 0, 0}));
    feed(fastFrame(129540, 0x30, {0x21, 'g', 'h', 'i', 'j', 0, 0, 0}));
    ASSERT_EQ(done.size(), 2u);
    EXPECT_EQ(memcmp(done[0]->buffer_, "ABCDEFGHIJ", 10), 0);
    EXPECT_EQ(memcmp(done[1]->buffer_, "abcdefghij", 10), 0);
}

// fast_packet_message.cpp:67-76 -- the frame counter (low 5 bits of byte 0) is never checked,
// so out-of-order or missing frames are stitched together silently.
TEST(TransportProtocolKnownIssues, DISABLED_FastPacketOutOfOrderFrameIsRejected) {
    TransportProtocol tp;
    tp.handleCanFrame(fastFrame(129029, 0x30, {0x00, 20, 1, 2, 3, 4, 5, 6}));
    tp.handleCanFrame(fastFrame(129029, 0x30, {0x02, 14, 15, 16, 17, 18, 19, 20}));
    auto r = tp.handleCanFrame(fastFrame(129029, 0x30, {0x01, 7, 8, 9, 10, 11, 12, 13}));
    EXPECT_FALSE(r) << "frames 0,2,1 must not reassemble into a 20-byte message";
}

// fast_packet_message.cpp:24 -- 65536 isn't an NMEA 2000 PGN (it would be PDU1 PF=0 on DP 1);
// probably a typo in the fast-packet PGN list.
TEST(TransportProtocolKnownIssues, DISABLED_FastPacketListHasNoPgn65536) {
    J1939_frame f = fastFrame(0, 0x30, {0, 0, 0, 0, 0, 0, 0, 0});
    f.pgn_ = 65536;
    EXPECT_FALSE(LongCanMessageIf::isFastPacketFrame(f));
}

// transport_protocol.h:17 -- TP sessions are keyed by SA only, so a BAM and an RTS/CTS
// transfer from the same node (or RTS/CTS to two destinations) collide. Should be keyed by
// (SA, DA).
TEST(TransportProtocolKnownIssues, DISABLED_BamAndRtsFromOneSourceDoNotCollide) {
    TransportProtocol tp;
    std::vector<std::unique_ptr<J1939_frame>> done;
    auto feed = [&](const J1939_frame &f) {
        auto r = tp.handleCanFrame(f);
        if (r) {
            done.push_back(std::move(r));
        }
    };
    feed(frame(0x1CECFF10, {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00}));  // BAM to global
    feed(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00}));  // RTS to 0x20
    feed(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    feed(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    feed(frame(0x1CEBFF10, {2, 'H', 'I', 'J', 0, 0, 0, 0}));
    feed(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0}));
    ASSERT_EQ(done.size(), 2u);
    EXPECT_EQ(done[0]->pgn_, 0xFEF1u);
    EXPECT_EQ(memcmp(done[0]->buffer_, "ABCDEFGHIJ", 10), 0);
    EXPECT_EQ(done[1]->pgn_, 0xFECAu);
    EXPECT_EQ(memcmp(done[1]->buffer_, "abcdefghij", 10), 0);
}

// long_can_message.cpp:42 -- a TP.DT addressed to a different destination than the session's
// is accepted as part of it.
TEST(TransportProtocolKnownIssues, DISABLED_DataTransferForAnotherDestinationIsIgnored) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00})); // RTS to 0x20
    tp.handleCanFrame(frame(0x1CEB3010, {1, 'x', 'x', 'x', 'x', 'x', 'x', 'x'})); // DT to 0x30
    tp.handleCanFrame(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    auto r = tp.handleCanFrame(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0}));
    ASSERT_TRUE(r);
    EXPECT_EQ(memcmp(r->buffer_, "abcdefghij", 10), 0);
}

// long_can_message_if.h:22-30 -- TP.CM Connection Abort (control byte 255) isn't routed to the
// session, so the transfer keeps going and completes after it was aborted.
TEST(TransportProtocolKnownIssues, DISABLED_ConnectionAbortEndsTheSession) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00})); // RTS
    tp.handleCanFrame(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    tp.handleCanFrame(frame(0x1CEC1020, {255, 3, 0xFF, 0xFF, 0xFF, 0xCA, 0xFE, 0x00})); // abort
    auto r = tp.handleCanFrame(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0}));
    EXPECT_FALSE(r);
}

// long_can_message.cpp:48 -- a TP.DT shorter than 8 bytes still has 7 bytes copied from it,
// so stale buffer content ends up in the message.
TEST(TransportProtocolKnownIssues, DISABLED_ShortDataTransferFrameIsRejected) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CECFF10, {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    auto r = tp.handleCanFrame(frame(0x1CEBFF10, {2, 'H', 'I'}));
    EXPECT_FALSE(r) << "a 3-byte DT can't carry bytes 8-10 of the message";
}
