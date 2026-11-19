//
// Created by igor on 9/7/21.
//

#include "j1939_frame.h"

#include <gtest/gtest.h>

class J1939CodecTest : public testing::Test {
protected:
    void SetUp() override {
        frame_.dlc_ = 10;
    }

    void TearDown() override {
    }

    void fillFrameWith(uint8_t value, uint8_t dlc = 8) {
        frame_.reset(value);
        frame_.dlc_ = dlc;
    }

    void setValue(uint64_t val, size_t index, size_t length, bool little_endian) {
        frame_.write_bits(val, index, length, little_endian);
    }

    uint64_t getValue(size_t index, size_t length, bool little_endian) {
        return frame_.read_bits(index, length, little_endian);
    }

    void printFrame() const {
        std::cout << "\n    7 6 5 4 3 2 1 0" << '\n';
        for (int i = 0; i < frame_.dlc_; i++) {
            std::cout << i << "   ";
            for (int j = 7; j >= 0; j--) {
                std::cout << ((frame_.buffer_[i] >> j) & 1) << ' ';
            }
            std::cout << "\n";
        }
    }

    J1939_frame frame_;
};

// Checks that the set bits method works
TEST_F(J1939CodecTest, SetSingleBitsTest) {
    for (bool endian : { true, false }) {
        for (int i = 0; i < frame_.dlc_; i++) {
            uint8_t byte_idx = i;
            for (int bit = 0; bit < 8; bit++) {
                fillFrameWith(0);
                setValue(1, bit + i * 8, 1, endian);
                EXPECT_EQ(frame_.buffer_[byte_idx], 1 << bit) << " little_endian: " << endian << " at byte " << i;
            }
        }
        for (int i = 0; i < 8; i++) {
            uint8_t byte_idx = i;
            for (int bit = 0; bit < frame_.dlc_; bit++) {
                fillFrameWith(0xFF);
                setValue(0, bit + i * 8, 1, endian);
                EXPECT_EQ(frame_.buffer_[byte_idx], (~(1 << bit)) & 0xFF) << " little_endian: " << endian << " at byte " << i;
            }
        }
    }
}

// Checks that the get bits method works.
TEST_F(J1939CodecTest, GetSingleBitsTest) {
    for (bool endian : { true, false }) {
        for (int i = 0; i < frame_.dlc_; i++) {
            uint8_t byte_idx = i;
            for (int bit = 0; bit < 8; bit++) {
                fillFrameWith(0);
                frame_.buffer_[byte_idx] = 1 << bit;
                EXPECT_EQ(getValue(bit + i * 8, 1, endian), 1) << " little_endian: " << endian << " at byte " << i;
            }
        }
        for (int i = 0; i < frame_.dlc_; i++) {
            uint8_t byte_idx = i;
            for (int bit = 0; bit < 8; bit++) {
                fillFrameWith(0xFF);
                frame_.buffer_[byte_idx] = ~(1 << bit) & 0xFF;
                EXPECT_EQ(getValue(bit + i * 8, 1, endian), 0) << " little_endian: " << endian << " at byte " << i;
            }
        }
    }
}

// Checks that it can set any valid number of buts (1 - 63) from 0 position.
TEST_F(J1939CodecTest, SetMultipleBitsTest) {
    for (bool endian : { true, false }) {
        for (size_t i = 0; i < 64; i++) {
            fillFrameWith(0);
            uint64_t expected = (1ull << (i + 1)) - 1;
            setValue(expected, 0, i + 1, endian);
            EXPECT_EQ(expected, getValue(0, i + 1, endian)) << " little_endian: " << endian << " length: " << i;
        }
    }
}

// Checks that it can set any valid number of buts (1 - 63) from floating position.
TEST_F(J1939CodecTest, SetFloatingMultipleBitsTest) {
    for (bool endian : {true, false }) {
        for (size_t i = 0; i < 64; i++) {
            fillFrameWith(0);
            uint64_t expected = (1ull << (i + 1)) - 1;
            setValue(expected, 63 - i, i + 1, endian);
            EXPECT_EQ(expected, getValue(63 -  i, i + 1, endian)) << " little_endian: " << endian << " length: " << (i + 1) << " from " << (63 - i);
        }
    }
}

// Checks that it can clear  any valid number of buts (1 - 63) from 0 position.
TEST_F(J1939CodecTest, ResetMultipleBitsTest) {
    for (bool endian : { true, false }) {
        for (size_t i = 0; i < 64; i++) {
            fillFrameWith(0xFF);
            uint64_t expected = (~((1ull << (i + 1)) - 1)) & make_mask(i + 1);
            setValue(expected, 0, i + 1, endian);
            uint64_t result = getValue(0, i + 1, endian);
            EXPECT_EQ(expected, result) << " little_endian: " << endian << " length: " << i;
        }
    }
}

// Checks that it can clear  any valid number of buts (1 - 63) from 0 position.
TEST_F(J1939CodecTest, ResetFloatingMultipleBitsTest) {
    for (bool endian : { true, false }) {
        for (size_t i = 0; i < 64; i++) {
            fillFrameWith(0xFF);
            uint64_t expected = (~((1ull << (i + 1)) - 1)) & make_mask(i + 1);
            setValue(expected, 63 - i, i + 1, endian);
            uint64_t result = getValue(63 - i, i + 1, endian);
            EXPECT_EQ(expected, result) << " little_endian: " << endian << " length: " << i;
        }
    }
}

// Checks that an arbitrary value can be set/get if it spawns for two bytes.
TEST_F(J1939CodecTest, Set10BitsTest) {
    for (bool endian : { true, false }) {
        uint64_t expected = 0x2AA; // 10 bits: 10 1010 1010
        for (size_t i = 0; i < 54; i++) {
            fillFrameWith(0);
            setValue(expected, i, 10, endian);
            uint64_t result = getValue(i, 10, endian);
            EXPECT_EQ(expected, result) << " little_endian: " << endian << " start bit: " << i;
        }
        expected >>= 1; // 10 bits: 01 0101 0101
        for (size_t i = 0; i < 54; i++) {
            fillFrameWith(0);
            setValue(expected, i, 10, endian);
            uint64_t result = getValue(i, 10, endian);
            EXPECT_EQ(expected, result) << " little_endian: " << endian << " start bit: " << i;
        }
    }
}

TEST(MakeMaskTest, CoversZeroFullAndRefusedLengths) {
    EXPECT_EQ(make_mask(0), 0ull);
    EXPECT_EQ(make_mask(32), 0xFFFFFFFFull);
    EXPECT_EQ(make_mask(63), 0x7FFFFFFFFFFFFFFFull);
    EXPECT_EQ(make_mask(64), ~0ull);
    EXPECT_EQ(make_mask(65), 0ull);
}

TEST_F(J1939CodecTest, RefusesLengthZeroAndLengthAbove64) {
    fillFrameWith(0x5A);
    uint8_t before[16];
    memcpy(before, frame_.buffer_, sizeof(before));
    EXPECT_EQ(getValue(0, 0, true), 0ull);
    EXPECT_EQ(getValue(3, 65, false), 0ull);
    setValue(0x11, 0, 0, true);
    setValue(0x11, 7, 200, false);
    EXPECT_EQ(memcmp(before, frame_.buffer_, sizeof(before)), 0);
}

TEST_F(J1939CodecTest, Reads64BitMotorolaPastBit31) {
    fillFrameWith(0);
    const uint64_t pattern = 0x123456789ULL;
    setValue(pattern, 7, 64, false);
    EXPECT_EQ(getValue(7, 64, false), pattern);
}

// --- SPN::toString / toInteger / toDouble ---
// Field order: name_, start_bit_, length_, signedness_, little_endian_, scalar_,
// offset_, min_, max_, enum_names_.

TEST(SpnTest, ToStringShowsPlaceholderForUnknownEnumValue) {
    SPN spn{"Mode", 0, 2, false, true, 1, 0, 0, 3, {{1, "On"}, {2, "Off"}}};
    EXPECT_EQ(spn.toString(5), " Mode: \"??\"");
}

TEST(SpnTest, ToStringShowsNameForKnownEnumValue) {
    SPN spn{"Mode", 0, 2, false, true, 1, 0, 0, 3, {{1, "On"}, {2, "Off"}}};
    EXPECT_EQ(spn.toString(1), " Mode: On");
}

TEST(SpnTest, ToStringSignedWithScaling) {
    SPN spn{"Temp", 0, 8, true, true, 0.5, 10, 0, 0, {}};
    // 0xFE (254) as an 8-bit two's complement value is -2; -2*0.5+10 = 9.
    EXPECT_EQ(spn.toString(0xFE), " Temp: 9.000000");
}

TEST(SpnTest, ToStringUnsignedWithScaling) {
    SPN spn{"Speed", 0, 8, false, true, 2, 1, 0, 0, {}};
    // 10*2+1 = 21.
    EXPECT_EQ(spn.toString(10), " Speed: 21");
}

TEST(SpnTest, ToIntegerSigned) {
    SPN spn{"Temp", 0, 8, true, true, 1, 0, 0, 0, {}};
    EXPECT_EQ(spn.toInteger(0xFE), -2);
}

TEST(SpnTest, ToIntegerUnsigned) {
    SPN spn{"Speed", 0, 8, false, true, 1, 0, 0, 0, {}};
    EXPECT_EQ(spn.toInteger(200), 200);
}

TEST(SpnTest, ToDoubleSignedWithoutScaling) {
    SPN spn{"Temp", 0, 8, true, true, 1, 0, 0, 0, {}};
    EXPECT_DOUBLE_EQ(spn.toDouble(0xFE), -2.0);
}

TEST(SpnTest, IntegerConversionRefusesLengthZeroAndLengthAbove64) {
    SPN zero{"Wide", 0, 0, true, true, 1, 0, 0, 0, {}};
    EXPECT_EQ(zero.toString(5), " Wide: 0");
    EXPECT_EQ(zero.toInteger(5), 0);
    EXPECT_DOUBLE_EQ(zero.toDouble(5), 0.0);
    SPN bytes{"Vin", 0, 200, true, true, 2, 3, 0, 0, {}};
    EXPECT_EQ(bytes.toString(5), " Vin: 0");
    EXPECT_EQ(bytes.toInteger(5), 0);
    EXPECT_DOUBLE_EQ(bytes.toDouble(5), 0.0);
}

// --- PGN::signalByName ---

TEST(PgnTest, SignalByNameReturnsNullptrWhenNotFound) {
    PGN pgn{"Test", 8, {SPN{"Speed", 0, 8, false, true, 1, 0, 0, 0, {}}}};
    EXPECT_EQ(pgn.signalByName("DoesNotExist"), nullptr);
}

TEST(PgnTest, SignalByNameFindsExistingSignal) {
    PGN pgn{"Test", 8, {SPN{"Speed", 0, 8, false, true, 1, 0, 0, 0, {}}}};
    const SPN *spn = pgn.signalByName("Speed");
    ASSERT_NE(spn, nullptr);
    EXPECT_EQ(spn->name_, "Speed");
}

// --- J1939_frame::setFrom ---

TEST(J1939FrameSetFromTest, RejectsDlcExceedingMaxDlc) {
    J1939_frame frame;
    uint8_t data[1] = {0};
    EXPECT_FALSE(frame.setFrom(0x18FEF100, data, J1939_frame::max_dlc_ + 1));
}

TEST(J1939FrameSetFromTest, RejectsExtendedDataPageBitSet) {
    J1939_frame frame;
    // Bit 0x02000000 is the Extended Data Page (EDP) bit; this project's PGN encoding
    // doesn't support it and setFrom() must reject rather than silently misdecode.
    EXPECT_FALSE(frame.setFrom(0x1A000000));
}

// Checks that an arbitrary value can be set/get if it spawns for three bytes.
TEST_F(J1939CodecTest, Set20BitsTest) {
    for (bool endian : { true, false }) {
        uint64_t expected = 0xAAAAA; // 20 bits: 1010 1010 1010 1010 1010
        for (size_t i = 0; i < 44; i++) {
            fillFrameWith(0);
            setValue(expected, i, 20, endian);
            uint64_t result = getValue(i, 20, endian);
            EXPECT_EQ(expected, result) << " little_endian: " << endian << " start bit: " << i;
        }
        expected >>= 1; // 20 bits: 0x55555
        for (size_t i = 0; i < 44; i++) {
            fillFrameWith(0);
            setValue(expected, i, 20, endian);
            uint64_t result = getValue(i, 20, endian);
            EXPECT_EQ(expected, result) << " little_endian: " << endian << " start bit: " << i;
        }
    }
}

