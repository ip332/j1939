//
// Created by Igor on 2021-09-09.
//

#ifndef DBCPARSER_SIGNAL_H
#define DBCPARSER_SIGNAL_H

#include "j1939_frame.h"

#include <cassert>
#include <map>
#include <sstream>
#include <stdint.h>
#include <string>
#include <vector>

#define assertm(exp, msg) assert(((void)msg, exp))

// A helper function to split a string onto fields by a dlimeter
std::vector<std::string> splitString(const std::string &line, char delimiter);
// Transforms a camel case name into a snake case.
std::string toSnakeCase(const std::string &name);

// This class stores a single Signal format.
class Signal {
    // This name is used only to present the content in a human readable format.
    std::string name_;
    // Start bit in the local_frame.
    uint16_t start_bit_;
    // Number of bits reserved for this signal.
    uint16_t length_;
    // This bit tells if the MSB represents a sign or not.
    bool signed_;
    // Most of the signals are encoded in little-endian format but not all.
    bool little_endian_;
    double min_;
    double max_;
    // Not sure why do we need units here but just in case.
    std::string units_;

    // Enum values are the raw wire pattern, wide enough for a 64-bit signal.
    std::map<uint64_t, std::string> enum_map_;
    // Set when a VAL_ number does not fit in uint64_t.
    std::string value_error_;

    // J1939 SPN attribute (BA_ "SPN"). 0 when the DBC does not assign one.
    uint32_t spn_ = 0;
    bool has_spn_ = false;
    // Multiplex switch value. Meaningful when multiplexed_ is set.
    bool portable_mux_invalid_ = false;
    uint16_t mux_value_ = 0;
    bool multiplexor_ = false;
    bool multiplexed_ = false;
    // Decimal text from the DBC, so the C database does not round through float.
    std::string scale_literal_;
    std::string offset_literal_;
    std::string min_literal_;
    std::string max_literal_;
    double scale_d_ = 1;
    double offset_d_ = 0;

    bool valid_;

    // Returns standard C type which is required to represent this signal.
    std::string fieldType() const;
public:
    // Parses SG_ record from a DBC file and set up validity flag:
    Signal(const std::string &line, bool classic_numbers = false);

    bool valid() const { return valid_; }

    // Returns the value which defines signal's position in the CAN frame.
    uint16_t startBit() const { return start_bit_; }

    bool little_endian() const { return little_endian_; }

    std::string name() const { return name_; }

    uint16_t bitLength() const { return length_; }

    bool isSigned() const { return signed_; }

    uint32_t spn() const { return spn_; }

    void setSpn(uint32_t spn) { spn_ = spn; has_spn_ = true; }

    bool hasSpn() const { return has_spn_; }

    // Quoted DBC unit text. Strict export excludes escapes in DBC strings.
    const std::string &unitsLiteral() const { return units_; }

    bool portableMuxValid() const { return !portable_mux_invalid_; }

    uint16_t muxValue() const { return mux_value_; }

    bool isMultiplexor() const { return multiplexor_; }

    bool isMultiplexed() const { return multiplexed_; }

    const std::string &scaleLiteral() const { return scale_literal_; }

    const std::string &offsetLiteral() const { return offset_literal_; }

    const std::string &minLiteral() const { return min_literal_; }

    const std::string &maxLiteral() const { return max_literal_; }

    double scaleValue() const { return scale_d_; }

    double offsetValue() const { return offset_d_; }

    double minValue() const { return min_; }

    double maxValue() const { return max_; }

    const std::map<uint64_t, std::string> &enums() const { return enum_map_; }

    const std::string &valueError() const { return value_error_; }

    // Bit layout, endianness, scale, multiplex and enums. Names and SPNs are not compared.
    bool sameLayout(const Signal &other) const;

    // Compares two signals.
    bool operator==(const Signal & other) const {
        bool result = start_bit_ == other.start_bit_ &&
                      length_ == other.length_ && offset_d_ == other.offset_d_ && signed_ == other.signed_ &&
                      scale_d_ == other.scale_d_ && min_ == other.min_ && max_ == other.max_ &&
                      little_endian_ == other.little_endian_ &&
                      enum_map_.size() == other.enum_map_.size();
        if (result) {
            // Make sure that both enums have the same values
            return std::equal(enum_map_.begin(), enum_map_.end(),
                              other.enum_map_.begin());
        }
        return false;
    }

    // Prints out the initialisation list for the Signal class in the SPN format (see j1939_frame.h)
    std::string toSpnString() const;

    // Prints out the signal definition as a packed structure fields to be used in a union.
    // The first argument shows the expected start bit for this signal in the message.
    std::string toPackedInt(uint16_t *start_bit, bool snake_case_name = true) const;

    // Prints out the set/get methods.
    std::string toGetSetMethod() const;

    // Adds the list of values (a tokenized VAL_ record from the DBC file) to the signal.
    // Returns false when a value is empty, negative, has trailing junk, or does not fit in uint64_t.
    bool addValue(const std::vector<std::string> &fields);

    // Creates a new instance of the SPN class and fills it out with *this parameters.
    SPN toSpn() const;

    // Print signal in a human-readable format.
    std::string toString() const;
};

#endif //DBCPARSER_SIGNAL_H
