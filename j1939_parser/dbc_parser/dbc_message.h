//
// Created by Igor on 2021-09-09.
//

#ifndef DBCPARSER_MESSAGE_H
#define DBCPARSER_MESSAGE_H

#include "dbc_signal.h"

#include <map>
#include <vector>

// This class represents a single CAN message identified by its name and PGN (or CAN ID).
class Message {
    // The message name as it is defined in the DBC.
    std::string name_;
    // Full CAN ID as it is stored in the DBC.
    uint32_t can_id_;
    // The PGN extracted from the CAN ID.
    uint32_t pgn_;
    // Source address extracted from the CAN ID.
    uint8_t sa_;
    // Number of bytes in the payload.
    uint16_t dlc_;
    // All signals, ordered by the start bit. The first signal at a start bit wins;
    // a multiplex alternative that shares that bit is kept only in signal_list_.
    std::map <uint16_t, Signal> signals_;
    // Every signal, including multiplex alternatives that share a start bit.
    std::vector<Signal> signal_list_;
    // We also need ability to find a signal by name.
    std::map <std::string, uint16_t> start_bit_by_name_;
    // BA_ "SPN" BO_. 0 when the message has no SPN attribute.
    uint32_t message_spn_ = 0;
    // BA_ "J1939Default" BO_ <id> 1. Marks the generic layout when one PGN has several.
    bool default_mark_ = false;
    // The message name in a lower case, underscore separated format (aka "snake case")
    std::string snake_name_;
    // Source file name for debugging purposes.
    std::string dbc_name_;
public:
    // Parses the DBC BO_ record.
    explicit Message(const std::string &line);

    // This operator is used to compare two messages. It ignore the names which are irrelevant from the format point of view.
    bool operator==(const Message &other) const {
        bool result = pgn_ == other.pgn_ &&
                      dlc_ == other.dlc_ &&
                      signals_.size() == other.signals_.size();
        if (result) {
            // Make sure that all signals are identical
            result = std::equal(signals_.begin(), signals_.end(),
                                other.signals_.begin());
        }
        return result;
    }
    void setDbcName(const std::string &dbc) { dbc_name_ = dbc; }

    bool operator!=(const Message &other) const { return !operator==(other); }

    uint32_t pgn() const { return pgn_; }

    uint32_t canId() const { return can_id_; }

    bool extended() const { return can_id_ & CAN_EFF_FLAG ? true : false; }

    uint16_t dlc() const { return dlc_; };

    uint8_t sa() const { return sa_; }

    std::string name() const { return name_; }

    // Every signal in parse order, including ones that share a start bit.
    const std::vector<Signal> &allSignals() const { return signal_list_; }

    Signal *findSignal(const std::string &name);

    void setMessageSpn(uint32_t spn) { message_spn_ = spn; }

    uint32_t messageSpn() const { return message_spn_; }

    void markDefault() { default_mark_ = true; }

    bool defaultMark() const { return default_mark_; }

    std::string snake_name() const { return snake_name_; }

    // Returns the little_endian value (aborts if not all signals have the same value).
    bool little_endian() const;

    // Prints out the initialisation list for the Message class in the PGN format (see j1939_frame.h)
    // Set argument to true to remove a suffix from the messages name (i.e. TSC1_03 -> TSC1)
    std::string toPgnString(bool generic_name = true) const;

    // Prints out the message as a union of a packed structure and a bytes buffer.
    std::string toUnion() const;

    // Prints out a comment for PGN format to show CAN ID fields.
    std::string details() const;
    // Parses the provided string and adds it to the signals_ map.
    bool addSignal(const std::string &string);

    // Passes the given vector to the signal which name is stored under index == 2.
    bool addValues(const std::vector <std::string> &fields);

    // Converts Message into PGN format and returns it.
    PGN toPgn() const;

    // Returns a copy of this message with suffix (like _0F) removed from the name and with SA set to 0.
    Message generalized() const;

    // Print Message in a human-readable format.
    std::string toString() const;
};

#endif //DBCPARSER_MESSAGE_H
