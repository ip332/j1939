//
// Tests for J1939Name, plus PDU1 / data-page PGN extraction for the network-management PGNs
// address claiming depends on.
//

#include "j1939_name.h"

#include <gtest/gtest.h>

TEST(J1939NameTest, DecodesRealNmea2000Claim) {
    // From canboat live.all: "60928,17,255,8,30,3d,b8,2f,00,87,78,c0".
    const uint8_t bytes[8] = {0x30, 0x3d, 0xb8, 0x2f, 0x00, 0x87, 0x78, 0xc0};
    uint64_t raw = J1939Name::rawFromBytes(bytes);
    EXPECT_EQ(raw, 0xc07887002fb83d30ULL);
    J1939Name n = J1939Name::fromRaw(raw);
    EXPECT_EQ(n.identity_number_, 0x183d30u);
    EXPECT_EQ(n.manufacturer_code_, 381u); // B&G
    EXPECT_EQ(n.ecu_instance_, 0);
    EXPECT_EQ(n.function_instance_, 0);
    EXPECT_EQ(n.function_, 135);
    EXPECT_FALSE(n.reserved_);
    EXPECT_EQ(n.vehicle_system_, 60);
    EXPECT_EQ(n.vehicle_system_instance_, 0);
    EXPECT_EQ(n.industry_group_, 4); // marine
    EXPECT_TRUE(n.arbitrary_address_capable_);
    EXPECT_EQ(n.raw(), raw);
}

TEST(J1939NameTest, EachFieldOccupiesExactlyItsBits) {
    struct Field {
        unsigned start, length;
    };
    const Field fields[] = {{0, 21}, {21, 11}, {32, 3}, {35, 5}, {40, 8},
                            {48, 1}, {49, 7},  {56, 4}, {60, 3}, {63, 1}};
    unsigned next = 0;
    for (const auto &f : fields) {
        EXPECT_EQ(f.start, next) << "fields must tile the 64 bits";
        next = f.start + f.length;
        uint64_t mask = ((f.length == 64 ? 0 : (1ULL << f.length)) - 1) << f.start;
        // All-ones in just this field decodes and re-encodes to just this field.
        EXPECT_EQ(J1939Name::fromRaw(mask).raw(), mask) << "field at bit " << f.start;
    }
    EXPECT_EQ(next, 64u);
    EXPECT_EQ(J1939Name::fromRaw(~0ULL).raw(), ~0ULL);
    EXPECT_EQ(J1939Name::fromRaw(0).raw(), 0u);
}

TEST(J1939NameTest, RawMasksOversizedFields) {
    J1939Name n = J1939Name::fromRaw(0);
    n.identity_number_ = 0xFFFFFFFF;
    n.ecu_instance_ = 0xFF;
    EXPECT_EQ(n.raw(), 0x1FFFFFULL | (7ULL << 32));
}

TEST(J1939NameTest, ByteRoundTripIsLittleEndian) {
    uint8_t bytes[8];
    J1939Name::rawToBytes(0x0102030405060708ULL, bytes);
    const uint8_t expected[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    EXPECT_EQ(memcmp(bytes, expected, 8), 0);
    EXPECT_EQ(J1939Name::rawFromBytes(bytes), 0x0102030405060708ULL);
}

TEST(J1939NameTest, ToStringShowsHexAndFields) {
    EXPECT_EQ(J1939Name::hex(0xc07887002fb83d30ULL), "0xc07887002fb83d30");
    std::string s = J1939Name::fromRaw(0xc07887002fb83d30ULL).toString();
    EXPECT_EQ(s.find("0xc07887002fb83d30"), 0u);
    EXPECT_NE(s.find("mfr=381"), std::string::npos);
    EXPECT_NE(s.find("aac=1"), std::string::npos);
}

// --- PGN extraction for the network-management PGNs (PDU1: destination in PS) ---

TEST(J1939FrameSetFromTest, AddressClaimToGlobalIsPdu1WithDestinationInPs) {
    J1939_frame frame;
    ASSERT_TRUE(frame.setFrom(CAN_EFF_FLAG | 0x18EEFF11));
    EXPECT_EQ(frame.pgn_, kPgnAddressClaimed);
    EXPECT_EQ(frame.dst_, kGlobalAddress);
    EXPECT_EQ(frame.src_, 0x11);
    EXPECT_EQ(frame.pri_, 6);
    EXPECT_EQ(frame.canID(), CAN_EFF_FLAG | 0x18EEFF11u);
}

TEST(J1939FrameSetFromTest, CannotClaimComesFromTheNullAddress) {
    J1939_frame frame;
    ASSERT_TRUE(frame.setFrom(CAN_EFF_FLAG | 0x18EEFFFE));
    EXPECT_EQ(frame.pgn_, kPgnAddressClaimed);
    EXPECT_EQ(frame.src_, kNullAddress);
}

TEST(J1939FrameSetFromTest, DestinationSpecificRequestKeepsDestination) {
    J1939_frame frame;
    // Request (0xEA00) from 0x54 to 0x21.
    ASSERT_TRUE(frame.setFrom(CAN_EFF_FLAG | 0x18EA2154));
    EXPECT_EQ(frame.pgn_, kPgnRequest);
    EXPECT_EQ(frame.dst_, 0x21);
    EXPECT_EQ(frame.src_, 0x54);
    EXPECT_EQ(frame.canID(), CAN_EFF_FLAG | 0x18EA2154u);
}

TEST(J1939FrameSetFromTest, Pdu1OnDataPageOneKeepsDataPageAndDestination) {
    J1939_frame frame;
    // 126208 = 0x1ED00 (NMEA Group Function), DP=1, PF=0xED, to 0x40 from 0x5C.
    ASSERT_TRUE(frame.setFrom(CAN_EFF_FLAG | 0x0DED405C));
    EXPECT_EQ(frame.pgn_, 0x1ED00u);
    EXPECT_EQ(frame.dst_, 0x40);
    EXPECT_EQ(frame.src_, 0x5C);
    EXPECT_EQ(frame.canID(), CAN_EFF_FLAG | 0x0DED405Cu);
}

TEST(J1939FrameSetFromTest, Pdu2KeepsGroupExtensionAndDataPage) {
    J1939_frame frame;
    // Commanded Address 65240 = 0xFED8 (PDU2, DP=0) and heartbeat 126993 = 0x1F011 (DP=1).
    ASSERT_TRUE(frame.setFrom(CAN_EFF_FLAG | 0x18FED801));
    EXPECT_EQ(frame.pgn_, kPgnCommandedAddress);
    EXPECT_EQ(frame.dst_, kGlobalAddress);
    ASSERT_TRUE(frame.setFrom(CAN_EFF_FLAG | 0x1DF01123));
    EXPECT_EQ(frame.pgn_, 126993u);
    EXPECT_EQ(frame.dst_, kGlobalAddress);
    EXPECT_EQ(frame.src_, 0x23);
}
