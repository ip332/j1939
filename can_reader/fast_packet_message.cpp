//
// Created by igor on 12/17/21.
//

#include "fast_packet_message.h"

#include <set>

static std::set<uint32_t> fast_pgn = {
        126208, 126464, 126720, 126976, 126983, 126984, 126985, 126986, 126987,
        126988, 126996, 126998, 127233, 127237, 127500, 127503, 127506, 127507,
        127510, 127513, 128275, 128520, 129029, 129038, 129039, 129040, 129041,
        129044, 129045, 129284, 129285, 129301, 129302, 129538, 129540, 129541,
        129542, 129545, 129547, 129549, 129550, 129551, 129556, 129792, 129793,
        129794, 129795, 129796, 129797, 129798, 129799, 129800, 129801, 129802,
        129804, 129805, 129806, 129807, 129808, 129809, 129810, 130060, 130061,
        130064, 130065, 130066, 130067, 130068, 130069, 130070, 130071, 130072,
        130073, 130074, 130320, 130321, 130322, 130323, 130324, 130560, 130567,
        130569, 130570, 130571, 130572, 130573, 130574, 130577, 130578, 130579,
        130580, 130581, 130582, 130583, 130584, 130585, 130586, 130816, 130817,
        130818, 130819, 130820, 130821, 130823, 130824, 130827, 130828, 130831,
        130832, 130834, 130835, 130836, 130837, 130838, 130839, 130840, 130842,
        130845, 130846, 130847, 130850, 130851, 130856, 130880, 130881, 130944,
        // (canboat's 65536 entry is its fallback for unknown addressed ISO PGNs in
        // 0x10000-0x1EC00, which travel over TP, not fast packet -- so not listed here.)
        130900,
};

bool LongCanMessageIf::isFastPacketFrame(const J1939_frame &frame) {
    return fast_pgn.count(frame.pgn_) == 1;
}

void FastPacketMessage::reset(const J1939_frame &frame) {
    error_ = false;
    // Initialize the data storage.
    message_ = frame;

    // This call is expected on the first frame of the fast packet format:
    // • Byte 1:
    //       b 0 – b 4 = 00000, b 0 =LSB
    //       b 5 – b 7 = 3-bit sequence counter to distinguish separate
    //       fast-packet messages of the same PGN, b 5 is the LSB of the counter.
    // • Byte 2 = Total number of data bytes to be transmitted in the message (0
    // to 223). This number includes up to (6)
    //            data bytes in the first frame and up to (7) data bytes in
    //            following frames.
    sequence_counter_ = (frame.buffer_[0] >> 5) & 0x7;
    next_frame_ = 1;
    expected_size_ = frame.buffer_[1];
    message_.dlc_ = expected_size_ < 6 ? expected_size_ : 6;
    memcpy(message_.buffer_, &frame.buffer_[2], message_.dlc_);
}

std::unique_ptr<J1939_frame> FastPacketMessage::handleDataFrame(
        const J1939_frame &frame) {
    std::unique_ptr<J1939_frame> result;
    // First 5 bits of the first data byte can be used to detect the first frame.
    if ((frame.buffer_[0] & 0x1F) == 0) {
        reset(frame);
        // A message whose entire payload fits in the first frame (total size <= 6,
        // common for many fast-packet PGNs) is already complete -- delivered here
        // rather than never, since there's no continuation frame to complete it.
        if (message_.dlc_ == expected_size_) {
            result.reset(new J1939_frame());
            *result = message_;
        }
        return result;
    }

    // Frames must come in order (frame counter 1, 2, ...): a missing or reordered frame would
    // otherwise shift the payload silently.
    if (error_ || (((frame.buffer_[0] >> 5) & 0x7) != sequence_counter_) ||
        ((frame.buffer_[0] & 0x1F) != next_frame_) ||
        (frame.src_ != message_.src_) || (frame.pgn_ != message_.pgn_)) {
        error_ = true;
        return result;
    }
    next_frame_++;
    // Copy the payload into the correct location in the data buffer.
    size_t remaining = expected_size_ - message_.dlc_;
    size_t copy_len = remaining < 7 ? remaining : 7;
    memcpy(&message_.buffer_[message_.dlc_], &frame.buffer_[1], copy_len);
    message_.dlc_ += copy_len;
    if (message_.dlc_ == expected_size_) {
        result.reset(new J1939_frame());
        *result = message_;
    }
    return result;
}
