//
// Created by Igor on 2021-09-09.
//

#include "dbc_message.h"

#include <sstream>
#include <algorithm>

Message::Message(const std::string &line) {
    // Example:
    // BO_ 2633984656 pDriveSystemStatus_144: 8 BCM_2
    std::vector <std::string> fields = splitString(line, ' ');

    can_id_ = std::stoll(fields[1]);

    // Drop ':' from the end of the name:
    name_ = fields[2];
    if (name_[name_.size() - 1] == ':') {
        name_.pop_back();
    }

    dlc_ = std::stoi(fields[3]);

    // Extract PGN and SA from the CAN ID.
    uint32_t dp = (can_id_ & 0x01000000) >> 24;
    uint32_t pf = (can_id_ & 0x00ff0000) >> 16;
    uint32_t ps = (can_id_ & 0x0000ff00) >> 8;
    sa_ = (can_id_ & 0x000000ff);

    if (pf < 240) {
        pgn_ = (dp << 16) + (pf << 8);
    } else {
        pgn_ = (dp << 16) + (pf << 8) + ps;
    }
    snake_name_ = toSnakeCase(name_);
    std::transform(snake_name_.begin(), snake_name_.end(), snake_name_.begin(), ::tolower);
}

Message Message::generalized() const {
    Message result(*this);
    // Reset SA and regenerate CAN ID
    J1939_frame tmp;
    tmp.setFrom(can_id_);
    tmp.src_ = 0;
    result.can_id_ = tmp.canID();
    // Check if the name has a suffix with two hex digits (e.g. _0F)
    auto pos = name_.rfind('_');
    if (pos != std::string::npos) {
        std::string suffix = name_.substr(pos + 1);
        if (suffix.length() == 2 && std::isxdigit(suffix[0]) && std::isxdigit(suffix[1])) {
            // Cut off the suffix from the name.
            result.name_.resize(pos);
            result.snake_name_ = name_;
            std::transform(result.snake_name_.begin(), result.snake_name_.end(), result.snake_name_.begin(), ::tolower);
        }
    }
    result.sa_ = 0;
    return result;
}

bool Message::little_endian() const {
    if (signals_.empty()) {
        return true;
    }
    bool little_endian = signals_.begin()->second.little_endian();
    for (const auto &signal: signals_) {
        assert(signal.second.little_endian() == little_endian);
    }
    return little_endian;
}

std::string Message::details() const {
    // /* Standard frame */
    // or
    // /* PGN: 1234 (0xabc) */
    if (extended()) {
        std::stringstream out;
        out << " /* PGN: " << pgn_ << " (0x" << std::hex << pgn_ << ") */ ";
        return out.str();
    }
    return " /* Standard Frame */ ";
}

std::string Message::toPgnString(bool generic_name) const {
    // Expected format (defined by the PGN class in j1939_frame.h
    // { "MessageName", 8, {
    //      <signal 1>
    //      <signal 2>
    //       . . .
    //    }}

    std::stringstream out;
    std::string msg_name = name_;
    if (generic_name) {
        // Generate source address suffix (i.e. _03)
        char suffix[4];
        snprintf(suffix, sizeof(suffix), "_%02X", sa_);
        auto pos = name_.find(suffix);
        msg_name = name_.substr(0, pos);
    }
    out << "{ \"" << msg_name << "\", " << dlc_ << ", {\n";
    for (const auto & signal: signals_) {
        out << signal.second.toSpnString();
    }
    out << "\t}}";
    return out.str();
}

std::string Message::toUnion() const {
// #define TSC1_0F_MESSAGE_ID          0xC000F2A
// #define TSC1_0F_MESSAGE_DLC         8
// union tsc1_0f_message_t {
//     uint8_t data[TSC1_0F_MESSAGE_DLC];
//     struct fields {
//         uint16_t EngOverrideCtrlIMode : 2;
//         ...
//     } __attribute__((packed));
// }
    std::stringstream out;
    // Output defines for the message name and the DLC
    out << "\n#define " << name_ << "_MESSAGE_ID\t0x" << std::hex << can_id_ << "\n";
    out << "#define " << name_ << "_PGN\t\t0x" << std::hex << pgn_ << "\n";
    out << "#define " << name_ << "_MESSAGE_DLC\t" << std::dec << dlc_ << "\n";

    // Output the packed struct definition.
    out << "struct " << snake_name_ << "_fields_t {\n";
    if (little_endian()) {
        uint16_t start_bit = 0;
        for (const auto & signal: signals_) {
            out << signal.second.toPackedInt(&start_bit);
        }
    } else {
        uint16_t start_bit = dlc_ * 8 - 1;
        for (auto it = signals_.rbegin(); it != signals_.rend(); ++it) {
            out << it->second.toPackedInt(&start_bit);
        }
    }
    out << "} __attribute__((packed));\n\n";

    out << "\nstruct " << snake_name_ << "_message_t {\n";
    out << "    union {\n";
    out << "        uint8_t data[" << name_ << "_MESSAGE_DLC" << "];\n";
    out << "        struct " << snake_name_ << "_fields_t fields;\n";
    out << "    };\n";
    // Generate methods which might be useful for using this type in C++ environment.
    out << "#ifdef __cplusplus\n";
    for (const auto & signal: signals_) {
        out << signal.second.toGetSetMethod();
    }
    // We also need methods to erase the content.
    out << "\n    void fillFrameWith(uint8_t value = 0xFF) { memset(data, value, sizeof(data)); }\n";
    out << "#endif\n";
    out << "\n};\n";
    // TODO: add list of values as defines where each name consists of the message name and first two words of the value, i.e.
    //   #define TSC1_0F_TORQUE_CONTROL 2
    return out.str();
}

// Parses the provided string and adds it to the signals_ map.
bool Message::addSignal(const std::string &string) {
    Signal signal(string);
    if (signal.valid()) {
        signal_list_.push_back(signal);
        // emplace keeps the first signal when two multiplex values share a start bit.
        // The PGN and union outputs still see that first signal; the C database sees signal_list_.
        signals_.emplace(signal.startBit(), signal);
        start_bit_by_name_.emplace(signal.name(), signal.startBit());
        return true;
    }
    return false;
}

Signal *Message::findSignal(const std::string &name) {
    for (auto &signal : signal_list_) {
        if (signal.name() == name) {
            return &signal;
        }
    }
    return nullptr;
}

bool Message::addValues(const std::vector <std::string> &fields) {
    if (dlc_ == 0) {
        // This is not a real message therefore don't bother to add VAL_records.
        return true;
    }
    auto start_bit = start_bit_by_name_.find(fields[2]);
    assertm(start_bit != start_bit_by_name_.end(), "Unexpected signal name in  VAL_ record");
    Signal *signal = findSignal(fields[2]);
    assertm(signal != nullptr, "Unexpected signal name in VAL_ record");
    signal->addValue(fields);
    auto it = signals_.find(start_bit->second);
    assertm(it != signals_.end(), "Unexpected start bit while processing VAL_ record");
    if (it->second.name() == fields[2]) {
        it->second.addValue(fields);
    }
    return true;
}

PGN Message::toPgn() const {
    PGN pgn;
    pgn.name_ = name_.c_str();
    pgn.dlc_ = dlc_;
    for (const auto & pair: signals_) {
        pgn.signals_.push_back(pair.second.toSpn());
    }
    return pgn;
}

std::string Message::toString() const {
    std::stringstream output;
    output << "From: " << dbc_name_ << ", Name: " << name_ << ", ID: 0x" << std::uppercase << std::hex << can_id_;
    output << ", (" << std::dec << can_id_ << "), ";
    output << "PGN: 0x" << std::dec << pgn_ << " (" << std::hex << pgn_ << "), ";
    output << "DLC: " << std::dec << dlc_ << std::endl;
    for (auto s : signals_) {
        output << "  " << s.second.toString() << std::endl;
    }
    return output.str();
}