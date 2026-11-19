//
// J1939 / ISO 11783 NAME (the 64-bit identity carried by Address Claimed, PGN 60928).
//

#ifndef J1939_NAME_H
#define J1939_NAME_H

#include "j1939_frame.h"

#include <stdint.h>
#include <string>

// PGNs of the network-management messages that deal with NAMEs and addresses (J1939-81,
// ISO 11783-5; NMEA 2000 uses the same messages).
constexpr uint32_t kPgnAddressClaimed = 0xEE00;   // 60928: NAME, from the SA being claimed
constexpr uint32_t kPgnRequest = 0xEA00;          // 59904: 3-byte requested PGN
constexpr uint32_t kPgnCommandedAddress = 0xFED8; // 65240: NAME + new SA, 9 bytes via TP BAM
constexpr uint8_t kNullAddress = 0xFE;            // 254: "no address" / Cannot Claim
constexpr uint8_t kGlobalAddress = 0xFF;          // 255: broadcast, never a valid source

// The NAME split into its fields. The raw uint64_t (byte 0 of the claim = least significant
// byte) is what identifies a node and what arbitration compares (lower wins); the fields
// exist for display and filtering only. NMEA 2000 names some fields differently:
// ECU instance = device instance lower, function instance = device instance upper,
// vehicle system = device class, vehicle system instance = system instance.
struct J1939Name {
    uint32_t identity_number_;          // bits  0-20
    uint16_t manufacturer_code_;        // bits 21-31
    uint8_t ecu_instance_;              // bits 32-34
    uint8_t function_instance_;         // bits 35-39
    uint8_t function_;                  // bits 40-47
    bool reserved_;                     // bit  48
    uint8_t vehicle_system_;            // bits 49-55
    uint8_t vehicle_system_instance_;   // bits 56-59
    uint8_t industry_group_;            // bits 60-62
    bool arbitrary_address_capable_;    // bit  63

    static J1939Name fromRaw(uint64_t raw);
    // Fields are masked to their width.
    uint64_t raw() const;

    // Reads the 8 little-endian NAME bytes from bytes[0..length) into *raw (bytes past the
    // 8th, e.g. Commanded Address's new SA, are ignored). Returns false, leaving *raw alone,
    // if `bytes` is null or holds fewer than 8 bytes.
    static bool rawFromBytes(const uint8_t *bytes, size_t length, uint64_t *raw);
    static void rawToBytes(uint64_t raw, uint8_t *bytes);

    // "0x<16 hex digits> (id=..., mfr=..., ...)" for logs.
    std::string toString() const;
    static std::string hex(uint64_t raw);
};

#endif //J1939_NAME_H
