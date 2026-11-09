//
// Regression tests for TP / fast-packet reassembly bugs found while adding address-claim
// handling (they used to be DISABLED_ "known issue" tests). Each test names what used to go
// wrong.
//

#include "long_can_message.h"
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

// Collects every message TransportProtocol completes.
struct Collector {
    TransportProtocol tp;
    std::vector<std::unique_ptr<J1939_frame>> done;
    void feed(const J1939_frame &f) {
        auto r = tp.handleCanFrame(f);
        if (r) {
            done.push_back(std::move(r));
        }
    }
};

} // namespace

// --- fast packet ---

// Fast-packet state used to be keyed by SA only, so two fast-packet PGNs interleaved by the
// same node clobbered each other. Now keyed by (SA, PGN).
TEST(TransportProtocolRegressionTest, InterleavedFastPacketPgnsFromOneSourceBothComplete) {
    Collector c;
    c.feed(fastFrame(129029, 0x30, {0x00, 10, 'A', 'B', 'C', 'D', 'E', 'F'}));
    c.feed(fastFrame(129540, 0x30, {0x20, 10, 'a', 'b', 'c', 'd', 'e', 'f'}));
    c.feed(fastFrame(129029, 0x30, {0x01, 'G', 'H', 'I', 'J', 0, 0, 0}));
    c.feed(fastFrame(129540, 0x30, {0x21, 'g', 'h', 'i', 'j', 0, 0, 0}));
    ASSERT_EQ(c.done.size(), 2u);
    EXPECT_EQ(memcmp(c.done[0]->buffer_, "ABCDEFGHIJ", 10), 0);
    EXPECT_EQ(memcmp(c.done[1]->buffer_, "abcdefghij", 10), 0);
}

// The frame counter (low 5 bits of byte 0) used to be ignored, so out-of-order or missing
// frames were stitched together silently.
TEST(TransportProtocolRegressionTest, FastPacketOutOfOrderFrameIsRejected) {
    TransportProtocol tp;
    tp.handleCanFrame(fastFrame(129029, 0x30, {0x00, 20, 1, 2, 3, 4, 5, 6}));
    tp.handleCanFrame(fastFrame(129029, 0x30, {0x02, 14, 15, 16, 17, 18, 19, 20}));
    auto r = tp.handleCanFrame(fastFrame(129029, 0x30, {0x01, 7, 8, 9, 10, 11, 12, 13}));
    EXPECT_FALSE(r) << "frames 0,2,1 must not reassemble into a 20-byte message";
}

TEST(TransportProtocolRegressionTest, FastPacketMissingFrameIsRejected) {
    TransportProtocol tp;
    tp.handleCanFrame(fastFrame(129029, 0x30, {0x00, 13, 1, 2, 3, 4, 5, 6}));
    auto r = tp.handleCanFrame(fastFrame(129029, 0x30, {0x02, 7, 8, 9, 10, 11, 12, 13}));
    EXPECT_FALSE(r) << "frame 1 was lost; frame 2 must not complete the message";
    // The next message starts cleanly.
    tp.handleCanFrame(fastFrame(129029, 0x30, {0x20, 13, 1, 2, 3, 4, 5, 6}));
    r = tp.handleCanFrame(fastFrame(129029, 0x30, {0x21, 7, 8, 9, 10, 11, 12, 13}));
    ASSERT_TRUE(r);
    EXPECT_EQ(r->dlc_, 13);
}

// The fast-packet PGN list had 65536, which is canboat's fallback entry for unknown addressed
// ISO PGNs in 0x10000-0x1EC00 (sent over TP), not a fast-packet PGN.
TEST(TransportProtocolRegressionTest, FastPacketListHasNoPgn65536) {
    J1939_frame f = fastFrame(0, 0x30, {0, 0, 0, 0, 0, 0, 0, 0});
    f.pgn_ = 65536;
    EXPECT_FALSE(LongCanMessageIf::isFastPacketFrame(f));
}

// --- TP (BAM, RTS/CTS) ---

// TP sessions used to be keyed by SA only, so a BAM and an RTS/CTS transfer from the same node
// (or RTS/CTS to two destinations) collided. Now keyed by (SA, DA).
TEST(TransportProtocolRegressionTest, BamAndRtsFromOneSourceDoNotCollide) {
    Collector c;
    c.feed(frame(0x1CECFF10, {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00}));  // BAM to global
    c.feed(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00}));  // RTS to 0x20
    c.feed(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    c.feed(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    c.feed(frame(0x1CEBFF10, {2, 'H', 'I', 'J', 0, 0, 0, 0}));
    c.feed(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0}));
    ASSERT_EQ(c.done.size(), 2u);
    EXPECT_EQ(c.done[0]->pgn_, 0xFEF1u);
    EXPECT_EQ(memcmp(c.done[0]->buffer_, "ABCDEFGHIJ", 10), 0);
    EXPECT_EQ(c.done[1]->pgn_, 0xFECAu);
    EXPECT_EQ(c.done[1]->dst_, 0x20);
    EXPECT_EQ(memcmp(c.done[1]->buffer_, "abcdefghij", 10), 0);
}

// A TP.DT addressed to another destination used to be accepted into the session.
TEST(TransportProtocolRegressionTest, DataTransferForAnotherDestinationIsIgnored) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00})); // RTS to 0x20
    tp.handleCanFrame(frame(0x1CEB3010, {1, 'x', 'x', 'x', 'x', 'x', 'x', 'x'})); // DT to 0x30
    tp.handleCanFrame(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    auto r = tp.handleCanFrame(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0}));
    ASSERT_TRUE(r);
    EXPECT_EQ(memcmp(r->buffer_, "abcdefghij", 10), 0);
}

TEST(TransportProtocolRegressionTest, DataTransferWithoutAnnouncementIsIgnored) {
    TransportProtocol tp;
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'})));
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEBFF10, {2, 'H', 'I', 'J', 0, 0, 0, 0})));
}

// TP.CM Connection Abort (control byte 255) used to be ignored, so an aborted transfer still
// completed. Either end can send it.
TEST(TransportProtocolRegressionTest, ConnectionAbortFromTheReceiverEndsTheSession) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00})); // RTS 0x10->0x20
    tp.handleCanFrame(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEC1020, {255, 3, 0xFF, 0xFF, 0xFF, 0xCA, 0xFE, 0x00})));
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0})));
}

TEST(TransportProtocolRegressionTest, ConnectionAbortFromTheOriginatorEndsTheSession) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    tp.handleCanFrame(frame(0x1CEC2010, {255, 1, 0xFF, 0xFF, 0xFF, 0xCA, 0xFE, 0x00}));
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0})));
}

TEST(TransportProtocolRegressionTest, ConnectionAbortForAnotherPgnLeavesTheSessionAlone) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CEC2010, {16, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEB2010, {1, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
    tp.handleCanFrame(frame(0x1CEC1020, {255, 3, 0xFF, 0xFF, 0xFF, 0xEC, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEC3040, {255, 3, 0xFF, 0xFF, 0xFF, 0xCA, 0xFE, 0x00})); // no such
    auto r = tp.handleCanFrame(frame(0x1CEB2010, {2, 'h', 'i', 'j', 0, 0, 0, 0}));
    ASSERT_TRUE(r);
    EXPECT_EQ(memcmp(r->buffer_, "abcdefghij", 10), 0);
}

TEST(TransportProtocolRegressionTest, AbortIsATransportProtocolFrame) {
    EXPECT_TRUE(LongCanMessageIf::isTpmFrame(frame(0x1CEC1020, {255, 3, 0xFF, 0xFF, 0xFF, 0xCA, 0xFE, 0x00})));
}

TEST(TransportProtocolRegressionTest, LongCanMessageAbortedDirectly) {
    LongCanMessage msg;
    msg.handleDataFrame(frame(0x1CECFF10, {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00}));
    EXPECT_EQ(msg.pgn(), 0xFEF1u);
    msg.handleDataFrame(frame(0x1CECFF10, {255, 1, 0xFF, 0xFF, 0xFF, 0xF1, 0xFE, 0x00}));
    msg.handleDataFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    EXPECT_FALSE(msg.handleDataFrame(frame(0x1CEBFF10, {2, 'H', 'I', 'J', 0, 0, 0, 0})));
}

// A TP.DT shorter than 8 bytes used to have 7 bytes copied from it anyway (stale buffer
// content in the message). Now it must at least hold the bytes it owes.
TEST(TransportProtocolRegressionTest, ShortDataTransferFrameIsRejected) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CECFF10, {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    auto r = tp.handleCanFrame(frame(0x1CEBFF10, {2, 'H', 'I'}));
    EXPECT_FALSE(r) << "a 3-byte DT can't carry bytes 8-10 of the message";
}

TEST(TransportProtocolRegressionTest, UnpaddedLastDataTransferFrameThatHoldsItsBytesIsAccepted) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CECFF10, {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    auto r = tp.handleCanFrame(frame(0x1CEBFF10, {2, 'H', 'I', 'J'}));
    ASSERT_TRUE(r);
    EXPECT_EQ(r->dlc_, 10);
    EXPECT_EQ(memcmp(r->buffer_, "ABCDEFGHIJ", 10), 0);
}

TEST(TransportProtocolRegressionTest, ShortMiddleDataTransferFrameIsRejected) {
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CECFF10, {32, 18, 0, 3, 0xFF, 0xF1, 0xFE, 0x00}));
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D'})));
    tp.handleCanFrame(frame(0x1CEBFF10, {2, 'H', 'I', 'J', 'K', 'L', 'M', 'N'}));
    EXPECT_FALSE(tp.handleCanFrame(frame(0x1CEBFF10, {3, 'O', 'P', 'Q', 'R', 0, 0, 0})));
}

TEST(TransportProtocolRegressionTest, MorePacketsAnnouncedThanTheSizeNeeds) {
    // 10 bytes announced in 3 packets: the 3rd carries nothing and still completes.
    TransportProtocol tp;
    tp.handleCanFrame(frame(0x1CECFF10, {32, 10, 0, 3, 0xFF, 0xF1, 0xFE, 0x00}));
    tp.handleCanFrame(frame(0x1CEBFF10, {1, 'A', 'B', 'C', 'D', 'E', 'F', 'G'}));
    tp.handleCanFrame(frame(0x1CEBFF10, {2, 'H', 'I', 'J', 0xFF, 0xFF, 0xFF, 0xFF}));
    auto r = tp.handleCanFrame(frame(0x1CEBFF10, {3}));
    ASSERT_TRUE(r);
    EXPECT_EQ(memcmp(r->buffer_, "ABCDEFGHIJ", 10), 0);
}
