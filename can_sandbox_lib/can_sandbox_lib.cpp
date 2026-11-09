//
// Created by igor.prilepov on 6/17/22.
//

#include "can_sandbox_lib.h"

#include <algorithm>
#include <cstdio>
#include <unistd.h>

CanSandbox::CanSandbox() {
    std::string info("\t\tDefines the input device (can0) or a log file (");
    info += SupportedStreams();
    info += ") to read the data from.";
    options_.addOption("-in", info, &input_stream_);
    options_.addOption("-out", "\t\tDefines the CAN interface to play back the log file\n"
                              "\t\t\t\tinto or a file name to record the stream into (.log)", &output_stream_);
    options_.addOption("-dbc", "\t\tDefines a DBC file which should be used to parse CAN frames.", &dbc_paths_);
    options_.addOption("-ext", "\t\t\tTreat input messages as extended CAN frames (by default the extended bit from the CAN ID is used)", &extended_);
    options_.addOption("-all", "\t\t\tShow unset fields, i.e. filled with all 1", &show_unset_bits_);
    options_.addOption("-quiet", "\t\t\tDo not show the stream content.", &quiet_);
    options_.addOption("-rewind", "\t\t\tRewind the input file after EOF.", &rewind_);
    options_.addOption("-nodes", "\t\t\tTrack address claims: print node events, the NAME behind\n"
                                 "\t\t\t\teach frame, and the final SA <-> NAME table.", &nodes_);
    options_.addOption("-nodes-report", "\tWrite a JSON summary of the address-claim table to this file.",
                       &nodes_report_);
    options_.addOption("-contest-window", "\tMilliseconds a contested address stays undecided (default 500).",
                       &contest_window_ms_);
    options_.addOption("-node-timeout", "\tSeconds without traffic before a node times out (default 0 = never).",
                       &node_timeout_s_);
    options_.addOption("-request-claims", "\tActive mode: send a Request for Address Claimed on the input\n"
                                          "\t\t\t\tCAN interface at startup (never on a file).", &request_claims_);
}

bool CanSandbox::sendAddressClaimRequest() {
    if (!input_ || !input_->realPort()) {
        std::cerr << "-request-claims ignored: the input is not a CAN interface." << std::endl;
        return false;
    }
    if (!input_->put(NodeTable::makeAddressClaimRequest())) {
        std::cerr << "Failed to send the Request for Address Claimed." << std::endl;
        return false;
    }
    return true;
}

void CanSandbox::onNodeEvent(const NodeEvent &e) {
    if (!nodes_report_.empty()) {
        node_events_.push_back(e);
    }
    if (!nodes_) {
        return;
    }
    printf("%17.6f NODE %s sa=%u", e.time_ns / 1E9, toString(e.type), e.sa);
    if (e.old_sa != kNullAddress) {
        printf(" old_sa=%u", e.old_sa);
    }
    if (e.source_sa != kNullAddress) {
        printf(" source_sa=%u", e.source_sa);
    }
    printf(" name=%s", J1939Name::hex(e.name).c_str());
    if (e.other_name != 0) {
        printf(" other=%s", J1939Name::hex(e.other_name).c_str());
    }
    if (e.type == NodeEventType::ContestResolved) {
        printf(" branch=%s", toString(e.branch));
    }
    if (e.commanded) {
        printf(" commanded");
    }
    if (e.type == NodeEventType::Attributed) {
        printf(" from=%.6f to=%.6f", e.from_ns / 1E9, e.to_ns / 1E9);
    }
    printf("\n");
}

void CanSandbox::printNodeTable() const {
    printf("Address-claim table:\n");
    node_table_->forEach([](const Node &n) {
        J1939Name name = J1939Name::fromRaw(n.name_);
        char sa[8] = "-";
        if (n.sa_ != kNullAddress) {
            snprintf(sa, sizeof(sa), "%u", n.sa_);
        }
        printf("  SA %-4s %-11s %s first=%.6f last=%.6f\n", sa, toString(n.state_),
               name.toString().c_str(), n.first_seen_ns_ / 1E9, n.last_seen_ns_ / 1E9);
    });
}

namespace {
std::string jsonString(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out + "\"";
}
} // namespace

bool CanSandbox::writeNodesReport(const std::string &path, uint64_t frames) const {
    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        std::cerr << "Cannot write the nodes report to " << path << std::endl;
        return false;
    }
    const NodeTableStats &s = node_table_->stats();
    uint64_t address_changes = 0;
    for (const auto &e : node_events_) {
        address_changes += e.type == NodeEventType::NodeAddressChanged;
    }
    size_t names = 0;
    node_table_->forEach([&](const Node &) { names++; });
    fprintf(f, "{\n  \"input\": %s,\n  \"frames\": %llu,\n  \"names\": %zu,\n",
            jsonString(input_stream_).c_str(), static_cast<unsigned long long>(frames), names);
    fprintf(f, "  \"stats\": {\"claims\": %llu, \"malformed_claims\": %llu, \"cannot_claims\": %llu, "
               "\"claim_requests\": %llu, \"commanded\": %llu, \"malformed_commanded\": %llu, "
               "\"commands_expired\": %llu, \"contests\": %llu, \"reassignments\": %llu, "
               "\"address_changes\": %llu, \"non_compliant\": %llu, \"duplicates\": %llu, "
               "\"pingpongs\": %llu, \"timeouts\": %llu, \"resolved\": {",
            (unsigned long long)s.claims, (unsigned long long)s.malformed_claims,
            (unsigned long long)s.cannot_claims, (unsigned long long)s.claim_requests,
            (unsigned long long)s.commanded, (unsigned long long)s.malformed_commanded,
            (unsigned long long)s.commands_expired, (unsigned long long)s.contests,
            (unsigned long long)s.reassignments, (unsigned long long)address_changes,
            (unsigned long long)s.non_compliant, (unsigned long long)s.duplicates,
            (unsigned long long)s.pingpongs, (unsigned long long)s.timeouts);
    for (int b = 0; b < 5; b++) {
        fprintf(f, "%s\"%s\": %llu", b ? ", " : "", toString(static_cast<Branch>(b)),
                (unsigned long long)s.resolved[b]);
    }
    fprintf(f, "}},\n  \"nodes\": [");
    bool first = true;
    node_table_->forEach([&](const Node &n) {
        fprintf(f, "%s\n    {\"name\": \"%s\", \"sa\": ", first ? "" : ",", J1939Name::hex(n.name_).c_str());
        first = false;
        if (n.sa_ == kNullAddress) {
            fprintf(f, "null");
        } else {
            fprintf(f, "%u", n.sa_);
        }
        J1939Name name = J1939Name::fromRaw(n.name_);
        fprintf(f, ", \"state\": \"%s\", \"manufacturer\": %u, \"function\": %u, "
                   "\"first_seen\": %.6f, \"last_seen\": %.6f}",
                toString(n.state_), name.manufacturer_code_, name.function_, n.first_seen_ns_ / 1E9,
                n.last_seen_ns_ / 1E9);
    });
    fprintf(f, "\n  ],\n  \"events\": [");
    first = true;
    for (const auto &e : node_events_) {
        fprintf(f, "%s\n    {\"t\": %.6f, \"type\": \"%s\", \"sa\": %u, \"old_sa\": %u, "
                   "\"source_sa\": %u, \"name\": \"%s\", \"other\": \"%s\", \"branch\": \"%s\", "
                   "\"commanded\": %s, \"from\": %.6f, \"to\": %.6f}",
                first ? "" : ",", e.time_ns / 1E9, toString(e.type), e.sa, e.old_sa, e.source_sa,
                J1939Name::hex(e.name).c_str(), J1939Name::hex(e.other_name).c_str(),
                toString(e.branch), e.commanded ? "true" : "false", e.from_ns / 1E9, e.to_ns / 1E9);
        first = false;
    }
    fprintf(f, "\n  ]\n}\n");
    return fclose(f) == 0;
}

bool CanSandbox::parseOptions(int argc, const char **argv) {
    if (!options_.processArgs(argc, argv)) {
        return false;
    }
    if (!input_stream_.empty()) {
        input_ = CanStreamFor(input_stream_);
        if (!input_ ||
                (!input_->realPort() && !input_->open(input_stream_))) {
            std::cerr << "Cannot open channel " << input_stream_ << " for reading." << std::endl;
            return false;
        }
    }
    if (!output_stream_.empty()) {
        output_ = CanStreamFor(output_stream_);
        if (!output_ ||
                (!output_->realPort() && !output_->open(output_stream_, true))) {
            std::cerr << "Cannot open channel " << output_stream_ << " for writing." << std::endl;
            return false;
        }
    }
    if (!input_) {
        printf( "Usage: can_sandbox <options>\n");
        printf( "   where <options> could be:\n");
        options_.print();
        return false;
    }

    for (const auto &str : dbc_paths_) {
        if (!dbc_parser_.parseFile(str)) {
            return false;
        }
    }
    dbc_parser_.removeDuplicates();
    dbc_parser_.generateRunTimeFormat();

    input_->setExtended(extended_);
    return true;
}

void CanSandbox::printMessage(const J1939Parser &parser, double time, bool show_unset) {
    char buf[100];
    snprintf(buf, sizeof(buf), "%17.6f ",time);
    ::std::cout << buf;
    parser.toString(::std::cout, show_unset);
    if (nodes_) {
        Resolution r = node_table_->resolve(parser.frame());
        if (r.kind == Resolution::Resolved) {
            ::std::cout << " NAME:" << J1939Name::hex(r.name);
        } else if (r.kind == Resolution::Contested) {
            ::std::cout << " CONTESTED:" << J1939Name::hex(r.name) << "|" << J1939Name::hex(r.candidate);
        } else {
            ::std::cout << " UNRESOLVED";
        }
    }
    ::std::cout << ::std::endl;
}

void CanSandbox::startProcessing() {
    uint64_t size = 0;
    uint64_t counter = 0;
    uint64_t start_time = 0;
    uint64_t last_time = 0;

    uint64_t last_frame_ns = 0;

    J1939_frame frame;
    TransportProtocol transportProtocol;

    NodeTableConfig node_config;
    node_config.contest_window_ns = static_cast<uint64_t>(contest_window_ms_ > 0 ? contest_window_ms_ : 0) * 1000000ULL;
    node_config.stale_timeout_ns = static_cast<uint64_t>(node_timeout_s_ > 0 ? node_timeout_s_ : 0) * 1000000000ULL;
    node_table_.reset(new NodeTable(node_config));
    node_events_.clear();
    node_table_->setEventCallback([this](const NodeEvent &e) { onNodeEvent(e); });
    if (request_claims_) {
        sendAddressClaimRequest();
    }

    while (true) {
        frame.reset();
        bool parsed = input_->get(&frame);
        if (parsed) {
            if (start_time == 0) {
                last_time = start_time = frame.time_ns_;
            }
            if ((output_ && output_->realPort())) {
                if (!output_->ready()) {
                    sleep(1);
                    if (!output_->open(output_stream_)) {
                        continue;
                    }
                }
                // Simulate real CAN bus timing.
                if (!input_->realPort()) {
                    if (frame.time_ns_ > last_time) {
                        usleep((frame.time_ns_ - last_time) / 1E3);
                        last_time = frame.time_ns_;
                    }
                }
            }
            if (output_) {
                if (!output_->put(frame)) {
                    if (output_->realPort()) {
                        output_->close();
                        sleep(1);
                        output_->open(output_stream_, true);
                    }
                    continue;
                }
            }
            // Count this physical frame's own bytes for the bus-load stats below,
            // before a multi-frame transfer (if any) substitutes the reassembled
            // logical message for `frame` — otherwise a transfer spanning N physical
            // frames would be counted as a single message of up to 1758 bytes instead
            // of N frames of up to 8 bytes each.
            counter++;
            size += frame.dlc_;
            // TODO(tp-by-name): TransportProtocol keys TP and fast-packet sessions on the
            // source address only. When an address changes hands mid-transfer (NodeTable
            // AddressReassigned / NodeAddressChanged), the half-built message of the old owner
            // gets the new owner's frames. Key sessions on NAME, or flush the SA's sessions on
            // those events. See can_reader/transport_protocol_known_issues_tests.cpp.
            if (!input_->preassembled() && TransportProtocol::longMessage(frame)) {
                auto long_message_completed = transportProtocol.handleCanFrame(frame);
                if (!long_message_completed) {
                    continue;
                }
                frame = *long_message_completed;
            }
            node_table_->handleFrame(frame);
            last_frame_ns = std::max(last_frame_ns, frame.time_ns_);
            J1939Parser parser(frame, dbc_parser_.dbc());
            if (parser_callback_) {
                parser_callback_(parser);
            }
            if (!quiet_) {
                printMessage(parser, frame.time_ns_ / 1E9, show_unset_bits_);
            }
        }
        if (!input_->dataAvailable()) {
            // Try to re-open CAN interface
            if (input_->realPort()) {
                input_->close();
                sleep(1);
                input_->open(input_stream_);
                continue;
            } else {
                if (rewind_) {
                    input_->rewind();
                    continue;
                }
                break;
            }
        }
    }
    // End of stream counts as silence: contests still open are decided as if nobody defended.
    node_table_->flush(last_frame_ns + node_config.contest_window_ns + 1);
    if (nodes_) {
        printNodeTable();
    }
    if (!nodes_report_.empty()) {
        writeNodesReport(nodes_report_, counter);
    }
    double delta_sec = (frame.time_ns_ - start_time) / 1E9;
    ::std::cout << "Average bus load: " << (size / delta_sec ) << " bytes/sec; duration: " << delta_sec << " sec." << ::std::endl;
    ::std::cout << "                  " << (counter / delta_sec ) << " msg/sec" << ::std::endl;
}