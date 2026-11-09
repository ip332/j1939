//
// Tests for ActisenseStream: the canboat actisense-serial text format.
//

#include "actisense_stream.h"
#include "can_stream.h"

#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>

class ActisenseStreamTest : public testing::Test {
protected:
    void TearDown() override {
        for (const auto &path : temp_paths_) {
            remove(path.c_str());
        }
    }

    std::string writeTemp(const std::string &content) {
        char path[] = "/tmp/actisense_stream_test_XXXXXX";
        int fd = mkstemp(path);
        FILE *f = fdopen(fd, "w");
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
        temp_paths_.emplace_back(path);
        return path;
    }

    // Reads every line of `content`; returns the frames get() accepted.
    std::vector<J1939_frame> decodeAll(const std::string &content) {
        std::vector<J1939_frame> frames;
        ActisenseStream stream;
        if (!stream.open(writeTemp(content))) {
            return frames;
        }
        J1939_frame frame;
        while (stream.dataAvailable()) {
            frame.reset();
            if (stream.get(&frame)) {
                frames.push_back(frame);
            }
        }
        return frames;
    }

    // Expects `line` to be rejected.
    void expectRejected(const std::string &line) {
        EXPECT_TRUE(decodeAll(line + "\n").empty()) << line;
    }

    std::vector<std::string> temp_paths_;
};

TEST_F(ActisenseStreamTest, DecodesPdu2Line) {
    auto frames = decodeAll("2016-02-28T19:57:02.364Z,2,127250,7,255,8,ff,10,3b,ff,7f,ce,f5,fc\n");
    ASSERT_EQ(frames.size(), 1u);
    const J1939_frame &f = frames[0];
    EXPECT_EQ(f.pgn_, 127250u);
    EXPECT_EQ(f.src_, 7);
    EXPECT_EQ(f.dst_, 0xFF);
    EXPECT_EQ(f.pri_, 2);
    EXPECT_EQ(f.dlc_, 8);
    EXPECT_TRUE(f.extended_);
    EXPECT_EQ(f.canID(), CAN_EFF_FLAG | 0x09F11207u);
    EXPECT_EQ(f.time_ns_, 1456689422364000000ULL);
    const uint8_t expected[8] = {0xff, 0x10, 0x3b, 0xff, 0x7f, 0xce, 0xf5, 0xfc};
    EXPECT_EQ(memcmp(f.buffer_, expected, 8), 0);
}

TEST_F(ActisenseStreamTest, DecodesAddressClaim) {
    auto frames = decodeAll("2026-07-06T05:37:29.736Z,6,60928,17,255,8,30,3d,b8,2f,00,87,78,c0\n");
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].pgn_, 0xEE00u);
    EXPECT_EQ(frames[0].src_, 17);
    EXPECT_EQ(frames[0].dst_, 0xFF);
    EXPECT_EQ(frames[0].canID(), CAN_EFF_FLAG | 0x18EEFF11u);
}

TEST_F(ActisenseStreamTest, KeepsPdu1DestinationAndDataPage) {
    // 126208 = 0x1ED00: PDU1 on data page 1, addressed to 64.
    auto frames = decodeAll("2023-03-07T13:45:28.994Z,3,126208,92,64,7,02,1a,fd,01,00,01,f0\n");
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].pgn_, 126208u);
    EXPECT_EQ(frames[0].dst_, 64);
    EXPECT_EQ(frames[0].src_, 92);
    EXPECT_EQ(frames[0].dlc_, 7);
    EXPECT_EQ(frames[0].canID(), CAN_EFF_FLAG | 0x0DED405Cu);
}

TEST_F(ActisenseStreamTest, DecodesReassembledMessageLongerThanEightBytes) {
    std::string line = "2016-02-28T19:57:12.000Z,3,129029,3,255,43";
    for (int i = 0; i < 43; i++) {
        char byte[4];
        snprintf(byte, sizeof(byte), ",%02x", i);
        line += byte;
    }
    auto frames = decodeAll(line + "\n");
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].dlc_, 43);
    EXPECT_EQ(frames[0].buffer_[42], 42);
}

TEST_F(ActisenseStreamTest, EmptyTimestampReusesThePreviousOne) {
    auto frames = decodeAll("2026-07-06T05:34:44.315Z,7,65305,21,255,8,41,9f,01,0b,00,00,00,00\n"
                            ",7,65305,21,255,8,41,9f,01,0b,00,00,00,00\n");
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].time_ns_, frames[0].time_ns_);
    EXPECT_EQ(frames[1].time_ns_, 1783316084315000000ULL);
}

TEST_F(ActisenseStreamTest, SkipsCommentsAndKeepsReadingAfterThem) {
    auto frames = decodeAll("# format=FAST\n"
                            "#\n"
                            "\n"
                            "2026-07-06T05:37:29.736Z,6,60928,17,255,8,30,3d,b8,2f,00,87,78,c0\n");
    EXPECT_EQ(frames.size(), 1u);
}

TEST_F(ActisenseStreamTest, RejectsMalformedLines) {
    // canboat-internal pseudo-PGN (gateway status), never a CAN frame.
    expectRejected(",7,262657,0,255,8,00,00,41,4c,4c,01,ff,ff");
    // PDU1 PGN whose low byte isn't 0 can't be put on the wire.
    expectRejected("2026-07-06T05:37:29.736Z,6,60929,17,255,8,30,3d,b8,2f,00,87,78,c0");
    // Field ranges.
    expectRejected("2026-07-06T05:37:29.736Z,8,60928,17,255,8,30,3d,b8,2f,00,87,78,c0");
    expectRejected("2026-07-06T05:37:29.736Z,6,60928,256,255,8,30,3d,b8,2f,00,87,78,c0");
    expectRejected("2026-07-06T05:37:29.736Z,6,60928,17,256,8,30,3d,b8,2f,00,87,78,c0");
    expectRejected("2026-07-06T05:37:29.736Z,6,60928,17,255,1759");
    expectRejected("2026-07-06T05:37:29.736Z,x,60928,17,255,8,30,3d,b8,2f,00,87,78,c0");
    expectRejected("2026-07-06T05:37:29.736Z,6,60928x,17,255,8,30,3d,b8,2f,00,87,78,c0");
    // Declared length larger than the bytes present, and a non-hex byte.
    expectRejected("2026-07-06T05:37:29.736Z,6,60928,17,255,8,30,3d,b8,2f,00,87,78");
    expectRejected("2026-07-06T05:37:29.736Z,6,60928,17,255,8,30,3d,b8,2f,00,87,78,zz");
    // Too few columns, a bad timestamp, and other formats that happen to share the
    // .raw extension (YDWG raw, Actisense W2K-1 N2K ASCII).
    expectRejected("2026-07-06T05:37:29.736Z,6,60928,17,255");
    expectRejected("2026-13-06T05:37:29.736Z,6,60928,17,255,8,30,3d,b8,2f,00,87,78,c0");
    expectRejected("21:55:35.425 R 15FD0723 FF C0 D9 6F FF 7F FF FF");
    expectRejected("A000057.055 09FF7 0FF00 3F9FDCFFFFFFFFFF");
}

TEST_F(ActisenseStreamTest, ParsesEveryTimestampShapeInTheCanboatSamples) {
    uint64_t ns = 0;
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2011-04-25-06:24:59", &ns));
    EXPECT_EQ(ns, 1303712699000000000ULL);
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2011-04-25-06:24:59.785", &ns));
    EXPECT_EQ(ns, 1303712699785000000ULL);
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2026-06-08T02:33:32.520", &ns));
    EXPECT_EQ(ns, 1780886012520000000ULL);
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2016-02-28T19:57:02.364Z", &ns));
    EXPECT_EQ(ns, 1456689422364000000ULL);
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2016-02-28T19:57:02.364123Z", &ns));
    EXPECT_EQ(ns, 1456689422364123000ULL);
    // Leap day with a numeric zone offset: 23:59:59.123456789 at UTC-7 is 06:59:59 next day UTC.
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2024-02-29T23:59:59.123456789-07:00", &ns));
    EXPECT_EQ(ns, 1709276399123456789ULL);
    ASSERT_TRUE(ActisenseStream::parseTimestamp("2024-03-01T07:59:59+01:00", &ns));
    EXPECT_EQ(ns, 1709276399000000000ULL);
    // Digits past nanoseconds are ignored.
    ASSERT_TRUE(ActisenseStream::parseTimestamp("1970-01-01T00:00:01.0000000019", &ns));
    EXPECT_EQ(ns, 1000000001ULL);
    // A device without a clock, logging in a zone west of UTC: before the epoch, clamped.
    ASSERT_TRUE(ActisenseStream::parseTimestamp("1969-12-31T16:00:34.206", &ns));
    EXPECT_EQ(ns, 0u);
    ASSERT_TRUE(ActisenseStream::parseTimestamp("0000-01-01T00:00:00", &ns));
    EXPECT_EQ(ns, 0u);
}

TEST_F(ActisenseStreamTest, RejectsMalformedTimestamps) {
    uint64_t ns = 0;
    const char *bad[] = {
        "",
        "2016-02-28",
        "2016-02-28 19:57:02",
        "2016-02-28T19:57",
        "2016-02-28T19-57-02",
        // A non-digit or wrong separator at each position in turn.
        "201x-02-28T19:57:02",
        "2016/02-28T19:57:02",
        "2016-0x-28T19:57:02",
        "2016-02/28T19:57:02",
        "2016-02-2xT19:57:02",
        "2016-02-28T1x:57:02",
        "2016-02-28T19.57:02",
        "2016-02-28T19:5x:02",
        "2016-02-28T19:57.02",
        "2016-02-28T19:57:0x",
        // Out-of-range fields.
        "2016-00-28T19:57:02",
        "2016-13-28T19:57:02",
        "2016-02-00T19:57:02",
        "2016-02-32T19:57:02",
        "2016-02-28T24:00:00",
        "2016-02-28T19:60:00",
        "2016-02-28T19:57:61",
        // Fraction and zone.
        "2016-02-28T19:57:02.",
        "2016-02-28T19:57:02.364ZZ",
        "2016-02-28T19:57:02+0700",
        "2016-02-28T19:57:02+07",
        "2016-02-28T19:57:02+x7:00",
        "2016-02-28T19:57:02-07:x0",
        "2016-02-28T19:57:02.364 ",
    };
    for (const char *text : bad) {
        EXPECT_FALSE(ActisenseStream::parseTimestamp(text, &ns)) << text;
    }
}

TEST_F(ActisenseStreamTest, FormatsTimestampsAsUtcWithMilliseconds) {
    char buf[32];
    ActisenseStream::formatTimestamp(1709276399123456789ULL, buf, sizeof(buf));
    EXPECT_STREQ(buf, "2024-03-01T06:59:59.123Z");
    ActisenseStream::formatTimestamp(0, buf, sizeof(buf));
    EXPECT_STREQ(buf, "1970-01-01T00:00:00.000Z");
}

TEST_F(ActisenseStreamTest, PutWritesLinesThatReadBackIdentically) {
    const char *lines[] = {
        "2016-02-28T19:57:02.364Z,2,127250,7,255,8,ff,10,3b,ff,7f,ce,f5,fc\n",
        "2023-03-07T13:45:28.994Z,3,126208,92,64,7,02,1a,fd,01,00,01,f0\n",
        "2026-07-06T05:37:29.736Z,6,60928,17,255,8,30,3d,b8,2f,00,87,78,c0\n",
    };
    std::string input;
    for (const char *line : lines) {
        input += line;
    }
    auto frames = decodeAll(input);
    ASSERT_EQ(frames.size(), 3u);

    std::string path = writeTemp("");
    {
        ActisenseStream out;
        ASSERT_TRUE(out.open(path, true));
        for (const auto &frame : frames) {
            EXPECT_TRUE(out.put(frame));
        }
    }
    // Byte-for-byte the same text (lower-case hex, millisecond UTC timestamps)...
    FILE *f = fopen(path.c_str(), "r");
    ASSERT_NE(f, nullptr);
    char buf[256];
    for (const char *line : lines) {
        ASSERT_NE(fgets(buf, sizeof(buf), f), nullptr);
        EXPECT_STREQ(buf, line);
    }
    fclose(f);
    // ...and the same frames.
    ActisenseStream in;
    ASSERT_TRUE(in.open(path));
    for (const auto &expected : frames) {
        J1939_frame frame;
        ASSERT_TRUE(in.get(&frame));
        EXPECT_TRUE(frame == expected);
        EXPECT_EQ(frame.time_ns_, expected.time_ns_);
    }
}

TEST_F(ActisenseStreamTest, IsPreassembledAndWritable) {
    ActisenseStream stream;
    EXPECT_TRUE(stream.preassembled());
    EXPECT_TRUE(stream.writable());
}

TEST_F(ActisenseStreamTest, CanStreamForDispatchesRawAndAllBySuffixOnly) {
    EXPECT_NE(dynamic_cast<ActisenseStream *>(CanStreamFor("live.all").get()), nullptr);
    EXPECT_NE(dynamic_cast<ActisenseStream *>(CanStreamFor("dir/dirona.raw").get()), nullptr);
    EXPECT_EQ(dynamic_cast<ActisenseStream *>(CanStreamFor("can0").get()), nullptr);
    EXPECT_EQ(dynamic_cast<ActisenseStream *>(CanStreamFor("x.rawdata").get()), nullptr);
    EXPECT_EQ(dynamic_cast<ActisenseStream *>(CanStreamFor("ra").get()), nullptr);
    EXPECT_NE(SupportedStreams().find("raw"), std::string::npos);
}
