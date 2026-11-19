//
// Created by igor on 12/16/21.
//

#ifndef J1939_LONG_CAN_MESSAGE_H
#define J1939_LONG_CAN_MESSAGE_H


#include "long_can_message_if.h"

// This class represents a single multi-frame message, i.e. when the payload size is more than 8 bytes and it is
// delivered using one of the Transport Protocols (BAM, RTS?CTS, etc. - see J1939-21).
class LongCanMessage : public LongCanMessageIf {
    // Total number of bytes expected.
    uint16_t expected_size_ = 0;
    // Total number of frames expected.
    uint8_t expected_frames_cnt_ = 0;
    // Next frame index.
    uint8_t expected_frame_ = 0;
    // Error flag. Set when an out of order packet is received. Defaults to true so a
    // frame arriving before any reset() has run (e.g. a capture starting mid-transfer)
    // is safely rejected instead of reading these otherwise-uninitialized members.
    bool error_ = true;

    void reset(const J1939_frame &frame) override;
public:
    // A TP.CM (BAM/RTS) starts the session; TP.DT frames from the same source to the same
    // destination fill it in. Returns the message once its last packet arrived.
    std::unique_ptr<J1939_frame> handleDataFrame(const J1939_frame &frame) override;

    // Connection Abort: the transfer in progress will never complete.
    void abort() { error_ = true; }
    // PGN of the message being transferred (from the TP.CM).
    uint32_t pgn() const { return message_.pgn_; }
};


#endif //J1939_LONG_CAN_MESSAGE_H
