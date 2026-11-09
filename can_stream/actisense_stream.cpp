//
// Reader/writer for the Actisense text format as written by canboat's actisense-serial.
//

#include "actisense_stream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's days_from_civil).
// Hand-rolled instead of timegm() so parsing never depends on the process time zone or on a
// non-standard libc function.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Inverse of daysFromCivil(), for days >= 0 (frame times are unsigned, so never before 1970).
void civilFromDays(uint64_t days, int64_t *y, unsigned *m, unsigned *d) {
    const uint64_t z = days + 719468;
    const int64_t era = static_cast<int64_t>(z / 146097);
    const unsigned doe = static_cast<unsigned>(z - static_cast<uint64_t>(era) * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = static_cast<int64_t>(yoe) + era * 400 + (*m <= 2);
}

// Reads exactly `digits` decimal digits from *p into *value and advances *p.
bool readDigits(const char **p, int digits, int *value) {
    int v = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit(static_cast<unsigned char>((*p)[i]))) {
            return false;
        }
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += digits;
    *value = v;
    return true;
}

// Parses a whole field as an unsigned decimal number no larger than `max`.
bool parseDecimal(const char *text, unsigned long max, unsigned long *value) {
    if (!isdigit(static_cast<unsigned char>(*text))) {
        return false;
    }
    char *end = nullptr;
    unsigned long v = strtoul(text, &end, 10);
    if (*end != 0 || v > max) {
        return false;
    }
    *value = v;
    return true;
}

} // namespace

bool ActisenseStream::parseTimestamp(const char *text, uint64_t *time_ns) {
    const char *p = text;
    int year, month, day, hour, minute, second;
    if (!readDigits(&p, 4, &year) || *p++ != '-' || !readDigits(&p, 2, &month) || *p++ != '-' ||
        !readDigits(&p, 2, &day) || (*p != 'T' && *p != '-') || !readDigits(&++p, 2, &hour) ||
        *p++ != ':' || !readDigits(&p, 2, &minute) || *p++ != ':' || !readDigits(&p, 2, &second)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return false;
    }
    // Fraction of a second, any number of digits (canboat samples have 3, 6, 8 and 9).
    int64_t fraction_ns = 0;
    if (*p == '.') {
        p++;
        int64_t scale = 100000000;
        if (!isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
        while (isdigit(static_cast<unsigned char>(*p))) {
            fraction_ns += (*p - '0') * scale;
            scale /= 10;
            p++;
        }
    }
    // Zone: none (UTC), 'Z', or a numeric offset from UTC.
    int64_t offset_sec = 0;
    if (*p == 'Z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = *p++ == '-' ? -1 : 1;
        int off_h, off_m;
        if (!readDigits(&p, 2, &off_h) || *p++ != ':' || !readDigits(&p, 2, &off_m)) {
            return false;
        }
        offset_sec = sign * (off_h * 3600 + off_m * 60);
    }
    if (*p != 0) {
        return false;
    }
    int64_t sec = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second -
                  offset_sec;
    *time_ns = sec < 0 ? 0 : static_cast<uint64_t>(sec) * 1000000000ULL + fraction_ns;
    return true;
}

void ActisenseStream::formatTimestamp(uint64_t time_ns, char *out, size_t size) {
    uint64_t sec = time_ns / 1000000000ULL;
    unsigned ms = static_cast<unsigned>((time_ns / 1000000ULL) % 1000);
    uint64_t days = sec / 86400;
    int64_t rem = static_cast<int64_t>(sec % 86400);
    int64_t y;
    unsigned m, d;
    civilFromDays(days, &y, &m, &d);
    snprintf(out, size, "%04lld-%02u-%02uT%02lld:%02lld:%02lld.%03uZ", static_cast<long long>(y), m,
             d, static_cast<long long>(rem / 3600), static_cast<long long>((rem / 60) % 60),
             static_cast<long long>(rem % 60), ms);
}

bool ActisenseStream::put(const J1939_frame &frame) const {
    char time[32];
    formatTimestamp(frame.time_ns_, time, sizeof(time));
    fprintf(file_, "%s,%u,%u,%u,%u,%u", time, frame.pri_, frame.pgn_, frame.src_, frame.dst_,
            frame.dlc_);
    for (int i = 0; i < frame.dlc_; i++) {
        fprintf(file_, ",%02x", frame.buffer_[i]);
    }
    fprintf(file_, "\n");
    // The error indicator is sticky, so this also catches a failure in any write above.
    return !ferror(file_);
}

bool ActisenseStream::get(J1939_frame *frame) {
    // The format has no whitespace, so a valid line is exactly one field.
    if (get_fields() != 1 || fields_[0][0] == '#') {
        return false;
    }
    // Split the line on commas in place, reusing fields_ so decodeFields() can read the bytes.
    char *line = fields_[0];
    fields_.clear();
    fields_.push_back(line);
    for (char *c = line; *c; c++) {
        if (*c == ',') {
            *c = 0;
            fields_.push_back(c + 1);
        }
    }
    // Fields: 0 time, 1 priority, 2 PGN, 3 source, 4 destination, 5 length, 6... data bytes.
    if (fields_.size() < 6) {
        return false;
    }
    uint64_t time_ns = last_time_ns_;
    if (*fields_[0] != 0) {
        if (!parseTimestamp(fields_[0], &time_ns)) {
            return false;
        }
        last_time_ns_ = time_ns;
    }
    unsigned long pri, pgn, src, dst, len;
    if (!parseDecimal(fields_[1], 7, &pri) || !parseDecimal(fields_[2], 0x1FFFF, &pgn) ||
        !parseDecimal(fields_[3], 0xFF, &src) || !parseDecimal(fields_[4], 0xFF, &dst) ||
        !parseDecimal(fields_[5], J1939_frame::max_dlc_, &len)) {
        return false;
    }
    // Rebuild the 29-bit identifier the message was sent with. PDU1 PGNs (PF < 240) carry the
    // destination in PS, so their low byte must be 0; PDU2 PGNs carry group extension in PS.
    uint32_t pf = (pgn >> 8) & 0xFF;
    uint32_t ps;
    if (pf < 240) {
        if ((pgn & 0xFF) != 0) {
            return false;
        }
        ps = dst;
    } else {
        ps = pgn & 0xFF;
    }
    uint32_t can_id = CAN_EFF_FLAG | (pri << 26) | (((pgn >> 16) & 1) << 24) | (pf << 16) |
                      (ps << 8) | src;
    uint8_t data[J1939_frame::max_dlc_];
    if (!decodeFields(6, len, 16, data)) {
        return false;
    }
    frame->time_ns_ = time_ns;
    return frame->setFrom(can_id | extended_, data, static_cast<uint16_t>(len));
}
