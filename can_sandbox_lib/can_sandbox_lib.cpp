//
// Created by igor.prilepov on 6/17/22.
//

#include "can_sandbox_lib.h"
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
    ::std::cout << ::std::endl;
}

void CanSandbox::startProcessing() {
    uint64_t size = 0;
    uint64_t counter = 0;
    uint64_t start_time = 0;
    uint64_t last_time = 0;

    J1939_frame frame;
    TransportProtocol transportProtocol;

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
            if (!input_->preassembled() && TransportProtocol::longMessage(frame)) {
                auto long_message_completed = transportProtocol.handleCanFrame(frame);
                if (!long_message_completed) {
                    continue;
                }
                frame = *long_message_completed;
            }
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
    double delta_sec = (frame.time_ns_ - start_time) / 1E9;
    ::std::cout << "Average bus load: " << (size / delta_sec ) << " bytes/sec; duration: " << delta_sec << " sec." << ::std::endl;
    ::std::cout << "                  " << (counter / delta_sec ) << " msg/sec" << ::std::endl;
}