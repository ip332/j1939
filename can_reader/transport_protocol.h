//
// Created by igor on 12/16/21.
//

#ifndef J1939_TRANSPORT_PROTOCOL_H
#define J1939_TRANSPORT_PROTOCOL_H

#include <map>
#include "long_can_message.h"
#include "fast_packet_message.h"

// This class implements the transport protocol (J1939-21)
class TransportProtocol {
    // More than one ECU can send long messages at the same time therefore we need a storage for incomplete messages.
    // TP (BAM or RTS/CTS) transfers, one per (source, destination): a node can run a BAM (to
    // global) and RTS/CTS transfers to several destinations at the same time.
    ::std::map<uint16_t, LongCanMessage> long_msgs_;
    // Fast-packet (NMEA 2000) transfers, one per (source, PGN): a node can interleave
    // fast-packet messages of different PGNs.
    ::std::map<uint32_t, FastPacketMessage> fast_msgs_;

    static uint16_t tpKey(uint8_t src, uint8_t dst) { return static_cast<uint16_t>((src << 8) | dst); }
    static uint32_t fastKey(uint8_t src, uint32_t pgn) { return (pgn << 8) | src; }
    // Drops the src -> dst transfer of `pgn`, if there is one.
    void abortTransfer(uint8_t src, uint8_t dst, uint32_t pgn);

public:
    static bool longMessage(const J1939_frame &frame) {
        return LongCanMessageIf::isTpmFrame(frame) || LongCanMessageIf::isFastPacketFrame(frame);
    }

    // Returns a long message if the input was the last piece of the TP or fast-packet protocol.
    std::unique_ptr<J1939_frame> handleCanFrame(const J1939_frame &frame);
};

#endif //J1939_TRANSPORT_PROTOCOL_H
