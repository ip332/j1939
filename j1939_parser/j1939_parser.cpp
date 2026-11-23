//
// Created by igor on 8/31/21.
//
#include "j1939_parser.h"

#include <algorithm>
#include <iostream>

std::string J1939Parser::PayloadToHex() const {
    char buf[2 * frame_.dlc_ + 1]; // (2 Hex digits + space) * 64;
    for (size_t i = 0; i < frame_.dlc_; i++) {
        sprintf(&buf[i * 2],"%02X", frame_.buffer_[ i ] );
    }
    return std::string(buf, frame_.dlc_ * 2);
}

std::string J1939Parser::PayloadToChar() const {
    char buf[frame_.dlc_ + 1];
    for (size_t i = 0; i < frame_.dlc_; i++) {
        char c = frame_.buffer_[i];
        if (isprint(c) && (c != '\n') && (c != '\r') && (c != '\t'))
            sprintf(&buf[i], "%c", c);
        else
            sprintf(&buf[i], ".");
    }
    return std::string(buf, frame_.dlc_);
}

void J1939Parser::toStringPgn(const PGN &pgn, std::basic_ostream<char> &out, bool show_unset_bits) const {
    char buf[100];
    if (frame_.extended_) {
        snprintf(buf, 100, "%s PGN:%03X(%d) SA:%02X(%d) DST:%02X(%d) Prio:%d Data(%d): ",
                 pgn.name_.c_str(), frame_.pgn_, frame_.pgn_, frame_.src_, frame_.src_, frame_.dst_, frame_.dst_, frame_.pri_, pgn.dlc_);
    } else {
        snprintf(buf, 100, "%s CAN ID:%04X(%d) Data(%d): ", pgn.name_.c_str(), frame_.canID(), frame_.canID(), pgn.dlc_);
    }

    out << std::string(buf) << PayloadToHex() << " {";
    for (const auto & spn : pgn.signals_) {
        if (!signalActive(pgn, spn)) continue;
        uint64_t value = read_value(spn.start_bit_, spn.length_, spn.little_endian_);
        if (show_unset_bits || (value != make_mask(spn.length_))) {
            out << spn.toString(value);
        }
    }
    out << " }";
}

void J1939Parser::toStringRawFrame(std::basic_ostream<char> &out) const {
    char buf[100];
    if (frame_.extended_) {
        snprintf(buf, 100, "UNDEF_%u PGN:%X(%d) SA:%X(%d) DA:%X(%d) Prio:%d Data(%d): ",
             frame_.canID(), frame_.pgn_, frame_.pgn_, frame_.src_, frame_.src_, frame_.dst_, frame_.dst_,
             frame_.pri_, frame_.dlc_);
    } else {
        snprintf(buf, 100, "UNDEFINED CAN ID:%X(%d) Data(%d): ", frame_.canID(), frame_.canID(), frame_.dlc_);
    }
    out << std::string(buf) << PayloadToHex() << " (" << PayloadToChar() << ")";
}

// Output message content into provided stream in a human readable form.
bool J1939Parser::toString(std::basic_ostream<char> &out, bool show_unset_bits) const {
    const PGN *pgn = getPgn();
    if (pgn != nullptr) {
        toStringPgn(*pgn, out, show_unset_bits);
        return true;
    }
    toStringRawFrame(out);
    return false;
}

const PGN* J1939Parser::getPgn() const {
    // An 11-bit frame is looked up by that id. An extended frame uses the DBC CAN id
    // when the map still has it, then the source-address row, then the generic PGN
    // row. Priority is not part of those last two keys.
    if (!frame_.extended_) {
        auto it = dbc_.find(frame_.canID() & 0x7FFu);
        if (it != dbc_.end()) {
            return &it->second;
        }
        // A hand-built map may store a non-extended id above 11 bits under that raw id.
        // The sliced PGN is not a key.
        it = dbc_.find(frame_.canID());
        if (it == dbc_.end()) {
            return nullptr;
        }
        return &it->second;
    }
    // An undeduplicated map is still keyed by the DBC CAN id. removeDuplicates
    // replaces those ids with the PGN and source-address keys below.
    auto exact = dbc_.find(frame_.canID());
    if (exact != dbc_.end()) {
        return &exact->second;
    }
    auto specific = dbc_.find(j1939SaKey(frame_.pgn_, frame_.src_));
    if (specific != dbc_.end()) {
        return &specific->second;
    }
    auto generic = dbc_.find(j1939PgnKey(frame_.pgn_));
    if (generic != dbc_.end()) {
        return &generic->second;
    }
    return nullptr;
}

void J1939Parser::scanAllSignals(const std::function<void(const SPN &)> &callback, bool show_unset_bits) const {
    const PGN *pgn = getPgn();
    if (pgn != nullptr) {
        for(const auto & spn : pgn->signals_) {
            if (signalActive(*pgn, spn) && (spn.start_bit_ + spn.length_) / 8 <= frame_.dlc_) {
                if (!show_unset_bits) {
                    auto raw_value_ptr = signalRawValue(spn);
                    if (raw_value_ptr && (*raw_value_ptr == make_mask(spn.length_))) {
                        continue;
                    }
                }
                callback(spn);
            }
        }
    }
}

const SPN *J1939Parser::signal(const char * name) const {
    const PGN *pgn = getPgn();
    if (pgn == nullptr) {
        return nullptr;
    }
    for (const auto &spn : pgn->signals_) {
        if (spn.name_ == name && signalActive(*pgn, spn)) return &spn;
    }
    return nullptr;
}

std::unique_ptr<uint64_t>J1939Parser::signalRawValue(const SPN &spn) const {
    std::unique_ptr<uint64_t> result;
    const PGN *pgn = getPgn();
    if (pgn == nullptr || !signalActive(*pgn, spn)) {
        return result;
    }
    uint64_t mask = make_mask(spn.length_);
    result.reset(new uint64_t);
    *result = read_value(spn.start_bit_, spn.length_, spn.little_endian_) & mask;
    return result;
}

std::unique_ptr<double> J1939Parser::signalValue(const SPN &spn) const {
    std::unique_ptr<double> result;
    auto result64 = signalRawValue(spn);
    if (result64) {
        result.reset(new double);
        *result = spn.toDouble(*result64);
    }
    return result;
}

std::unique_ptr<uint32_t> J1939Parser::signalRawValue32(const SPN &spn) const {
    std::unique_ptr<uint32_t> result32;
    auto result = signalRawValue(spn);
    if (result) {
        result32.reset(new uint32_t);
        *result32 = *result & 0xFFFFFFFF;
    }
    return result32;
}

std::unique_ptr<double> J1939Parser::signalValue(const char * name, bool show_unset_bits) const {
    const SPN* spn = signal(name);
    if (spn != nullptr) {
        if (show_unset_bits) {
            return signalValue(*spn);
        }
        auto raw_value = signalRawValue(*spn);
        if (raw_value && (*raw_value != make_mask(spn->length_))) {
            return std::unique_ptr<double>(new double (spn->toDouble(*raw_value)));
        }
    }
    return std::unique_ptr<double>();
}

std::unique_ptr<uint64_t> J1939Parser::signalRawValue(const char * name, bool show_unset_bits) const {
    const SPN* spn = signal(name);
    if (spn != nullptr) {
        if (show_unset_bits) {
            return signalRawValue(*spn);
        }
        auto raw_value = signalRawValue(*spn);
        if (raw_value && (*raw_value != make_mask(spn->length_))) {
            return raw_value;
        }
    }
    return std::unique_ptr<uint64_t>();
}

std::unique_ptr<uint32_t> J1939Parser::signalRawValue32(const char * name, bool show_unset_bits) const {
    const SPN* spn = signal(name);
    if (spn != nullptr) {
        if (show_unset_bits) {
            return signalRawValue32(*spn);
        }
        auto raw_value = signalRawValue32(*spn);
        if (raw_value && (*raw_value != make_mask(spn->length_))) {
            return raw_value;
        }
    }
    return std::unique_ptr<uint32_t>();
}

bool J1939Parser::setSignalValue(const char * name, double value) {
    const auto * pgn = getPgn();
    if (pgn == nullptr) {
        return false;
    }
    const auto * spn = pgn->signalByName(name);
    if (spn == nullptr) {
        return false;
    }
    if ((spn->offset_ == 0) && (spn->scalar_ == 1)) {
        write_value(static_cast<uint64_t>(value), spn->start_bit_, spn->length_, spn->little_endian_);
    } else if (value < 0) {
        write_value(static_cast<uint64_t>((value - spn->offset_) / spn->scalar_ - 0.5), spn->start_bit_, spn->length_, spn->little_endian_);
    } else {
        write_value(static_cast<uint64_t>((value - spn->offset_) / spn->scalar_ + 0.5), spn->start_bit_, spn->length_, spn->little_endian_);
    }
    return true;
}

bool J1939Parser::setSignalValue(const char * name, const std::string & enum_name){
    const auto * pgn = getPgn();
    if (pgn == nullptr) {
        return false;
    }
    const auto * spn = pgn->signalByName(name);
    if (spn == nullptr) {
        return false;
    }
    for (const auto & it : spn->enum_names_) {
        if (it.second == enum_name) {
            write_value(it.first, spn->start_bit_, spn->length_, spn->little_endian_);
            return true;
        }
    }
    return false;
}

std::set<uint32_t> J1939Parser::pgnFor(const std::string & signal, const std::map<uint32_t, PGN> &dbc) {
    std::set<uint32_t> result;
    for (const auto & it: dbc) {
        if (it.second.signalByName(signal.c_str())) {
            result.emplace(it.first);
            break;
        }
    }
    return result;
}

bool J1939Parser::signalActive(const PGN &pgn, const SPN &spn) const {
    if (!spn.multiplexed_) return true;
    for (const auto &sw : pgn.signals_) {
        if (!sw.multiplexor_ || sw.multiplexed_) continue;
        if (sw.length_ == 0 || sw.length_ > 64) return false;
        int bit = sw.start_bit_;
        for (unsigned i = 0; i < sw.length_; ++i) {
            if (bit < 0 || bit >= frame_.dlc_ * 8) return false;
            bit = sw.little_endian_ ? bit + 1 : (bit % 8 == 0 ? bit + 15 : bit - 1);
        }
        return read_value(sw.start_bit_, sw.length_, sw.little_endian_) == spn.mux_value_;
    }
    return false;
}
