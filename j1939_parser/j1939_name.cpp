//
// J1939 / ISO 11783 NAME (the 64-bit identity carried by Address Claimed, PGN 60928).
//

#include "j1939_name.h"

#include <cstdio>

namespace {
uint64_t bits(uint64_t raw, unsigned start, unsigned length) {
    return (raw >> start) & ((1ULL << length) - 1);
}
uint64_t place(uint64_t value, unsigned start, unsigned length) {
    return (value & ((1ULL << length) - 1)) << start;
}
} // namespace

J1939Name J1939Name::fromRaw(uint64_t raw) {
    J1939Name n;
    n.identity_number_ = static_cast<uint32_t>(bits(raw, 0, 21));
    n.manufacturer_code_ = static_cast<uint16_t>(bits(raw, 21, 11));
    n.ecu_instance_ = static_cast<uint8_t>(bits(raw, 32, 3));
    n.function_instance_ = static_cast<uint8_t>(bits(raw, 35, 5));
    n.function_ = static_cast<uint8_t>(bits(raw, 40, 8));
    n.reserved_ = bits(raw, 48, 1) != 0;
    n.vehicle_system_ = static_cast<uint8_t>(bits(raw, 49, 7));
    n.vehicle_system_instance_ = static_cast<uint8_t>(bits(raw, 56, 4));
    n.industry_group_ = static_cast<uint8_t>(bits(raw, 60, 3));
    n.arbitrary_address_capable_ = bits(raw, 63, 1) != 0;
    return n;
}

uint64_t J1939Name::raw() const {
    return place(identity_number_, 0, 21) | place(manufacturer_code_, 21, 11) |
           place(ecu_instance_, 32, 3) | place(function_instance_, 35, 5) |
           place(function_, 40, 8) | place(reserved_ ? 1 : 0, 48, 1) |
           place(vehicle_system_, 49, 7) | place(vehicle_system_instance_, 56, 4) |
           place(industry_group_, 60, 3) | place(arbitrary_address_capable_ ? 1 : 0, 63, 1);
}

bool J1939Name::rawFromBytes(const uint8_t *bytes, size_t length, uint64_t *raw) {
    if (bytes == nullptr || length < 8) {
        return false;
    }
    uint64_t value = 0;
    for (int i = 7; i >= 0; i--) {
        value = (value << 8) | bytes[i];
    }
    *raw = value;
    return true;
}

void J1939Name::rawToBytes(uint64_t raw, uint8_t *bytes) {
    for (int i = 0; i < 8; i++) {
        bytes[i] = static_cast<uint8_t>(raw >> (8 * i));
    }
}

std::string J1939Name::hex(uint64_t raw) {
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(raw));
    return buf;
}

std::string J1939Name::toString() const {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%s (id=%u mfr=%u ecu=%u func_inst=%u func=%u vsys=%u vsys_inst=%u ind=%u aac=%d)",
             hex(raw()).c_str(), identity_number_, manufacturer_code_, ecu_instance_,
             function_instance_, function_, vehicle_system_, vehicle_system_instance_,
             industry_group_, arbitrary_address_capable_ ? 1 : 0);
    return buf;
}
