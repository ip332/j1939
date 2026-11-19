#include "j1939_db.h"
#include "j1939_db_parser.h"
#include "j1939_frame.h"

#include <gtest/gtest.h>
#include <string.h>

// "A\0B\0BeLong\0BeShort\0EngSpeed\0Mode\0On\0Signed\0Speed\0Switch\0Vin\0"
static const char kStrings[] =
        "A\0"
        "B\0"
        "BeLong\0"
        "BeShort\0"
        "EngSpeed\0"
        "Mode\0"
        "On\0"
        "Signed\0"
        "Speed\0"
        "Switch\0"
        "Vin\0";

enum {
    OFF_A = 0,
    OFF_B = 2,
    OFF_BELONG = 4,
    OFF_BESHORT = 11,
    OFF_ENGSPEED = 19,
    OFF_MODE = 28,
    OFF_ON = 33,
    OFF_SIGNED = 36,
    OFF_SPEED = 43,
    OFF_SWITCH = 49,
    OFF_VIN = 56
};

static const j1939_enum_t kEnums[] = {
        {1u, OFF_ON},
};

static const j1939_signal_t kSignals[] = {
        {OFF_SPEED, 10u, 0, 8, 0, 0, 0, J1939_SIG_LITTLE_ENDIAN, 1, 0, 0, 255},
        {OFF_SIGNED, 11u, 8, 8, 0, 0, 0, J1939_SIG_SIGNED | J1939_SIG_LITTLE_ENDIAN, 1, 0, -128, 127},
        {OFF_BESHORT, 20u, 7, 2, 0, 0, 0, 0, 1, 0, 0, 3},
        {OFF_BELONG, 21u, 7, 16, 0, 0, 0, 0, 1, 0, 0, 65535},
        {OFF_VIN, 30u, 0, 80, 0, 0, 0, J1939_SIG_BYTES | J1939_SIG_LITTLE_ENDIAN, 1, 0, 0, 0},
        {OFF_SWITCH, 40u, 0, 8, 0, 0, 0, J1939_SIG_MULTIPLEXOR | J1939_SIG_LITTLE_ENDIAN, 1, 0, 0, 255},
        {OFF_A, 41u, 8, 8, 0, 0, 0, J1939_SIG_MULTIPLEXED | J1939_SIG_LITTLE_ENDIAN, 1, 0, 0, 255},
        {OFF_B, 42u, 8, 8, 0, 0, 1, J1939_SIG_MULTIPLEXED | J1939_SIG_LITTLE_ENDIAN, 1, 0, 0, 255},
        {OFF_ENGSPEED, 190u, 24, 16, 0, 0, 0, J1939_SIG_LITTLE_ENDIAN, 0.125, 0, 0, 8031.875},
        {OFF_MODE, 512u, 0, 2, 0, 1, 0, J1939_SIG_LITTLE_ENDIAN, 1, 0, 0, 3},
};

static const j1939_message_t kMessages[] = {
        {J1939_NO_NAME, 0, 0x64u, 0, 0, 0, 8, 0, 2},
        {J1939_NO_NAME, 0, 0x200u, 0, 0, 0, 8, 2, 2},
        {J1939_NO_NAME, 0, 0x300u, 0, 0, 0, 16, 4, 1},
        {J1939_NO_NAME, 0xEF00u, 0xEF0000u, 0, 0, 1, 8, 5, 3},
        {J1939_NO_NAME, 0xF004u, 0xF00400u, 0, 0, 1, 8, 8, 1},
        {J1939_NO_NAME, 0xF004u, 0xF0040Bu, 0xFFu, 0x0Bu, 1, 8, 9, 1},
};

static const j1939_spn_ref_t kSpns[] = {
        {10u, 0, 0},  {11u, 0, 1},  {20u, 1, 0},  {21u, 1, 1},  {30u, 2, 0},
        {40u, 3, 0},  {41u, 3, 1},  {42u, 3, 2},  {190u, 4, 0}, {512u, 5, 0},
};

static const j1939_name_ref_t kNames[] = {
        {OFF_A, 3, 1},        {OFF_B, 3, 2},       {OFF_BELONG, 1, 1},
        {OFF_BESHORT, 1, 0},  {OFF_ENGSPEED, 4, 0},{OFF_MODE, 5, 0},
        {OFF_SIGNED, 0, 1},   {OFF_SPEED, 0, 0},   {OFF_SPEED, 1, 0},
        {OFF_SWITCH, 3, 0},   {OFF_VIN, 2, 0},
};

static const j1939_db_t kDb = {
        kStrings, kMessages, 6, kSignals, 10, kEnums, 1, kSpns, 10, kNames, 11,
};

static j1939_signal_t sigOf(uint16_t start, uint16_t length, uint16_t flags) {
    j1939_signal_t sig{};
    sig.start_bit = start;
    sig.length = length;
    sig.flags = flags;
    sig.scale = 1;
    sig.offset = 0;
    return sig;
}

static uint64_t mask_u64(int length) {
    if (length >= 64) {
        return ~0ull;
    }
    return (1ull << length) - 1ull;
}

TEST(J1939DbStrings, OffsetsMatchTheBlob) {
    EXPECT_STREQ(kStrings + OFF_A, "A");
    EXPECT_STREQ(kStrings + OFF_SPEED, "Speed");
    EXPECT_STREQ(kStrings + OFF_ENGSPEED, "EngSpeed");
    EXPECT_STREQ(kStrings + OFF_ON, "On");
    EXPECT_STREQ(kStrings + OFF_VIN, "Vin");
}

TEST(J1939IdUnpack, StandardFrameClearsJ1939Fields) {
    uint32_t pgn = 9;
    uint8_t sa = 9, da = 9, pri = 9;
    j1939_id_unpack(0x123, 0, &pgn, &sa, &da, &pri);
    EXPECT_EQ(pgn, 0u);
    EXPECT_EQ(sa, 0);
    EXPECT_EQ(da, 0);
    EXPECT_EQ(pri, 0);
    j1939_id_unpack(0x18F00400u, 0, NULL, NULL, NULL, NULL);
    j1939_id_unpack(0x18F00400u, 1, NULL, NULL, NULL, NULL);
    j1939_id_unpack(0x0CEF2A11u, 1, &pgn, NULL, NULL, NULL);
    EXPECT_EQ(pgn, 0xEF00u);
}

TEST(J1939IdUnpack, ExtendedPdu2AndPdu1) {
    uint32_t pgn = 0;
    uint8_t sa = 0, da = 0, pri = 0;
    j1939_id_unpack(0x18F00400u | CAN_EFF_FLAG, 1, &pgn, &sa, &da, &pri);
    EXPECT_EQ(pgn, 0xF004u);
    EXPECT_EQ(sa, 0);
    EXPECT_EQ(da, 0xFF);
    EXPECT_EQ(pri, 6);

    // Priority 3, PF 0xEF (PDU1), destination 0x2A, source 0x11.
    j1939_id_unpack(0x0CEF2A11u, 1, &pgn, &sa, &da, &pri);
    EXPECT_EQ(pgn, 0xEF00u);
    EXPECT_EQ(sa, 0x11);
    EXPECT_EQ(da, 0x2A);
    EXPECT_EQ(pri, 3);
}

TEST(J1939DbFind, RejectsNullEmptyAndEdp) {
    EXPECT_EQ(j1939_db_find(NULL, 0x64, 0), nullptr);
    j1939_db_t empty = kDb;
    empty.messages = NULL;
    empty.message_count = 0;
    EXPECT_EQ(j1939_db_find(&empty, 0x64, 0), nullptr);
    empty.messages = kMessages;
    empty.message_count = 0;
    EXPECT_EQ(j1939_db_find(&empty, 0x64, 0), nullptr);
    EXPECT_EQ(j1939_db_find(&kDb, 0x64u | 0x02000000u, 1), nullptr);
}

TEST(J1939DbFind, StandardAndExtendedKeysIgnorePriority) {
    const j1939_message_t *std_msg = j1939_db_find(&kDb, 0x864u, 0);
    ASSERT_NE(std_msg, nullptr);
    EXPECT_EQ(std_msg->can_id, 0x64u);

    EXPECT_EQ(j1939_db_find(&kDb, 0x1u, 0), nullptr);
    EXPECT_EQ(j1939_db_find(&kDb, 0x7FFu, 0), nullptr);

    const j1939_message_t *generic = j1939_db_find(&kDb, 0x18F0042Au, 1);
    ASSERT_NE(generic, nullptr);
    EXPECT_EQ(generic->can_id, 0xF00400u);
    EXPECT_EQ(generic->sa_mask, 0u);

    const j1939_message_t *specific = j1939_db_find(&kDb, 0x0CF0040Bu, 1);
    ASSERT_NE(specific, nullptr);
    EXPECT_EQ(specific->sa, 0x0B);
    EXPECT_EQ(specific, &kMessages[5]);

    // Bit 25 is ignored for a standard id, and the 11-bit key still matches.
    EXPECT_EQ(j1939_db_find(&kDb, 0x64u | 0x02000000u, 0), &kMessages[0]);
    EXPECT_EQ(j1939_db_find(&kDb, 0x0CEF2A11u, 1)->pgn, 0xEF00u);
}

TEST(J1939DbSpnAndName, Lookup) {
    const j1939_message_t *message = nullptr;
    const j1939_signal_t *eng = j1939_signal_by_spn(&kDb, 190u, &message);
    ASSERT_NE(eng, nullptr);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->pgn, 0xF004u);
    EXPECT_EQ(message->sa_mask, 0u);
    EXPECT_EQ(eng, &kSignals[8]);

    EXPECT_EQ(j1939_signal_by_spn(&kDb, 512u, NULL), &kSignals[9]);
    EXPECT_EQ(j1939_signal_by_spn(&kDb, 0, &message), nullptr);
    EXPECT_EQ(message, nullptr);
    EXPECT_EQ(j1939_signal_by_spn(&kDb, 999u, &message), nullptr);
    EXPECT_EQ(j1939_signal_by_spn(NULL, 190u, NULL), nullptr);

    J1939DbParser view(&kDb);
    const j1939_message_t *found = view.find(0x18F00400u, true);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(view.signalByName(found, "EngSpeed"), &kSignals[8]);
    EXPECT_EQ(view.signalByName(found, "Missing"), nullptr);
    EXPECT_EQ(view.signalBySpn(10u, &message)->name_off, OFF_SPEED);

    EXPECT_EQ(j1939_signal_by_name(NULL, found, "EngSpeed"), nullptr);
    EXPECT_EQ(j1939_signal_by_name(&kDb, NULL, "EngSpeed"), nullptr);
    EXPECT_EQ(j1939_signal_by_name(&kDb, found, NULL), nullptr);

    j1939_db_t no_strings = kDb;
    no_strings.strings = NULL;
    EXPECT_EQ(j1939_signal_by_name(&no_strings, found, "EngSpeed"), nullptr);

    uint16_t indices[4] = {9, 9, 9, 9};
    EXPECT_EQ(j1939_messages_by_name(&kDb, "Speed", indices, 4), 2u);
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, "Speed", indices, 1), 2u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, "Speed", NULL, 0), 2u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, "Nope", indices, 4), 0u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, NULL, indices, 4), 0u);
    EXPECT_EQ(j1939_messages_by_name(&no_strings, "Speed", indices, 4), 0u);
    EXPECT_EQ(j1939_messages_by_name(NULL, "Speed", indices, 4), 0u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, "A", indices, 4), 1u);
    EXPECT_EQ(indices[0], 3u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, "Vin", indices, 4), 1u);
    EXPECT_EQ(j1939_messages_by_name(&kDb, "Zzz", indices, 4), 0u);

    EXPECT_EQ(j1939_db_find(&kDb, 0x200u, 0)->can_id, 0x200u);
    EXPECT_EQ(j1939_db_find(&kDb, 0x300u, 0), &kMessages[2]);
    EXPECT_EQ(j1939_db_find(&kDb, 0x18FFFF00u, 1), nullptr);

    j1939_db_t no_spn = kDb;
    no_spn.spn_count = 0;
    EXPECT_EQ(j1939_signal_by_spn(&no_spn, 190u, NULL), nullptr);
    no_spn.spn_index = NULL;
    no_spn.spn_count = 10;
    EXPECT_EQ(j1939_signal_by_spn(&no_spn, 190u, NULL), nullptr);

    j1939_spn_ref_t bad_spn[] = {{190u, 9, 0}, {191u, 0, 9}};
    j1939_db_t bad = kDb;
    bad.spn_index = bad_spn;
    bad.spn_count = 2;
    EXPECT_EQ(j1939_signal_by_spn(&bad, 190u, &message), nullptr);
    EXPECT_EQ(j1939_signal_by_spn(&bad, 191u, &message), nullptr);
    bad.signals = NULL;
    bad.spn_index = kSpns;
    bad.spn_count = 10;
    EXPECT_EQ(j1939_signal_by_spn(&bad, 190u, NULL), nullptr);

    j1939_message_t wide = kMessages[0];
    wide.signal_begin = 8;
    wide.signal_count = 5;
    EXPECT_EQ(j1939_signal_by_name(&kDb, &wide, "Speed"), nullptr);

    j1939_signal_t unnamed = kSignals[0];
    unnamed.name_off = J1939_NO_NAME;
    j1939_message_t one = kMessages[0];
    one.signal_begin = 0;
    one.signal_count = 1;
    j1939_db_t unnamed_db = kDb;
    unnamed_db.signals = &unnamed;
    unnamed_db.signal_count = 1;
    EXPECT_EQ(j1939_signal_by_name(&unnamed_db, &one, "Speed"), nullptr);
}

TEST(J1939SignalCodec, MatchesFrameBitsAcrossLayouts) {
    uint8_t payload[16];
    J1939_frame frame;
    const int starts[] = {0, 1, 7, 8, 9, 12};
    const int lengths[] = {1, 3, 8, 9, 16, 31, 32};
    const int endians[] = {0, 1};
    for (int little : endians) {
        for (int start : starts) {
            for (int length : lengths) {
                if (little && start + length > 64) {
                    continue;
                }
                j1939_signal_t sig = sigOf(static_cast<uint16_t>(start), static_cast<uint16_t>(length),
                                           little ? J1939_SIG_LITTLE_ENDIAN : 0);
                uint64_t value = mask_u64(length);
                memset(payload, 0xA5, sizeof(payload));
                frame.reset(0xA5);
                ASSERT_EQ(j1939_signal_store(payload, &sig, static_cast<double>(value)), 0);
                frame.write_bits(value, static_cast<uint16_t>(start), static_cast<uint8_t>(length), little != 0);
                EXPECT_EQ(memcmp(payload, frame.buffer_, sizeof(payload)), 0)
                        << "start " << start << " len " << length << " le " << little;

                uint64_t raw = 0;
                ASSERT_EQ(j1939_signal_raw(payload, 16, &sig, &raw), 0);
                uint64_t from_frame =
                        frame.read_bits(static_cast<uint16_t>(start), static_cast<uint8_t>(length), little != 0);
                EXPECT_EQ(raw, from_frame) << "start " << start << " len " << length << " le " << little;
                EXPECT_EQ(raw, value);
            }
        }
    }

    j1939_signal_t wide = sigOf(0, 64, J1939_SIG_LITTLE_ENDIAN);
    frame.reset(0);
    frame.write_bits(~0ull, 0, 64, true);
    uint64_t raw = 0;
    memcpy(payload, frame.buffer_, 8);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &wide, &raw), 0);
    EXPECT_EQ(raw, ~0ull);

    // 64-bit Motorola. J1939_frame::read_bits shifts a 32-bit int, so a 56-bit
    // shift is undefined there. Compare the C codec with write_bits instead.
    j1939_signal_t wide_be = sigOf(7, 64, 0);
    const uint64_t pattern = 0x123456789ULL;
    frame.reset(0xA5);
    frame.write_bits(pattern, 7, 64, false);
    memset(payload, 0xA5, sizeof(payload));
    ASSERT_EQ(j1939_signal_store(payload, &wide_be, static_cast<double>(pattern)), 0);
    EXPECT_EQ(memcmp(payload, frame.buffer_, 8), 0);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &wide_be, &raw), 0);
    EXPECT_EQ(raw, pattern);
}

TEST(J1939SignalCodec, RawPhysStoreAndBytesRejectBadSignals) {
    uint8_t payload[16];
    memset(payload, 0x11, sizeof(payload));
    uint64_t raw = 7;
    j1939_signal_t sig = sigOf(0, 8, J1939_SIG_LITTLE_ENDIAN);

    EXPECT_EQ(j1939_signal_raw(NULL, 8, &sig, &raw), -1);
    EXPECT_EQ(j1939_signal_raw(payload, 8, NULL, &raw), -1);
    EXPECT_EQ(j1939_signal_raw(payload, 8, &sig, NULL), -1);
    sig.length = 0;
    EXPECT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), -1);
    sig.length = 65;
    EXPECT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), -1);
    sig = sigOf(56, 16, J1939_SIG_LITTLE_ENDIAN);
    EXPECT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), -1);
    sig = sigOf(7, 32, 0);
    EXPECT_EQ(j1939_signal_raw(payload, 2, &sig, &raw), -1);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), 0);
    sig = sigOf(1, 16, J1939_SIG_BYTES | J1939_SIG_LITTLE_ENDIAN);
    EXPECT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), -1);

    EXPECT_EQ(j1939_signal_phys(NULL, 1), 0.0);
    sig = sigOf(0, 0, 0);
    EXPECT_EQ(j1939_signal_phys(&sig, 1), 0.0);
    sig.length = 80;
    EXPECT_EQ(j1939_signal_phys(&sig, 1), 0.0);
    sig = sigOf(0, 8, J1939_SIG_LITTLE_ENDIAN);
    EXPECT_DOUBLE_EQ(j1939_signal_phys(&sig, 0xFF), 255.0);
    sig.flags |= J1939_SIG_SIGNED;
    EXPECT_DOUBLE_EQ(j1939_signal_phys(&sig, 0xFF), -1.0);
    sig.flags = J1939_SIG_LITTLE_ENDIAN;
    sig.scale = 0.125;
    sig.offset = 1;
    EXPECT_DOUBLE_EQ(j1939_signal_phys(&sig, 8), 2.0);

    sig = sigOf(0, 8, J1939_SIG_SIGNED | J1939_SIG_LITTLE_ENDIAN);
    memset(payload, 0xA5, sizeof(payload));
    ASSERT_EQ(j1939_signal_store(payload, &sig, -2.0), 0);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), 0);
    EXPECT_EQ(raw, 0xFEu);
    EXPECT_DOUBLE_EQ(j1939_signal_phys(&sig, raw), -2.0);

    sig.scale = 0.125;
    sig.offset = 0;
    sig.flags = J1939_SIG_LITTLE_ENDIAN;
    ASSERT_EQ(j1939_signal_store(payload, &sig, 1.0), 0);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), 0);
    EXPECT_EQ(raw, 8u);

    sig = sigOf(0, 16, J1939_SIG_SIGNED | J1939_SIG_LITTLE_ENDIAN);
    sig.scale = 1.0;
    sig.offset = 5.0;
    memset(payload, 0, sizeof(payload));
    ASSERT_EQ(j1939_signal_store(payload, &sig, 0.0), 0);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), 0);
    EXPECT_EQ(raw, 0xFFFCu);

    sig.scale = 0.125;
    sig.offset = 0;
    ASSERT_EQ(j1939_signal_store(payload, &sig, -1.0), 0);
    ASSERT_EQ(j1939_signal_raw(payload, 8, &sig, &raw), 0);
    EXPECT_EQ(raw, 0xFFF8u);

    sig.scale = 0;
    EXPECT_EQ(j1939_signal_store(payload, &sig, 1.0), -1);
    sig.scale = 1;
    sig.length = 0;
    EXPECT_EQ(j1939_signal_store(payload, &sig, 1.0), -1);
    sig.length = 65;
    EXPECT_EQ(j1939_signal_store(payload, &sig, 1.0), -1);
    EXPECT_EQ(j1939_signal_store(NULL, &sig, 1.0), -1);
    EXPECT_EQ(j1939_signal_store(payload, NULL, 1.0), -1);

    memset(payload, 0, sizeof(payload));
    memcpy(payload, "HELLO_VIN!!", 11);
    sig = sigOf(0, 80, J1939_SIG_BYTES | J1939_SIG_LITTLE_ENDIAN);
    char dst[16];
    ASSERT_EQ(j1939_signal_bytes(payload, 16, &sig, dst, sizeof(dst)), 0);
    EXPECT_STREQ(dst, "HELLO_VIN!");
    ASSERT_EQ(j1939_signal_bytes(payload, 16, &sig, dst, 4), 0);
    EXPECT_STREQ(dst, "HEL");
    ASSERT_EQ(j1939_signal_bytes(payload, 16, &sig, dst, 1), 0);
    EXPECT_STREQ(dst, "");
    EXPECT_EQ(j1939_signal_bytes(payload, 4, &sig, dst, 8), -1);
    sig.flags = J1939_SIG_LITTLE_ENDIAN;
    EXPECT_EQ(j1939_signal_bytes(payload, 16, &sig, dst, 8), -1);
    sig = sigOf(1, 80, J1939_SIG_BYTES);
    EXPECT_EQ(j1939_signal_bytes(payload, 16, &sig, dst, 8), -1);
    sig = sigOf(0, 12, J1939_SIG_BYTES);
    payload[0] = 'Z';
    ASSERT_EQ(j1939_signal_bytes(payload, 8, &sig, dst, 8), 0);
    EXPECT_STREQ(dst, "Z");
    EXPECT_EQ(j1939_signal_bytes(payload, 1, &sig, dst, 8), -1);
    EXPECT_EQ(j1939_signal_bytes(NULL, 8, &sig, dst, 8), -1);
    EXPECT_EQ(j1939_signal_bytes(payload, 8, NULL, dst, 8), -1);
    EXPECT_EQ(j1939_signal_bytes(payload, 8, &sig, NULL, 8), -1);
    EXPECT_EQ(j1939_signal_bytes(payload, 8, &sig, dst, 0), -1);
    sig = sigOf(0, 0, J1939_SIG_BYTES);
    EXPECT_EQ(j1939_signal_bytes(payload, 8, &sig, dst, 8), -1);
}

TEST(J1939SignalCodec, ActiveFollowsTheMultiplexSwitch) {
    uint8_t payload[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(j1939_signal_active(NULL, &kMessages[3], payload, 8, &kSignals[6]), -1);
    EXPECT_EQ(j1939_signal_active(&kDb, NULL, payload, 8, &kSignals[6]), -1);
    EXPECT_EQ(j1939_signal_active(&kDb, &kMessages[3], payload, 8, NULL), -1);
    EXPECT_EQ(j1939_signal_active(&kDb, &kMessages[4], NULL, 8, &kSignals[8]), 1);

    EXPECT_EQ(j1939_signal_active(&kDb, &kMessages[3], payload, 8, &kSignals[6]), 1);
    payload[0] = 1;
    EXPECT_EQ(j1939_signal_active(&kDb, &kMessages[3], payload, 8, &kSignals[6]), 0);
    EXPECT_EQ(j1939_signal_active(&kDb, &kMessages[3], payload, 8, &kSignals[7]), 1);
    EXPECT_EQ(j1939_signal_active(&kDb, &kMessages[3], payload, 0, &kSignals[6]), -1);

    j1939_message_t only_a = kMessages[3];
    only_a.signal_begin = 6;
    only_a.signal_count = 1;
    EXPECT_EQ(j1939_signal_active(&kDb, &only_a, payload, 8, &kSignals[6]), 0);

    j1939_signal_t both = kSignals[5];
    both.flags = J1939_SIG_MULTIPLEXOR | J1939_SIG_MULTIPLEXED | J1939_SIG_LITTLE_ENDIAN;
    j1939_signal_t local[2] = {both, kSignals[6]};
    j1939_message_t mixed = kMessages[3];
    mixed.signal_begin = 0;
    mixed.signal_count = 2;
    j1939_db_t db = kDb;
    db.signals = local;
    db.signal_count = 2;
    EXPECT_EQ(j1939_signal_active(&db, &mixed, payload, 8, &local[1]), 0);

    db.signals = NULL;
    EXPECT_EQ(j1939_signal_active(&db, &mixed, payload, 8, &local[1]), -1);
}
