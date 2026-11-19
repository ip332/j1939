//
// Tests for CanSandbox's main processing loop, using MockCanStream to exercise it
// deterministically without real files or CAN hardware. Includes regression coverage
// for two bugs fixed in an earlier pass: frames could be silently dropped at EOF, and
// a real-port output reconnect used the wrong stream's name.
//

#include "actisense_stream.h"
#include "can_sandbox_lib.h"
#include "mock_can_stream.h"

#include <cstdio>
#include <gtest/gtest.h>

namespace {

J1939_frame makeFrame(uint32_t pgn, uint8_t src, const uint8_t data[8]) {
    J1939_frame frame;
    frame.reset();
    frame.pgn_ = pgn;
    frame.dst_ = 0xFF;
    frame.src_ = src;
    frame.pri_ = 6;
    frame.dlc_ = 8;
    memcpy(frame.buffer_, data, 8);
    return frame;
}

} // namespace

class CanSandboxParseOptionsTest : public testing::Test {
protected:
    void TearDown() override {
        for (const auto &path : temp_paths_) {
            remove(path.c_str());
        }
    }

    // Writes `content` to a fresh temp file with the given extension and returns its
    // path. parseOptions() integrates with the real filesystem via CanStreamFor(), so
    // this needs real files rather than the MockCanStream injection seam.
    std::string writeTemp(const std::string &content, const char *suffix) {
        std::string path = "/tmp/can_sandbox_test_XXXXXX" + std::string(suffix);
        std::vector<char> buf(path.begin(), path.end());
        buf.push_back('\0');
        int fd = mkstemps(buf.data(), static_cast<int>(strlen(suffix)));
        FILE *f = fdopen(fd, "w");
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
        std::string result(buf.data());
        temp_paths_.push_back(result);
        return result;
    }

    std::vector<std::string> temp_paths_;
};

TEST_F(CanSandboxParseOptionsTest, FailsWithNoInputAndPrintsUsage) {
    CanSandbox sandbox;
    const char *argv[] = {"prog"};
    EXPECT_FALSE(sandbox.parseOptions(1, argv));
}

TEST_F(CanSandboxParseOptionsTest, FailsWithAnUnrecognizedFlag) {
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-not-a-real-flag"};
    EXPECT_FALSE(sandbox.parseOptions(2, argv));
}

TEST_F(CanSandboxParseOptionsTest, SucceedsWithAValidInputFile) {
    std::string path = writeTemp("(1557265716.818982) can0 0CFF0686#F22700F2FFFFFF5F\n", ".log");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", path.c_str()};
    EXPECT_TRUE(sandbox.parseOptions(3, argv));
}

TEST_F(CanSandboxParseOptionsTest, FailsWithNonexistentInputFile) {
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", "/nonexistent/path/does_not_exist.log"};
    EXPECT_FALSE(sandbox.parseOptions(3, argv));
}

TEST_F(CanSandboxParseOptionsTest, FailsWithOutputInAnUnwritableLocation) {
    std::string in_path = writeTemp("", ".log");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", in_path.c_str(), "-out", "/nonexistent_dir/out.log"};
    EXPECT_FALSE(sandbox.parseOptions(5, argv));
}

TEST_F(CanSandboxParseOptionsTest, SucceedsWithAValidOutputFile) {
    std::string in_path = writeTemp("", ".log");
    std::string out_path = "/tmp/can_sandbox_test_out.log";
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", in_path.c_str(), "-out", out_path.c_str()};
    EXPECT_TRUE(sandbox.parseOptions(5, argv));
    remove(out_path.c_str());
}

TEST_F(CanSandboxParseOptionsTest, FailsWithABadDbcFile) {
    // DbcParser::parseFile() is lenient about content (unrecognized lines are just
    // skipped) -- it only fails on a non-.dbc extension or a file it can't open.
    std::string in_path = writeTemp("", ".log");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", in_path.c_str(), "-dbc", "/nonexistent/path.dbc"};
    EXPECT_FALSE(sandbox.parseOptions(5, argv));
}

TEST_F(CanSandboxParseOptionsTest, SucceedsWithAValidDbcFile) {
    std::string in_path = writeTemp("", ".log");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", in_path.c_str(), "-dbc", DBC_FOLDER "test_database.dbc"};
    EXPECT_TRUE(sandbox.parseOptions(5, argv));
}

TEST(CanSandboxTest, RoundTripsFramesFromMockInput) {
    auto input = std::make_unique<MockCanStream>();
    const uint8_t data1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t data2[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    const uint8_t data3[8] = {0xAA, 0, 0, 0, 0, 0, 0, 0};
    input->enqueueFrame(makeFrame(0x00FEF100, 0x11, data1));
    input->enqueueFrame(makeFrame(0x00FEF100, 0x11, data2));
    input->enqueueFrame(makeFrame(0x00FEF100, 0x11, data3));

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input));

    std::vector<J1939_frame> received;
    sandbox.setParserCallback([&](const J1939Parser &parser) {
        received.push_back(parser.frame());
    });

    sandbox.startProcessing();

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0].buffer_[0], 1);
    EXPECT_EQ(received[1].buffer_[0], 8);
    EXPECT_EQ(received[2].buffer_[0], 0xAA);
}

TEST(CanSandboxTest, ForwardsInputFramesToOutput) {
    auto input = std::make_unique<MockCanStream>();
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    input->enqueueFrame(makeFrame(0x00FEF100, 0x11, data));

    auto output = std::make_unique<MockCanStream>();
    MockCanStream *output_ptr = output.get();

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input));
    sandbox.setOutputStream(std::move(output));

    sandbox.startProcessing();

    ASSERT_EQ(output_ptr->written().size(), 1u);
    EXPECT_EQ(output_ptr->written()[0].buffer_[0], 1);
}

TEST(CanSandboxTest, ReassemblesMultiFrameTransfer) {
    auto input = std::make_unique<MockCanStream>();
    // BAM: control=32, size=10 (LSB/MSB), 2 frames, reserved, PGN=0x00FEF1
    const uint8_t bam[8] = {32, 10, 0, 2, 0xFF, 0xF1, 0xFE, 0x00};
    const uint8_t dt1[8] = {1, 'H', 'e', 'l', 'l', 'o', ' ', 'J'};
    const uint8_t dt2[8] = {2, '1', '9', '3', '9', 0xFF, 0xFF, 0xFF};
    input->enqueueFrame(makeFrame(0xEC00, 0x22, bam));
    input->enqueueFrame(makeFrame(0xEB00, 0x22, dt1));
    input->enqueueFrame(makeFrame(0xEB00, 0x22, dt2));

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input));

    std::vector<J1939_frame> received;
    sandbox.setParserCallback([&](const J1939Parser &parser) {
        received.push_back(parser.frame());
    });

    sandbox.startProcessing();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].dlc_, 10);
    EXPECT_EQ(received[0].pgn_, 0x00FEF1u);
    EXPECT_EQ(memcmp(received[0].buffer_, "Hello J", 7), 0);
    EXPECT_EQ(memcmp(&received[0].buffer_[7], "193", 3), 0);
}

TEST(CanSandboxTest, PassesPreassembledMessagesThroughWithoutReassembly) {
    // A gateway log (e.g. ActisenseStream) already carries complete fast-packet messages.
    // 130824 is a fast-packet PGN; this 2-byte message's first byte (0x7d) would read as a
    // continuation frame and be dropped if it went through TransportProtocol again.
    auto input = std::make_unique<MockCanStream>();
    input->setPreassembled(true);
    J1939_frame message;
    message.reset();
    message.pgn_ = 130824;
    message.dst_ = 0xFF;
    message.src_ = 38;
    message.pri_ = 3;
    message.dlc_ = 2;
    message.buffer_[0] = 0x7d;
    message.buffer_[1] = 0x99;
    input->enqueueFrame(message);

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input));
    std::vector<J1939_frame> received;
    sandbox.setParserCallback([&](const J1939Parser &parser) {
        received.push_back(parser.frame());
    });
    sandbox.startProcessing();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_TRUE(received[0] == message);
}

TEST(CanSandboxTest, ReconnectsRealPortOutputUsingCorrectStreamName) {
    auto input = std::make_unique<MockCanStream>();
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    input->enqueueFrame(makeFrame(0x00FEF100, 0x11, data));

    auto output = std::make_unique<MockCanStream>();
    output->setRealPort(true);
    output->setReady(false); // forces the reconnect path
    MockCanStream *output_ptr = output.get();

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input), "mock-input");
    sandbox.setOutputStream(std::move(output), "mock-output");

    sandbox.startProcessing();

    ASSERT_FALSE(output_ptr->openCalls().empty());
    for (const auto &name : output_ptr->openCalls()) {
        EXPECT_EQ(name, "mock-output");
    }
}

TEST(CanSandboxTest, SimulatesRealBusTimingWhenReplayingToARealPortOutput) {
    // Needs: input NOT a real port (so timing simulation applies), output IS a real
    // port and ready, and a second frame with time_ns_ strictly greater than the
    // first's -- otherwise the `frame.time_ns_ > last_time` branch never triggers.
    auto input = std::make_unique<MockCanStream>();
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    J1939_frame frame1 = makeFrame(0x00FEF100, 0x11, data);
    frame1.time_ns_ = 1000000;
    J1939_frame frame2 = makeFrame(0x00FEF100, 0x11, data);
    frame2.time_ns_ = 6000000; // 5ms later -> a short, bounded usleep()
    input->enqueueFrame(frame1);
    input->enqueueFrame(frame2);

    auto output = std::make_unique<MockCanStream>();
    output->setRealPort(true);
    MockCanStream *output_ptr = output.get();

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input));
    sandbox.setOutputStream(std::move(output));

    sandbox.startProcessing();

    EXPECT_EQ(output_ptr->written().size(), 2u);
}

TEST(CanSandboxTest, ReconnectsRealPortOutputAfterAPutFailure) {
    auto input = std::make_unique<MockCanStream>();
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    input->enqueueFrame(makeFrame(0x00FEF100, 0x11, data));

    auto output = std::make_unique<MockCanStream>();
    output->setRealPort(true);
    output->failPut(true); // every put() fails -> triggers close/reopen
    MockCanStream *output_ptr = output.get();

    CanSandbox sandbox;
    sandbox.setInputStream(std::move(input));
    sandbox.setOutputStream(std::move(output), "mock-output");

    sandbox.startProcessing();

    EXPECT_TRUE(output_ptr->written().empty()); // put() always failed
    ASSERT_FALSE(output_ptr->openCalls().empty());
    EXPECT_EQ(output_ptr->openCalls().back(), "mock-output");
}

// --- address-claim tracking (-nodes, -nodes-report, -request-claims) ---

namespace {
// Actisense lines: unresolved traffic on SA 10, kLow claims it, kHigh contests and loses
// (kLow defends), kHigh settles on 11.
const char *kClaimLog =
    "2026-01-01T00:00:00.000Z,3,61444,10,255,8,01,02,03,04,05,06,07,08\n"
    "2026-01-01T00:00:00.010Z,6,60928,10,255,8,01,00,00,00,00,8c,50,c0\n"
    "2026-01-01T00:00:00.020Z,3,61444,10,255,8,01,02,03,04,05,06,07,08\n"
    "2026-01-01T00:00:00.100Z,6,60928,10,255,8,03,00,00,00,00,8c,50,c0\n"
    "2026-01-01T00:00:00.110Z,3,61444,10,255,8,01,02,03,04,05,06,07,08\n"
    "2026-01-01T00:00:00.120Z,6,60928,10,255,8,01,00,00,00,00,8c,50,c0\n"
    "2026-01-01T00:00:00.200Z,6,60928,11,255,8,03,00,00,00,00,8c,50,c0\n";

std::string readFile(const std::string &path) {
    std::string content;
    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        return content;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        content.append(buf, n);
    }
    fclose(f);
    return content;
}
} // namespace

TEST_F(CanSandboxParseOptionsTest, NodesPrintsEventsResolutionsAndTheTable) {
    std::string path = writeTemp(kClaimLog, ".raw");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", path.c_str(), "-nodes"};
    ASSERT_TRUE(sandbox.parseOptions(4, argv));
    testing::internal::CaptureStdout();
    sandbox.startProcessing();
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("UNRESOLVED"), std::string::npos);
    EXPECT_NE(out.find(" NAME:0xc0508c0000000001"), std::string::npos);
    EXPECT_NE(out.find(" CONTESTED:0xc0508c0000000001|0xc0508c0000000003"), std::string::npos);
    EXPECT_NE(out.find("NODE NodeAppeared sa=10 name=0xc0508c0000000001"), std::string::npos);
    EXPECT_NE(out.find("NODE ContestResolved sa=10 name=0xc0508c0000000001 other=0xc0508c0000000003 "
                       "branch=HolderDefended"),
              std::string::npos);
    EXPECT_NE(out.find("NODE Attributed sa=10"), std::string::npos);
    EXPECT_NE(out.find("Address-claim table:"), std::string::npos);
    EXPECT_NE(out.find("SA 11   Active"), std::string::npos);

    const NodeTable *table = sandbox.nodeTable();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->bySa(10)->name_, 0xc0508c0000000001ULL);
    EXPECT_EQ(table->bySa(11)->name_, 0xc0508c0000000003ULL);
    EXPECT_EQ(table->stats().resolved[static_cast<int>(Branch::HolderDefended)], 1u);
}

TEST_F(CanSandboxParseOptionsTest, NodesPrintsACommandedMove) {
    // Actisense logs carry Commanded Address already reassembled (9 bytes).
    std::string path = writeTemp(
        "2026-01-01T00:00:00.000Z,6,60928,10,255,8,01,00,00,00,00,8c,50,c0\n"
        "2026-01-01T00:00:00.100Z,6,65240,39,255,9,01,00,00,00,00,8c,50,c0,2a\n"
        "2026-01-01T00:00:00.200Z,6,60928,42,255,8,01,00,00,00,00,8c,50,c0\n",
        ".raw");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", path.c_str(), "-quiet", "-nodes"};
    ASSERT_TRUE(sandbox.parseOptions(5, argv));
    testing::internal::CaptureStdout();
    sandbox.startProcessing();
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("NODE CommandedAddress sa=42 old_sa=10 source_sa=39 name=0xc0508c0000000001"),
              std::string::npos) << out;
    EXPECT_NE(out.find("NODE NodeAddressChanged sa=42 old_sa=10 name=0xc0508c0000000001 commanded"),
              std::string::npos) << out;
}

TEST_F(CanSandboxParseOptionsTest, NodesReportWritesJson) {
    std::string log = writeTemp(kClaimLog, ".raw");
    std::string report = writeTemp("", ".json");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", log.c_str(), "-quiet", "-nodes-report", report.c_str(),
                          "-contest-window", "250", "-node-timeout", "60"};
    ASSERT_TRUE(sandbox.parseOptions(10, argv));
    // A name that needs JSON escaping.
    auto input = std::make_unique<ActisenseStream>();
    ASSERT_TRUE(input->open(log));
    sandbox.setInputStream(std::move(input), "we\"ird\\name\x01");
    testing::internal::CaptureStdout();
    sandbox.startProcessing();
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(out.find("NODE "), std::string::npos); // -nodes not given: no event lines

    std::string json = readFile(report);
    EXPECT_NE(json.find("\"input\": \"we\\\"ird\\\\name\\u0001\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"frames\": 7"), std::string::npos);
    EXPECT_NE(json.find("\"names\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"claims\": 4"), std::string::npos);
    EXPECT_NE(json.find("\"contests\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"address_changes\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"HolderDefended\": 1"), std::string::npos);
    EXPECT_NE(json.find("{\"name\": \"0xc0508c0000000003\", \"sa\": 11, \"state\": \"Active\""),
              std::string::npos);
    EXPECT_NE(json.find("\"type\": \"ContestResolved\""), std::string::npos);
    EXPECT_NE(json.find("\"branch\": \"HolderDefended\""), std::string::npos);
    EXPECT_EQ(sandbox.nodeTable()->config().contest_window_ns, 250000000ULL);
    EXPECT_EQ(sandbox.nodeTable()->config().stale_timeout_ns, 60000000000ULL);
}

TEST_F(CanSandboxParseOptionsTest, NodesReportToAnUnwritablePathDoesNotStopProcessing) {
    std::string log = writeTemp(kClaimLog, ".raw");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", log.c_str(), "-quiet", "-nodes-report",
                          "/nonexistent/dir/report.json"};
    ASSERT_TRUE(sandbox.parseOptions(6, argv));
    testing::internal::CaptureStdout();
    sandbox.startProcessing();
    testing::internal::GetCapturedStdout();
    EXPECT_EQ(sandbox.nodeTable()->stats().claims, 4u);
}

TEST_F(CanSandboxParseOptionsTest, RequestClaimsIsIgnoredForAFileInput) {
    std::string log = writeTemp(kClaimLog, ".raw");
    CanSandbox sandbox;
    const char *argv[] = {"prog", "-in", log.c_str(), "-quiet", "-request-claims"};
    ASSERT_TRUE(sandbox.parseOptions(5, argv));
    testing::internal::CaptureStdout();
    sandbox.startProcessing(); // warns, then processes the file normally
    testing::internal::GetCapturedStdout();
    EXPECT_EQ(sandbox.nodeTable()->stats().claims, 4u);
    EXPECT_FALSE(sandbox.sendAddressClaimRequest());
}

TEST(CanSandboxTest, AddressClaimRequestGoesOutOnARealPortInput) {
    auto input = std::make_unique<MockCanStream>();
    input->setRealPort(true);
    MockCanStream *input_ptr = input.get();
    CanSandbox sandbox;
    EXPECT_FALSE(sandbox.sendAddressClaimRequest()); // no input yet
    sandbox.setInputStream(std::move(input), "can0");
    ASSERT_TRUE(sandbox.sendAddressClaimRequest());
    ASSERT_EQ(input_ptr->written().size(), 1u);
    EXPECT_EQ(input_ptr->written()[0].canID(), CAN_EFF_FLAG | 0x18EAFFFEu);
    input_ptr->failPut(true);
    EXPECT_FALSE(sandbox.sendAddressClaimRequest());
}
