#include "dbc_parser.h"
#include "j1939_db.h"
#include "j1939_parser.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

class TempDir {
public:
    TempDir() {
        std::string pattern = "/tmp/j1939_rt_XXXXXX";
        std::vector<char> buf(pattern.begin(), pattern.end());
        buf.push_back('\0');
        if (mkdtemp(buf.data()) != nullptr) {
            path_ = buf.data();
        }
    }
    ~TempDir() {
        if (path_.empty()) {
            return;
        }
        remove((path_ + "/db.c").c_str());
        remove((path_ + "/db.h").c_str());
        remove((path_ + "/check.c").c_str());
        remove((path_ + "/db.so").c_str());
        remove((path_ + "/log.txt").c_str());
        rmdir(path_.c_str());
    }
    const std::string &path() const { return path_; }

private:
    std::string path_;
};

struct LoadedDb {
    void *handle = nullptr;
    const j1939_db_t *db = nullptr;
    ~LoadedDb() {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }
};

std::string fileText(const std::string &path) {
    std::ifstream in(path.c_str());
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

bool compileDatabase(const std::string &dir, const std::string &c_text, const std::string &h_text, std::string &symbol,
                     std::string &error) {
    {
        std::ofstream c(dir + "/db.c");
        std::ofstream h(dir + "/db.h");
        c << c_text;
        h << h_text;
    }
    const std::string marker = "extern const j1939_db_t ";
    auto pos = h_text.find(marker);
    auto end = h_text.find(';', pos);
    if (pos == std::string::npos || end == std::string::npos) {
        error = "generated header has no database symbol";
        return false;
    }
    symbol = h_text.substr(pos + marker.size(), end - (pos + marker.size()));
    std::ofstream check(dir + "/check.c");
    check << "#include \"db.h\"\nint compile_db(void) { return (int)" << symbol << ".message_count; }\n";
    check.close();
    std::string cmd = "cc -std=c11 -Wall -Wextra -Werror -shared -fPIC -I \"" + std::string(J1939_INCLUDE_DIR) +
                      "\" -I \"" + dir + "\" \"" + dir + "/check.c\" \"" + dir + "/db.c\" -o \"" + dir +
                      "/db.so\" > \"" + dir + "/log.txt\" 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        error = fileText(dir + "/log.txt");
        return false;
    }
    return true;
}

uint64_t fieldMask(uint8_t length) {
    if (length >= 64) {
        return ~0ull;
    }
    return (1ull << length) - 1ull;
}

void expectSameRaw(const j1939_db_t *db, const std::map<uint32_t, PGN> &cpp_db, uint32_t can_id, const PGN &pgn,
                   const SPN &spn, uint64_t pattern) {
    bool extended = (can_id & CAN_EFF_FLAG) != 0 || (can_id & 0x1FFFFFFFu) > 0x7FFu;
    const j1939_message_t *message = j1939_db_find(db, can_id, extended ? 1 : 0);
    ASSERT_NE(message, nullptr) << pgn.name_ << " id " << std::hex << can_id;
    const j1939_signal_t *sig = j1939_signal_by_name(db, message, spn.name_.c_str());
    ASSERT_NE(sig, nullptr) << pgn.name_ << " signal " << spn.name_;
    EXPECT_EQ(sig->start_bit, spn.start_bit_) << spn.name_;
    EXPECT_EQ(sig->length, spn.length_) << spn.name_;
    EXPECT_EQ((sig->flags & J1939_SIG_LITTLE_ENDIAN) != 0, spn.little_endian_) << spn.name_;

    J1939_frame writer;
    writer.reset(0);
    writer.write_bits(pattern, spn.start_bit_, spn.length_, spn.little_endian_);
    uint8_t payload[64];
    memset(payload, 0, sizeof(payload));
    memcpy(payload, writer.buffer_, sizeof(payload));

    uint64_t from_c = 0;
    ASSERT_EQ(j1939_signal_raw(payload, 64, sig, &from_c), 0) << pgn.name_ << " " << spn.name_;

    J1939_frame frame;
    ASSERT_TRUE(frame.setFrom(can_id, payload, 64)) << std::hex << can_id;
    J1939Parser view(frame, cpp_db);
    auto from_cpp = view.signalRawValue(spn);
    ASSERT_NE(from_cpp.get(), nullptr) << pgn.name_ << " " << spn.name_ << " can " << std::hex << can_id
                                       << " rebuilt " << frame.canID();
    EXPECT_EQ(from_c, *from_cpp) << pgn.name_ << " " << spn.name_;
    EXPECT_EQ(from_c, pattern & fieldMask(spn.length_)) << pgn.name_ << " " << spn.name_;
    EXPECT_EQ(*from_cpp, writer.read_bits(spn.start_bit_, spn.length_, spn.little_endian_));
}

void roundTrip(const std::string &dbc_path) {
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(dbc_path)) << dbc_path;
    std::ostringstream c_text;
    std::ostringstream h_text;
    std::string error;
    ASSERT_TRUE(parser.generateCOutput(c_text, h_text, false, error)) << error;
    parser.generateRunTimeFormat();

    TempDir dir;
    ASSERT_FALSE(dir.path().empty());
    std::string symbol;
    ASSERT_TRUE(compileDatabase(dir.path(), c_text.str(), h_text.str(), symbol, error)) << error;

    LoadedDb loaded;
    loaded.handle = dlopen((dir.path() + "/db.so").c_str(), RTLD_NOW);
    ASSERT_NE(loaded.handle, nullptr) << dlerror();
    loaded.db = static_cast<const j1939_db_t *>(dlsym(loaded.handle, symbol.c_str()));
    ASSERT_NE(loaded.db, nullptr) << dlerror();
    EXPECT_GT(loaded.db->message_count, 0u);
    EXPECT_EQ(loaded.db->strings == nullptr, false);

    for (const auto &pair : parser.dbc()) {
        for (const SPN &spn : pair.second.signals_) {
            uint64_t mask = fieldMask(spn.length_);
            expectSameRaw(loaded.db, parser.dbc(), pair.first, pair.second, spn, 0x0123456789ABCDEFULL & mask);
            expectSameRaw(loaded.db, parser.dbc(), pair.first, pair.second, spn, mask);
        }
    }
}

}  // namespace

TEST(J1939DbRoundTrip, J1939DbcMatchesTheCppMap) {
    roundTrip(std::string(DBC_FOLDER) + "j1939.dbc");
}

TEST(J1939DbRoundTrip, BigEndianDbcMatchesTheCppMap) {
    roundTrip(std::string(DBC_FOLDER) + "big_endian_database.dbc");
}

TEST(J1939DbRoundTrip, IdenticalRealLayoutsShareOneRow) {
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(std::string(DBC_FOLDER) + "j1939.dbc"));
    std::ostringstream c_text;
    std::ostringstream h_text;
    std::string error;
    ASSERT_TRUE(parser.generateCOutput(c_text, h_text, false, error)) << error;
    TempDir dir;
    std::string symbol;
    ASSERT_TRUE(compileDatabase(dir.path(), c_text.str(), h_text.str(), symbol, error)) << error;
    LoadedDb loaded;
    loaded.handle = dlopen((dir.path() + "/db.so").c_str(), RTLD_NOW);
    ASSERT_NE(loaded.handle, nullptr) << dlerror();
    loaded.db = static_cast<const j1939_db_t *>(dlsym(loaded.handle, symbol.c_str()));
    ASSERT_NE(loaded.db, nullptr) << dlerror();

    const j1939_message_t *vdc2 = j1939_db_find(loaded.db, 2565867787u, 1);
    const j1939_message_t *wabco = j1939_db_find(loaded.db, 2565867838u, 1);
    ASSERT_NE(vdc2, nullptr);
    EXPECT_EQ(vdc2, wabco);
    EXPECT_EQ(vdc2->sa_mask, 0u);

    const j1939_message_t *mux = j1939_db_find(loaded.db, 2565668650u, 1);
    ASSERT_NE(mux, nullptr);
    const j1939_signal_t *control = j1939_signal_by_name(loaded.db, mux, "ControlByte");
    const j1939_signal_t *size = j1939_signal_by_name(loaded.db, mux, "TotalMessageSize");
    ASSERT_NE(control, nullptr);
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(control->flags & J1939_SIG_MULTIPLEXOR, J1939_SIG_MULTIPLEXOR);
    EXPECT_EQ(size->flags & J1939_SIG_MULTIPLEXED, J1939_SIG_MULTIPLEXED);
    EXPECT_EQ(size->mux_value, 16u);
}
