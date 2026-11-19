file(GLOB DBC_PARSER_SRC dbc_parser/*)
# Test files colocated in dbc_parser/ must not end up compiled into the production
# library (define_lib's own glob-filter only covers j1939_parser/*.cpp, not this one).
list(FILTER DBC_PARSER_SRC EXCLUDE REGEX ".*_tests?\\.cpp$")

define_lib(j1939_parser ${DBC_PARSER_SRC} j1939_db.c)
