// Source-tree projection through the existing DBC parser, not a second DBC grammar.
#include "dbc_parser.h"
#include <fstream>
#include <clocale>
#include <locale>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

static std::string json(const std::string &s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o << '\\' << c;
        else if (c < 32) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else o << c;
    }
    return o.str() + '"';
}
int main(int argc, char **argv) {
    try {
        if (argc != 4 && argc != 5) return 2;
        // Pin std::stod and stream output independently of the invoking environment.
        if (!std::setlocale(LC_NUMERIC, "C")) throw std::runtime_error("C numeric locale unavailable");
        std::locale::global(std::locale::classic());
        DbcParser parser;
        parser.setName("observed");
        if (argc == 5) {
            if (std::string(argv[4]) != "--strict") return 2;
            parser.setModelExportMode();
        }
        if (!parser.parseFile(argv[1])) throw std::runtime_error("DBC parse rejected input");
        std::string error;
        std::ostringstream c, h;
        if (!parser.generateCOutput(c, h, false, error)) throw std::runtime_error(error);
        std::ofstream cf(argv[2]), hf(argv[3]);
        cf << c.str(); hf << h.str();
        cf.close(); hf.close();
        if (!cf || !hf) throw std::runtime_error("database output write failed");
        std::cout << '[';
        bool first = true;
        for (const auto &entry : parser.sourceMessages()) {
            const auto &m = entry.second;
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << m.canId() << ",\"pgn\":" << m.pgn()
                      << ",\"name\":" << json(m.name()) << ",\"dlc\":" << m.dlc()
                      << ",\"sa\":" << unsigned(m.sa()) << ",\"extended\":" << m.extended()
                      << ",\"default\":";
            if (m.hasDefaultMark()) std::cout << m.defaultMark(); else std::cout << "null";
            std::cout << ",\"spn\":";
            if (m.hasMessageSpn()) std::cout << m.messageSpn(); else std::cout << "null";
            std::cout << ",\"signals\":[";
            bool sf = true;
            for (const auto &s : m.signalList()) {
                if (!sf) std::cout << ',';
                sf = false;
                std::cout << "{\"name\":" << json(s.name()) << ",\"start_bit\":" << s.startBit()
                    << ",\"width\":" << s.bitLength() << ",\"byte_order\":" << (s.little_endian() ? 0 : 1)
                    << ",\"is_signed\":" << s.isSigned() << ",\"scale\":" << json(s.scaleLiteral())
                    << ",\"offset\":" << json(s.offsetLiteral()) << ",\"minimum\":" << json(s.minLiteral())
                    << ",\"maximum\":" << json(s.maxLiteral()) << ",\"unit\":" << json(s.unitsLiteral().substr(1,s.unitsLiteral().size()-2))
                    << ",\"is_mux_switch\":" << s.isMultiplexor() << ",\"multiplexed\":" << s.isMultiplexed()
                    << ",\"mux_value\":" << s.muxValue() << ",\"spn\":";
                if (s.hasSpn()) std::cout << s.spn(); else std::cout << "null";
                std::cout << ",\"enums\":[";
                bool ef = true;
                for (const auto &e : s.enums()) {
                    if (!ef) std::cout << ',';
                    ef = false;
                    std::cout << "{\"raw\":" << e.first << ",\"label\":" << json(e.second.substr(1,e.second.size()-2)) << '}';
                }
                std::cout << "]}";
            }
            std::cout << "]}";
        }
        std::cout << "]\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
