//
// Created by Igor on 09/01/2021
//
#ifndef J1939_PARSER_H
#define J1939_PARSER_H

#include "j1939_frame.h"

#include <cstring>
#include <functional>
#include <stdint.h>
#include <memory>
#include <set>

// This class implements parsing/packing functionality for a given CAN local_frame
// The raw CAN local_frame as well as the DBC runtime representation are stored outside this class:
//  - the CAN local_frame can be declared as a local variable where the actual data will be read from the bus.
//  - there are more than one DBC used in the system so we need a mechanism to bind different DBC here.
class J1939Parser {
    // Raw local_frame reference.
    J1939_frame &frame_;
    // Compiled DBC reference
    const std::map<uint32_t, PGN> &dbc_;

    // Parsing/serialization shall be always defined with respect to the signal endianess.
    uint64_t read_value(uint16_t start_bit, uint16_t length, bool little_endian) const {
        return frame_.read_bits(start_bit, length, little_endian);
    }
    void write_value(uint64_t value, uint16_t start_bit, uint16_t length, bool little_endian) {
        frame_.write_bits(value, start_bit, length, little_endian);
    }

    // Returns payload bytes in Hex format.
    ::std::string PayloadToHex() const;

    // Returns payload bytes as an ASCII string.
    ::std::string PayloadToChar() const;

    // Returns bits corresponded to given SPN.
    uint64_t spnValue(const SPN &spn) const;

    // Output content into provided stream in a human readable form.
    void toStringPgn(const PGN &pgn, std::basic_ostream<char> &out, bool show_unset_bits) const;

    // Output raw local_frame when it's structure was unknown (i.e. when the DBC did not include definition for it).
    void toStringRawFrame(std::basic_ostream<char> &out) const;

public:
    J1939Parser(J1939_frame & j1939, const std::map<uint32_t, PGN> &dbc) : frame_(j1939), dbc_(dbc) {}

    // Output message content into provided stream in a human-readable form.
    bool toString(std::basic_ostream<char> &out, bool show_unset_bits = false) const;

    // Find description of the PGN stored in frame_).
    const PGN * getPgn() const;

    // Provides access to the raw CAN frame:
    const J1939_frame &frame() const { return frame_; };

    // Main API section to iterate through signals and to get their values.

    // The application might need to iterate through all signals defined in the PGN using the following method.
    void scanAllSignals(const std::function<void(const SPN &)> &,  bool show_unset_bits = true) const;

    // Alternatively the application might be interested in a specific signal which can be found by name.
    const SPN *signal(const char * name) const;
    const SPN *signal(const std::string &name) const {
        return signal(name.c_str());
    }

    // Reading the signal value in float format.
    std::unique_ptr<double> signalValue(const SPN &spn) const;
    std::unique_ptr<uint64_t> signalRawValue(const SPN &spn) const; // CAN message may have 64 bit value.
    std::unique_ptr<uint32_t> signalRawValue32(const SPN &spn) const; // For compatibility with the current roscan code.

    std::unique_ptr<double> signalValue(const char * name, bool show_unset_bits = false) const;
    std::unique_ptr<double> signalValue(const std::string &name, bool show_unset_bits = false) const {
        return signalValue(name.c_str(), show_unset_bits);
    }
    std::unique_ptr<uint64_t> signalRawValue(const char * name, bool show_unset_bits = false) const;
    std::unique_ptr<uint64_t> signalRawValue(const std::string &name, bool show_unset_bits = false) const {
        return signalRawValue(name.c_str(), show_unset_bits);
    }
    std::unique_ptr<uint32_t> signalRawValue32(const char * name, bool show_unset_bits = false) const;
    std::unique_ptr<uint32_t> signalRawValue32(const std::string &name, bool show_unset_bits = false) const {
        return signalRawValue32(name.c_str(), show_unset_bits);
    }

    // Setting the bits for the given signal.
    bool setSignalValue(const char * name, double value);
    bool setSignalValue(const std::string & name, double value) {
        return setSignalValue(name.c_str(), value);
    }
    // Similar for the SPN type ENUM. Returns false if the enum name is unknown.
    bool setSignalValue(const char * name, const std::string & enum_name);
    bool setSignalValue(const std::string & name, const std::string & enum_name) {
        return setSignalValue(name.c_str(), enum_name);
    }

    // Find all PGNs which may contain the given signal
    static std::set<uint32_t> pgnFor(const std::string & signal, const std::map<uint32_t, PGN> &dbc);
};

#endif // J1939_MESSAGE_H
