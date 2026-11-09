//
// Created by igor on 12/16/21.
//

#include "long_can_message.h"

#include <algorithm>

void LongCanMessage::reset(const J1939_frame &frame) {
    error_ = false;
    // Initialize the data storage.
    message_ = frame;

    // Message format:
    // Byte index:  0  1  2  3  4  5  6  7
    // Data:       10 73 00 11 11 DA FE 00
    //  0 - control byte (we handle only BAM and RTS)
    //  1 (LSB),2 (MSB) - number of bytes.
    //  3 - total number of data frames (control byte == RTS)
    //  4 - maximum number of data frames
    //  5,6,7 - PGN of the long message.
    // uint8_t control_byte = frame.buffer_[0];
    uint8_t frames_cnt = frame.buffer_[3];
    uint16_t size = frame.buffer_[1] + (frame.buffer_[2] << 8);
    // Reject a declared size/frame count that wouldn't fit message_.buffer_ rather than
    // trusting these untrusted wire values enough to memcpy into it later.
    if ((frames_cnt == 0) || (size > J1939_frame::max_dlc_) ||
        ((static_cast<uint32_t>(frames_cnt) - 1) * 7 + 7 > J1939_frame::max_dlc_)) {
        error_ = true;
        return;
    }
    expected_frames_cnt_ = frames_cnt;
    expected_frame_ = 1;
    message_.dlc_ = expected_size_ = size;
    message_.pgn_ = frame.buffer_[5] + (frame.buffer_[6] << 8) + (frame.buffer_[7] << 16);
}

std::unique_ptr<J1939_frame> LongCanMessage::handleDataFrame(const J1939_frame &frame) {
    std::unique_ptr<J1939_frame> result;
    if (frame.pgn_ == 0xEC00) {
        if (frame.buffer_[0] == kTpAbort) {
            abort();
        } else {
            reset(frame);
        }
        return result;
    }
    if (frame.dst_ != message_.dst_) {
        // A TP.DT to another destination belongs to a different session (or none): not ours.
        return result;
    }
    const size_t offset = static_cast<size_t>(expected_frame_ - 1) * 7;
    // Bytes this packet must carry: 7, or whatever is left for the last one. A TP.DT is
    // normally padded to 8 bytes, but a short one must at least hold the bytes it owes.
    const size_t needed = expected_size_ > offset ? std::min<size_t>(7, expected_size_ - offset) : 0;
    if (error_ || (frame.buffer_[0] != expected_frame_) ||
        (frame.src_ != message_.src_) || (frame.dlc_ < needed + 1)) {
        error_ = true;
        return result;
    }
    // Copy the payload into the correct location in the data buffer.
    memcpy(&message_.buffer_[offset], &frame.buffer_[1], needed);
    if (expected_frame_ == expected_frames_cnt_) {
        result.reset(new J1939_frame());
        *result = message_;
    } else {
        expected_frame_++;
    }
    return result;
}
