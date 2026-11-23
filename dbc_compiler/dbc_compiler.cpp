#include <iostream>
#include <fstream>
#include <memory>
#include <sstream>

#include "dbc_parser.h"
#include "options.h"

std::unique_ptr<std::ofstream> getFile(const std::string &path) {
    std::unique_ptr<std::ofstream> file;
    if (path.size() > 0) {
        file.reset(new std::ofstream());
        file->open(path.c_str());
        if (!file->is_open()) {
            std::cerr << "Error opening file " << path << std::endl;
            exit(2);
        }
    }
    return file;
}

int main(int argc, const char* argv[]) {
    OptionsManager options;

    std::vector<std::string> inputs;
    std::string pgn_path;
    std::string union_path;
    std::string accessor_path;
    std::string c_path;
    std::string var_name;
    bool no_strings = false;

    options.addOption("-in", "Full path of the input DBC file (multiple input files are supported).", &inputs);
    options.addOption("-name", "Defines variable name in the output files. By default, the first input DBC name will be used.", &var_name);
    options.addOption("-pgn_path", "Full path of the output file to store the DBC content in PGN format.", &pgn_path);
    options.addOption("-union_path", "Full path of the output file to store the DBC content in the 'union' format.", &union_path);
    options.addOption("-accessor_path", "Standalone portable C/C++ payload accessors (header).", &accessor_path);
    options.addOption("-c_path", "Full path of the generated plain-C database (.c). A matching .h is written beside it.", &c_path);
    options.addOption("-no-strings", "Omit signal and message names from the plain-C database.", &no_strings);

    if (!options.processArgs(argc, argv)) {
        return 1;
    }

    if (inputs.size() == 0 || (union_path.empty() && pgn_path.empty() && c_path.empty() && accessor_path.empty())) {
        printf( "Usage: dbc_compiler <options>\n");
        printf( "   where <options> could be:\n");
        options.print();
        return 2;
    }
    // Open the output file(s).
    std::unique_ptr<std::ofstream> pgn_file = getFile(pgn_path);
    std::unique_ptr<std::ofstream> union_file = getFile(union_path);

    // Load all input files into RAM
    DbcParser parser;
    for (auto in : inputs) {
        if (!parser.parseFile(in)) {
            return -3;
        }
    }
    // The C database has its own collapse rule and must see every source address,
    // so it is emitted before removeDuplicates() rewrites the message keys.
    if (!c_path.empty()) {
        std::string header_path = c_path;
        auto dot = header_path.rfind('.');
        if (dot == std::string::npos) {
            header_path += ".h";
        } else {
            header_path.replace(dot, std::string::npos, ".h");
        }
        std::ostringstream c_text;
        std::ostringstream h_text;
        std::string error;
        if (!parser.generateCOutput(c_text, h_text, no_strings, error)) {
            std::cerr << error << std::endl;
            return 4;
        }
        std::ofstream c_file(c_path.c_str());
        std::ofstream h_file(header_path.c_str());
        if (!c_file.is_open() || !h_file.is_open()) {
            std::cerr << "Error opening C database output " << c_path << std::endl;
            return 2;
        }
        c_file << c_text.str();
        h_file << h_text.str();
    }

    if (!accessor_path.empty()) {
        std::ostringstream text;
        std::string error;
        if (!parser.generateAccessorOutput(text, error)) {
            std::cerr << error << std::endl;
            return 4;
        }
        auto file = getFile(accessor_path);
        *file << text.str();
    }

    // TODO: add option to disable the following step.
    parser.removeDuplicates();

    // Optionally: set desired variable name.
    if (!var_name.empty()) {
        parser.setName(var_name);
    }
    parser.generatePgnOutput(pgn_file.get());
    parser.generateUnionOutput(union_file.get());

    if (union_file) {
        union_file->close();
    }
    if (pgn_file) {
        pgn_file->close();
    }
    return 0;
}