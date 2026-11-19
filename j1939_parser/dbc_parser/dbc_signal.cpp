//
// Created by Igor on 2021-09-09.
//

#include <iostream>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include "dbc_signal.h"

// A helper function to split a string onto fields by a delimiter
std::vector <std::string> splitString(const std::string &line, char delimiter) {
    std::vector <std::string> result;

    std::stringstream stream(line);
    std::string temp;

    while (getline(stream, temp, delimiter)) {
        result.push_back(temp);
    }
    return result;
}

std::string toSnakeCase(const std::string &name) {
    // Snake case: any Capital letter to be replaced by an underscore and the lowercase version of the same letter.
    // So in the worst case scenario we will need more characters, for example:
    //  AbCdEfGhIjKlMnOpRsTu -> ab_cd_ef_gh_ij_kl_mn_op_rs_tu
    char result[name.length() + name.length() / 2 + 1];
    bool last_character_uppercase = false;
    size_t size = 0;
    for (auto c: name) {
        if (size == sizeof(result)) {
            break; // defensive: buffer sizing guarantees at least 1 char of slack, unreachable
        }
        if(c == '_') {
            if ((size > 0) && (result[size - 1] != '_')) {
                result[size++] = '_';
                last_character_uppercase = false;
            }
            continue;
        }
        if (isupper(c)) {
            if (!last_character_uppercase && ((size > 0) && (result[size - 1] != '_'))) {
                result[size++] = '_';
                if (size == sizeof(result)) {
                    break; // defensive: same buffer-sizing guarantee as above, unreachable
                }
            }
            last_character_uppercase = true;
        } else {
            last_character_uppercase = false;
        }
        result[size++] = tolower(c);
    }
    return std::string(result, size);
}

Signal::Signal(const std::string &line) : valid_(false) {
    // Examlpe of a record:
    //    SG_ pHeatdGripDwnSwSts : 52|2@1+ (1,0) [0|3] "status" Vector__XXX
    // Indices:
    // 0 1    2                  3 4        5     6       7
    std::vector <std::string> fields = splitString(line, ' ');

    if (fields.size() < 7) {
        std::cerr << "Error: wrong number of fields in SG_ record:" << std::endl;
        std::cerr << "     " << line << std::endl;
        return;
    }
    name_ = fields[2];

    int fmt_idx = 4;
    if (fields[3] != ":") {
        // Multiplex marker: "M" (the switch), "m16" (shown when the switch is 16),
        // or "m16M" (extended multiplex: both a switch and a switched signal).
        const std::string &mux = fields[3];
        if (mux == "M") {
            multiplexor_ = true;
        } else if (!mux.empty() && mux[0] == 'm') {
            size_t i = 1;
            unsigned value = 0;
            bool digits = false;
            while (i < mux.size() && std::isdigit(static_cast<unsigned char>(mux[i]))) {
                digits = true;
                value = value * 10u + static_cast<unsigned>(mux[i] - '0');
                i++;
            }
            if (digits && value <= 0xFFFFu) {
                multiplexed_ = true;
                mux_value_ = static_cast<uint16_t>(value);
                if (i < mux.size() && mux[i] == 'M' && i + 1 == mux.size()) {
                    multiplexor_ = true;
                }
            }
        }
        fmt_idx++;
    }

    std::vector <std::string> fmt = splitString(fields[fmt_idx], '|');
    start_bit_ = std::stoi(fmt[0]);
    std::vector <std::string> attr = splitString(fmt[1], '@');
    length_ = std::stoi(attr[0]);

    little_endian_ = attr[1][0] == '1';
    signed_ = attr[1][1] == '-';

    std::string pair1 = fields[fmt_idx + 1].substr(1, fields[fmt_idx + 1].length() - 2);
    std::vector <std::string> scale_offset = splitString(pair1, ',');
    scale_literal_ = scale_offset[0];
    offset_literal_ = scale_offset[1];
    // Keep the double from the decimal text. A float factor such as 1e-7 is already rounded.
    scale_d_ = std::stod(scale_offset[0]);
    offset_d_ = std::stod(scale_offset[1]);

    std::string pair2 = fields[fmt_idx + 2].substr(1, fields[fmt_idx + 2].length() - 2);
    std::vector <std::string> min_max = splitString(pair2, '|');
    min_literal_ = min_max[0];
    max_literal_ = min_max[1];
    min_ = std::stod(min_max[0]);
    max_ = std::stod(min_max[1]);

    units_ = fields[fmt_idx + 3];
    valid_ = true;
}

std::string Signal::toSpnString() const {
    // Expected format (defined by the SPN class in j1939_frame.h
    //      {"Signal", 9, 16, false, 1, 0, {0,65535}, {}},
    // or
    //      {"Signal", 0, 2, false, 1, 0, {0,3}, {
    //          {0, "Off"}, {1, "On"}, {2, "Error"}, {3, "Undefined"}}},

    std::stringstream out;
    out << "\t\t{\"" << name_ << "\", " << start_bit_ << ", " << length_ << ", ";
    out << (signed_ ? "true, " : "false, ");
    out << (little_endian_ ? "true, " : "false, ");
    out << scale_d_ << ", " << offset_d_ << ", ";
    out << min_ << ", " << max_ << ", {";
    if (!enum_map_.empty()) {
        out << "\n\t\t\t";
        size_t length = 0;
        for (const auto & it: enum_map_) {
            // TODO: add an option to choose the maximum output string width.
            if (length > 80) {
                length = 0;
                out << "\n\t\t\t";
            }
            // Note: the string value is stored already wrapped into quotas.
            std::string one_value = "{" + std::to_string(it.first) + ", " + std::string(it.second) + "}, ";
            out << one_value;
            length += one_value.length();
        }
    }
    out << "}},\n";
    return out.str();
};

// The vector represents the DBC VAL_ record, separated onto fields using space as a delimeter, e.g.
//     VAL_ 2566887215 pVehicle_Event_Status__Accelerat 2 "Error" 0 "Off" 1 "On" 3 "Not-Available" ;
// is expected to be passed in as a vector of 12 strings.
bool Signal::addValue(const std::vector <std::string> &fields) {
    // The first three indices represent the record type, CAN ID and the Signal name.
    for (size_t i = 3; i + 1 < fields.size(); i += 2) {
        const std::string &text = fields[i];
        // strtoull accepts a leading '-' and wraps. VAL_ values are wire patterns, so reject it.
        if (text.empty() || text[0] == '-') {
            value_error_ = "VAL_ value \"" + text + "\" is not an unsigned integer";
            return false;
        }
        errno = 0;
        char *end = nullptr;
        unsigned long long value = std::strtoull(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
            value_error_ = "VAL_ value \"" + text + "\" does not fit in an unsigned 64-bit integer";
            return false;
        }
        std::string name(fields[i + 1]);
        auto it = enum_map_.find(static_cast<uint64_t>(value));
        if (it != enum_map_.end()) {
            // Ignore full duplicates and assert on other cases.
            assertm(it->second == name, "Duplicated VAL_ record with a different content");
            continue;
        }
        enum_map_.emplace(static_cast<uint64_t>(value), name);
    }
    return true;
}

std::string Signal::fieldType() const {
    if (length_ <= 8) {
        return signed_ ? "int8_t " : "uint8_t ";
    }
    if (length_ <= 16) {
        return signed_ ? "int16_t " : "uint16_t ";
    }
    if (length_ <= 32) {
        return signed_ ? "int32_t " : "uint32_t ";
    }
    return signed_ ? "int64_t " : "uint64_t ";
}

std::string Signal::toPackedInt(uint16_t *start_bit, bool snake_case_name) const {
    std::stringstream out;
    // Check if the passed value matches this signal's start bit.
    int gap = little_endian_ ? start_bit_ - *start_bit : *start_bit - (start_bit_ + length_ - 1);
    if (gap < 0) {
        // Add some diagnostics first and only then use assert.
        std::cerr << "Field " << name_ << " starts from bit " << uint32_t(start_bit_) <<
                  " but the previous field ends at " << *start_bit
                  << ". Please check the DBC file and fix the problem\n";
        assert(gap >= 0);
    }
    if (gap > 0) {
        out << "    uint64_t : " << gap << ";\n";
    }
    auto field_type = signed_ ? "int64_t " : "uint64_t ";
    if (snake_case_name) {
        out << "    " << field_type << toSnakeCase(name_) << " : " << length_ << ";\n";
    } else {
        out << "    " << field_type << name_ << " : " << length_ << ";\n";
    }
    *start_bit = little_endian_ ? start_bit_ + length_ : start_bit_ - 1;
    return out.str();
}

std::string Signal::toGetSetMethod() const {
// Expected format:
//    float <snake case signal name>() { return fields_.<FullSignalName> * 0.125 + offset; }
//    void <snake case signal name>(float value) { fields_.<FullSignalName> = value / scalar_ - offset_; }
    std::stringstream out;
    std::string snake_name = toSnakeCase(name_);
    out << "\n";
    if (scale_d_ == 1 && offset_d_ == 0) {
        out << "\t" << fieldType() << snake_name << "() const { return fields." << snake_name << "; }\n";
        out << "\tvoid " << snake_name << "(" << fieldType() << "value) { fields." << snake_name << " = value; }\n";
    } else {
        out << "\tdouble " << snake_name << "() const { return fields." << snake_name << " * " << scale_d_ << " + "
            << offset_d_ << "; }\n";
        out << "\tvoid " << snake_name << "(double value) { fields." << snake_name << " = (value - " << offset_d_
            << ") / " << scale_d_ << "; }\n";
    }
    return out.str();

// TODO: add special treatment for the field with fixed set of values (i.e. enum).
}

bool Signal::sameLayout(const Signal &other) const {
    return start_bit_ == other.start_bit_ &&
           length_ == other.length_ &&
           signed_ == other.signed_ &&
           little_endian_ == other.little_endian_ &&
           scale_d_ == other.scale_d_ &&
           offset_d_ == other.offset_d_ &&
           min_ == other.min_ &&
           max_ == other.max_ &&
           multiplexor_ == other.multiplexor_ &&
           multiplexed_ == other.multiplexed_ &&
           mux_value_ == other.mux_value_ &&
           enum_map_ == other.enum_map_;
}

SPN Signal::toSpn() const {
    SPN spn;
    spn.name_ = name_.c_str();
    spn.length_ = length_;
    spn.offset_ = offset_d_;
    spn.scalar_ = scale_d_;
    spn.start_bit_ = start_bit_;
    spn.signedness_ = signed_;
    spn.little_endian_ = little_endian_;
    for (const auto & val: enum_map_) {
        spn.enum_names_.emplace(val.first, val.second.c_str());
    }
    return spn;
}

std::string Signal::toString() const {
    std::stringstream output;
    output << "name: '" << name_ << "', ";
    output << "start: " << start_bit_ << ", ";
    output << "len: " << length_ << ", ";
    output << "signed: " << signed_ << ", ";
    output << "LE: " << little_endian_ << ", ";
    output << "offset: " << offset_d_ << ", ";
    output << "scalar: " << scale_d_;
    if (!enum_map_.empty()) {
        for (auto e : enum_map_) {
            output << std::endl << "    " << e.second << " = " << e.first;
        }
    }
    return output.str();
}
