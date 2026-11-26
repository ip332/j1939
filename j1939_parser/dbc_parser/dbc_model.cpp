// Strict model-v1 parsing and deterministic host export. Uses the same Message,
// Signal and VAL_ model as the target code generators; no Quarry dependencies.
#include "dbc_parser.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <regex>
#include <set>
#include <stdexcept>

namespace {
std::string trim(const std::string &s) {
    const auto first = s.find_first_not_of(" \t\r");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

uint64_t integer(const std::string &s, uint64_t maximum) {
    size_t used = 0;
    const uint64_t value = std::stoull(s, &used);
    if (used != s.size() || value > maximum) throw std::invalid_argument("integer outside supported range: " + s);
    return value;
}

double decimal(const std::string &s) {
    std::istringstream input(s);
    input.imbue(std::locale::classic());
    double value;
    if (!(input >> value) || !input.eof() || !std::isfinite(value))
        throw std::invalid_argument("decimal outside finite host double range: " + s);
    return value;
}

// A provenance checksum, explicitly not a cryptographic identity or schema ID.
std::string fingerprint(const std::string &bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char c : bytes) { hash ^= c; hash *= UINT64_C(1099511628211); }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string textString(const std::string &s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 32 || c >= 127) out << '\\' << std::oct << std::setw(3) << std::setfill('0') << unsigned(c);
        else out << char(c);
    }
    out << '"';
    return out.str();
}


std::vector<std::string> words(const std::string &text, char delimiter = ' ') {
    std::vector<std::string> result;
    std::istringstream stream(text);
    std::string word;
    while (std::getline(stream, word, delimiter)) {
        word = trim(word);
        if (!word.empty()) result.push_back(word);
    }
    return result;
}
}

bool DbcParser::parseModelFile(const std::string &path) {
    // A failure poisons this export session. Do not publish a partially parsed model.
    if (model_failed_) return false;
    size_t line_number = 0;
    const auto slash = path.find_last_of("/\\");
    const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    // Preserve literals without allocating a temporary string on error paths.
    const auto fail = [&](const auto &why) {
        model_failed_ = true;
        std::cerr << path << ':' << line_number << ": model-v1: " << why << '\n';
        return false;
    };
    if (!raw_messages_.empty() && model_sources_.empty()) return fail("enable model export before parsing any files");
    if (model_normalized_) return fail("cannot parse after PGN normalization");
    if (!std::regex_match(name, std::regex("[A-Za-z0-9_.-]+\\.dbc")))
        return fail("source basename must be an ASCII logical name ending in .dbc");
    if (model_sources_.count(name)) return fail("duplicate source logical name: " + name);
    std::ifstream input(path, std::ios::binary);
    if (!input) return fail("cannot open source");
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) return fail("source read failed");
    ModelSource source;
    source.name = name;
    source.bytes = bytes.size();
    source.fingerprint = fingerprint(bytes);

    const std::string id = "[A-Za-z_][A-Za-z0-9_]*";
    const std::string num = "[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?";
    const std::regex bo("BO_ ([0-9]+) (" + id + "): ([0-9]+) (" + id + ")");
    const std::regex sg("SG_ (" + id + ")(?: (M|m[0-9]+))? : ([0-9]+)\\|([0-9]+)@([01])([+-]) \\((" +
        num + "),(" + num + ")\\) \\[(" + num + ")\\|(" + num + ")\\] \"([^\"\\\\]*)\" (" + id + "(?:," + id + ")*)");
    const std::regex val("VAL_ ([0-9]+) (" + id + ") (.+) ;");
    const std::regex pair("([0-9]+) \"([^\"\\\\]*)\"(?: |$)");
    const std::regex spn("BA_ \"SPN\" (BO_|SG_) ([0-9]+)(?: (" + id + "))? ([0-9]+);");
    const std::regex def("BA_ \"J1939Default\" BO_ ([0-9]+) ([01]);");
    const std::regex version("VERSION \"([^\"\\\\]*)\"");
    std::set<uint32_t> local_messages;
    std::set<std::pair<uint32_t, std::string>> enum_records;
    uint32_t current = 0;
    bool in_message = false, in_namespace = false, has_namespace = false, has_nodes = false, has_bs = false;
    std::istringstream lines(bytes);
    std::string line;
    try {
        while (std::getline(lines, line)) {
            ++line_number;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            for (unsigned char c : line)
                if ((c < 32 && c != '\t') || c >= 127)
                    return fail("only printable ASCII, tabs and CR/LF are supported");
            // Normalize whitespace outside quoted strings only, for the shared legacy constructors.
            std::string normalized;
            bool quoted = false, space = false;
            for (char c : trim(line)) {
                if (c == '"') quoted = !quoted;
                if (!quoted && (c == ' ' || c == '\t')) { space = true; continue; }
                if (space && !normalized.empty()) normalized += ' ';
                normalized += c;
                space = false;
            }
            line = normalized;
            if (line.empty()) { in_namespace = false; continue; }
            std::smatch match;
            if (line == "NS_ :") {
                if (has_namespace) return fail("duplicate NS_ header");
                has_namespace = in_namespace = true;
                in_message = false;
                continue;
            }
            if (in_namespace && std::regex_match(line, std::regex(id))) {
                source.namespace_symbols.push_back(line);
                continue;
            }
            in_namespace = false;
            if (std::regex_match(line, match, version)) {
                if (source.has_version) return fail("duplicate VERSION");
                source.has_version = true; source.dbc_version = match[1]; in_message = false;
            } else if (line == "BS_:") {
                if (has_bs) return fail("duplicate BS_ header");
                has_bs = true; in_message = false;
            } else if (line == "BU_:" || line.compare(0, 5, "BU_: ") == 0) {
                if (has_nodes) return fail("duplicate BU_ header");
                has_nodes = true; in_message = false;
                source.nodes = words(line.substr(4));
                std::set<std::string> seen;
                for (const auto &node : source.nodes)
                    if (!std::regex_match(node, std::regex(id)) || !seen.insert(node).second)
                        return fail("invalid or duplicate node: " + node);
            } else if (std::regex_match(line, match, bo)) {
                const auto dbc_id = static_cast<uint32_t>(integer(match[1], UINT32_MAX));
                const bool extended = (dbc_id & UINT32_C(0x80000000)) != 0;
                if ((!extended && dbc_id > 0x7ff) || (extended && (dbc_id & 0x60000000u)))
                    return fail("invalid CAN ID or missing extended flag: " + match[1].str());
                if (extended && (dbc_id & 0x02000000u)) return fail("J1939 reserved ID bit is unsupported");
                const auto dlc = integer(match[3], 8);
                if (!dlc) return fail("message DLC must be 1..8: " + match[2].str());
                if (raw_messages_.count(dbc_id)) return fail("duplicate message CAN ID: " + match[1].str());
                for (const auto &entry : raw_messages_)
                    if (entry.second.name() == match[2]) return fail("duplicate message name: " + match[2].str());
                raw_messages_.emplace(dbc_id, Message(line));
                model_messages_[dbc_id] = {name, line_number, match[4]};
                local_messages.insert(dbc_id);
                current = dbc_id; in_message = true;
            } else if (std::regex_match(line, match, sg)) {
                if (!in_message) return fail("SG_ requires a preceding BO_");
                Message &message = raw_messages_.at(current);
                const std::string signal_name = match[1];
                if (message.findSignal(signal_name)) return fail("duplicate signal: " + signal_name);
                integer(match[3], 63); integer(match[4], 64);
                if (match[2].str().size() > 1) integer(match[2].str().substr(1), UINT16_MAX);
                for (size_t i = 7; i <= 10; ++i) decimal(match[i]);
                if (decimal(match[7]) == 0 || decimal(match[9]) > decimal(match[10]))
                    return fail("zero factor or reversed range: " + signal_name);
                if (!message.addSignal(" " + line, true)) return fail("invalid signal " + signal_name + ": " + message.signalError());
                model_signals_[{current, signal_name}] = {name, line_number, match[12]};
            } else if (std::regex_match(line, match, val)) {
                in_message = false;
                const auto dbc_id = static_cast<uint32_t>(integer(match[1], UINT32_MAX));
                if (!local_messages.count(dbc_id)) return fail("VAL_ must reference a message in this source");
                Message &message = raw_messages_.at(dbc_id);
                Signal *signal = message.findSignal(match[2]);
                if (!signal) return fail("VAL_ references unknown signal: " + match[2].str());
                if (!enum_records.insert({dbc_id, match[2]}).second) return fail("duplicate VAL_ record: " + match[2].str());
                std::string remaining = match[3];
                std::vector<std::string> fields{"VAL_", match[1], match[2]};
                std::set<uint64_t> values;
                while (!remaining.empty()) {
                    std::smatch item;
                    if (!std::regex_search(remaining, item, pair, std::regex_constants::match_continuous))
                        return fail("unsupported or malformed VAL_ entries: " + match[2].str());
                    const uint64_t value = integer(item[1], UINT64_MAX);
                    if ((signal->bitLength() < 64 && value >= (UINT64_C(1) << signal->bitLength())) || !values.insert(value).second)
                        return fail("duplicate or out-of-width enum value: " + item[1].str());
                    fields.push_back(item[1]); fields.push_back('"' + item[2].str() + '"');
                    remaining = item.suffix();
                }
                if (!message.addValues(fields)) return fail(message.signalError());
            } else if (std::regex_match(line, match, spn)) {
                in_message = false;
                const auto dbc_id = static_cast<uint32_t>(integer(match[2], UINT32_MAX));
                if (!local_messages.count(dbc_id)) return fail("SPN must reference a message in this source");
                Message &message = raw_messages_.at(dbc_id);
                const auto value = static_cast<uint32_t>(integer(match[4], UINT32_MAX));
                if (match[1] == "BO_" && !match[3].matched) {
                    if (message.hasMessageSpn()) return fail("duplicate message SPN");
                    message.setMessageSpn(value);
                } else if (match[1] == "SG_" && match[3].matched) {
                    Signal *signal = message.findSignal(match[3]);
                    if (!signal) return fail("SPN references unknown signal: " + match[3].str());
                    if (signal->hasSpn()) return fail("duplicate signal SPN: " + match[3].str());
                    signal->setSpn(value);
                } else return fail("malformed SPN attribute");
            } else if (std::regex_match(line, match, def)) {
                in_message = false;
                const auto dbc_id = static_cast<uint32_t>(integer(match[1], UINT32_MAX));
                if (!local_messages.count(dbc_id)) return fail("J1939Default must reference a message in this source");
                Message &message = raw_messages_.at(dbc_id);
                if (message.hasDefaultMark()) return fail("duplicate J1939Default attribute");
                message.markDefault(match[2] == "1");
            } else return fail("unsupported or malformed declaration: " + line);
        }
        source.has_nodes = has_nodes;
        source.has_namespace = has_namespace;
        std::sort(source.nodes.begin(), source.nodes.end());
        std::sort(source.namespace_symbols.begin(), source.namespace_symbols.end());
        model_sources_.emplace(name, source);
        std::string error;
        if (!validateModel(error)) return fail(error);
    } catch (const std::exception &e) { return fail(e.what()); }
    return true;
}

bool DbcParser::validateModel(std::string &error) const {
    for (const auto &entry : raw_messages_) {
        const Message &message = entry.second;
        const auto &location = model_messages_.at(entry.first);
        const auto bad = [&](const char *why, const Signal *signal = nullptr) {
            const auto &where = signal ? model_signals_.at({entry.first, signal->name()}) : location;
            error = where.source + ':' + std::to_string(where.line) + ": message " + message.name() +
                (signal ? " signal " + signal->name() : "") + ": " + why;
            return false;
        };
        const auto &signals = message.signalList();
        if (signals.empty()) return bad("message has no signals");
        const Signal *sw = nullptr;
        for (const auto &signal : signals) if (signal.isMultiplexor()) {
            if (sw || signal.isMultiplexed() || signal.isSigned()) return bad("requires one unsigned non-nested mux switch", &signal);
            sw = &signal;
        }
        std::vector<std::set<unsigned>> bits;
        for (const auto &signal : signals) {
            if (!signal.bitLength()) return bad("zero-width signal", &signal);
            if (signal.isMultiplexed() && (!sw || (sw->bitLength() < 16 && signal.muxValue() >= (1u << sw->bitLength()))))
                return bad("missing switch or unrepresentable mux value", &signal);
            std::set<unsigned> positions;
            unsigned bit = signal.startBit();
            for (unsigned i = 0; i < signal.bitLength(); ++i) {
                if (bit >= message.dlc() * 8u) return bad("signal exceeds message DLC", &signal);
                positions.insert(bit);
                bit = signal.little_endian() ? bit + 1 : (bit % 8 == 0 ? bit + 15 : bit - 1);
            }
            bits.push_back(std::move(positions));
        }
        for (size_t i = 0; i < signals.size(); ++i) for (size_t j = 0; j < i; ++j) {
            if (signals[i].isMultiplexed() && signals[j].isMultiplexed() && signals[i].muxValue() != signals[j].muxValue()) continue;
            for (unsigned bit : bits[i]) if (bits[j].count(bit)) return bad("overlapping simultaneously active signals", &signals[i]);
        }
    }
    return true;
}

bool DbcParser::generateModelOutput(std::ostream &out, std::string &error) const {
    if (!model_mode_ || model_failed_ || model_sources_.empty() || model_normalized_) {
        error = "model-v1 requires successful strict parsing before PGN normalization";
        return false;
    }
    if (raw_messages_.empty()) { error = "model-v1 requires at least one message"; return false; }
    if (!validateModel(error)) return false;
    std::ostringstream code;
    code.imbue(std::locale::classic());
    code << "format: \"j1939.dbc-model\"\nversion: 1\n";
    for (const auto &entry : model_sources_) {
        const auto &s = entry.second;
        code << "sources {\n  name: " << textString(s.name) << "\n  byte_length: " << s.bytes
             << "\n  fnv1a64: " << textString(s.fingerprint) << '\n';
        if (s.has_version) code << "  dbc_version: " << textString(s.dbc_version) << '\n';
        if (s.has_nodes) {
            code << "  nodes {\n";
            for (const auto &node : s.nodes) code << "    values: " << textString(node) << '\n';
            code << "  }\n";
        }
        if (s.has_namespace) {
            code << "  namespace_symbols {\n";
            for (const auto &symbol : s.namespace_symbols) code << "    values: " << textString(symbol) << '\n';
            code << "  }\n";
        }
        code << "}\n";
    }
    for (const auto &entry : raw_messages_) {
        const Message &m = entry.second;
        const auto &loc = model_messages_.at(entry.first);
        code << "messages {\n  dbc_id: " << m.canId() << "\n  can_id: " << (m.canId() & 0x1fffffffu)
             << "\n  extended: " << (m.extended() ? "true" : "false") << "\n  name: " << textString(m.name())
             << "\n  dlc: " << m.dlc() << '\n';
        if (m.extended()) code << "  pgn: " << m.pgn() << "\n  source_address: " << unsigned(m.sa()) << '\n';
        if (m.hasMessageSpn()) code << "  spn: " << m.messageSpn() << '\n';
        if (m.hasDefaultMark()) code << "  j1939_default: " << (m.defaultMark() ? "true" : "false") << '\n';
        code << "  source: " << textString(loc.source) << "\n  line: " << loc.line << "\n  transmitter: " << textString(loc.nodes) << '\n';
        auto signals = m.signalList();
        std::sort(signals.begin(), signals.end(), [](const Signal &a, const Signal &b) {
            if (a.startBit() != b.startBit()) return a.startBit() < b.startBit();
            if (a.muxValue() != b.muxValue()) return a.muxValue() < b.muxValue();
            return a.name() < b.name();
        });
        std::string switch_name;
        for (const auto &s : signals) if (s.isMultiplexor()) switch_name = s.name();
        for (const auto &s : signals) {
            const auto &sloc = model_signals_.at({entry.first, s.name()});
            code << "  signals {\n    name: " << textString(s.name()) << "\n    start_bit: " << s.startBit()
                 << "\n    width: " << s.bitLength() << "\n    byte_order: " << (s.little_endian() ? "INTEL" : "MOTOROLA")
                 << "\n    is_signed: " << (s.isSigned() ? "true" : "false")
                 << "\n    scale: " << textString(s.scaleLiteral()) << "\n    offset: " << textString(s.offsetLiteral())
                 << "\n    minimum: " << textString(s.minLiteral()) << "\n    maximum: " << textString(s.maxLiteral())
                 << "\n    unit: " << textString(s.unitsLiteral().substr(1, s.unitsLiteral().size() - 2)) << '\n';
            if (s.hasSpn()) code << "    spn: " << s.spn() << '\n';
            code << "    source: " << textString(sloc.source) << "\n    line: " << sloc.line << '\n';
            for (const auto &receiver : words(sloc.nodes, ',')) code << "    receivers: " << textString(receiver) << '\n';
            if (s.isMultiplexor()) code << "    multiplex { role: SWITCH }\n";
            else if (s.isMultiplexed()) code << "    multiplex { role: BRANCH switch_name: " << textString(switch_name) << " value: " << s.muxValue() << " }\n";
            for (const auto &e : s.enums())
                code << "    enums { raw: " << e.first << " label: " << textString(e.second.substr(1, e.second.size() - 2)) << " }\n";
            code << "  }\n";
        }
        code << "}\n";
    }
    out << code.str();
    if (!out) { error = "model-v1 output stream failed"; return false; }
    return true;
}
