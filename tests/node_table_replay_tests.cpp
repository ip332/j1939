//
// Replays real NMEA 2000 captures (canboat) through NodeTable. See testdata/README.md.
//
// Where the two kinds of expectation come from:
//  - Final SA owners and address walks are read straight off the logs (the last NAME to
//    claim each SA, and the order a NAME claimed SAs in).
//  - The contest branches come from this implementation; each was checked by hand against
//    the claim order in the log and which NAME is lower. They pin behaviour rather than
//    being an independent oracle.
//

#include "actisense_stream.h"
#include "external_samples.h"
#include "node_table.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

namespace {

struct ReplayResult {
    std::vector<NodeEvent> events;
    NodeTableStats stats;
    std::map<int, uint64_t> owners; // SA -> NAME at the end
};

ReplayResult replay(const std::string &path) {
    ReplayResult result;
    NodeTable table;
    table.setEventCallback([&](const NodeEvent &e) { result.events.push_back(e); });
    ActisenseStream stream;
    EXPECT_TRUE(stream.open(path)) << path;
    J1939_frame frame;
    uint64_t last = 0;
    while (stream.dataAvailable()) {
        frame.reset();
        if (stream.get(&frame)) {
            table.handleFrame(frame);
            last = frame.time_ns_;
        }
    }
    table.flush(last + table.config().contest_window_ns + 1); // end of log = silence
    result.stats = table.stats();
    for (int sa = 0; sa < kNullAddress; sa++) {
        const Node *n = table.bySa(static_cast<uint8_t>(sa));
        if (n != nullptr) {
            result.owners[sa] = n->name_;
        }
    }
    return result;
}

struct Resolved {
    int sa;
    Branch branch;
    uint64_t winner;
    uint64_t loser;
    bool operator==(const Resolved &o) const {
        return sa == o.sa && branch == o.branch && winner == o.winner && loser == o.loser;
    }
};

std::ostream &operator<<(std::ostream &os, const Resolved &r) {
    return os << "SA " << r.sa << " " << toString(r.branch) << " winner " << J1939Name::hex(r.winner)
              << " loser " << J1939Name::hex(r.loser);
}

std::vector<Resolved> contests(const ReplayResult &r) {
    std::vector<Resolved> out;
    for (const auto &e : r.events) {
        if (e.type == NodeEventType::ContestResolved) {
            out.push_back({e.sa, e.branch, e.name, e.other_name});
        }
    }
    return out;
}

std::vector<int> walk(const ReplayResult &r, uint64_t name) {
    std::vector<int> sas;
    for (const auto &e : r.events) {
        if (e.name == name && e.type == NodeEventType::NodeAddressChanged) {
            if (sas.empty()) {
                sas.push_back(e.old_sa);
            }
            sas.push_back(e.sa);
        }
    }
    return sas;
}

std::string excerpt(const char *name) {
    return std::string(TESTDATA_FOLDER) + "canboat/" + name;
}

// --- NAMEs ---
// live.all cascade
constexpr uint64_t kWalker = 0xc07887002fb83d30ULL;
// nac3 reset (B&G / Simrad devices re-claiming after an autopilot reset)
constexpr uint64_t kN327b = 0xc0328700227b5bfbULL;
constexpr uint64_t kN227a = 0xc0508c00227a9bfbULL;
constexpr uint64_t kN2fbc = 0xc0508c002fbcfb2aULL;
constexpr uint64_t kN1bfb = 0xc0509600227d1bfbULL;
constexpr uint64_t kN709b = 0xc0509b0022709bfbULL;
constexpr uint64_t kNdbfb = 0xc0509bff2270dbfbULL;

// Every contest in the nac3 reset, in order, with how it was decided.
const std::vector<Resolved> kNac3Contests = {
    // SA 0: dbfb challenges the lower 1bfb, which re-claims within the window.
    {0, Branch::HolderDefended, kN1bfb, kNdbfb},
    // SA 1: 1bfb challenges the lower 2fbc, then claims SA 2 instead.
    {1, Branch::ChallengerWithdrew, kN2fbc, kN1bfb},
    // SA 0: 327b (lower) takes it from 227a; 227a moves on to SA 1.
    {0, Branch::LowerNameWins, kN327b, kN227a},
    // SA 1: 227a (lower) takes it from 2fbc; 2fbc keeps re-claiming SA 1 inside the window
    // (ClaimPingPong) and 227a defends each time.
    {1, Branch::LowerNameWins, kN227a, kN2fbc},
    {1, Branch::HolderDefended, kN227a, kN2fbc},
    {1, Branch::HolderDefended, kN227a, kN2fbc},
    // SA 3: 1bfb (lower) took it from 709b; decided when 709b claims SA 4.
    {3, Branch::LowerNameWins, kN1bfb, kN709b},
    // SA 3: 2fbc (lower) takes it from 1bfb; decided when 1bfb claims SA 4.
    {3, Branch::LowerNameWins, kN2fbc, kN1bfb},
    // SA 4: 1bfb (lower) takes it from 709b; 709b re-claims once and 1bfb defends.
    {4, Branch::LowerNameWins, kN1bfb, kN709b},
    {4, Branch::HolderDefended, kN1bfb, kN709b},
    // SA 5: 709b (lower) takes it from dbfb; dbfb stays silent for the rest of the reset.
    {5, Branch::LowerNameWins, kN709b, kNdbfb},
};

void expectNac3Reset(const ReplayResult &r) {
    EXPECT_EQ(contests(r), kNac3Contests);
    // Every reassignment (an SA changing hands) was a lower NAME taking it at once; the other
    // five contests left the SA where it was.
    std::vector<std::pair<int, uint64_t>> reassigned;
    for (const auto &e : r.events) {
        if (e.type == NodeEventType::AddressReassigned) {
            reassigned.push_back({e.sa, e.name});
        }
    }
    EXPECT_EQ(reassigned, (std::vector<std::pair<int, uint64_t>>{
                              {0, kN327b}, {3, kN1bfb}, {1, kN227a}, {3, kN2fbc}, {4, kN1bfb}, {5, kN709b}}));
    EXPECT_EQ(r.stats.resolved[static_cast<int>(Branch::LowerNameWins)], 6u);
    EXPECT_EQ(r.stats.resolved[static_cast<int>(Branch::HolderDefended)], 4u);
    EXPECT_EQ(r.stats.resolved[static_cast<int>(Branch::ChallengerWithdrew)], 1u);
    EXPECT_EQ(r.stats.resolved[static_cast<int>(Branch::HolderSilent)], 0u);
    EXPECT_EQ(r.stats.non_compliant, 0u);
    EXPECT_EQ(r.stats.duplicates, 0u);
    EXPECT_EQ(r.stats.pingpongs, 3u); // SA 0, 1 and 4
}

} // namespace

TEST(NodeTableReplayTest, LiveCascadeEndsWithTheOwnersTheLogShows) {
    ReplayResult r = replay(excerpt("live_claim_cascade.all"));
    // Last claimant of each SA in the excerpt (canboat live.all lines 54941-55240).
    const std::map<int, uint64_t> expected = {
        {2, 0xc07891002fb53d30ULL},  {42, 0xc0aa82002fb08758ULL},
        {6, 0xc0508c002fb97d30ULL},  {17, 0xc0508c002fbda94dULL}, {18, 0xc0508c0022768542ULL},
        {19, 0xc0509600227d1bfbULL}, {20, 0xc0508c00e83c8d21ULL}, {21, 0xc0508c00e83c8d2dULL},
        {22, 0xc0509b0022709bfbULL}, {23, kWalker},               {24, 0xc07891002fb8bb2aULL},
        {25, 0xc078be002fba7d30ULL}, {26, 0xc078be002fbe7b2aULL}, {27, 0xc0788c0022709d9bULL},
        {28, 0xc078be002fbee94dULL}, {29, 0xc0f082002fba3d30ULL}, {30, 0xc0f082002fbba7cfULL},
        {31, 0xc0f082002fbc294dULL},
    };
    EXPECT_EQ(r.owners, expected);
    EXPECT_EQ(walk(r, kWalker), (std::vector<int>{17, 18, 19, 20, 21, 22, 23}));
    EXPECT_EQ(r.stats.non_compliant, 0u);
    EXPECT_EQ(r.stats.duplicates, 0u);
    EXPECT_EQ(r.stats.resolved[static_cast<int>(Branch::HolderSilent)], 0u);
}

TEST(NodeTableReplayTest, LiveCascadeNodeThatLosesAndMovesOnIsNotNonCompliant) {
    // 0xc078be002fbee94d contends for 27, loses to the lower holder, and moves to 28: the
    // textbook HolderDefended path, from a real bus.
    ReplayResult r = replay(excerpt("live_claim_cascade.all"));
    std::vector<Resolved> c = contests(r);
    const Resolved defended{27, Branch::HolderDefended, 0xc0788c0022709d9bULL, 0xc078be002fbee94dULL};
    EXPECT_NE(std::find(c.begin(), c.end(), defended), c.end());
    EXPECT_EQ(r.owners.at(28), 0xc078be002fbee94dULL);
}

TEST(NodeTableReplayTest, Nac3ResetBranchesFromTheExcerpt) {
    // The excerpt holds every address claim of the reset (the capture's first claim is its
    // first line), so it reproduces the full capture's contests exactly.
    ReplayResult r = replay(excerpt("nac3_reset_reclaim.raw"));
    expectNac3Reset(r);
}

TEST(NodeTableReplayTest, Nac3ResetBranchesFromTheFullCapture) {
    std::string path = externalSamplesDir() + "nac3-reset-dockside-commissioning.raw";
    REQUIRE_EXTERNAL_SAMPLE(path);
    ReplayResult r = replay(path);
    expectNac3Reset(r);
    // 41 s later dbfb, displaced from 5, finally claims SA 6.
    EXPECT_EQ(r.owners.at(6), kNdbfb);
    EXPECT_EQ(r.owners.at(0), kN327b);
    EXPECT_EQ(r.owners.at(1), kN227a);
    EXPECT_EQ(r.owners.at(3), kN2fbc);
    EXPECT_EQ(r.owners.at(4), kN1bfb);
    EXPECT_EQ(r.owners.at(5), kN709b);
}

TEST(NodeTableReplayTest, NavicoWalkerFromTheFullCapture) {
    std::string path = externalSamplesDir() + "navico-source-selection.raw";
    REQUIRE_EXTERNAL_SAMPLE(path);
    ReplayResult r = replay(path);
    // dbfb claims 3, 4, 5, 7, 8, 11, 15, 17 in turn (tests/canboat_fixture_tests.cpp), but only
    // ever owns 3 and 17: everywhere in between a lower NAME holds the SA and keeps it.
    EXPECT_EQ(walk(r, kNdbfb), (std::vector<int>{3, 17}));
    EXPECT_EQ(r.owners.at(17), kNdbfb);
    std::vector<Resolved> lost;
    for (const auto &c : contests(r)) {
        if (c.loser == kNdbfb) {
            lost.push_back(c);
        }
    }
    const std::vector<Resolved> expected = {
        {3, Branch::LowerNameWins, kN2fbc, kNdbfb},
        // It claims 5 before 4's holder has answered.
        {4, Branch::ChallengerWithdrew, 0xc03287002fbcc727ULL, kNdbfb},
        {5, Branch::HolderDefended, 0xc03287002fbd0727ULL, kNdbfb},
        {7, Branch::HolderDefended, kN709b, kNdbfb},
        {8, Branch::HolderDefended, 0xc03284002fb14727ULL, kNdbfb},
        {11, Branch::HolderDefended, 0xc03284002fb20727ULL, kNdbfb},
        {15, Branch::HolderDefended, 0xc03284002fb30727ULL, kNdbfb},
    };
    EXPECT_EQ(lost, expected);
}
