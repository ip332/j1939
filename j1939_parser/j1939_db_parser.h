// C++ view over a compiled j1939_db_t. The lookup and the codec stay in C.
#ifndef J1939_DB_PARSER_H
#define J1939_DB_PARSER_H

#include "j1939_db.h"

#include <stdint.h>

class J1939DbParser {
    const j1939_db_t *db_;

public:
    explicit J1939DbParser(const j1939_db_t *db) : db_(db) {}

    const j1939_db_t *db() const { return db_; }

    const j1939_message_t *find(uint32_t can_id, bool extended) const {
        return j1939_db_find(db_, can_id, extended ? 1 : 0);
    }

    const j1939_signal_t *signalByName(const j1939_message_t *message, const char *name) const {
        return j1939_signal_by_name(db_, message, name);
    }

    const j1939_signal_t *signalBySpn(uint32_t spn, const j1939_message_t **message) const {
        return j1939_signal_by_spn(db_, spn, message);
    }
};

#endif
