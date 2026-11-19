//
// Created by igor on 12/16/21.
//

#include "transport_protocol.h"

void TransportProtocol::abortTransfer(uint8_t src, uint8_t dst, uint32_t pgn) {
    auto it = long_msgs_.find(tpKey(src, dst));
    if (it != long_msgs_.end() && it->second.pgn() == pgn) {
        long_msgs_.erase(it);
    }
}

std::unique_ptr<J1939_frame> TransportProtocol::handleCanFrame(const J1939_frame &frame) {
    std::unique_ptr<J1939_frame> none;
    if (LongCanMessageIf::isTpmFrame(frame)) {
        if (frame.pgn_ == 0xEC00) {
            if (frame.buffer_[0] == kTpAbort) {
                // Either end may abort: the originator (src -> dst) or the receiver (dst -> src).
                uint32_t pgn = frame.buffer_[5] + (frame.buffer_[6] << 8) + (frame.buffer_[7] << 16);
                abortTransfer(frame.src_, frame.dst_, pgn);
                abortTransfer(frame.dst_, frame.src_, pgn);
                return none;
            }
            // BAM / RTS: (re)starts the transfer from this source to this destination.
            return long_msgs_[tpKey(frame.src_, frame.dst_)].handleDataFrame(frame);
        }
        // TP.DT: only part of a transfer that was announced.
        auto it = long_msgs_.find(tpKey(frame.src_, frame.dst_));
        if (it == long_msgs_.end()) {
            return none;
        }
        return it->second.handleDataFrame(frame);
    } else if (LongCanMessageIf::isFastPacketFrame(frame)) {
        return fast_msgs_[fastKey(frame.src_, frame.pgn_)].handleDataFrame(frame);
    }
    return none;
}
