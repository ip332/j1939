//
// Created by igor on 9/1/21.
//

#ifndef J1939_FRAME_H
#define J1939_FRAME_H

#include <map>
#include <string>
#include <stdint.h>
#include <vector>

#include <stdint.h>
#include <cstring>

#ifdef __linux__
#include <linux/can.h>
#else
#define CAN_EFF_FLAG 0x80000000U
#endif

// Extended-message keys. Priority and CAN_EFF_FLAG are not identity.
// Bit 31 is the generic row for a PGN. Bit 30 is the row for one source address.
// An 11-bit id stays in 0..0x7FF and is not stored as a PGN.
inline uint32_t j1939PgnKey(uint32_t pgn) {
    return CAN_EFF_FLAG | (pgn & 0x3FFFFu);
}

inline uint32_t j1939SaKey(uint32_t pgn, uint8_t sa) {
    return 0x40000000u | ((pgn & 0x3FFFFu) << 8) | sa;
}

// Lowest `length` bits set. Length 0 is an empty field. Length 64 is every bit.
// A longer integer signal is refused: the mask is 0, and read_bits/write_bits do nothing.
// The shift is unsigned long long so lengths 32..63 are defined on a 32-bit target.
inline uint64_t make_mask(size_t length) {
    if (length == 0 || length > 64) {
        return 0;
    }
    if (length == 64) {
        return ~0ull;
    }
    return (1ull << length) - 1ull;
}

// This union is used to simplify bits packing/unpacking to/from a CAN frame.
// Note that it can only support 8 bits frames when a real CAN frame might be longer than that.
typedef union {
    uint64_t value_;
    uint8_t buffer_[8];
} union64_t;

// This class is going to be used as an intermediate storage where the CAN local_frame will be read from the bus.
// It is going to be used by the J1939 parser class.
struct J1939_frame {
    static constexpr uint16_t max_dlc_ = 1758; // Absolute maximum size a long message could have.

    uint32_t pgn_;     // Parameters Group Number.
    uint8_t dst_;      // Destination of message (ECU address) or 0xFF in case of a broadcast
    uint8_t src_;      // Sender's source address.
    uint8_t pri_;      // Priority (0 - the highest, 7 - the lowest
    uint16_t dlc_;           // NUmber of bytes used in the buffer
    uint8_t buffer_[max_dlc_];  // Data buffer
    bool extended_;

    uint64_t time_ns_;  // optional field to store the time when this frame was filled with data.

    J1939_frame() { reset(); }

    void reset(uint8_t value = 0xff) {
        pgn_ = 0;
        dst_ = 0;
        src_ = 0;
        dlc_ = 0;
        pri_ = 0;
        extended_ = false;
        memset(buffer_, value, sizeof (buffer_));
    }

    bool operator== (const J1939_frame &other) const {
        if (pgn_ == other.pgn_ && dst_ == other.dst_ && src_ == other.src_ && dlc_ == other.dlc_ &&
            pri_ == other.pri_ && extended_ == other.extended_) {
            return memcmp(buffer_, other.buffer_, dlc_) == 0;
        }
        return false;
    }

    // Parses the CAN ID onto components and copies the source
    bool setFrom(uint32_t can_id, const uint8_t *buffer = nullptr, uint16_t dlc = 0) {
        if (dlc > max_dlc_) {
            return false;
        }
        extended_ = can_id & CAN_EFF_FLAG ? true : false;
        uint32_t p = (can_id & 0x1c000000) >> 26;
        uint32_t edp = (can_id & 0x02000000) >> 25;
        uint32_t dp = (can_id & 0x01000000) >> 24;
        uint32_t pf = (can_id & 0x00ff0000) >> 16;
        uint32_t ps = (can_id & 0x0000ff00) >> 8;
        uint32_t sa = (can_id & 0x000000ff);

        if (edp != 0) {
            return false;
        }

        if (pf < 240) {
            pgn_ = (dp << 16) + (pf << 8);
            dst_ = ps;
        } else {
            pgn_ = (dp << 16) + (pf << 8) + ps;
            dst_ = 0xFF;
        }
        pri_ = p;
        src_ = sa;
        dlc_ = dlc;
        if ((dlc_ > 0) && (buffer != nullptr)) {
            memcpy(buffer_, buffer, dlc);
        }
        return true;
    }

    uint32_t canID() const {
        uint32_t dp = (pgn_ >> 16) & 1;
        uint32_t pf = (pgn_ >> 8) & 0xff;
        uint32_t ps = pf < 240 ? dst_ : (pgn_ & 0xff);
        uint32_t flag = extended_ ? CAN_EFF_FLAG : 0;
        return flag | ((pri_ & 0x7) << 26) | ((dp & 0x1) << 24) | (pf << 16) | (ps << 8) | src_;
    };

    // Copies all bytes which stores 'length' bits starting from 'start_bit'
    void copyUsedTo(union64_t *buf, uint16_t start_bit, uint16_t length, bool little_endian) const;
    // Reverse operation to replace used bytes in the frame
    void copyUsedFrom(const union64_t &buf, uint16_t start_bit, uint16_t length, bool little_endian);

    uint64_t read_bits(uint16_t start_bit, uint16_t length, bool little_endian) const;
    void write_bits(uint64_t value, uint16_t start_bit, uint16_t length, bool little_endian);
};

// This class defines SPN format produced by the DBC compiler in a lookup table in a read-only mode
// (i.e. no modifications in such table shall be done for the duration of the code running).
struct SPN {
    // This name is used only to present the content in a human readable format.
    std::string name_;
    // Start bit in the local_frame.
    uint16_t start_bit_;
    // Number of bits reserved for this signal. Wider than 64 only for a byte string.
    uint16_t length_;
    // Indicates if the MSB stores sign bit or not.
    bool signedness_;
    // Most of the signals are encoded in little-endian format but not all.
    bool little_endian_;
    // Scalar and offset convert the raw value into a physical value (mph, degree, etc.).
    double scalar_;
    double offset_;
    double min_;
    double max_;
    // Enum values are the raw wire pattern, so the key is wide enough for any integer signal.
    std::map<uint64_t, std::string> enum_names_;

    // Return given value in human-readable format.
    std::string toString(uint64_t value) const;
    // Return given value in universal format.
    double toDouble(uint64_t value) const;
    // Return given value as integer.
    int64_t toInteger(uint64_t value) const;

    bool operator==(const SPN & other) const {
        return name_ == other.name_;
    }
};

struct PGN {
    // Name
    std::string name_;
    // Expected number of bytes in the payload. A transport-protocol frame can exceed 255.
    uint16_t dlc_;
    // All signals (order is not guaranteed)
    std::vector<SPN> signals_;

    // Returns a pointer to the signal or nullptr if the name was not found.
    const SPN * signalByName(const char *name) const;
};

#endif //J1939_FRAME_H
