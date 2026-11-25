#ifndef J1939_DBC_NUMBER_H
#define J1939_DBC_NUMBER_H
#include <regex>
#include <string>
// Preserve the DBC decimal spelling, but force integer-form tokens to C floating
// constants: leading zeroes must never become octal or overflow an integer type.
inline std::string dbcCNumber(const std::string &text) {
    static const std::regex decimal("[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");
    if (!std::regex_match(text, decimal)) return "";
    return text.find_first_of(".eE") == std::string::npos ? text + ".0" : text;
}
#endif
