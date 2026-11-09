//
// Created by igor on 12/17/21.
//

#ifndef J1939_FAST_PACKET_MESSAGE_H
#define J1939_FAST_PACKET_MESSAGE_H

#include "long_can_message_if.h"

class FastPacketMessage : public LongCanMessageIf {
    // Total number of bytes expected (0-223)
    uint8_t expected_size_ = 0;
    // Sequence counter
    uint8_t sequence_counter_ = 0;
    // Frame counter (low 5 bits of byte 0) the next continuation frame must carry.
    uint8_t next_frame_ = 0;
    // Error flag. Set when an out-of-order packet is received. Defaults to true so a
    // frame arriving before any reset() has run (e.g. a capture starting mid-transfer)
    // is safely rejected instead of reading these otherwise-uninitialized members.
    bool error_ = true;

    void reset(const J1939_frame &frame) override;
public:
    std::unique_ptr<J1939_frame> handleDataFrame(const J1939_frame &frame) override;
};


#endif //J1939_FAST_PACKET_MESSAGE_H
