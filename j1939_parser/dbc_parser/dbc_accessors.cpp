#include "dbc_parser.h"
#include "dbc_number.h"

#include <cctype>
#include <sstream>
#include <set>

namespace {
std::string identifier(const std::string &text) {
    std::string result;
    for (unsigned char c : text) result += std::isalnum(c) ? char(c) : '_';
    return result;
}
}

bool DbcParser::generateAccessorOutput(std::ostream &out, std::string &error) const {
    if (!portable_error_.empty()) { error = portable_error_; return false; }
    std::ostringstream code;
    code << "/* Generated portable payload accessors. No bitfield ABI or runtime library.\n"
         << " * Caller supplies non-null buffers of at least the message DLC; no length checks.\n"
         << " * Caller selects active multiplex alternatives. Byte-copy buffers must not overlap. */\n"
         << "#include <stdint.h>\n#include <string.h>\n";
    for (const auto &entry : raw_messages_) {
        const auto &msg = entry.second;
        if (!msg.validatePortableMultiplexing(error)) return false;

        const std::string prefix = "j1939_" + identifier(msg.name()) + "_" + std::to_string(entry.first);
        const std::string select = "J1939_ACCESSORS_" + identifier(msg.name()) + "_" + std::to_string(entry.first);
        code << "\n#if !defined(J1939_ACCESSORS_SELECT) || defined(" << select << ")\n"
             << "#ifndef " << select << "_DEFINED\n#define " << select << "_DEFINED\n"
             << "#define " << prefix << "_CAN_ID " << entry.first << "u\n"
             << "#define " << prefix << "_DLC " << msg.dlc() << "u\n";
        std::set<std::string> names;
        // Reserve the complete API before emitting labels, including functions
        // belonging to later signals and message-selection macros.
        std::set<std::string> symbols{prefix + "_CAN_ID", prefix + "_DLC", select, select + "_DEFINED"};
        for (const auto &sig : msg.signalList()) {
            const std::string base = prefix + "_" + identifier(sig.name());
            symbols.insert(base);
            for (const auto &suffix : {"_raw", "_store_raw", "_phys", "_get_bytes", "_set_bytes"})
                symbols.insert(base + suffix);
        }
        for (const auto &sig : msg.signalList()) {
            std::string name = prefix + "_" + identifier(sig.name());
            if (!names.insert(name).second) {
                error = "accessor identifier collision: " + name;
                return false;
            }
            std::vector<unsigned> bits;
            int bit = sig.startBit();
            for (unsigned i = 0; i < sig.bitLength(); ++i) {
                if (bit < 0 || bit >= msg.dlc() * 8) {
                    error = "accessor signal outside payload: " + sig.name();
                    return false;
                }
                bits.push_back(static_cast<unsigned>(bit));
                bit = sig.little_endian() ? bit + 1 : (bit % 8 == 0 ? bit + 15 : bit - 1);
            }
            if (bits.empty()) {
                error = "accessor signal has zero length: " + sig.name();
                return false;
            }
            if (bits.size() > 64) {
                if (!sig.little_endian() || sig.startBit() % 8 || bits.size() % 8) {
                    error = "accessor byte string must be byte aligned and little endian: " + sig.name();
                    return false;
                }
                code << "static inline void " << name << "_get_bytes(const uint8_t *payload, uint8_t *value) { memcpy(value, payload + "
                     << sig.startBit() / 8 << ", " << bits.size() / 8 << "); }\n"
                     << "static inline void " << name << "_set_bytes(uint8_t *payload, const uint8_t *value) { memcpy(payload + "
                     << sig.startBit() / 8 << ", value, " << bits.size() / 8 << "); }\n";
                continue;
            }
            code << "static inline uint64_t " << name << "_raw(const uint8_t *payload) {\n    uint64_t value = 0;\n";
            for (unsigned i = 0; i < bits.size(); ++i) {
                const unsigned shift = sig.little_endian() ? i : bits.size() - 1 - i;
                code << "    value |= (uint64_t)((payload[" << bits[i] / 8 << "] >> " << bits[i] % 8
                     << ") & 1u) << " << shift << ";\n";
            }
            code << "    return value;\n}\n"
                 << "static inline void " << name << "_store_raw(uint8_t *payload, uint64_t value) {\n";
            for (unsigned i = 0; i < bits.size(); ++i) {
                const unsigned shift = sig.little_endian() ? i : bits.size() - 1 - i;
                code << "    payload[" << bits[i] / 8 << "] = (uint8_t)((payload[" << bits[i] / 8 << "] & "
                     << (255u ^ (1u << (bits[i] % 8))) << "u) | (((value >> " << shift << ") & 1u) << " << bits[i] % 8 << "));\n";
            }
            code << "}\nstatic inline double " << name << "_phys(const uint8_t *payload) {\n    uint64_t raw = " << name << "_raw(payload);\n";
            if (sig.isSigned()) {
                code << "    double value = (raw & (UINT64_C(1) << " << bits.size() - 1
                     << ")) ? (double)(-1 - (int64_t)(~raw & UINT64_C(" << make_mask(bits.size())
                     << "))) : (double)raw;\n";
            } else {
                code << "    double value = (double)raw;\n";
            }
            const auto scale = dbcCNumber(sig.scaleLiteral()), offset = dbcCNumber(sig.offsetLiteral());
            if (scale.empty() || offset.empty()) { error = "invalid accessor decimal: " + sig.name(); return false; }
            code << "    return value * (" << scale << ") + (" << offset << ");\n}\n";
            std::set<std::string> labels;
            for (const auto &label : sig.enums()) {
                std::string id = name + "_" + identifier(label.second.substr(1, label.second.size() - 2));
                if (!labels.insert(id).second) id += "_" + std::to_string(label.first);
                while (!symbols.insert(id).second) id += "_" + std::to_string(label.first);
                code << "#define " << id << " UINT64_C(" << label.first << ")\n";
            }
        }
        code << "#endif\n#endif\n";
    }
    out << code.str();
    if (!out) { error = "accessor output stream failed"; return false; }
    return true;
}
