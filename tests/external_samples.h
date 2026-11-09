//
// Locating the full canboat captures that scripts/fetch_test_logs.sh downloads, for the
// opt-in full-log tests.
//

#ifndef J1939_EXTERNAL_SAMPLES_H
#define J1939_EXTERNAL_SAMPLES_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// testdata/external/canboat/samples/, or $CANBOAT_SAMPLES_DIR.
inline std::string externalSamplesDir() {
    const char *env = getenv("CANBOAT_SAMPLES_DIR");
    if (env != nullptr && *env != 0) {
        return std::string(env) + "/";
    }
    return std::string(TESTDATA_FOLDER) + "external/canboat/samples/";
}

inline bool sampleFileExists(const std::string &path) {
    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        return false;
    }
    fclose(f);
    return true;
}

// J1939_FULL_LOGS_REQUIRED=1 turns a missing capture into a test failure instead of a skip,
// so a CI job whose fetch silently broke can't pass having tested nothing.
inline bool fullLogsRequired() {
    const char *env = getenv("J1939_FULL_LOGS_REQUIRED");
    return env != nullptr && strcmp(env, "1") == 0;
}

// Use at the top of a test: skips (or fails, see above) when `path` doesn't exist.
#define REQUIRE_EXTERNAL_SAMPLE(path)                                                         \
    do {                                                                                      \
        if (!sampleFileExists(path)) {                                                        \
            if (fullLogsRequired()) {                                                         \
                FAIL() << "canboat sample " << (path) << " not found and "                    \
                       << "J1939_FULL_LOGS_REQUIRED=1 -- run scripts/fetch_test_logs.sh";     \
            }                                                                                 \
            GTEST_SKIP() << "canboat sample " << (path) << " not found -- run "               \
                         << "scripts/fetch_test_logs.sh (or set CANBOAT_SAMPLES_DIR)";        \
        }                                                                                     \
    } while (0)

#endif //J1939_EXTERNAL_SAMPLES_H
