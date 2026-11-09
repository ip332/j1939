//
// Tests for CanSandbox's main processing loop, using MockCanStream to exercise it
// deterministically without real files or CAN hardware. Includes regression coverage
// for two bugs fixed in an earlier pass: frames could be silently dropped at EOF, and
// a real-port output reconnect used the wrong stream's name.
//

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
