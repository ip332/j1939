//
// Reader/writer for the Actisense text format as written by canboat's actisense-serial.
//

#ifndef J1939_ACTISENSE_STREAM_H
#define J1939_ACTISENSE_STREAM_H

#include "can_stream.h"

// One NMEA 2000 / J1939 message per line, comma separated:
//   <timestamp>,<priority>,<pgn>,<src>,<dst>,<length>,<hex byte>,<hex byte>,...
//   2016-02-28T19:57:02.364Z,2,127250,7,255,8,ff,10,3b,ff,7f,ce,f5,fc
// This is what canboat's actisense-serial produces from an Actisense NGT-1, and what most of
// the canboat sample logs use (both the plain files and the "# format=FAST" ones).
//
// The NGT-1 reassembles fast-packet and TP messages itself, so every line is already a
// complete message and may carry more than 8 bytes; preassembled() reports that so callers
// don't push the lines through TransportProtocol a second time. (canboat also has files that
// use this same layout for individual, still-split CAN frames, e.g. its sample-fast.raw.txt;
// those are not what this stream is for.)
//
// Lines that are skipped (get() returns false): '#' comments, anything that doesn't parse,
// and canboat's internal pseudo-PGNs above 0x1FFFF (gateway status messages that never
// existed as CAN frames). An empty timestamp reuses the previous line's time.
class ActisenseStream : public CanStream {
    // Time of the last line that had a timestamp, for lines that don't.
    uint64_t last_time_ns_ = 0;
public:
    ActisenseStream() : CanStream() { writable_ = true; }

    bool preassembled() const override { return true; }

    bool put(const J1939_frame &frame) const override;
    bool get(J1939_frame *frame) override;

    // Parses "YYYY-MM-DD[T|-]HH:MM:SS[.fraction][Z|+HH:MM|-HH:MM]" into nanoseconds since the
    // Unix epoch. A timestamp without a zone is taken as UTC. Times before 1970 (seen in
    // captures from devices without a clock) are clamped to 0. Returns false if malformed.
    static bool parseTimestamp(const char *text, uint64_t *time_ns);
    // Formats `time_ns` as "YYYY-MM-DDTHH:MM:SS.mmmZ" into `out` (at least 25 bytes).
    static void formatTimestamp(uint64_t time_ns, char *out, size_t size);
};

#endif //J1939_ACTISENSE_STREAM_H
