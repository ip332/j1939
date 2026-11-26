//
// Created by igor on 2/17/21.
//

#include "dbc_parser.h"
#include "j1939_parser.h"

#include <gtest/gtest.h>

// Test DBC fragment
const std::map<uint32_t, PGN> TEST_DBC{
        {1234, // PGN value
         {"ShortSignals", 8, // PGN name and the DLC
          {
                  {"Signal1", 0, 1, false, true, 1, 0, {}},
                  {"Signal2", 1, 8, false, true, 1, 0, {}},
                  {"Signal3", 9, 16, false, true, 1, 0, {}},
                  {"Signal4", 31, 32, false, true, 1, 0, {}},
          }
         },
        },
        {5678, // PGN value
         {"LongSignals", 64, // This is an example of a super long PGN with 64 bytes local_frame.
          {
                  {"LongSignal1", 0, 64, false, true, 1, 0, {}},
                  {"LongSignal2", 64, 64, false, true, 1, 0, {}},
                  {"LongSignal3", 128, 64, false, true, 1, 0, {}},
                  {"LongSignal4", 192, 64, false, true, 1, 0, {}},
                  {"LongSignal5", 256, 64, false, true, 1, 0, {}},
                  {"LongSignal6", 320, 64, false, true, 1, 0, {}},
                  {"LongSignal7", 384, 64, false, true, 1, 0, {}},
                  {"LongSignal8", 448, 64, false, true, 1, 0, {}},
          }
         },
        },
        {1112, // PGN value
                {"SignedSignals", 16, // PGN name and the DLC
                        {
                                {"Signed_8", 0, 8, true, true, 1, 0, {}},
                                {"Signal_16", 9, 16, true, true, 1, 0, {}},
                                {"Signal_32", 24, 32, true, true, 1, 0, {}},
                                {"Signal_64", 64, 64, true, true, 1, 0, {}},
                        }
                },
        },
};

class J1939ParserTest : public testing::Test {
protected:
    void SetUp() override {
        frame_.reset();
        parser_ = std::make_unique<J1939Parser>(frame_, TEST_DBC);
    }

    void TearDown() override {
    }

    void reset() {
        frame_.reset();
    }

    void setValue(uint64_t val, size_t index, size_t length) {
        frame_.write_bits(val, index, length, true);
    }

    uint64_t getValue(size_t index, size_t length) {
        return frame_.read_bits(index, length, true);
    }

    void testRawSignalsReading(uint16_t can_id) {
        auto it = TEST_DBC.find(can_id);

        frame_.reset();
        frame_.setFrom(can_id);
        frame_.dlc_ = it->second.dlc_;

        for (const auto & spn : TEST_DBC.at(can_id).signals_){
            // Reset the buffer
            memset(frame_.buffer_, 0xFF, sizeof(frame_.buffer_));
            // Set the signal value to 0.
            setValue(0, spn.start_bit_, spn.length_);
            // Now check that reading value by name gives the same result (0)
            auto result = parser_->signalRawValue(spn.name_);
            EXPECT_NE(result.get(), nullptr);
            if (result) {
                EXPECT_EQ(*result, 0);
            }
            // Set the value to 1
            uint64_t expected = make_mask(spn.length_);
            setValue(expected, spn.start_bit_, spn.length_);
            result = parser_->signalRawValue(spn.name_, true);
            EXPECT_EQ(*result, expected);
        }
    }

    void testRawSignalsWriting(uint16_t can_id) {
        auto it = TEST_DBC.find(can_id);

        frame_.reset();
        frame_.setFrom(can_id);
        frame_.dlc_ = it->second.dlc_;

        for (const auto & spn : TEST_DBC.at(can_id).signals_){
            // Reset the buffer
            memset(frame_.buffer_, 0xFF, sizeof(frame_.buffer_));
            // Set the signal value to 0.
            parser_->setSignalValue(spn.name_, 0);
            // Now check that the correct bits were set in the frame.
            uint64_t result = getValue(spn.start_bit_, spn.length_);
            EXPECT_EQ(result, 0);
            // Set the value to 1
            parser_->setSignalValue(spn.name_, double(0xFFFFFFFFFFFFFFFF));
            uint64_t expected = make_mask(spn.length_);
            result = getValue(spn.start_bit_, spn.length_);
            EXPECT_EQ(result, expected);
        }
    }
    std::unique_ptr<std::ofstream> file() {
        std::unique_ptr<std::ofstream> file(new std::ofstream());
        file->open("pgn_.cpp");
        return file;
    }

    bool setDbc(const char *dbc_file) {
        std::string path(DBC_FOLDER);
        dbc_parser_.setVerbose(false);
        if (!dbc_parser_.parseFile(path + dbc_file)) {
            return false;
        }
        dbc_parser_.generateRunTimeFormat();
        parser_ = std::make_unique<J1939Parser>(frame_, dbc_parser_.dbc());
        return true;
    }

    J1939_frame frame_;
    DbcParser dbc_parser_;
    std::unique_ptr<J1939Parser> parser_;
};

// Tests that signalRawValue returns the corresponding bits from the underlying frame.
TEST_F(J1939ParserTest, SimpleSignalsReadTest) {
    testRawSignalsReading(1234);
}

// Tests that signalRawValue works with large messages and long fields.
TEST_F(J1939ParserTest, LongSignalsReadTest) {
    testRawSignalsReading(5678);
}

// Tests that signalRawValue returns the corresponding bits from the underlying frame.
TEST_F(J1939ParserTest, SimpleSignalsWriteTest) {
    testRawSignalsReading(1234);
}

// Tests that signalRawValue works with large messages and long fields.
TEST_F(J1939ParserTest, LongSignalsWriteTest) {
    testRawSignalsReading(5678);
}

TEST_F(J1939ParserTest, SignedSignalsTest) {
    auto it = TEST_DBC.find(1112);

    frame_.reset();
    frame_.setFrom(1112);
    frame_.dlc_ = it->second.dlc_;

    for (const auto & spn : TEST_DBC.at(1112).signals_){
        double test_vector[] = {-100, -20, -10, -1, 0, 1, 10, 20, 100};
        for (auto x : test_vector) {
            // Reset the buffer
            memset(frame_.buffer_, 0xFF, sizeof(frame_.buffer_));
            parser_->setSignalValue(spn.name_, x);
            // Now check that reading value by name gives the same result (0)
            auto result = parser_->signalValue(spn.name_, true);
            EXPECT_NE(result.get(), nullptr);
            if (result) {
                EXPECT_EQ(*result, x);
            }
        }
    }
}


TEST_F(J1939ParserTest, SignalLookupsReturnNullWhenPgnIsUnknown) {
    frame_.reset();
    frame_.setFrom(9999); // not a TEST_DBC key
    EXPECT_EQ(parser_->signal("Signal1"), nullptr);
    EXPECT_EQ(parser_->signalRawValue(SPN{"Signal1", 0, 1, false, true, 1, 0, 0, 0, {}}).get(), nullptr);
    EXPECT_EQ(parser_->signalRawValue("Signal1").get(), nullptr);
    EXPECT_EQ(parser_->signalRawValue32("Signal1").get(), nullptr);
    EXPECT_EQ(parser_->signalValue("Signal1").get(), nullptr);
    EXPECT_FALSE(parser_->setSignalValue("Signal1", 1.0));
    EXPECT_FALSE(parser_->setSignalValue("Signal1", std::string("On")));
}

TEST_F(J1939ParserTest, SignalRawValue32HidesUnsetValueUnlessRequested) {
    frame_.reset();
    frame_.setFrom(1234);
    frame_.dlc_ = TEST_DBC.at(1234).dlc_;
    memset(frame_.buffer_, 0xFF, sizeof(frame_.buffer_)); // Signal2 now reads as "unset"
    EXPECT_EQ(parser_->signalRawValue32("Signal2", false).get(), nullptr);
    EXPECT_NE(parser_->signalRawValue32("Signal2", true).get(), nullptr);

    parser_->setSignalValue("Signal2", 5); // now a real (non-masked) value
    auto value = parser_->signalRawValue32("Signal2", false);
    ASSERT_NE(value.get(), nullptr);
    EXPECT_EQ(*value, 5u);
}

TEST_F(J1939ParserTest, SetSignalValueRejectsUnknownSignalName) {
    frame_.reset();
    frame_.setFrom(1234);
    frame_.dlc_ = TEST_DBC.at(1234).dlc_;
    EXPECT_FALSE(parser_->setSignalValue("NoSuchSignal", 1.0));
    EXPECT_FALSE(parser_->setSignalValue("NoSuchSignal", std::string("On")));
}

TEST_F(J1939ParserTest, SetSignalValueNegativeWithScaling) {
    const std::map<uint32_t, PGN> dbc{
        {999, {"Scaled", 1, {{"Temp", 0, 8, true, true, 0.5, 10, 0, 0, {}}}}},
    };
    J1939_frame frame;
    frame.reset();
    frame.setFrom(999);
    frame.dlc_ = 1;
    J1939Parser parser(frame, dbc);
    // scalar_ != 1, so the scaled path runs; a negative value hits its own branch
    // (distinct rounding direction from the non-negative scaled branch).
    EXPECT_TRUE(parser.setSignalValue("Temp", -5.0));
    auto value = parser.signalValue("Temp", true);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value, -5.0);
}

TEST_F(J1939ParserTest, ParserRuntimeMessageFindTest) {
    ASSERT_TRUE(setDbc("test_database.dbc"));

    frame_.setFrom(160);
    EXPECT_EQ(parser_->getPgn(), nullptr);

    frame_.setFrom(16);
    EXPECT_NE(parser_->getPgn(), nullptr);
    frame_.setFrom(17);
    EXPECT_NE(parser_->getPgn(), nullptr);
    frame_.setFrom(18);
    EXPECT_NE(parser_->getPgn(), nullptr);
}

TEST_F(J1939ParserTest, GetSetSingleSignalRuntimeTest) {
    ASSERT_TRUE(setDbc("test_database.dbc"));

    frame_.setFrom(16);
    frame_.dlc_ = parser_->getPgn()->dlc_;

    const auto * signal_ptr = parser_->signal("signal1");
    EXPECT_TRUE(signal_ptr->little_endian_);
    auto raw_ptr = parser_->signalRawValue32("signal1", true);
    EXPECT_TRUE(raw_ptr != nullptr);
    EXPECT_EQ(*raw_ptr, make_mask(signal_ptr->length_));
    EXPECT_TRUE(parser_->setSignalValue("signal1", -6770764));
    auto ptr = parser_->signalValue("signal1");
    EXPECT_TRUE(ptr != nullptr);
    EXPECT_EQ(*ptr, -6770764);

    EXPECT_TRUE(parser_->setSignalValue("signal_2", 26));
    ptr = parser_->signalValue("signal_2");
    EXPECT_EQ(*ptr, 26);

}

TEST_F(J1939ParserTest, GetSetMultipleSignalsRuntimeTest) {
    ASSERT_TRUE(setDbc("test_database.dbc"));

    frame_.setFrom(17);
    frame_.dlc_ = parser_->getPgn()->dlc_;

    EXPECT_TRUE(parser_->setSignalValue("signal_1", 0x44));
    EXPECT_TRUE(parser_->setSignalValue("signal_2", 516349));
    EXPECT_TRUE(parser_->setSignalValue("signal_3", 208.0 * 0.25));

    auto ptr = parser_->signalValue("signal_1");
    EXPECT_EQ(*ptr, 0x44);
    ptr = parser_->signalValue("signal_2");
    EXPECT_EQ(*ptr, 516349);
    ptr = parser_->signalValue("signal_3");
    EXPECT_EQ(*ptr, 208.0 * 0.25);
}

TEST_F(J1939ParserTest, GetValuesLittleEndianTest) {
    ASSERT_TRUE(setDbc("test_database.dbc"));

    // first message
    uint8_t data[] = {0xb4, 0xaf, 0x98, 0x1a};
    frame_.setFrom(16, data, 4);

    auto ptr = parser_->signalValue("signal_2");
    EXPECT_EQ(*ptr, 26);
    ptr = parser_->signalValue("signal1");
    EXPECT_EQ(*ptr, -6770764);
}

TEST_F(J1939ParserTest, GetValuesBigEndianTest) {
    ASSERT_TRUE(setDbc("big_endian_database.dbc"));

    uint8_t data[7] = {0x3f, 0xff, 0xd4, 0x5a, 0x6a, 0x76, 0x69};
    frame_.setFrom(69, data, 7);

    auto ptr = parser_->signalValue("s1");
    EXPECT_EQ(*ptr, 1641);
    ptr = parser_->signalValue("s2");
    EXPECT_EQ(*ptr, -345);
    ptr = parser_->signalValue("s3");
    EXPECT_EQ(*ptr, 1365879257);
    ptr = parser_->signalValue("s4");
    EXPECT_EQ(*ptr, 0);
}

TEST_F(J1939ParserTest, PrintValuesBigEndianTest) {
    ASSERT_TRUE(setDbc("big_endian_database.dbc"));

    uint8_t data[7] = {0x3f, 0xff, 0xd4, 0x5a, 0x6a, 0x76, 0x69};
    frame_.setFrom(69, data, 7);

    std::stringstream stream;
    parser_->toString(stream);
    std::string expected("m1 CAN ID:0045(69) Data(7): 3FFFD45A6A7669 { s4: 0 s3: 1365879257 s2: -345 s1: 1641 }");
    EXPECT_EQ(stream.str(), expected);
}

TEST_F(J1939ParserTest, SetValuesBigEndianTest) {
    ASSERT_TRUE(setDbc("big_endian_database.dbc"));

    frame_.reset();
    frame_.setFrom(69);
    const auto *pgn = parser_->getPgn();
    ASSERT_NE(pgn, nullptr);
    frame_.dlc_ = pgn->dlc_;

    parser_->setSignalValue("s1",1641);
    parser_->setSignalValue("s2",-345);
    parser_->setSignalValue("s3",1365879257);
    parser_->setSignalValue("s4",0);

    uint8_t expected[7] = {0x3f, 0xff, 0xd4, 0x5a, 0x6a, 0x76, 0x69};
    EXPECT_EQ(memcmp(expected, frame_.buffer_, frame_.dlc_), 0);
}

TEST_F(J1939ParserTest, GetValuesRadarBigEndianTest) {
    ASSERT_TRUE(setDbc("big_endian_database.dbc"));

    uint8_t data[8] = {64, 12, 0, 0, 0, 200, 0, 0};
    frame_.setFrom(513, data, 8);

    auto ptr = parser_->signalValue("RadarState_MaxDistanceCfg");
    EXPECT_EQ(*ptr, 96);
    ptr = parser_->signalValue("RadarState_MotionRxState", true);
    EXPECT_EQ(*ptr, 3);
    ptr = parser_->signalValue("RadarState_OutputTypeCfg", true);
    EXPECT_EQ(*ptr, 2);
    ptr = parser_->signalValue("RadarState_NVMReadStatus", true);
    EXPECT_EQ(*ptr, 1);
}

TEST_F(J1939ParserTest, SetValuesRadarBigEndianTest) {
    ASSERT_TRUE(setDbc("big_endian_database.dbc"));

    frame_.reset(0);
    frame_.setFrom(768);
    const auto *pgn = parser_->getPgn();
    ASSERT_NE(pgn, nullptr);
    frame_.dlc_ = pgn->dlc_;

    parser_->setSignalValue("RadarDevice_SpeedDirection",2);
    parser_->setSignalValue("RadarDevice_Speed",0);

    uint8_t expected[2] = {0x80, 0};
    EXPECT_EQ(memcmp(expected, frame_.buffer_, frame_.dlc_), 0);
}

TEST(J1939ParserLookupTest, SpecificSourceAddressBeatsGenericRow) {
    const std::map<uint32_t, PGN> dbc{
        {j1939PgnKey(0xFE00), {"Generic", 8, {{"G", 0, 8, false, true, 1, 0, 0, 255, {}}}}},
        {j1939SaKey(0xFE00, 0x11), {"Specific", 8, {{"S", 0, 8, false, true, 1, 0, 0, 255, {}}}}},
    };
    J1939_frame frame;
    frame.reset();
    // Priority 6, SA 0x11: the source-address row wins over the generic PGN row.
    ASSERT_TRUE(frame.setFrom(0x98FE0011u));
    J1939Parser parser(frame, dbc);
    ASSERT_NE(parser.getPgn(), nullptr);
    EXPECT_EQ(parser.getPgn()->name_, "Specific");

    // Same SA at priority 0 still hits that row.
    ASSERT_TRUE(frame.setFrom(0x80FE0011u));
    EXPECT_EQ(parser.getPgn()->name_, "Specific");

    // Priority 3, SA 0x22 has no row of its own, so the generic PGN row is used.
    ASSERT_TRUE(frame.setFrom(0x8CFE0022u));
    ASSERT_NE(parser.getPgn(), nullptr);
    EXPECT_EQ(parser.getPgn()->name_, "Generic");
}

TEST(J1939ParserLookupTest, StandardIdIsNotLookedUpAsPgn) {
    const std::map<uint32_t, PGN> dbc{
        {0x100u, {"Std", 1, {{"A", 0, 8, false, true, 1, 0, 0, 255, {}}}}},
        {j1939PgnKey(0), {"TSC1", 8, {{"B", 0, 8, false, true, 1, 0, 0, 255, {}}}}},
    };
    J1939_frame frame;
    frame.reset();
    // 0x100 slices to PGN 0. Lookup must return the 11-bit message.
    ASSERT_TRUE(frame.setFrom(0x100u));
    EXPECT_EQ(frame.pgn_, 0u);
    J1939Parser parser(frame, dbc);
    ASSERT_NE(parser.getPgn(), nullptr);
    EXPECT_EQ(parser.getPgn()->name_, "Std");

    // A frame whose sliced PGN equals the 11-bit id is not that message.
    ASSERT_TRUE(frame.setFrom(0x00010000u));
    EXPECT_EQ(frame.pgn_, 0x100u);
    EXPECT_FALSE(frame.extended_);
    EXPECT_EQ(parser.getPgn(), nullptr);

    // Extended PGN 0, SA 0x2A, finds TSC1 through the generic key.
    ASSERT_TRUE(frame.setFrom(0x8000002Au));
    ASSERT_NE(parser.getPgn(), nullptr);
    EXPECT_EQ(parser.getPgn()->name_, "TSC1");
}

TEST_F(J1939ParserTest, ScanAllSignalsSkipsUnsetSignalsByDefault) {
    frame_.reset();
    frame_.setFrom(1234);
    frame_.dlc_ = TEST_DBC.at(1234).dlc_;
    memset(frame_.buffer_, 0xFF, sizeof(frame_.buffer_)); // every signal "unset"
    parser_->setSignalValue("Signal2", 5); // one real value amid the rest

    std::vector<std::string> seen;
    parser_->scanAllSignals([&](const SPN &spn) { seen.push_back(spn.name_); }, false);
    EXPECT_EQ(seen, std::vector<std::string>{"Signal2"});
}

TEST_F(J1939ParserTest, ScanAllSignalsIncludesUnsetSignalsWhenRequested) {
    frame_.reset();
    frame_.setFrom(1234);
    frame_.dlc_ = TEST_DBC.at(1234).dlc_;
    memset(frame_.buffer_, 0xFF, sizeof(frame_.buffer_));

    std::vector<std::string> seen;
    parser_->scanAllSignals([&](const SPN &spn) { seen.push_back(spn.name_); }, true);
    EXPECT_EQ(seen.size(), TEST_DBC.at(1234).signals_.size());
}

TEST_F(J1939ParserTest, ScanAllSignalsSkipsSignalsThatDontFitInDlc) {
    frame_.reset();
    frame_.setFrom(5678); // LongSignals: 8 signals of 64 bits (8 bytes) each
    frame_.dlc_ = 8;      // only enough room for the first signal
    std::vector<std::string> seen;
    parser_->scanAllSignals([&](const SPN &spn) { seen.push_back(spn.name_); }, true);
    EXPECT_EQ(seen, std::vector<std::string>{"LongSignal1"});
}

TEST(J1939ParserEnumTest, SetSignalValueByEnumName) {
    const std::map<uint32_t, PGN> dbc{
        {999, {"WithEnum", 1,
               {{"Mode", 0, 2, false, true, 1, 0, 0, 3, {{1, "On"}, {2, "Off"}}}}}},
    };
    J1939_frame frame;
    frame.reset();
    frame.setFrom(999);
    frame.dlc_ = 1;
    J1939Parser parser(frame, dbc);

    EXPECT_TRUE(parser.setSignalValue("Mode", std::string("On")));
    auto value = parser.signalRawValue("Mode");
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, 1u);

    EXPECT_FALSE(parser.setSignalValue("Mode", std::string("NotARealEnumName")));
}

TEST(J1939ParserPgnForTest, FindsPgnsContainingASignal) {
    auto pgns = J1939Parser::pgnFor("Signal1", TEST_DBC);
    EXPECT_EQ(pgns, std::set<uint32_t>{1234});
}

TEST(J1939ParserPgnForTest, ReturnsEmptySetForUnknownSignal) {
    auto pgns = J1939Parser::pgnFor("NoSuchSignal", TEST_DBC);
    EXPECT_TRUE(pgns.empty());
}

TEST(J1939MultiplexTest, ReadsOnlyActiveAlternativesButWritesByName) {
    Message message("BO_ 100 Mux: 3 X\n");
    ASSERT_TRUE(message.addSignal(" SG_ Switch M : 0|8@1+ (1,0) [0|255] \"\" X\n"));
    ASSERT_TRUE(message.addSignal(" SG_ A m0 : 8|8@1+ (1,0) [0|255] \"\" X\n"));
    ASSERT_TRUE(message.addSignal(" SG_ B m1 : 8|8@1+ (1,0) [0|255] \"\" X\n"));
    ASSERT_TRUE(message.addSignal(" SG_ Always : 16|8@1+ (1,0) [0|255] \"\" X\n"));
    const std::map<uint32_t, PGN> dbc{{100, message.toPgn()}};
    J1939_frame frame;
    const uint8_t payload[] = {0, 42, 9};
    ASSERT_TRUE(frame.setFrom(100, payload, sizeof(payload)));
    J1939Parser parser(frame, dbc);
    const SPN &a = *dbc.at(100).signalByName("A");
    const SPN &b = *dbc.at(100).signalByName("B");
    EXPECT_TRUE(dbc.at(100).signals_[0].multiplexor_);
    EXPECT_EQ(b.mux_value_, 1);
    EXPECT_NE(message.toPgnString().find("true, false, 0"), std::string::npos);
    EXPECT_NE(message.toPgnString().find("false, true, 1"), std::string::npos);
    for (unsigned sw = 0; sw < 2; ++sw) {
        frame.buffer_[0] = sw;
        const char *active = sw ? "B" : "A";
        const char *inactive = sw ? "A" : "B";
        EXPECT_NE(parser.signal(active), nullptr);
        EXPECT_EQ(parser.signal(inactive), nullptr);
        ASSERT_TRUE(parser.signalRawValue(active, true));
        EXPECT_EQ(*parser.signalRawValue(active, true), 42u);
        EXPECT_FALSE(parser.signalRawValue(inactive, true));
        EXPECT_FALSE(parser.signalValue(inactive, true));
        EXPECT_FALSE(parser.signalRawValue32(inactive, true));
        EXPECT_FALSE(parser.signalRawValue(sw ? a : b));
        EXPECT_FALSE(parser.signalValue(sw ? a : b));
        ASSERT_TRUE(parser.signalRawValue("Always"));
        EXPECT_EQ(*parser.signalRawValue("Always"), 9u);
        for (bool show_unset : {false, true}) {
            std::vector<std::string> names;
            parser.scanAllSignals([&](const SPN &s) { names.push_back(s.name_); }, show_unset);
            EXPECT_EQ(names, (std::vector<std::string>{"Switch", active, "Always"}));
            std::ostringstream text;
            parser.toString(text, show_unset);
            EXPECT_EQ(text.str().find(std::string(inactive) + ":"), std::string::npos);
        }
        ASSERT_TRUE(parser.setSignalValue(inactive, 17));
        EXPECT_EQ(frame.buffer_[0], sw);
        EXPECT_EQ(frame.buffer_[1], 17);
        frame.buffer_[1] = 42;
    }
    frame.dlc_ = 0; // A truncated switch cannot make an alternative active.
    EXPECT_FALSE(parser.signalRawValue(a));
    EXPECT_FALSE(parser.signalRawValue(b));
    auto no_switch = dbc;
    no_switch.at(100).signals_[0].multiplexed_ = true; // Nested switches are not pure switches.
    frame.dlc_ = 3;
    J1939Parser nested(frame, no_switch);
    EXPECT_EQ(nested.signal("A"), nullptr);
}

TEST(J1939MultiplexTest, UsesMotorolaSwitchAndFindsActiveDuplicateName) {
    const SPN sw{"Switch", 7, 12, false, false, 1, 0, 0, 4095, {}, true, false, 0};
    const SPN a{"Same", 16, 8, false, true, 1, 0, 0, 255, {{5, "Five"}}, false, true, 0};
    const SPN b{"Same", 24, 8, false, true, 1, 0, 0, 255, {}, false, true, 1};
    std::map<uint32_t, PGN> dbc{{100, {"Mux", 4, {sw, a, b}}}};
    J1939_frame frame;
    uint8_t payload[] = {0, 0x1f, 42, 17}; // Motorola switch = 1, low nibble is unrelated.
    ASSERT_TRUE(frame.setFrom(100, payload, sizeof(payload)));
    J1939Parser parser(frame, dbc);
    EXPECT_EQ(parser.signal("Same"), &dbc.at(100).signals_[2]);
    ASSERT_TRUE(parser.signalRawValue("Same"));
    EXPECT_EQ(*parser.signalRawValue("Same"), 17u);
    // Enum writes use metadata even when the named signal is inactive.
    EXPECT_TRUE(parser.setSignalValue("Same", "Five"));
    EXPECT_EQ(frame.buffer_[2], 5);
    EXPECT_EQ(frame.buffer_[0], 0);
    EXPECT_EQ(frame.buffer_[1], 0x1f);
    frame.dlc_ = 1;
    EXPECT_EQ(parser.signal("Same"), nullptr);
    frame.dlc_ = 4;
    for (unsigned length : {0u, 65u}) {
        dbc.at(100).signals_[0].length_ = length;
        EXPECT_EQ(parser.signal("Same"), nullptr);
    }
}

TEST(J1939ParserLookupTest, ToStringOnExtendedFrameUsesExtendedFormat) {
    const std::map<uint32_t, PGN> dbc{
        {j1939PgnKey(0xFE00), {"StatusMsg", 8, {}}},
    };
    J1939_frame frame;
    frame.reset();
    // Priority 6 and SA 0x22 are not stored in the generic key.
    ASSERT_TRUE(frame.setFrom(0x98FE0022u));
    J1939Parser parser(frame, dbc);

    std::stringstream stream;
    EXPECT_TRUE(parser.toString(stream));
    EXPECT_NE(stream.str().find("StatusMsg"), std::string::npos);
    EXPECT_NE(stream.str().find("SA:"), std::string::npos);
    EXPECT_NE(stream.str().find("DST:"), std::string::npos);
}

TEST_F(J1939ParserTest, ToStringOnUndefinedExtendedFrameUsesExtendedFormat) {
    frame_.reset();
    frame_.setFrom(0x00FFFFFF); // not present in TEST_DBC
    frame_.extended_ = true;

    std::stringstream stream;
    parser_->toString(stream);
    EXPECT_NE(stream.str().find("UNDEF_"), std::string::npos);
}

TEST_F(J1939ParserTest, SetValuesConfigBigEndianTest) {
    ASSERT_TRUE(setDbc("big_endian_database.dbc"));


    frame_.reset(0);
    frame_.setFrom(512);
    auto id = dbc_parser_.getId("RadarConfiguration_0");
    const auto *pgn = parser_->getPgn();
    ASSERT_NE(pgn, nullptr);
    frame_.dlc_ = pgn->dlc_;

    parser_->setSignalValue("RadarCfg_MaxDistance",50);
    EXPECT_EQ(frame_.buffer_[1], 6);
    EXPECT_EQ(frame_.buffer_[2], 64);
}

TEST(J1939ParserLookupTest, FindsEveryMessageContainingTheName) {
    const SPN shared{"Shared", 0, 8, false, true, 1, 0, 0, 255, {}};
    const std::map<uint32_t, PGN> dbc{{100, {"First", 1, {shared}}},
                                       {101, {"Second", 1, {shared}}},
                                       {102, {"Empty", 1, {}}}};
    EXPECT_EQ(J1939Parser::pgnFor("Shared", dbc), (std::set<uint32_t>{100, 101}));
}
