#include "dbc_parser.h"
#include "dbc_number.h"
#include "j1939_db.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <vector>
#include <utility>

namespace {

struct GenSignal {
    Signal signal;
};

struct GenMessage {
    std::string name;
    uint32_t pgn = 0;
    uint32_t can_id = 0;
    uint32_t sa_mask = 0;
    uint8_t sa = 0;
    uint8_t extended = 0;
    uint16_t dlc = 0;
    uint32_t message_spn = 0;
    std::vector<GenSignal> signals;
};

struct GroupKey {
    uint8_t extended;
    uint32_t key;
    bool operator<(const GroupKey &other) const {
        if (extended != other.extended) {
            return extended < other.extended;
        }
        return key < other.key;
    }
};

std::string cIdent(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "unnamed";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front()))) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string uniqueIdent(std::set<std::string> &used, const std::string &base) {
    std::string name = base;
    int suffix = 2;
    while (!used.insert(name).second) {
        name = base + "_" + std::to_string(suffix);
        suffix++;
    }
    return name;
}

bool hasSaSuffix(const std::string &name) {
    if (name.size() < 3 || name[name.size() - 3] != '_') {
        return false;
    }
    return std::isxdigit(static_cast<unsigned char>(name[name.size() - 2])) &&
           std::isxdigit(static_cast<unsigned char>(name[name.size() - 1]));
}

std::string stripSaSuffix(const std::string &name) {
    return name.substr(0, name.size() - 3);
}

bool signalLess(const Signal &a, const Signal &b) {
    if (a.startBit() != b.startBit()) {
        return a.startBit() < b.startBit();
    }
    if (a.muxValue() != b.muxValue()) {
        return a.muxValue() < b.muxValue();
    }
    if (a.isMultiplexor() != b.isMultiplexor()) {
        return a.isMultiplexor() < b.isMultiplexor();
    }
    return a.name() < b.name();
}

bool sameMessageLayout(const Message &a, const Message &b) {
    if (a.dlc() != b.dlc()) {
        return false;
    }
    std::vector<Signal> left = a.allSignals();
    std::vector<Signal> right = b.allSignals();
    if (left.size() != right.size()) {
        return false;
    }
    std::sort(left.begin(), left.end(), signalLess);
    std::sort(right.begin(), right.end(), signalLess);
    for (size_t i = 0; i < left.size(); i++) {
        if (!left[i].sameLayout(right[i])) {
            return false;
        }
    }
    return true;
}

bool isExtendedId(const Message &message) {
    uint32_t id = message.canId();
    if ((id & CAN_EFF_FLAG) != 0) {
        return true;
    }
    return (id & 0x1FFFFFFFu) > 0x7FFu;
}

uint32_t maskedCanId(const Message &message, bool generic) {
    uint32_t sa = generic ? 0u : message.sa();
    return (message.pgn() << 8) | sa;
}

std::string chooseGenericName(const std::vector<const Message *> &messages) {
    for (const Message *message : messages) {
        if (!hasSaSuffix(message->name())) {
            return message->name();
        }
    }
    return stripSaSuffix(messages.front()->name());
}

std::string flagExpr(const Signal &signal) {
    std::vector<const char *> parts;
    if (signal.isSigned()) {
        parts.push_back("J1939_SIG_SIGNED");
    }
    if (signal.little_endian()) {
        parts.push_back("J1939_SIG_LITTLE_ENDIAN");
    }
    if (signal.isMultiplexor()) {
        parts.push_back("J1939_SIG_MULTIPLEXOR");
    }
    if (signal.isMultiplexed()) {
        parts.push_back("J1939_SIG_MULTIPLEXED");
    }
    if (signal.bitLength() > 64) {
        parts.push_back("J1939_SIG_BYTES");
    }
    if (parts.empty()) {
        return "0";
    }
    std::string out = parts[0];
    for (size_t i = 1; i < parts.size(); i++) {
        out += " | ";
        out += parts[i];
    }
    return out;
}

// All tokens are checked before building or writing either output stream.
std::string emitNumber(const std::string &literal) { return dbcCNumber(literal); }

std::string enumLabel(const std::string &stored) {
    if (stored.size() >= 2 && stored.front() == '"' && stored.back() == '"') {
        return stored.substr(1, stored.size() - 2);
    }
    return stored;
}

class StringBlob {
public:
    uint32_t intern(const std::string &text) {
        auto it = offsets_.find(text);
        if (it != offsets_.end()) {
            return it->second;
        }
        uint32_t offset = static_cast<uint32_t>(bytes_.size());
        offsets_.emplace(text, offset);
        bytes_.append(text);
        bytes_.push_back('\0');
        return offset;
    }

    const std::string &bytes() const { return bytes_; }

private:
    std::string bytes_;
    std::map<std::string, uint32_t> offsets_;
};

GenMessage makeRow(const Message &message, const std::string &name, bool generic) {
    GenMessage row;
    row.name = name;
    row.dlc = message.dlc();
    row.message_spn = message.messageSpn();
    row.extended = isExtendedId(message) ? 1u : 0u;
    if (row.extended) {
        row.pgn = message.pgn();
        row.can_id = maskedCanId(message, generic);
        if (generic) {
            row.sa_mask = 0;
            row.sa = 0;
        } else {
            row.sa_mask = 0xFFu;
            row.sa = message.sa();
        }
    } else {
        row.pgn = 0;
        row.can_id = message.canId() & 0x7FFu;
        row.sa_mask = 0;
        row.sa = 0;
    }
    std::vector<Signal> signals = message.allSignals();
    std::sort(signals.begin(), signals.end(), signalLess);
    for (const Signal &signal : signals) {
        row.signals.push_back(GenSignal{signal});
    }
    return row;
}

bool signalOk(const Signal &signal, std::string &error) {
    if (signal.bitLength() == 0) {
        error = "signal " + signal.name() + " has zero length";
        return false;
    }
    if (signal.bitLength() > 64) {
        if ((signal.bitLength() % 8u) != 0 || (signal.startBit() % 8u) != 0) {
            error = "signal " + signal.name() + " length " + std::to_string(signal.bitLength()) +
                    " is wider than 64 bits and is not a whole number of aligned bytes";
            return false;
        }
    }
    return true;
}

struct Cluster {
    const Message *representative;
    std::vector<const Message *> members;
};

bool collapseGroup(const std::vector<const Message *> &group, std::vector<GenMessage> &rows, std::string &error) {
    std::vector<Cluster> clusters;
    for (const Message *message : group) {
        bool placed = false;
        for (Cluster &cluster : clusters) {
            if (sameMessageLayout(*cluster.representative, *message)) {
                cluster.members.push_back(message);
                placed = true;
                break;
            }
        }
        if (!placed) {
            clusters.push_back(Cluster{message, {message}});
        }
    }
    if (clusters.size() == 1) {
        rows.push_back(makeRow(*clusters[0].representative, chooseGenericName(clusters[0].members), true));
        return true;
    }

    const Message *generic = nullptr;
    for (const Cluster &cluster : clusters) {
        for (const Message *message : cluster.members) {
            if (!message->defaultMark()) {
                continue;
            }
            if (generic != nullptr && !sameMessageLayout(*generic, *message)) {
                error = "two generic rows for PGN 0x" + ([&]() {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%X", message->pgn());
                    return std::string(buf);
                })() + " are not identical";
                return false;
            }
            if (generic == nullptr) {
                generic = message;
            }
        }
    }
    if (generic != nullptr) {
        std::vector<const Message *> named;
        for (const Cluster &cluster : clusters) {
            if (!sameMessageLayout(*cluster.representative, *generic)) {
                continue;
            }
            named.insert(named.end(), cluster.members.begin(), cluster.members.end());
        }
        rows.push_back(makeRow(*generic, chooseGenericName(named), true));
    }

    std::map<uint8_t, const Message *> by_sa;
    for (const Cluster &cluster : clusters) {
        if (generic != nullptr && sameMessageLayout(*cluster.representative, *generic)) {
            continue;
        }
        for (const Message *message : cluster.members) {
            auto it = by_sa.find(message->sa());
            if (it != by_sa.end()) {
                if (!sameMessageLayout(*it->second, *message)) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%X", message->pgn());
                    error = "PGN 0x" + std::string(buf) + " has two layouts for source address " +
                            std::to_string(message->sa());
                    return false;
                }
                continue;
            }
            by_sa.emplace(message->sa(), message);
            rows.push_back(makeRow(*message, message->name(), false));
        }
    }
    return true;
}

bool buildRows(const std::map<uint32_t, Message> &messages, std::vector<GenMessage> &rows, std::string &error) {
    std::map<GroupKey, std::vector<const Message *>> groups;
    for (const auto &pair : messages) {
        const Message &message = pair.second;
        if (message.dlc() == 0) {
            continue;
        }
        for (const Signal &signal : message.allSignals()) {
            if (!signalOk(signal, error)) {
                return false;
            }
        }
        GroupKey key;
        key.extended = isExtendedId(message) ? 1u : 0u;
        key.key = key.extended ? message.pgn() : (message.canId() & 0x7FFu);
        groups[key].push_back(&message);
    }
    for (const auto &group : groups) {
        if (!collapseGroup(group.second, rows, error)) {
            return false;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const GenMessage &a, const GenMessage &b) {
        if (a.extended != b.extended) {
            return a.extended < b.extended;
        }
        uint32_t ka = a.extended ? a.pgn : a.can_id;
        uint32_t kb = b.extended ? b.pgn : b.can_id;
        if (ka != kb) {
            return ka < kb;
        }
        if (a.sa_mask != b.sa_mask) {
            return a.sa_mask < b.sa_mask;
        }
        return a.sa < b.sa;
    });
    return true;
}

void emitBytes(std::ostream &out, const std::string &bytes) {
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i % 16u == 0) {
            out << "    ";
        }
        char buf[8];
        std::snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned char>(bytes[i]));
        out << buf;
        if (i + 1 != bytes.size()) {
            out << ",";
        }
        if (i % 16u == 15u || i + 1 == bytes.size()) {
            out << "\n";
        } else {
            out << " ";
        }
    }
}

std::string hex32(uint32_t value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%X", value);
    return buf;
}

}  // namespace

bool DbcParser::generateCOutput(std::ostream &c_out, std::ostream &h_out, bool no_strings, std::string &error) const {
    if (!portable_error_.empty()) { error = portable_error_; return false; }
    // Validate before PGN/layout collapse or writing either output stream.
    for (const auto &entry : raw_messages_) {
        if (!entry.second.validatePortableMultiplexing(error)) return false;
        for (const auto &signal : entry.second.signalList()) {
            for (const auto &item : {std::make_pair("scale", signal.scaleLiteral()),
                                    std::make_pair("offset", signal.offsetLiteral()),
                                    std::make_pair("minimum", signal.minLiteral()),
                                    std::make_pair("maximum", signal.maxLiteral())}) {
                if (dbcCNumber(item.second).empty()) {
                    error = "invalid portable decimal: " + entry.second.name() + "." + signal.name() + " " + item.first;
                    return false;
                }
            }
        }
    }
    std::vector<GenMessage> rows;
    if (!buildRows(raw_messages_, rows, error)) {
        return false;
    }
    if (rows.size() > 0xFFFFu) {
        error = "too many messages for a uint16 index";
        return false;
    }

    std::string symbol = cIdent(name_.empty() ? "j1939" : name_);
    StringBlob blob;
    struct FlatEnum {
        uint64_t value;
        uint32_t name_off;
    };
    std::vector<FlatEnum> enums;
    struct FlatSignal {
        const GenSignal *signal;
        uint32_t name_off;
        uint16_t enum_begin;
        uint16_t enum_count;
        uint16_t message_index;
        uint16_t signal_index;
    };
    std::vector<FlatSignal> flat;
    std::vector<uint16_t> signal_begin(rows.size());
    std::vector<uint16_t> signal_count(rows.size());

    for (size_t m = 0; m < rows.size(); m++) {
        if (flat.size() > 0xFFFFu) {
            error = "too many signals for a uint16 index";
            return false;
        }
        signal_begin[m] = static_cast<uint16_t>(flat.size());
        for (size_t s = 0; s < rows[m].signals.size(); s++) {
            const Signal &signal = rows[m].signals[s].signal;
            if (enums.size() > 0xFFFFu || flat.size() > 0xFFFFu) {
                error = "too many signals for a uint16 index";
                return false;
            }
            FlatSignal item;
            item.signal = &rows[m].signals[s];
            item.name_off = no_strings ? J1939_NO_NAME : blob.intern(signal.name());
            item.enum_begin = static_cast<uint16_t>(enums.size());
            item.enum_count = static_cast<uint16_t>(signal.enums().size());
            item.message_index = static_cast<uint16_t>(m);
            item.signal_index = static_cast<uint16_t>(s);
            for (const auto &en : signal.enums()) {
                FlatEnum e;
                e.value = en.first;
                e.name_off = no_strings ? J1939_NO_NAME : blob.intern(enumLabel(en.second));
                enums.push_back(e);
            }
            flat.push_back(item);
        }
        signal_count[m] = static_cast<uint16_t>(rows[m].signals.size());
    }

    std::vector<uint32_t> message_name_off(rows.size(), J1939_NO_NAME);
    if (!no_strings) {
        for (size_t m = 0; m < rows.size(); m++) {
            message_name_off[m] = blob.intern(rows[m].name);
        }
    }

    struct SpnRef {
        uint32_t spn;
        uint16_t message_index;
        uint16_t signal_index;
    };
    std::vector<SpnRef> spns;
    struct NameRef {
        uint32_t name_off;
        uint16_t message_index;
        uint16_t signal_index;
        std::string name;
    };
    std::vector<NameRef> names;
    for (const FlatSignal &item : flat) {
        if (item.signal->signal.spn() != 0) {
            spns.push_back(SpnRef{item.signal->signal.spn(), item.message_index, item.signal_index});
        }
        if (!no_strings) {
            names.push_back(NameRef{item.name_off, item.message_index, item.signal_index, item.signal->signal.name()});
        }
    }
    std::sort(spns.begin(), spns.end(), [](const SpnRef &a, const SpnRef &b) {
        if (a.spn != b.spn) {
            return a.spn < b.spn;
        }
        if (a.message_index != b.message_index) {
            return a.message_index < b.message_index;
        }
        return a.signal_index < b.signal_index;
    });
    std::sort(names.begin(), names.end(), [](const NameRef &a, const NameRef &b) {
        int cmp = a.name.compare(b.name);
        if (cmp != 0) {
            return cmp < 0;
        }
        if (a.message_index != b.message_index) {
            return a.message_index < b.message_index;
        }
        return a.signal_index < b.signal_index;
    });
    if (spns.size() > 0xFFFFu || names.size() > 0xFFFFu || enums.size() > 0xFFFFu) {
        error = "too many index entries for a uint16 index";
        return false;
    }

    std::ostringstream c;
    std::ostringstream h;
    std::string guard = cIdent(symbol + "_db_h");
    for (char &ch : guard) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    // j1939_db.h already uses J1939_DB_H. A database named "j1939" would skip that header.
    if (guard == "J1939_DB_H") {
        guard = "J1939_DATABASE_H";
    }

    c << "/* Generated from DBC. Do not edit. */\n";
    c << "#include \"j1939_db.h\"\n\n";
    if (!no_strings) {
        c << "static const char " << symbol << "_strings[] = {\n";
        if (blob.bytes().empty()) {
            c << "    0\n";
        } else {
            emitBytes(c, blob.bytes());
        }
        c << "};\n\n";
    }
    if (!enums.empty()) {
        c << "static const j1939_enum_t " << symbol << "_enums[] = {\n";
        for (const FlatEnum &en : enums) {
            // `u` fits every value that fits in uint32_t. A wider wire pattern needs ull
            // so a 32-bit compiler accepts the literal.
            const char *suffix = en.value > 0xFFFFFFFFull ? "ull" : "u";
            c << "    { " << en.value << suffix << ", "
              << (no_strings ? "J1939_NO_NAME" : std::to_string(en.name_off) + "u") << " },\n";
        }
        c << "};\n\n";
    }
    if (!flat.empty()) {
        c << "static const j1939_signal_t " << symbol << "_signals[] = {\n";
        for (const FlatSignal &item : flat) {
            const Signal &signal = item.signal->signal;
            c << "    { /* " << signal.name() << " */\n";
            c << "        " << (no_strings ? "J1939_NO_NAME" : std::to_string(item.name_off) + "u") << ", ";
            c << signal.spn() << "u, ";
            c << signal.startBit() << "u, " << signal.bitLength() << "u, ";
            c << item.enum_begin << "u, " << item.enum_count << "u, ";
            c << signal.muxValue() << "u, " << flagExpr(signal) << ", ";
            c << emitNumber(signal.scaleLiteral()) << ", ";
            c << emitNumber(signal.offsetLiteral()) << ", ";
            c << emitNumber(signal.minLiteral()) << ", ";
            c << emitNumber(signal.maxLiteral()) << " },\n";
        }
        c << "};\n\n";
    }
    if (!rows.empty()) {
        c << "static const j1939_message_t " << symbol << "_messages[] = {\n";
        for (size_t m = 0; m < rows.size(); m++) {
            const GenMessage &row = rows[m];
            c << "    { /* " << row.name << " */\n";
            c << "        " << (no_strings ? "J1939_NO_NAME" : std::to_string(message_name_off[m]) + "u") << ", ";
            c << hex32(row.pgn) << "u, " << hex32(row.can_id) << "u, " << hex32(row.sa_mask) << "u, ";
            c << static_cast<unsigned>(row.sa) << "u, " << static_cast<unsigned>(row.extended) << "u, ";
            c << row.dlc << "u, " << signal_begin[m] << "u, " << signal_count[m] << "u },\n";
        }
        c << "};\n\n";
    }
    if (!spns.empty()) {
        c << "static const j1939_spn_ref_t " << symbol << "_spn_index[] = {\n";
        for (const SpnRef &ref : spns) {
            c << "    { " << ref.spn << "u, " << ref.message_index << "u, " << ref.signal_index << "u },\n";
        }
        c << "};\n\n";
    }
    if (!names.empty()) {
        c << "static const j1939_name_ref_t " << symbol << "_name_index[] = {\n";
        for (const NameRef &ref : names) {
            c << "    { " << ref.name_off << "u, " << ref.message_index << "u, " << ref.signal_index << "u },\n";
        }
        c << "};\n\n";
    }

    auto ptr = [&](bool present, const std::string &array) {
        return present ? array : std::string("0");
    };
    c << "const j1939_db_t " << symbol << "_db = {\n";
    c << "    " << ptr(!no_strings, symbol + "_strings") << ",\n";
    c << "    " << ptr(!rows.empty(), symbol + "_messages") << ",\n";
    c << "    " << rows.size() << "u,\n";
    c << "    " << ptr(!flat.empty(), symbol + "_signals") << ",\n";
    c << "    " << flat.size() << "u,\n";
    c << "    " << ptr(!enums.empty(), symbol + "_enums") << ",\n";
    c << "    " << enums.size() << "u,\n";
    c << "    " << ptr(!spns.empty(), symbol + "_spn_index") << ",\n";
    c << "    " << spns.size() << "u,\n";
    c << "    " << ptr(!names.empty(), symbol + "_name_index") << ",\n";
    c << "    " << names.size() << "u\n";
    c << "};\n";

    h << "/* Generated from DBC. Do not edit. */\n";
    h << "#ifndef " << guard << "\n";
    h << "#define " << guard << "\n\n";
    h << "#include \"j1939_db.h\"\n\n";
    h << "extern const j1939_db_t " << symbol << "_db;\n\n";
    std::set<std::string> defines;
    for (const GenMessage &row : rows) {
        std::string id = uniqueIdent(defines, cIdent(row.name) + (row.extended ? "_PGN" : "_CAN_ID"));
        h << "#define " << id << " " << hex32(row.extended ? row.pgn : row.can_id) << "u\n";
        if (row.message_spn != 0) {
            std::string spn_id = uniqueIdent(defines, cIdent(row.name) + "_SPN");
            h << "#define " << spn_id << " " << row.message_spn << "u\n";
        }
        for (const GenSignal &gs : row.signals) {
            if (gs.signal.spn() == 0) {
                continue;
            }
            std::string spn_id = uniqueIdent(defines, cIdent(row.name) + "_" + cIdent(gs.signal.name()) + "_SPN");
            h << "#define " << spn_id << " " << gs.signal.spn() << "u\n";
        }
    }
    h << "\n#endif /* " << guard << " */\n";

    c_out << c.str();
    h_out << h.str();
    return true;
}
