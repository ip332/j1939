// Header-only availability inventory; not a claim that signal dialects are supported.
#include "dbc_message.h"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::ifstream in(argv[1]); std::string line;
    if (!in) return 1;
    while (std::getline(in,line)) if (line.compare(0,4,"BO_ ") == 0) {
        try { Message m(line); std::cout << m.pgn() << ' ' << m.canId() << ' ' << m.name() << '\n'; }
        catch (...) { std::cerr << "unreadable BO_: " << line << '\n'; return 3; }
    }
}
