//
// Created by igor.prilepov on 6/17/22.
//

#ifndef J1939_CAN_SANDBOX_LIB_H
#define J1939_CAN_SANDBOX_LIB_H

#include "options.h"
#include "socket_can_stream.h"
#include "dbc_parser.h"
#include "j1939_parser.h"
#include "node_table.h"
#include "transport_protocol.h"

// This class defines all supported options and functionalities for reading/writing CAN stream.
class CanSandbox {
    OptionsManager options_;
    // Input/output streams could be defined as CAN interface name (can0) or a file name.
    std::string input_stream_;
    std::string output_stream_;
    // More than one DBC is supported therefore we'll store their names in a vector:
    std::vector<std::string> dbc_paths_;
    // Do not print every message
    bool quiet_ = false;
    // Treat all input messages as extended CAN Ids
    bool extended_ = false;
    // Print out fields even when all bits are set to 1 (undefined).
    bool show_unset_bits_ = false;
    // Automatically rewind the input file once EOF has been reached.
    bool rewind_ = false;
    // Address-claim tracking (NodeTable): print its events, the NAME behind each frame and
    // the final table.
    bool nodes_ = false;
    // Write a JSON summary of the node table (claims, NAMEs, churn, event list) here.
    std::string nodes_report_;
    int contest_window_ms_ = 500;
    int node_timeout_s_ = 0;
    // Active mode, off by default: send a Request for Address Claimed (from the null
    // address) on the input CAN interface at startup.
    bool request_claims_ = false;

    std::unique_ptr<CanStream> input_;
    std::unique_ptr<CanStream> output_;
    DbcParser dbc_parser_;
    std::unique_ptr<NodeTable> node_table_;
    std::vector<NodeEvent> node_events_; // kept only for -nodes-report

    std::function<void(const J1939Parser & parser)> parser_callback_;

    void printMessage(const J1939Parser &parser, double time, bool show_unset);
    void onNodeEvent(const NodeEvent &event);
    void printNodeTable() const;
    bool writeNodesReport(const std::string &path, uint64_t frames) const;

public:
    explicit CanSandbox();

    // Application can add extra options
    void addOption( const std::string &flag, const std::string & info, std::string *var) {
        options_.addOption(flag, info, var);
    }
    void addOption( const std::string &flag, const std::string & info, std::vector<std::string> *var) {
        options_.addOption(flag, info, var);
    }
    void addOption( const std::string &flag, const std::string & info, bool *var) {
        options_.addOption(flag, info, var);
    }
    void addOption( const std::string &flag, const std::string & info, int *var) {
        options_.addOption(flag, info, var);
    }
    // Parses command line arguments, loads all necessary files/streams and return the result.
    bool parseOptions(int argc, const char *argv[]);

    // Defines extra processing for a parsed message
    void setParserCallback(std::function<void(const J1939Parser & parser)> cb) { parser_callback_ = cb; }

    // Injects a stream directly instead of going through parseOptions()'s
    // CanStreamFor(path) lookup -- useful both for tests (e.g. with MockCanStream)
    // and for embedding CanSandbox programmatically without a CLI argument round-trip.
    void setInputStream(std::unique_ptr<CanStream> stream, std::string name = "") {
        input_stream_ = std::move(name);
        input_ = std::move(stream);
    }
    void setOutputStream(std::unique_ptr<CanStream> stream, std::string name = "") {
        output_stream_ = std::move(name);
        output_ = std::move(stream);
    }

    // Start input stream handling.
    void startProcessing();

    // The address-claim table built by startProcessing() (nullptr before it runs).
    const NodeTable *nodeTable() const { return node_table_.get(); }

    // Active mode: sends a Request for Address Claimed on the input stream. Only possible
    // when the input is a real CAN interface; returns false (and does nothing) otherwise.
    bool sendAddressClaimRequest();
};


#endif //J1939_CAN_SANDBOX_LIB_H
