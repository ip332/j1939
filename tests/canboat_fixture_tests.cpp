//
// Replays canboat NMEA 2000 captures through ActisenseStream and checks the address-claim
// (PGN 60928) traffic they contain. These are the ground-truth fixtures for SA <-> NAME
// tracking; see testdata/README.md for where each file comes from and what it shows.
//
// Expected numbers were derived independently of this code (a separate Python reader over
// the same files) and are tied to the canboat commit pinned in scripts/fetch_test_logs.sh.
//
//  - CanboatExcerptTest: small excerpts committed under testdata/canboat/, always run.
//  - CanboatFullLogTest: the full captures, fetched by scripts/fetch_test_logs.sh into
//    testdata/external/canboat/ (or $CANBOAT_SAMPLES_DIR); skipped when absent.
//

#include "actisense_stream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

struct Claim {
    uint8_t sa;
    uint64_t name;
};

struct Replay {
    size_t messages = 0;
    size_t long_messages = 0; // more than 8 bytes, i.e. reassembled by the gateway
    uint64_t first_ns = 0;
    uint64_t last_ns = 0;
    std::vector<Claim> claims;
    std::vector<J1939_frame> first_of_pgn_129029; // at most one, to inspect a long message
};

bool fileExists(const std::string &path) {
    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        return false;
    }
    fclose(f);
    return true;
}

Replay replay(const std::string &path) {
    Replay r;
    ActisenseStream stream;
    EXPECT_TRUE(stream.open(path)) << path;
    J1939_frame frame;
    while (stream.dataAvailable()) {
        frame.reset();
        if (!stream.get(&frame)) {
            continue;
        }
        if (r.messages++ == 0) {
            r.first_ns = frame.time_ns_;
        }
        r.last_ns = frame.time_ns_;
        if (frame.dlc_ > 8) {
            r.long_messages++;
        }
        if (frame.pgn_ == 0xEE00 && frame.dlc_ >= 8) {
            uint64_t name = 0;
            for (int i = 7; i >= 0; i--) {
                name = (name << 8) | frame.buffer_[i];
            }
            r.claims.push_back({frame.src_, name});
        }
        if (frame.pgn_ == 129029 && r.first_of_pgn_129029.empty()) {
            r.first_of_pgn_129029.push_back(frame);
        }
    }
    return r;
}

// The SAs `name` claimed, in order, with repeated claims of the same SA collapsed.
std::vector<int> saWalk(const Replay &r, uint64_t name) {
    std::vector<int> walk;
    for (const auto &c : r.claims) {
        if (c.name == name && (walk.empty() || walk.back() != c.sa)) {
            walk.push_back(c.sa);
        }
    }
    return walk;
}

// Index of the first claim of `sa` by `name` at or after `from`, or -1.
int findClaim(const Replay &r, uint8_t sa, uint64_t name, size_t from = 0) {
    for (size_t i = from; i < r.claims.size(); i++) {
        if (r.claims[i].sa == sa && r.claims[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The next claim after index `after` that involves `sa` or `name`.
const Claim *nextClaimInvolving(const Replay &r, size_t after, uint8_t sa, uint64_t name) {
    for (size_t i = after + 1; i < r.claims.size(); i++) {
        if (r.claims[i].sa == sa || r.claims[i].name == name) {
            return &r.claims[i];
        }
    }
    return nullptr;
}

std::string excerpt(const char *name) {
    return std::string(TESTDATA_FOLDER) + "canboat/" + name;
}

// NAMEs from the live.all cascade. Lower NAME wins address arbitration.
constexpr uint64_t kWalker = 0xc07887002fb83d30ULL;       // walks SA 17 -> 23
constexpr uint64_t kDefender17 = 0xc0508c002fbda94dULL;   // holds SA 17, lower than kWalker
constexpr uint64_t kDisplaced23 = 0xc07891002fb8bb2aULL;  // holds SA 23, higher than kWalker

} // namespace

TEST(CanboatExcerptTest, LiveCascadeCountsAndWalk) {
    Replay r = replay(excerpt("live_claim_cascade.all"));
    EXPECT_EQ(r.messages, 299u); // 300 lines, one canboat pseudo-PGN skipped
    EXPECT_EQ(r.claims.size(), 32u);
    EXPECT_EQ(r.long_messages, 41u);
    EXPECT_EQ(r.first_ns, 1783316249616000000ULL);
    EXPECT_EQ(r.last_ns, 1783316250494000000ULL);
    EXPECT_EQ(saWalk(r, kWalker), (std::vector<int>{17, 18, 19, 20, 21, 22, 23}));
}

TEST(CanboatExcerptTest, LiveCascadeLowerNameDefendsItsAddress) {
    // kWalker claims SA 17, which kDefender17 (lower NAME) holds: the holder re-claims 17 and
    // kWalker moves on to 18.
    ASSERT_LT(kDefender17, kWalker);
    Replay r = replay(excerpt("live_claim_cascade.all"));
    int contest = findClaim(r, 17, kWalker);
    ASSERT_GE(contest, 0);
    const Claim *reply = nextClaimInvolving(r, contest, 17, kWalker);
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->sa, 17);
    EXPECT_EQ(reply->name, kDefender17);
    EXPECT_GE(findClaim(r, 18, kWalker, contest), 0);
}

TEST(CanboatExcerptTest, LiveCascadeLowerNameTakesTheAddress) {
    // kWalker claims SA 23, held by kDisplaced23 (higher NAME): kWalker keeps 23 and the
    // displaced node re-claims at 24 instead.
    ASSERT_LT(kWalker, kDisplaced23);
    Replay r = replay(excerpt("live_claim_cascade.all"));
    int contest = findClaim(r, 23, kWalker);
    ASSERT_GE(contest, 0);
    const Claim *reply = nextClaimInvolving(r, contest, 23, kDisplaced23);
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->sa, 24);
    EXPECT_EQ(reply->name, kDisplaced23);
    EXPECT_EQ(saWalk(r, kWalker).back(), 23);
}

TEST(CanboatExcerptTest, Nac3ResetReclaim) {
    Replay r = replay(excerpt("nac3_reset_reclaim.raw"));
    EXPECT_EQ(r.messages, 280u);
    EXPECT_EQ(r.claims.size(), 197u);
    EXPECT_EQ(r.long_messages, 9u);
    EXPECT_EQ(r.first_ns, 1780886022802000000ULL);
    EXPECT_EQ(r.last_ns, 1780886023398000000ULL);
    EXPECT_EQ(saWalk(r, 0xc0509600227d1bfbULL), (std::vector<int>{0, 1, 2, 3, 4}));
    EXPECT_EQ(saWalk(r, 0xc0509bff2270dbfbULL), (std::vector<int>{0, 4, 5}));
    EXPECT_EQ(saWalk(r, 0xc0328700227b5bfbULL), (std::vector<int>{0}));
}

TEST(CanboatExcerptTest, NavicoNameWalk) {
    Replay r = replay(excerpt("navico_name_walk.raw"));
    EXPECT_EQ(r.messages, 321u);
    EXPECT_EQ(r.claims.size(), 303u);
    EXPECT_EQ(r.long_messages, 6u);
    EXPECT_EQ(r.first_ns, 1780904063448000000ULL);
    EXPECT_EQ(r.last_ns, 1780904064881000000ULL);
    EXPECT_EQ(saWalk(r, 0xc0509bff2270dbfbULL), (std::vector<int>{4, 5, 7, 8, 11, 15}));
}

TEST(CanboatExcerptTest, DironaPlainFormat) {
    Replay r = replay(excerpt("dirona_plain_claims.raw"));
    EXPECT_EQ(r.messages, 199u); // 200 lines, one canboat pseudo-PGN (262386) skipped
    EXPECT_EQ(r.claims.size(), 35u);
    EXPECT_EQ(r.long_messages, 11u);
    EXPECT_EQ(r.first_ns, 1456689432400000000ULL);
    EXPECT_EQ(r.last_ns, 1456689432878000000ULL);
    // Fast-packet GNSS position arrives as one 43-byte message, not as split frames.
    ASSERT_EQ(r.first_of_pgn_129029.size(), 1u);
    EXPECT_EQ(r.first_of_pgn_129029[0].dlc_, 43);
}

namespace {

std::string externalSamplesDir() {
    const char *env = getenv("CANBOAT_SAMPLES_DIR");
    if (env != nullptr && *env != 0) {
        return std::string(env) + "/";
    }
    return std::string(TESTDATA_FOLDER) + "external/canboat/samples/";
}

struct FullLog {
    const char *file;
    size_t messages;
    size_t claims;
    size_t long_messages;
    uint64_t first_ns;
    uint64_t last_ns;
};

const FullLog kFullLogs[] = {
    {"live.all", 257941, 1466, 38676, 1783316084315000000ULL, 1783316877414000000ULL},
    {"ap48-tack.all", 459384, 2584, 66191, 1783209897054000000ULL, 1783211265286000000ULL},
    {"nac3-reset-dockside-commissioning.raw", 67106, 380, 6945, 1780886012520000000ULL,
     1780886323975000000ULL},
    {"navico-source-selection.raw", 52496, 2713, 18209, 1780903569154000000ULL,
     1780905921933000000ULL},
    {"dirona-actisense-serial.raw", 39870, 420, 2482, 1456689422364000000ULL,
     1456689485000000000ULL},
    {"merrimac-actisense-serial-2011.raw", 967093, 6495, 87971, 1303712699772000000ULL,
     1303726603019000000ULL},
};

class CanboatFullLogTest : public testing::TestWithParam<FullLog> {};

const char *const kFetchHint = " -- run scripts/fetch_test_logs.sh (or set CANBOAT_SAMPLES_DIR)";

} // namespace

TEST_P(CanboatFullLogTest, MatchesIndependentCounts) {
    const FullLog &log = GetParam();
    std::string path = externalSamplesDir() + log.file;
    if (!fileExists(path)) {
        GTEST_SKIP() << "canboat sample " << path << " not found" << kFetchHint;
    }
    Replay r = replay(path);
    EXPECT_EQ(r.messages, log.messages);
    EXPECT_EQ(r.claims.size(), log.claims);
    EXPECT_EQ(r.long_messages, log.long_messages);
    EXPECT_EQ(r.first_ns, log.first_ns);
    EXPECT_EQ(r.last_ns, log.last_ns);
}

INSTANTIATE_TEST_SUITE_P(Canboat, CanboatFullLogTest, testing::ValuesIn(kFullLogs),
                         [](const testing::TestParamInfo<FullLog> &info) {
                             std::string name = info.param.file;
                             for (auto &c : name) {
                                 if (!isalnum(static_cast<unsigned char>(c))) {
                                     c = '_';
                                 }
                             }
                             return name;
                         });

TEST(CanboatFullLogWalkTest, LiveAllWalkerAndNavicoWalker) {
    std::string live = externalSamplesDir() + "live.all";
    std::string navico = externalSamplesDir() + "navico-source-selection.raw";
    if (!fileExists(live) || !fileExists(navico)) {
        GTEST_SKIP() << "canboat samples not found in " << externalSamplesDir() << kFetchHint;
    }
    EXPECT_EQ(saWalk(replay(live), kWalker), (std::vector<int>{17, 18, 19, 20, 21, 22, 23}));
    EXPECT_EQ(saWalk(replay(navico), 0xc0509bff2270dbfbULL),
              (std::vector<int>{3, 4, 5, 7, 8, 11, 15, 17}));
}
