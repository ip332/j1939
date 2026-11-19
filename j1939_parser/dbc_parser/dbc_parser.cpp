//
// Created by Igor on 2021-09-10.
//

#include "dbc_parser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

bool DbcParser::parseFile(const std::string &dbc_path) {
    top_level_ = true;
    last_message_.reset(nullptr);
    if (verbose_) {
        std::cout << dbc_path << std::endl;
    }
    auto end = dbc_path.rfind(".dbc");
    if (end == std::string::npos) {
        std::cerr << "Error: " << dbc_path << "is not a DBC file" << std::endl;
        return false;
    }
    // TODO: check the assumption that only Linux path will be used.
    auto start = dbc_path.rfind("/");
    if (start == std::string::npos) {
        dbc_name_ = dbc_path;
    } else {
        dbc_name_ = dbc_path.substr(start + 1, end - start - 1);
    }

    if (name_.empty()) {
        name_ = dbc_name_;
        std::replace( name_.begin(), name_.end(), '.', '_');
    }

    // Open file and start parsing.
    std::ifstream infile(dbc_path);
    if (!infile.is_open()) {
        std::cerr << "Error: could not open file " << dbc_path << std::endl;
        return false;
    } else {
        if (verbose_) {
            std::cout << "Loading dbc file " << dbc_path << std::endl;
        }
    }
    std::string line;
    while (std::getline(infile, line)) {
        if (line[line.size() - 1] == '\r') {
            line.pop_back();
        }

        // Empty line resets parsing mode to the top level.
        if (line.empty()) {
            top_level_ = true;
            storeLastMessage();
            continue;
        }
        top_level_ ? topLevelParser(line) : signalsParser(line);
    }
    storeLastMessage();
    if (attribute_error_) {
        std::cerr << attribute_error_text_ << std::endl;
        return false;
    }
    return true;
}

void DbcParser::storeLastMessage() {
    if (last_message_) {
        // Before storing the message, check if the same CAN ID already exists.
        auto it = raw_messages_.find(last_message_->canId());
        if (it != raw_messages_.end()) {
            if(it->second != *last_message_) {
                std::cerr << "Input file error: CAN ID " << std::hex << last_message_->canId() << ", message "
                          << last_message_->name() << " was already used for " << it->second.name() << std::endl;
                std::cerr << "Message: " << std::endl;
                std::cerr << last_message_->toString();
                std::cerr << "Previous: " << std::endl;
                std::cerr << it->second.toString();
            } else {
                //TODO: Store message id in some storage.
            }
        } else {
            // Store message.
            raw_messages_.emplace(last_message_->canId(), *last_message_);
            if (last_message_->extended()) {
                extended_ids_.push_back(last_message_->canId());
            }
        }
        last_message_.reset(nullptr);
    }
}
void DbcParser::removeDuplicates() {
    // Extended identity is the PGN, plus the source address only when that PGN has
    // more than one layout. Priority is ignored. One layout becomes the generic row.
    // Several layouts stay source-address specific, and a generic row is added only
    // when the BA_ "J1939Default" messages share a single layout. DLC does not choose it.
    std::map<uint32_t, std::vector<Message>> pgn_groups;
    for (const auto & id : extended_ids_) {
        auto msg = raw_messages_.find(id);
        if (msg->second.dlc() == 0) {
            // Ignore messages which do not represent a real CAN frame.
            raw_messages_.erase(msg);
            continue;
        }
        pgn_groups[msg->second.pgn()].push_back(msg->second);
        raw_messages_.erase(msg);
    }
    for (const auto & group : pgn_groups) {
        std::vector<std::vector<Message>> clusters;
        for (const Message &message : group.second) {
            bool placed = false;
            for (auto &cluster : clusters) {
                if (cluster.front() == message) {
                    cluster.push_back(message);
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                clusters.push_back(std::vector<Message>(1, message));
            }
        }
        const uint32_t pgn = group.first;
        // clusters.size() means there is no generic row.
        size_t generic_index = clusters.size();
        if (clusters.size() == 1) {
            generic_index = 0;
        } else {
            bool conflict = false;
            for (size_t i = 0; i < clusters.size(); ++i) {
                for (const auto &message : clusters[i]) {
                    if (!message.defaultMark()) {
                        continue;
                    }
                    if (generic_index == clusters.size()) {
                        generic_index = i;
                    } else if (!(clusters[generic_index].front() == clusters[i].front())) {
                        conflict = true;
                    }
                }
            }
            if (conflict) {
                generic_index = clusters.size();
            }
        }
        if (generic_index != clusters.size()) {
            raw_messages_.emplace(j1939PgnKey(pgn), clusters[generic_index].front().generalized());
        }
        if (clusters.size() == 1) {
            continue;
        }
        for (size_t i = 0; i < clusters.size(); ++i) {
            if (i == generic_index) {
                continue;
            }
            for (const auto &message : clusters[i]) {
                raw_messages_.emplace(j1939SaKey(pgn, message.sa()), message);
            }
        }
    }
}

void DbcParser::generatePgnOutput(std::ofstream *out) const{
    if (out == nullptr) {
        return;
    }
    *out << "// This file was generated from the DBC files. Do not edit!\n";
    *out << "#include \"j1939_frame.h\"\n";
    *out << "\nstd::map<uint32_t, PGN> " + name_ + "_dbc = {\n";
    for (const auto & msg : raw_messages_) {
        *out << "\t{ " << msg.first << msg.second.details() << ", ";
        *out << msg.second.toPgnString() << "},\n";
    }
    *out << "};\n\n";
}

void DbcParser::generateUnionOutput(std::ofstream *out) const {
    if (out == nullptr) {
        return;
    }
    *out << "\n// This file was generated from a DBC file. Do not edit!\n\n";
    std::string define(toSnakeCase(dbcName() + "_h"));
    std::transform(define.begin(), define.end(), define.begin(), ::toupper);
    *out << "#ifndef " << define << "\n";
    *out << "#define " << define << "\n\n";
    *out << "#include <stdint.h>\n";
    *out << "#include <string.h>\n";
    uint16_t max_dlc = 0;
    for (const auto & msg : raw_messages_) {
        *out << msg.second.toUnion();
        if (msg.second.dlc() > max_dlc) {
            max_dlc = msg.second.dlc();
        }
    }
    *out << "\n";
    // Now generate another structure to combine all data types into a single entity.
    *out << "#define MAX_DLC_FOR_ALL " << max_dlc << "\n";
    *out << "\nstruct all_" << name_ << "_t {\n";
    *out << "    union {\n";
    *out << "\t\t// Use this buffer to read the data from the CAN stream into. Mind the buffer size.\n";
    *out << "\t\tuint8_t buffer_[MAX_DLC_FOR_ALL]; \n";
    *out << "\t\t// Once you place the CAN frame into the buffer_[], use CAN ID to see if this is the\n";
    *out << "\t\t// message you need and select the corresponding field below.\n";
    for (const auto & msg : raw_messages_) {
        *out << "\t\tstruct " << msg.second.snake_name() << "_message_t " << msg.second.snake_name() << "_;\n";
    }
    *out << "    };\n";
    *out << "};\n";
    *out << "\n#endif // " << define << "\n";
}

void DbcParser::generateRunTimeFormat() {
    rt_messages_.clear();
    id_by_name_.clear();
    extended_only_ = true;
    for (auto & pair : raw_messages_) {
        extended_only_ &= pair.second.extended();
        rt_messages_.emplace(pair.first, pair.second.toPgn());
        id_by_name_.emplace(pair.second.name(), pair.first);
    }
}

std::unique_ptr<uint32_t> DbcParser::getId(const std::string &name) const {
    std::unique_ptr<uint32_t> result;
    auto it = id_by_name_.find(name);
    if (it != id_by_name_.end()) {
        result = std::make_unique<uint32_t>(it->second);
    }
    return result;
}

void DbcParser::topLevelParser(const std::string &line) {
    std::string type4 = line.substr(0, 4);
    std::string type5 = line.substr(0, 5);
    if (type4 == "BO_ ") {
        // Create a new message. Do not add it to the storage yet - we need to check if it is unique first (which requires all signals to be collected).
        last_message_ = std::make_unique<Message>(line);
        if (verbose_) {
            std::cout << "M " << std::hex << last_message_->canId() << " " << last_message_->name() << std::endl;
        }
        last_message_->setDbcName(dbc_name_);
        top_level_ = false;
    } else if (type5 == "VAL_ ") {
        // Split line onto fields.
        // Since this record includes string literals in double qoutes, first split string using them.
        std::vector<std::string> literals = splitString(line, '\"');
        // This is where the final list will be assembled.
        std::vector<std::string> fields;
        bool string_mode = false;
        for (const auto & literal : literals) {
            if (string_mode) {
                fields.push_back("\"" + literal + "\"");
            } else {
                // The first one contains all characters before the double quote. Therefore, all words from there should be added.
                std::vector<std::string> words = splitString(literal, ' ');
                for (const auto & word : words) {
                    if (!word.empty()) {
                        fields.push_back(word);
                    }
                }
            }
            string_mode = !string_mode ;
        }

        // Second field stores the CAN ID so let's get it out.
        uint32_t can_id = std::stoll(fields[1]);
        auto it = raw_messages_.find(can_id);
        assertm(it != raw_messages_.end(), "VAL_ record refers to unknown CAN ID");
        it->second.addValues(fields);
    } else if (type4 == "BA_ ") {
        attributeParser(line);
    }
}

Message *DbcParser::findMutable(uint32_t can_id) {
    auto it = raw_messages_.find(can_id);
    if (it != raw_messages_.end()) {
        return &it->second;
    }
    return nullptr;
}

static bool parseU32(const std::string &text, uint32_t *out) {
    char *end = nullptr;
    unsigned long value = strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value > 0xFFFFFFFFul) {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

void DbcParser::attributeParser(const std::string &line) {
    auto open = line.find('"');
    if (open == std::string::npos) {
        return;
    }
    auto close = line.find('"', open + 1);
    if (close == std::string::npos) {
        return;
    }
    std::string attr = line.substr(open + 1, close - open - 1);
    if (attr != "SPN" && attr != "J1939Default") {
        return;
    }
    std::string rest = line.substr(close + 1);
    auto semi = rest.find(';');
    if (semi != std::string::npos) {
        rest.resize(semi);
    }
    std::vector<std::string> tok;
    std::stringstream stream(rest);
    std::string word;
    while (stream >> word) {
        tok.push_back(word);
    }
    if (tok.size() < 3) {
        return;
    }
    uint32_t can_id = 0;
    if (!parseU32(tok[1], &can_id)) {
        return;
    }
    Message *message = findMutable(can_id);
    if (message == nullptr) {
        attribute_error_ = true;
        attribute_error_text_ = "BA_ \"" + attr + "\" refers to unknown CAN ID " + tok[1];
        return;
    }
    if (attr == "J1939Default" && tok[0] == "BO_") {
        uint32_t flag = 0;
        if (parseU32(tok[2], &flag) && flag != 0) {
            message->markDefault();
        }
        return;
    }
    if (attr == "SPN" && tok[0] == "BO_") {
        uint32_t spn = 0;
        if (parseU32(tok[2], &spn)) {
            message->setMessageSpn(spn);
        }
        return;
    }
    if (attr == "SPN" && tok[0] == "SG_" && tok.size() >= 4) {
        Signal *signal = message->findSignal(tok[2]);
        if (signal == nullptr) {
            attribute_error_ = true;
            attribute_error_text_ = "BA_ \"SPN\" refers to unknown signal " + tok[2];
            return;
        }
        uint32_t spn = 0;
        if (parseU32(tok[3], &spn)) {
            signal->setSpn(spn);
        }
    }
}

void DbcParser::signalsParser(const std::string &line) {
    assertm(line.substr(0, 5) == " SG_ ", "SG_ record missing.");
    assertm(last_message_ != nullptr, "Unepxected SG_ record");
    last_message_->addSignal(line);
}
