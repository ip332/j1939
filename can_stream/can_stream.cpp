//
// Created by igor.prilepov on 3/14/22.
//

#include "actisense_stream.h"
#include "asc_stream.h"
#include "log_stream.h"
#include "out_stream.h"
#include "trc_stream.h"
#include "txt_stream.h"
#include "socket_can_stream.h"

#include <cstdlib>
#include <cstring>

bool CanStream::decodeFields(size_t start, size_t count, int base, uint8_t *out) const {
    if (count > J1939_frame::max_dlc_) {
        return false;
    }
    if (start + count > fields_.size()) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        char *end = nullptr;
        unsigned long value = strtoul(fields_[start + i], &end, base);
        if (end == fields_[start + i]) {
            return false;
        }
        out[i] = static_cast<uint8_t>(value & 0xFF);
    }
    return true;
}

bool CanStream::decodeHexString(const char *hex, size_t count, uint8_t *out) const {
    if (count > J1939_frame::max_dlc_) {
        return false;
    }
    if (strlen(hex) < count * 2) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t tmp = 0;
        if (sscanf(hex + 2 * i, "%2X", &tmp) != 1) {
            return false;
        }
        out[i] = tmp & 0xFF;
    }
    return true;
}

size_t CanStream::get_fields() {
    buffer_[0] = 0;
    auto p = fgets(buffer_, sizeof(buffer_) - 1, file_);
    if (feof(file_) || (p == NULL)) {
        return 0;
    }
    if ((strchr(buffer_, '\n') == nullptr) && !feof(file_)) {
        // The line is longer than buffer_ and got truncated by fgets(). Discard the
        // rest of it so the next call doesn't misparse its tail as a new line, and
        // skip this (malformed/oversized) line rather than silently parsing a
        // truncated fragment.
        int c;
        while (((c = fgetc(file_)) != '\n') && (c != EOF)) {
        }
        return 0;
    }
    // Find the first non whitespace character
    char *string = buffer_;
    while (isspace(*string) && *string != '\n' && *string != '\r') {
        string++;
    }
    char *field = string;
    fields_.clear();
    while (*string && *string != '\n' && *string != '\r') {
        if (isspace(*string)) {
            // Replace the first white space and move pointer to the next character.
            *string++ = 0;
            // Store the beginning of the field
            fields_.push_back(field);
            // Find the beginning of the next field.
            while (isspace(*string)) {
                string++;
            }
            field = string;
        } else {
            string++;
        }
    }
    *string = 0;
    if (strlen(field)) {
        fields_.push_back(field);
    }
    return fields_.size();
}

std::string SupportedStreams() {
    return "asc|log|out|trc|txt|raw|all";
}

namespace {
bool endsWith(const std::string &s, const char *suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
} // namespace

std::unique_ptr<CanStream> CanStreamFor(const std::string &input) {
    if (input.find(".asc") != std::string::npos) {
        return std::make_unique<AscStream>();
    }
    if (input.find(".log") != std::string::npos) {
        return std::make_unique<LogStream>();
    }
    if (input.find(".out") != std::string::npos) {
        return std::make_unique<OutStream>();
    }
    if (input.find(".trc") != std::string::npos) {
        return std::make_unique<TrcStream>();
    }
    if (input.find(".txt") != std::string::npos) {
        return std::make_unique<TxtStream>();
    }
    // canboat's names for actisense-serial output. Matched on the suffix only: unlike the
    // formats above, ".all"/".raw" are short enough to show up inside unrelated paths.
    if (endsWith(input, ".raw") || endsWith(input, ".all")) {
        return std::make_unique<ActisenseStream>();
    }
    return std::make_unique<SocketCanStream>();
}

