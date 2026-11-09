//
// Commanded Address (PGN 65240) end to end: 9 bytes, so it travels as a TP BAM; the
// reassembled message from TransportProtocol is what NodeTable consumes.
//

#include "node_table.h"
#include "transport_protocol.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

constexpr uint64_t kMs = 1000000ULL;
constexpr uint64_t kTarget = 0xc0508c0022768542ULL;

J1939_frame frame(uint32_t can_id, const uint8_t *data, uint16_t dlc, uint64_t t_ms) {
    J1939_frame f;
    f.setFrom(CAN_EFF_FLAG | can_id, data, dlc);
    f.time_ns_ = t_ms * kMs;
    return f;
}

// Feeds physical frames through TransportProtocol then NodeTable, as CanSandbox does.
struct Pipeline {
    TransportProtocol tp;
    NodeTable table;
    std::vector<NodeEvent> events;
    std::vector<J1939_frame> messages;

    Pipeline() {
        table.setEventCallback([this](const NodeEvent &e) { events.push_back(e); });
    }
    void feed(const J1939_frame &f) {
        if (TransportProtocol::longMessage(f)) {
            auto done = tp.handleCanFrame(f);
            if (!done) {
                return;
            }
            messages.push_back(*done);
            table.handleFrame(*done);
            return;
        }
        messages.push_back(f);
        table.handleFrame(f);
    }
    // BAM from `src` carrying Commanded Address {NAME, new SA}.
    void commandedAddressBam(uint8_t src, uint64_t name, uint8_t new_sa, uint64_t t_ms) {
        uint8_t payload[9];
        J1939Name::rawToBytes(name, payload);
        payload[8] = new_sa;
        const uint8_t cm[8] = {32, 9, 0, 2, 0xFF, 0xD8, 0xFE, 0x00}; // BAM, 9 bytes, 2 packets
        uint8_t dt1[8] = {1};
        memcpy(&dt1[1], payload, 7);
        uint8_t dt2[8] = {2, payload[7], payload[8], 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        feed(frame(0x1CECFF00 | src, cm, 8, t_ms));
        feed(frame(0x1CEBFF00 | src, dt1, 8, t_ms + 50));
        feed(frame(0x1CEBFF00 | src, dt2, 8, t_ms + 100));
    }
    void claim(uint8_t sa, uint64_t name, uint64_t t_ms) {
        uint8_t data[8];
        J1939Name::rawToBytes(name, data);
        feed(frame(0x18EEFF00 | sa, data, 8, t_ms));
    }
};

} // namespace

TEST(CommandedAddressTest, BamIsReassembledIntoANineByteMessage) {
    Pipeline p;
    p.commandedAddressBam(0x27, kTarget, 0x42, 0);
    ASSERT_EQ(p.messages.size(), 1u);
    const J1939_frame &m = p.messages[0];
    EXPECT_EQ(m.pgn_, kPgnCommandedAddress);
    EXPECT_EQ(m.dlc_, 9);
    EXPECT_EQ(m.src_, 0x27);
    EXPECT_EQ(m.dst_, kGlobalAddress);
    uint64_t name = 0;
    ASSERT_TRUE(J1939Name::rawFromBytes(m.buffer_, m.dlc_, &name));
    EXPECT_EQ(name, kTarget);
    EXPECT_EQ(m.buffer_[8], 0x42);
}

TEST(CommandedAddressTest, TargetMovesWhenItClaimsTheCommandedAddress) {
    Pipeline p;
    p.claim(0x12, kTarget, 0);
    p.commandedAddressBam(0x27, kTarget, 0x42, 100);
    ASSERT_EQ(p.table.stats().commanded, 1u);
    const NodeEvent *cmd = nullptr;
    for (const auto &e : p.events) {
        if (e.type == NodeEventType::CommandedAddress) {
            cmd = &e;
        }
    }
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->name, kTarget);
    EXPECT_EQ(cmd->sa, 0x42);
    EXPECT_EQ(cmd->old_sa, 0x12);
    EXPECT_EQ(cmd->source_sa, 0x27);
    EXPECT_EQ(p.table.bySa(0x12)->name_, kTarget); // not until the target claims

    p.claim(0x42, kTarget, 300);
    const NodeEvent &moved = p.events.back();
    EXPECT_EQ(moved.type, NodeEventType::NodeAddressChanged);
    EXPECT_TRUE(moved.commanded);
    EXPECT_EQ(moved.old_sa, 0x12);
    EXPECT_EQ(moved.sa, 0x42);
    EXPECT_EQ(p.table.bySa(0x12), nullptr);
}
