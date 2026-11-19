//
// Synthetic tests for NodeTable: one or more per branch of the address-claim handling.
//

#include "node_table.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr uint64_t kMs = 1000000ULL;
// Arbitration compares raw NAMEs; lower wins.
constexpr uint64_t kLow = 0xc0508c0000000001ULL;
constexpr uint64_t kMid = 0xc0508c0000000002ULL;
constexpr uint64_t kHigh = 0xc0508c0000000003ULL;

// Drives a NodeTable with synthetic frames and records every event.
class Bus {
public:
    explicit Bus(NodeTableConfig config = NodeTableConfig()) : table(config) {
        table.setEventCallback([this](const NodeEvent &e) { events.push_back(e); });
    }

    void claim(uint64_t t_ms, uint8_t sa, uint64_t name, uint16_t dlc = 8) {
        J1939_frame f;
        uint8_t data[8];
        J1939Name::rawToBytes(name, data);
        f.setFrom(CAN_EFF_FLAG | 0x18EEFF00 | sa, data, dlc);
        f.time_ns_ = t_ms * kMs;
        table.handleFrame(f);
    }
    void cannotClaim(uint64_t t_ms, uint64_t name) { claim(t_ms, kNullAddress, name); }
    // EEC1-like PDU2 data frame from `sa`.
    J1939_frame data(uint64_t t_ms, uint8_t sa, uint32_t pgn = 0xF004) {
        J1939_frame f;
        const uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        uint32_t pf = (pgn >> 8) & 0xFF;
        uint32_t ps = pgn & 0xFF;
        f.setFrom(CAN_EFF_FLAG | (3u << 26) | (((pgn >> 16) & 1) << 24) | (pf << 16) | (ps << 8) | sa,
                  bytes, 8);
        f.time_ns_ = t_ms * kMs;
        table.handleFrame(f);
        return f;
    }
    // A Commanded Address as TransportProtocol delivers it: one 9-byte message.
    void commanded(uint64_t t_ms, uint8_t src, uint64_t name, uint8_t new_sa, uint16_t dlc = 9) {
        J1939_frame f;
        uint8_t bytes[9];
        J1939Name::rawToBytes(name, bytes);
        bytes[8] = new_sa;
        f.setFrom(CAN_EFF_FLAG | 0x18FED800 | src, bytes, dlc);
        f.time_ns_ = t_ms * kMs;
        table.handleFrame(f);
    }

    std::vector<NodeEvent> of(NodeEventType type) const {
        std::vector<NodeEvent> out;
        for (const auto &e : events) {
            if (e.type == type) {
                out.push_back(e);
            }
        }
        return out;
    }
    size_t count(NodeEventType type) const { return of(type).size(); }
    NodeState state(uint64_t name) const { return table.byName(name)->state_; }
    uint64_t ownerOf(uint8_t sa) const {
        const Node *n = table.bySa(sa);
        return n == nullptr ? 0 : n->name_;
    }
    Resolution resolveFrom(uint8_t sa) {
        J1939_frame f;
        f.setFrom(CAN_EFF_FLAG | 0x0CF00400 | sa);
        return table.resolve(f);
    }

    NodeTable table;
    std::vector<NodeEvent> events;
};

} // namespace

// --- basic ownership ---

TEST(NodeTableTest, FirstClaimMakesTheNodeAppear) {
    Bus bus;
    bus.claim(100, 10, kMid);
    ASSERT_EQ(bus.events.size(), 1u);
    EXPECT_EQ(bus.events[0].type, NodeEventType::NodeAppeared);
    EXPECT_EQ(bus.events[0].sa, 10);
    EXPECT_EQ(bus.events[0].name, kMid);
    const Node *n = bus.table.byName(kMid);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n, bus.table.bySa(10));
    EXPECT_EQ(n->state_, NodeState::Active);
    EXPECT_EQ(n->first_seen_ns_, 100 * kMs);
    bus.data(250, 10);
    EXPECT_EQ(n->last_seen_ns_, 250 * kMs);
    Resolution r = bus.resolveFrom(10);
    EXPECT_EQ(r.kind, Resolution::Resolved);
    EXPECT_EQ(r.name, kMid);
    EXPECT_EQ(bus.table.stats().claims, 1u);
}

TEST(NodeTableTest, ReclaimOfOwnAddressChangesNothing) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.claim(50, 10, kMid);
    EXPECT_EQ(bus.events.size(), 1u);
    EXPECT_EQ(bus.table.byName(kMid)->last_claim_ns_, 50 * kMs);
}

TEST(NodeTableTest, ResolveOfAClaimIsTheNameItCarries) {
    Bus bus;
    J1939_frame f;
    uint8_t data[8];
    J1939Name::rawToBytes(kHigh, data);
    f.setFrom(CAN_EFF_FLAG | 0x18EEFF21, data, 8);
    Resolution r = bus.table.resolve(f);
    EXPECT_EQ(r.kind, Resolution::Resolved);
    EXPECT_EQ(r.name, kHigh);
    EXPECT_EQ(bus.resolveFrom(kNullAddress).kind, Resolution::Unresolved);
}

TEST(NodeTableTest, TrafficBeforeAnyClaimIsUnresolvedThenAttributed) {
    Bus bus;
    bus.data(0, 5);
    bus.data(10, 5);
    EXPECT_EQ(bus.resolveFrom(5).kind, Resolution::Unresolved);
    bus.claim(20, 5, kMid);
    auto attributed = bus.of(NodeEventType::Attributed);
    ASSERT_EQ(attributed.size(), 1u);
    EXPECT_EQ(attributed[0].sa, 5);
    EXPECT_EQ(attributed[0].name, kMid);
    EXPECT_EQ(attributed[0].from_ns, 0u);
    EXPECT_EQ(attributed[0].to_ns, 10 * kMs);
    EXPECT_EQ(bus.resolveFrom(5).name, kMid);
}

TEST(NodeTableTest, NameAtNewAddressReleasesItsOldOne) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.claim(100, 11, kMid);
    auto moved = bus.of(NodeEventType::NodeAddressChanged);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].old_sa, 10);
    EXPECT_EQ(moved[0].sa, 11);
    EXPECT_FALSE(moved[0].commanded);
    EXPECT_EQ(bus.table.bySa(10), nullptr);
    EXPECT_EQ(bus.ownerOf(11), kMid);
    EXPECT_EQ(bus.resolveFrom(10).kind, Resolution::Unresolved);
}

TEST(NodeTableTest, CannotClaimLeavesTheNameUnaddressed) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.cannotClaim(100, kMid);
    auto cc = bus.of(NodeEventType::CannotClaim);
    ASSERT_EQ(cc.size(), 1u);
    EXPECT_EQ(cc[0].old_sa, 10);
    EXPECT_EQ(cc[0].name, kMid);
    EXPECT_EQ(bus.state(kMid), NodeState::CannotClaim);
    EXPECT_EQ(bus.table.byName(kMid)->sa_, kNullAddress);
    EXPECT_EQ(bus.table.bySa(10), nullptr);
    EXPECT_EQ(bus.table.stats().cannot_claims, 1u);
    // Later it gets an address after all.
    bus.claim(200, 12, kMid);
    EXPECT_EQ(bus.state(kMid), NodeState::Active);
    EXPECT_EQ(bus.of(NodeEventType::NodeAddressChanged).back().old_sa, 10);
}

TEST(NodeTableTest, NameFirstSeenAsCannotClaim) {
    Bus bus;
    bus.cannotClaim(0, kHigh);
    ASSERT_EQ(bus.events.size(), 2u);
    EXPECT_EQ(bus.events[0].type, NodeEventType::NodeAppeared);
    EXPECT_EQ(bus.events[0].sa, kNullAddress);
    EXPECT_EQ(bus.events[1].type, NodeEventType::CannotClaim);
    EXPECT_EQ(bus.events[1].old_sa, kNullAddress);
}

TEST(NodeTableTest, MalformedClaimsAreCountedAndIgnored) {
    Bus bus;
    bus.claim(0, 10, kMid, 7);
    bus.claim(0, kGlobalAddress, kMid);
    EXPECT_TRUE(bus.events.empty());
    EXPECT_EQ(bus.table.stats().malformed_claims, 2u);
    EXPECT_EQ(bus.table.stats().claims, 0u);
    EXPECT_EQ(bus.table.byName(kMid), nullptr);
}

TEST(NodeTableTest, CountsRequestsForAddressClaimed) {
    Bus bus;
    J1939_frame req = NodeTable::makeAddressClaimRequest(5 * kMs);
    bus.table.handleFrame(req);
    J1939_frame other;
    const uint8_t pgn[3] = {0x00, 0xEF, 0x00};
    other.setFrom(CAN_EFF_FLAG | 0x18EAFF21, pgn, 3);
    bus.table.handleFrame(other);
    J1939_frame short_req;
    short_req.setFrom(CAN_EFF_FLAG | 0x18EAFF21, pgn, 2);
    bus.table.handleFrame(short_req);
    EXPECT_EQ(bus.table.stats().claim_requests, 1u);
}

TEST(NodeTableTest, AddressClaimRequestFrame) {
    J1939_frame req = NodeTable::makeAddressClaimRequest(7);
    EXPECT_EQ(req.canID(), CAN_EFF_FLAG | 0x18EAFFFEu);
    EXPECT_EQ(req.pgn_, kPgnRequest);
    EXPECT_EQ(req.src_, kNullAddress);
    EXPECT_EQ(req.dst_, kGlobalAddress);
    ASSERT_EQ(req.dlc_, 3);
    EXPECT_EQ(req.buffer_[0], 0x00);
    EXPECT_EQ(req.buffer_[1], 0xEE);
    EXPECT_EQ(req.buffer_[2], 0x00);
    EXPECT_EQ(req.time_ns_, 7u);
}

// --- contests: challenger has the lower NAME ---

TEST(NodeTableTest, LowerNameTakesTheAddressAtOnce) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    auto contested = bus.of(NodeEventType::AddressContested);
    ASSERT_EQ(contested.size(), 1u);
    EXPECT_EQ(contested[0].name, kLow);
    EXPECT_EQ(contested[0].other_name, kHigh);
    auto reassigned = bus.of(NodeEventType::AddressReassigned);
    ASSERT_EQ(reassigned.size(), 1u);
    EXPECT_EQ(reassigned[0].sa, 10);
    EXPECT_EQ(reassigned[0].name, kLow);
    EXPECT_EQ(reassigned[0].other_name, kHigh);
    EXPECT_EQ(bus.ownerOf(10), kLow);
    EXPECT_EQ(bus.state(kHigh), NodeState::Displaced);
    EXPECT_EQ(bus.table.byName(kHigh)->sa_, kNullAddress);

    // Frames on 10 are CONTESTED until the window closes, then attributed to the winner.
    bus.data(150, 10);
    Resolution r = bus.resolveFrom(10);
    EXPECT_EQ(r.kind, Resolution::Contested);
    EXPECT_EQ(r.name, kLow);
    EXPECT_EQ(r.candidate, kHigh);
    EXPECT_EQ(bus.count(NodeEventType::ContestResolved), 0u);
    bus.data(601, 10);
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].branch, Branch::LowerNameWins);
    EXPECT_EQ(resolved[0].time_ns, 600 * kMs);
    auto attributed = bus.of(NodeEventType::Attributed);
    ASSERT_EQ(attributed.size(), 1u);
    EXPECT_EQ(attributed[0].name, kLow);
    EXPECT_EQ(attributed[0].from_ns, 150 * kMs);
    EXPECT_EQ(attributed[0].to_ns, 150 * kMs);
    EXPECT_EQ(bus.resolveFrom(10).kind, Resolution::Resolved);
    // The displaced node stays Displaced (no staleness timeout configured).
    EXPECT_EQ(bus.state(kHigh), NodeState::Displaced);
    EXPECT_EQ(bus.table.stats().resolved[static_cast<int>(Branch::LowerNameWins)], 1u);
}

TEST(NodeTableTest, DisplacedNodeMovingOnClosesTheContestEarly) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.claim(120, 11, kHigh);
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].branch, Branch::LowerNameWins);
    EXPECT_EQ(resolved[0].time_ns, 120 * kMs);
    auto moved = bus.of(NodeEventType::NodeAddressChanged);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].name, kHigh);
    EXPECT_EQ(moved[0].old_sa, 10);
    EXPECT_EQ(moved[0].sa, 11);
    EXPECT_EQ(bus.state(kHigh), NodeState::Active);
}

TEST(NodeTableTest, DisplacedNodeSendingCannotClaimClosesTheContest) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.cannotClaim(130, kHigh);
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::LowerNameWins);
    EXPECT_EQ(bus.state(kHigh), NodeState::CannotClaim);
    EXPECT_EQ(bus.of(NodeEventType::CannotClaim).at(0).old_sa, 10);
}

TEST(NodeTableTest, LowerWinnerLeavingDuringTheWindowClosesTheContest) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.claim(150, 12, kLow);
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::LowerNameWins);
    EXPECT_EQ(bus.ownerOf(12), kLow);
    EXPECT_EQ(bus.table.bySa(10), nullptr);
}

TEST(NodeTableTest, LowerWinnerRepeatingItsClaimChangesNothing) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    size_t before = bus.events.size();
    bus.claim(110, 10, kLow);
    EXPECT_EQ(bus.events.size(), before);
}

TEST(NodeTableTest, LoserReclaimingWithinTheWindowStartsANewContest) {
    // kHigh lost 10 to kLow; it claims 10 again inside the window. That's ordinary
    // arbitration (it may not have seen kLow's claim yet), not non-compliance.
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.claim(200, 10, kHigh);
    EXPECT_EQ(bus.count(NodeEventType::NonCompliantNode), 0u);
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::LowerNameWins);
    EXPECT_EQ(bus.count(NodeEventType::AddressContested), 2u);
    EXPECT_EQ(bus.state(kHigh), NodeState::Contending);
    EXPECT_EQ(bus.ownerOf(10), kLow);
    bus.claim(220, 10, kLow); // and the lower NAME defends
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).back().branch, Branch::HolderDefended);
    EXPECT_EQ(bus.state(kHigh), NodeState::Displaced);
}

TEST(NodeTableTest, LoserReclaimingAfterTheWindowIsNonCompliant) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.data(700, 10); // window (ends 600) closes
    bus.claim(800, 10, kHigh);
    auto nc = bus.of(NodeEventType::NonCompliantNode);
    ASSERT_EQ(nc.size(), 1u);
    EXPECT_EQ(nc[0].sa, 10);
    EXPECT_EQ(nc[0].name, kHigh);
    EXPECT_EQ(nc[0].other_name, kLow);
    EXPECT_EQ(bus.table.stats().non_compliant, 1u);
    // Its claim is still treated as a (losing) contest; kLow keeps the address.
    EXPECT_EQ(bus.ownerOf(10), kLow);
}

TEST(NodeTableTest, DataOnTheLostAddressAfterTheWindowIsTheWinners) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.data(700, 10);
    bus.data(800, 10);
    EXPECT_EQ(bus.count(NodeEventType::NonCompliantNode), 0u);
    Resolution r = bus.resolveFrom(10);
    EXPECT_EQ(r.kind, Resolution::Resolved);
    EXPECT_EQ(r.name, kLow);
}

// --- contests: challenger has the higher NAME ---

TEST(NodeTableTest, HolderDefendsAgainstAHigherName) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(100, 10, kHigh);
    auto contested = bus.of(NodeEventType::AddressContested);
    ASSERT_EQ(contested.size(), 1u);
    EXPECT_EQ(contested[0].name, kHigh);
    EXPECT_EQ(contested[0].other_name, kLow);
    EXPECT_EQ(bus.ownerOf(10), kLow);
    EXPECT_EQ(bus.state(kHigh), NodeState::Contending);
    bus.data(110, 10);
    Resolution r = bus.resolveFrom(10);
    EXPECT_EQ(r.kind, Resolution::Contested);
    EXPECT_EQ(r.name, kLow);
    EXPECT_EQ(r.candidate, kHigh);
    bus.claim(100, 10, kHigh); // repeated challenge: still waiting
    EXPECT_EQ(bus.count(NodeEventType::ContestResolved), 0u);

    bus.claim(120, 10, kLow);
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].branch, Branch::HolderDefended);
    EXPECT_EQ(resolved[0].name, kLow);
    EXPECT_EQ(resolved[0].other_name, kHigh);
    EXPECT_EQ(bus.state(kHigh), NodeState::Displaced);
    EXPECT_EQ(bus.ownerOf(10), kLow);
    EXPECT_EQ(bus.of(NodeEventType::Attributed).at(0).name, kLow);
    EXPECT_EQ(bus.count(NodeEventType::AddressReassigned), 0u);
}

TEST(NodeTableTest, HigherChallengerWithdrawsByClaimingElsewhere) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(50, 20, kHigh);
    bus.claim(100, 10, kHigh); // leaves 20, contends for 10
    EXPECT_EQ(bus.table.bySa(20), nullptr);
    bus.claim(120, 11, kHigh);
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].branch, Branch::ChallengerWithdrew);
    EXPECT_EQ(resolved[0].name, kLow);
    EXPECT_EQ(bus.ownerOf(11), kHigh);
    auto moved = bus.of(NodeEventType::NodeAddressChanged);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].old_sa, 20);
    EXPECT_EQ(moved[0].sa, 11);
}

TEST(NodeTableTest, HigherChallengerGivingUpWithCannotClaim) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(100, 10, kHigh);
    bus.cannotClaim(150, kHigh);
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::ChallengerWithdrew);
    EXPECT_EQ(bus.state(kHigh), NodeState::CannotClaim);
    EXPECT_EQ(bus.ownerOf(10), kLow);
}

TEST(NodeTableTest, SilentHolderLosesToAHigherName) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(10, 20, kHigh);
    bus.claim(100, 10, kHigh);
    bus.data(300, 10);
    bus.table.flush(601 * kMs); // nobody defended by 600
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].branch, Branch::HolderSilent);
    EXPECT_EQ(resolved[0].name, kHigh);
    EXPECT_EQ(resolved[0].other_name, kLow);
    EXPECT_EQ(bus.ownerOf(10), kHigh);
    EXPECT_EQ(bus.state(kLow), NodeState::Offline);
    EXPECT_NE(bus.table.byName(kLow), nullptr); // record kept
    auto reassigned = bus.of(NodeEventType::AddressReassigned);
    ASSERT_EQ(reassigned.size(), 1u);
    EXPECT_EQ(reassigned[0].name, kHigh);
    EXPECT_EQ(reassigned[0].other_name, kLow);
    auto moved = bus.of(NodeEventType::NodeAddressChanged);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].old_sa, 20);
    EXPECT_EQ(moved[0].sa, 10);
    auto attributed = bus.of(NodeEventType::Attributed);
    ASSERT_EQ(attributed.size(), 1u);
    EXPECT_EQ(attributed[0].name, kHigh);
    EXPECT_EQ(attributed[0].from_ns, 300 * kMs);
}

TEST(NodeTableTest, SilentHolderAgainstANewNameReportsNoAddressChange) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(100, 10, kHigh); // first sighting of kHigh
    bus.table.flush(601 * kMs);
    EXPECT_EQ(bus.ownerOf(10), kHigh);
    EXPECT_EQ(bus.count(NodeEventType::NodeAddressChanged), 0u);
}

TEST(NodeTableTest, HolderWalkingAwayFromAContestedAddress) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(100, 10, kHigh);
    bus.claim(150, 30, kLow);
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::HolderSilent);
    EXPECT_EQ(bus.ownerOf(10), kHigh);
    EXPECT_EQ(bus.ownerOf(30), kLow);
    EXPECT_EQ(bus.state(kLow), NodeState::Active);
}

TEST(NodeTableTest, ThirdClaimantSupersedesTheWaitingChallenger) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(100, 10, kHigh);
    bus.claim(150, 10, kMid);
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].branch, Branch::None);
    EXPECT_EQ(resolved[0].other_name, kHigh);
    EXPECT_EQ(bus.state(kHigh), NodeState::Displaced);
    EXPECT_EQ(bus.state(kMid), NodeState::Contending);
    EXPECT_EQ(bus.count(NodeEventType::AddressContested), 2u);
}

TEST(NodeTableTest, ThirdClaimantDuringALowerWinsWindow) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kMid); // kMid takes 10
    bus.claim(150, 10, kLow); // and kLow takes it from kMid
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::LowerNameWins);
    EXPECT_EQ(bus.ownerOf(10), kLow);
    EXPECT_EQ(bus.state(kMid), NodeState::Displaced);
    EXPECT_EQ(bus.count(NodeEventType::AddressReassigned), 2u);
}

TEST(NodeTableTest, TimestampsGoingBackwardsDoNotCloseOrReopenWindows) {
    Bus bus;
    bus.claim(1000, 10, kLow);
    bus.claim(1100, 10, kHigh);
    bus.data(50, 10);   // older timestamp: clock stays at 1100
    bus.data(1500, 10); // 400 ms in: still open
    EXPECT_EQ(bus.count(NodeEventType::ContestResolved), 0u);
    bus.data(1601, 10);
    EXPECT_EQ(bus.count(NodeEventType::ContestResolved), 1u);
}

// --- anomalies ---

TEST(NodeTableTest, SameNameActiveOnTwoAddressesIsADuplicate) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.claim(100, 11, kMid);  // looks like a move...
    bus.claim(150, 10, kMid);  // ...but 10 is still claimed by the same NAME
    auto dup = bus.of(NodeEventType::DuplicateName);
    ASSERT_EQ(dup.size(), 1u);
    EXPECT_EQ(dup[0].name, kMid);
    EXPECT_EQ(dup[0].sa, 10);
    EXPECT_EQ(dup[0].old_sa, 11);
    EXPECT_EQ(bus.ownerOf(10), kMid);
    EXPECT_EQ(bus.ownerOf(11), kMid);
    const Node *n = bus.table.byName(kMid);
    EXPECT_EQ(n->sa_, 10);
    EXPECT_EQ(n->duplicate_sa_, 11);
    // Both keep claiming: reported once.
    bus.claim(200, 11, kMid);
    bus.claim(210, 10, kMid);
    EXPECT_EQ(bus.count(NodeEventType::DuplicateName), 1u);
    EXPECT_EQ(bus.table.stats().duplicates, 1u);
    // Someone takes 11 from one of them: the other keeps 10.
    bus.claim(300, 11, kLow);
    EXPECT_EQ(bus.ownerOf(11), kLow);
    EXPECT_EQ(bus.ownerOf(10), kMid);
    EXPECT_EQ(bus.state(kMid), NodeState::Active);
    EXPECT_EQ(n->duplicate_sa_, kNullAddress);
}

TEST(NodeTableTest, DuplicateLosingItsPrimaryAddressKeepsTheOther) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.claim(100, 11, kMid);
    bus.claim(150, 10, kMid); // primary 10, duplicate 11
    bus.claim(300, 10, kLow);
    EXPECT_EQ(bus.ownerOf(11), kMid);
    EXPECT_EQ(bus.table.byName(kMid)->sa_, 11);
    EXPECT_EQ(bus.state(kMid), NodeState::Active);
}

TEST(NodeTableTest, ReturningAfterSomeoneElseClaimedIsNotADuplicate) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.claim(100, 11, kMid);
    bus.claim(120, 10, kHigh);
    bus.claim(130, 12, kHigh);
    bus.claim(150, 10, kMid);
    EXPECT_EQ(bus.count(NodeEventType::DuplicateName), 0u);
}

TEST(NodeTableTest, ReturningAfterTheWindowIsAMoveNotADuplicate) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.claim(100, 11, kMid);
    bus.claim(700, 10, kMid);
    EXPECT_EQ(bus.count(NodeEventType::DuplicateName), 0u);
    EXPECT_EQ(bus.table.bySa(11), nullptr);
}

TEST(NodeTableTest, ClaimPingPongIsReportedAndRateLimited) {
    Bus bus;
    // kLow and kHigh keep re-claiming 10: four changes of claimant within 2 s.
    uint64_t t = 0;
    for (int i = 0; i < 3; i++) {
        bus.claim(t, 10, kHigh);
        bus.claim(t + 20, 10, kLow);
        t += 100;
    }
    auto pp = bus.of(NodeEventType::ClaimPingPong);
    ASSERT_EQ(pp.size(), 1u);
    EXPECT_EQ(pp[0].sa, 10);
    // Still ping-ponging, but within the quiet period: no more events.
    for (int i = 0; i < 5; i++) {
        bus.claim(t, 10, kHigh);
        bus.claim(t + 20, 10, kLow);
        t += 100;
    }
    EXPECT_EQ(bus.count(NodeEventType::ClaimPingPong), 1u);
    // After the quiet period it is reported again.
    t = 7000;
    for (int i = 0; i < 3; i++) {
        bus.claim(t, 10, kHigh);
        bus.claim(t + 20, 10, kLow);
        t += 100;
    }
    EXPECT_EQ(bus.count(NodeEventType::ClaimPingPong), 2u);
    EXPECT_EQ(bus.table.stats().pingpongs, 2u);
}

TEST(NodeTableTest, SlowAlternationIsNotPingPong) {
    Bus bus;
    for (uint64_t t = 0; t < 10000; t += 1000) {
        bus.claim(t, 10, (t / 1000) % 2 ? kLow : kHigh);
    }
    EXPECT_EQ(bus.count(NodeEventType::ClaimPingPong), 0u);
}

// --- staleness ---

namespace {
NodeTableConfig withTimeout(uint64_t ms) {
    NodeTableConfig config;
    config.stale_timeout_ns = ms * kMs;
    return config;
}
} // namespace

TEST(NodeTableTest, DisplacedNodeTimesOutFromDisplacementNotOldAddressTraffic) {
    // kHigh is displaced from 10 at t=100. The winner keeps sending on 10 -- that traffic
    // must not keep kHigh alive, since kHigh no longer has an SA.
    Bus bus(withTimeout(1000));
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    for (uint64_t t = 150; t <= 3000; t += 50) {
        bus.data(t, 10);
    }
    auto timed_out = bus.of(NodeEventType::NodeTimedOut);
    ASSERT_EQ(timed_out.size(), 1u);
    EXPECT_EQ(timed_out[0].name, kHigh);
    EXPECT_EQ(timed_out[0].sa, kNullAddress);
    // Times out ~1 s after displacement (scan granularity is timeout / 16), not later.
    EXPECT_GT(timed_out[0].time_ns, 1100 * kMs);
    EXPECT_LE(timed_out[0].time_ns, (1100 + 1000 / 16 + 50) * kMs);
    EXPECT_EQ(bus.state(kHigh), NodeState::TimedOut);
    EXPECT_EQ(bus.state(kLow), NodeState::Active);
}

TEST(NodeTableTest, DisplacedNodeStaysDisplacedWithoutATimeout) {
    Bus bus; // stale_timeout_ns = 0
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    for (uint64_t t = 150; t <= 60000; t += 500) {
        bus.data(t, 10);
    }
    EXPECT_EQ(bus.state(kHigh), NodeState::Displaced);
    EXPECT_EQ(bus.count(NodeEventType::NodeTimedOut), 0u);
}

TEST(NodeTableTest, DisplacedNodeClaimRestartsItsStalenessClock) {
    Bus bus(withTimeout(1000));
    bus.claim(0, 20, kLow);
    bus.claim(10, 20, kHigh);   // kHigh contends...
    bus.claim(20, 20, kLow);    // ...and loses at t=20
    bus.claim(800, 20, kHigh);  // claims again (non-compliant, but it's alive)
    bus.claim(810, 20, kLow);
    bus.data(1500, 20);
    EXPECT_EQ(bus.count(NodeEventType::NodeTimedOut), 0u);
    bus.data(2000, 20);
    EXPECT_EQ(bus.count(NodeEventType::NodeTimedOut), 1u);
}

TEST(NodeTableTest, ActiveNodeTimesOutKeepsItsAddressAndRevivesOnTraffic) {
    Bus bus(withTimeout(1000));
    bus.claim(0, 10, kMid);
    bus.claim(0, 11, kLow);
    for (uint64_t t = 100; t <= 2000; t += 100) {
        bus.data(t, 11); // only kLow talks
    }
    auto timed_out = bus.of(NodeEventType::NodeTimedOut);
    ASSERT_EQ(timed_out.size(), 1u);
    EXPECT_EQ(timed_out[0].name, kMid);
    EXPECT_EQ(timed_out[0].sa, 10);
    EXPECT_EQ(bus.ownerOf(10), kMid); // still attributed
    bus.data(2100, 10);
    EXPECT_EQ(bus.state(kMid), NodeState::Active);
    auto appeared = bus.of(NodeEventType::NodeAppeared);
    EXPECT_EQ(appeared.back().name, kMid);
    EXPECT_EQ(appeared.back().time_ns, 2100 * kMs);
    EXPECT_EQ(bus.table.stats().timeouts, 1u);
}

TEST(NodeTableTest, TimedOutNodeRevivesByReclaiming) {
    Bus bus(withTimeout(1000));
    bus.claim(0, 10, kMid);
    bus.claim(0, 11, kLow);
    bus.data(2000, 11);
    ASSERT_EQ(bus.state(kMid), NodeState::TimedOut);
    bus.claim(2100, 10, kMid);
    EXPECT_EQ(bus.state(kMid), NodeState::Active);
    EXPECT_EQ(bus.of(NodeEventType::NodeAppeared).back().name, kMid);
}

TEST(NodeTableTest, TimedOutHolderLosesItsAddressWithoutAContest) {
    Bus bus(withTimeout(1000));
    bus.claim(0, 10, kLow);
    bus.claim(0, 11, kMid);
    bus.data(2000, 11);
    ASSERT_EQ(bus.state(kLow), NodeState::TimedOut);
    bus.claim(2100, 10, kHigh); // higher NAME, but the holder is gone
    EXPECT_EQ(bus.count(NodeEventType::AddressContested), 0u);
    auto reassigned = bus.of(NodeEventType::AddressReassigned);
    ASSERT_EQ(reassigned.size(), 1u);
    EXPECT_EQ(reassigned[0].name, kHigh);
    EXPECT_EQ(reassigned[0].other_name, kLow);
    EXPECT_EQ(bus.ownerOf(10), kHigh);
    EXPECT_EQ(bus.table.byName(kLow)->sa_, kNullAddress);
}

TEST(NodeTableTest, HeartbeatKeepsANodeAlive) {
    Bus bus(withTimeout(1000));
    bus.claim(0, 10, kMid);
    for (uint64_t t = 500; t <= 5000; t += 500) {
        bus.data(t, 10, 126993); // NMEA 2000 Heartbeat
    }
    EXPECT_EQ(bus.count(NodeEventType::NodeTimedOut), 0u);
}

TEST(NodeTableTest, CannotClaimNodeTimesOut) {
    Bus bus(withTimeout(1000));
    bus.cannotClaim(0, kMid);
    bus.table.flush(2000 * kMs);
    EXPECT_EQ(bus.state(kMid), NodeState::TimedOut);
}

// --- commanded address ---

TEST(NodeTableTest, CommandedAddressThenClaimIsACommandedMove) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.commanded(100, 0x80, kMid, 42);
    auto cmd = bus.of(NodeEventType::CommandedAddress);
    ASSERT_EQ(cmd.size(), 1u);
    EXPECT_EQ(cmd[0].name, kMid);
    EXPECT_EQ(cmd[0].sa, 42);
    EXPECT_EQ(cmd[0].old_sa, 10);
    EXPECT_EQ(cmd[0].source_sa, 0x80);
    EXPECT_EQ(bus.ownerOf(10), kMid); // nothing changes until the target claims
    bus.claim(150, 42, kMid);
    auto moved = bus.of(NodeEventType::NodeAddressChanged);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_TRUE(moved[0].commanded);
    EXPECT_EQ(moved[0].old_sa, 10);
    EXPECT_EQ(moved[0].sa, 42);
    EXPECT_EQ(bus.table.stats().commanded, 1u);
    EXPECT_EQ(bus.table.stats().commands_expired, 0u);
}

TEST(NodeTableTest, CommandedAddressForAnUnknownNameAndIgnored) {
    Bus bus;
    bus.commanded(0, 0x80, kHigh, 42);
    EXPECT_EQ(bus.of(NodeEventType::CommandedAddress).at(0).old_sa, kNullAddress);
    bus.table.flush(2000 * kMs);
    EXPECT_EQ(bus.table.stats().commands_expired, 1u);
    bus.claim(2100, 42, kHigh); // too late to count as commanded
    EXPECT_EQ(bus.count(NodeEventType::NodeAddressChanged), 0u);
}

TEST(NodeTableTest, MalformedCommandedAddress) {
    Bus bus;
    bus.commanded(0, 0x80, kMid, 42, 8);
    bus.commanded(0, 0x80, kMid, kNullAddress);
    bus.commanded(0, 0x80, kMid, kGlobalAddress);
    EXPECT_EQ(bus.table.stats().malformed_commanded, 3u);
    EXPECT_EQ(bus.count(NodeEventType::CommandedAddress), 0u);
}

TEST(NodeTableTest, MoreCommandsThanSlotsEvictsTheOldest) {
    Bus bus;
    for (uint64_t i = 0; i < 9; i++) {
        bus.commanded(i, 0x80, kLow + i, static_cast<uint8_t>(40 + i));
    }
    EXPECT_EQ(bus.table.stats().commands_expired, 1u);
    // Re-commanding the same NAME reuses its entry.
    bus.commanded(20, 0x80, kLow + 8, 60);
    EXPECT_EQ(bus.table.stats().commands_expired, 1u);
    bus.claim(30, 60, kLow + 8);
    EXPECT_EQ(bus.count(NodeEventType::NodeAddressChanged), 0u); // first sighting: appears
}

TEST(NodeTableTest, CommandEvictionPicksTheEntryClosestToExpiring) {
    Bus bus;
    bus.claim(0, 10, kLow);
    for (uint64_t i = 0; i < 8; i++) {
        bus.commanded(i, 0x80, kLow + i, static_cast<uint8_t>(40 + i));
    }
    bus.commanded(100, 0x80, kLow, 40); // refresh entry 0: entry 1 is now the oldest
    bus.commanded(110, 0x80, 0xd000000000000001ULL, 70); // a 9th NAME: evicts entry 1
    EXPECT_EQ(bus.table.stats().commands_expired, 1u);
    bus.claim(150, 40, kLow); // entry 0 survived
    ASSERT_EQ(bus.count(NodeEventType::NodeAddressChanged), 1u);
    EXPECT_TRUE(bus.of(NodeEventType::NodeAddressChanged)[0].commanded);
}

TEST(NodeTableTest, ClaimOfAnotherSaIsNotTheCommandedMove) {
    Bus bus;
    bus.claim(0, 10, kMid);
    bus.commanded(100, 0x80, kMid, 42);
    bus.claim(150, 43, kMid);
    bus.claim(200, 42, kMid);
    auto moved = bus.of(NodeEventType::NodeAddressChanged);
    ASSERT_EQ(moved.size(), 2u);
    EXPECT_FALSE(moved[0].commanded);
    EXPECT_TRUE(moved[1].commanded);
}

TEST(NodeTableTest, OverlappingContestsAndCommandsEachExpireOnTime) {
    Bus bus;
    bus.claim(0, 10, kLow);
    bus.claim(0, 20, kLow + 1);
    bus.commanded(0, 0x80, kHigh, 77);  // expires at 1250
    bus.claim(0, 10, kHigh + 2);       // contest on 10, deadline 500
    bus.claim(300, 20, kHigh + 3);     // contest on 20, deadline 800
    bus.data(550, 30);
    auto resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].sa, 10);
    EXPECT_EQ(bus.table.stats().commands_expired, 0u);
    bus.data(850, 30);
    resolved = bus.of(NodeEventType::ContestResolved);
    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_EQ(resolved[1].sa, 20);
    EXPECT_EQ(resolved[1].time_ns, 800 * kMs);
    EXPECT_EQ(bus.table.stats().commands_expired, 0u);
    bus.table.flush(1300 * kMs);
    EXPECT_EQ(bus.table.stats().commands_expired, 1u);
}

TEST(NodeTableTest, ReclaimingTheLostAddressAfterTheWinnerLeftIsFine) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(100, 10, kLow);
    bus.claim(200, 12, kLow); // the winner moves on; 10 is free
    bus.claim(800, 10, kHigh);
    EXPECT_EQ(bus.count(NodeEventType::NonCompliantNode), 0u);
    EXPECT_EQ(bus.ownerOf(10), kHigh);
    EXPECT_EQ(bus.state(kHigh), NodeState::Active);
}

TEST(NodeTableTest, ContenderReturningToItsOldAddressIsNotADuplicate) {
    Bus bus;
    bus.claim(0, 10, kHigh);
    bus.claim(0, 11, kLow);
    bus.claim(100, 11, kHigh); // leaves 10, contends for 11
    bus.claim(150, 10, kHigh); // gives up and goes back
    EXPECT_EQ(bus.count(NodeEventType::DuplicateName), 0u);
    EXPECT_EQ(bus.of(NodeEventType::ContestResolved).at(0).branch, Branch::ChallengerWithdrew);
    EXPECT_EQ(bus.ownerOf(10), kHigh);
}

TEST(NodeTableTest, ShortClaimFrameResolvesLikeAnyOtherFrame) {
    Bus bus;
    J1939_frame f;
    const uint8_t data[7] = {1, 2, 3, 4, 5, 6, 7};
    f.setFrom(CAN_EFF_FLAG | 0x18EEFF21, data, 7);
    EXPECT_EQ(bus.table.resolve(f).kind, Resolution::Unresolved);
}

TEST(NodeTableTest, RequestForAnotherPgnIsNotCounted) {
    NodeTable table; // no callback set
    J1939_frame f;
    const uint8_t a[3] = {0x00, 0xEE, 0x01};
    f.setFrom(CAN_EFF_FLAG | 0x18EAFF21, a, 3);
    table.handleFrame(f);
    const uint8_t b[3] = {0x01, 0xEE, 0x00};
    f.setFrom(CAN_EFF_FLAG | 0x18EAFF21, b, 3);
    table.handleFrame(f);
    EXPECT_EQ(table.stats().claim_requests, 0u);
    // ... and a table without a callback still tracks claims.
    uint8_t name[8];
    J1939Name::rawToBytes(kMid, name);
    f.setFrom(CAN_EFF_FLAG | 0x18EEFF21, name, 8);
    table.handleFrame(f);
    EXPECT_NE(table.bySa(0x21), nullptr);
}

// --- forEach / strings ---

TEST(NodeTableTest, ForEachIsInNameOrder) {
    Bus bus;
    bus.claim(0, 1, kHigh);
    bus.claim(0, 2, kLow);
    bus.claim(0, 3, kMid);
    std::vector<uint64_t> names;
    bus.table.forEach([&](const Node &n) { names.push_back(n.name_); });
    EXPECT_EQ(names, (std::vector<uint64_t>{kLow, kMid, kHigh}));
}

TEST(NodeTableTest, EnumsHaveNames) {
    for (int i = 0; i <= static_cast<int>(NodeEventType::Attributed); i++) {
        EXPECT_STRNE(toString(static_cast<NodeEventType>(i)), "?");
    }
    for (int i = 0; i <= static_cast<int>(Branch::ChallengerWithdrew); i++) {
        EXPECT_STRNE(toString(static_cast<Branch>(i)), "?");
    }
    for (int i = 0; i <= static_cast<int>(NodeState::TimedOut); i++) {
        EXPECT_STRNE(toString(static_cast<NodeState>(i)), "?");
    }
    EXPECT_STREQ(toString(static_cast<NodeEventType>(99)), "?");
    EXPECT_STREQ(toString(static_cast<Branch>(99)), "?");
    EXPECT_STREQ(toString(static_cast<NodeState>(99)), "?");
    EXPECT_STREQ(toString(Branch::None), "Superseded");
}
