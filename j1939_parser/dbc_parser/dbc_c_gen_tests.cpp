#include "dbc_parser.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

class DbcCGenTest : public testing::Test {
protected:
    void TearDown() override {
        for (const auto &path : temp_paths_) {
            remove(path.c_str());
        }
        for (const auto &dir : temp_dirs_) {
            rmdir(dir.c_str());
        }
    }

    std::string writeTemp(const std::string &content) {
        std::string path = "/tmp/dbc_c_gen_XXXXXX.dbc";
        std::vector<char> buf(path.begin(), path.end());
        buf.push_back('\0');
        int fd = mkstemps(buf.data(), 4);
        FILE *f = fdopen(fd, "w");
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
        std::string result(buf.data());
        temp_paths_.push_back(result);
        return result;
    }

    struct Output {
        bool ok = false;
        std::string c;
        std::string h;
        std::string error;
    };

    Output generate(const std::string &dbc, bool no_strings = false, const std::string &name = "sample") {
        Output out;
        DbcParser parser;
        parser.setName(name);
        if (!parser.parseFile(writeTemp(dbc))) {
            out.error = "parse failed";
            return out;
        }
        std::ostringstream c;
        std::ostringstream h;
        out.ok = parser.generateCOutput(c, h, no_strings, out.error);
        out.c = c.str();
        out.h = h.str();
        return out;
    }

    std::vector<std::string> temp_paths_;
    std::vector<std::string> temp_dirs_;
};

const char *kSig = " SG_ Speed : 0|16@1+ (0.125,0) [0|8031.875] \"rpm\" X\n";

}  // namespace

TEST_F(DbcCGenTest, IdenticalLayoutsCollapseToOneGenericRow) {
    std::string dbc =
            "BO_ 2550202127 TSC1_0F: 8 X\n"
            " SG_ Speed : 0|16@1+ (0.125,0) [0|8031.875] \"rpm\" X\n"
            "\n"
            "BO_ 2550202154 TSC1_2A: 8 X\n"
            " SG_ Speed : 0|16@1+ (0.125,0) [0|8031.875] \"rpm\" X\n"
            "\n"
            "BA_ \"VFrameFormat\" BO_ 2550202127 3;\n"
            "BA_ \"SPN\" SG_ 2550202127 Speed 190;\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* TSC1 */"), std::string::npos);
    EXPECT_EQ(out.c.find("TSC1_0F"), std::string::npos);
    EXPECT_NE(out.c.find("0x0u, 0x0u, 0x0u, 0u, 1u,"), std::string::npos);
    EXPECT_NE(out.c.find("0.125"), std::string::npos);
    EXPECT_NE(out.c.find("190u"), std::string::npos);
    EXPECT_NE(out.h.find("#define TSC1_PGN 0x0u"), std::string::npos);
    EXPECT_NE(out.h.find("#define TSC1_Speed_SPN 190u"), std::string::npos);
    EXPECT_NE(out.h.find("SAMPLE_DB_H"), std::string::npos);
    EXPECT_EQ(out.c.find("0xFFu"), std::string::npos);
}

TEST_F(DbcCGenTest, UnsiffixedNameWinsAndSpnFollowsTheLowestId) {
    std::string dbc =
            "BO_ 2550202127 TSC1_0F: 8 X\n" +
            std::string(kSig) +
            "\n"
            "BO_ 2550202154 TSC1: 8 X\n" +
            std::string(kSig) +
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* TSC1 */"), std::string::npos);
    EXPECT_EQ(out.c.find("TSC1_0F"), std::string::npos);
}

TEST_F(DbcCGenTest, DifferentLayoutsStaySourceSpecificWithoutADefault) {
    std::string dbc =
            "BO_ 2565867787 VDC2: 8 X\n"
            " SG_ Yaw : 24|16@1+ (1,0) [0|65535] \"\" X\n"
            "\n"
            "BO_ 2565867838 VDC2_Wabco: 8 X\n"
            " SG_ Yaw : 24|8@1+ (1,0) [0|255] \"\" X\n"
            "\n"
            "BA_ \"VFrameFormat\" BO_ 2565867787 3;\n"
            "BA_ \"J1939Default\" BO_ 2565867787 0;\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* VDC2 */"), std::string::npos);
    EXPECT_NE(out.c.find("{ /* VDC2_Wabco */"), std::string::npos);
    EXPECT_NE(out.c.find("0xF0090Bu, 0xFFu, 11u, 1u,"), std::string::npos);
    EXPECT_NE(out.c.find("0xF0093Eu, 0xFFu, 62u, 1u,"), std::string::npos);
    EXPECT_EQ(out.c.find("0xF00900u, 0x0u, 0u, 1u,"), std::string::npos);
}

TEST_F(DbcCGenTest, MarkedLayoutIsTheGenericRow) {
    std::string dbc =
            "BO_ 2565866513 EngSpeed: 8 X\n"
            " SG_ Speed : 24|16@1+ (0.125,0) [0|8031.875] \"rpm\" X\n"
            "\n"
            "BO_ 2565866530 Other: 8 X\n"
            " SG_ Speed : 24|8@1+ (1,0) [0|255] \"\" X\n"
            "\n"
            "BA_ \"J1939Default\" BO_ 2565866513 1;\n"
            "BA_ \"SPN\" BO_ 2565866513 61444;\n"
            "BA_ \"SPN\" SG_ 2565866513 Speed 190;\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* EngSpeed */"), std::string::npos);
    EXPECT_NE(out.c.find("0xF00400u, 0x0u, 0u, 1u,"), std::string::npos);
    EXPECT_NE(out.c.find("{ /* Other */"), std::string::npos);
    EXPECT_NE(out.c.find("0xF00422u, 0xFFu, 34u, 1u,"), std::string::npos);
    EXPECT_EQ(out.c.find("0xF00411u"), std::string::npos);
    EXPECT_NE(out.h.find("#define EngSpeed_PGN 0xF004u"), std::string::npos);
    EXPECT_NE(out.h.find("#define EngSpeed_SPN 61444u"), std::string::npos);
    EXPECT_NE(out.h.find("#define EngSpeed_Speed_SPN 190u"), std::string::npos);
}

TEST_F(DbcCGenTest, TwoDefaultsWithDifferentLayoutsFail) {
    std::string dbc =
            "BO_ 2565866513 One: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866530 Two: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BA_ \"J1939Default\" BO_ 2565866513 1;\n"
            "BA_ \"J1939Default\" BO_ 2565866530 1;\n"
            "\n";
    std::ostringstream c;
    std::ostringstream h;
    c << "SENTINEL";
    h << "SENTINEL";
    DbcParser parser;
    parser.setName("sample");
    ASSERT_TRUE(parser.parseFile(writeTemp(dbc)));
    std::string error;
    EXPECT_FALSE(parser.generateCOutput(c, h, false, error));
    EXPECT_EQ(error, "two generic rows for PGN 0xF004 are not identical");
    EXPECT_EQ(c.str(), "SENTINEL");
    EXPECT_EQ(h.str(), "SENTINEL");
}

TEST_F(DbcCGenTest, TwoDefaultsWithTheSameLayoutShareOneGenericRow) {
    std::string dbc =
            "BO_ 2364539921 Low: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866513 High: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866530 Other: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BA_ \"J1939Default\" BO_ 2364539921 1;\n"
            "BA_ \"J1939Default\" BO_ 2565866513 1;\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* Low */"), std::string::npos);
    EXPECT_EQ(out.c.find("{ /* High */"), std::string::npos);
    EXPECT_NE(out.c.find("{ /* Other */"), std::string::npos);
    EXPECT_NE(out.c.find("0xF00400u, 0x0u, 0u, 1u,"), std::string::npos);
}

TEST_F(DbcCGenTest, SameSourceAddressWithTwoLayoutsFails) {
    std::string dbc =
            "BO_ 2364539921 Low: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866513 High: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n";
    Output out = generate(dbc);
    EXPECT_FALSE(out.ok);
    EXPECT_EQ(out.error, "PGN 0xF004 has two layouts for source address 17");
    EXPECT_TRUE(out.c.empty());
}

TEST_F(DbcCGenTest, SameSourceAddressAndLayoutKeepsTheFirstRow) {
    std::string dbc =
            "BO_ 2364539921 Generic: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2364539938 First: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866530 Second: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BA_ \"J1939Default\" BO_ 2364539921 1;\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* Generic */"), std::string::npos);
    EXPECT_NE(out.c.find("{ /* First */"), std::string::npos);
    EXPECT_EQ(out.c.find("{ /* Second */"), std::string::npos);
}

TEST_F(DbcCGenTest, PriorityIsNotIdentity) {
    std::string dbc =
            "BO_ 2364539921 EEC1_11: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866513 EEC1_0B: 8 X\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|1] \"\" X\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* EEC1 */"), std::string::npos);
    EXPECT_EQ(out.c.find("EEC1_11"), std::string::npos);
    EXPECT_EQ(out.c.find("EEC1_0B"), std::string::npos);
    EXPECT_EQ(out.c.find("0xFFu"), std::string::npos);
}

TEST_F(DbcCGenTest, EndiannessAndOtherLayoutFieldsDoNotCollapse) {
    const uint32_t left = 2565866513u;
    const uint32_t right = 2565866530u;
    struct Case {
        const char *left;
        const char *right;
        bool same;
    };
    const Case cases[] = {
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", true},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 8|8@1+ (1,0) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|4@1+ (1,0) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1- (1,0) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@0+ (1,0) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (2,0) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,1) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,0) [1|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,0) [0|10] \"\" X\n", false},
            {" SG_ S M : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", false},
            {" SG_ S m1 : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", false},
            {" SG_ S m1 : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S m2 : 0|8@1+ (1,0) [0|255] \"\" X\n", false},
            {" SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n", false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        std::ostringstream dbc;
        const bool branches = std::string(cases[i].left).find(" m") != std::string::npos ||
                              std::string(cases[i].right).find(" m") != std::string::npos;
        const std::string sw = branches ? " SG_ Mode M : 56|8@1+ (1,0) [0|255] \"\" X\n" : "";
        dbc << "BO_ " << left << " Left: 8 X\n" << cases[i].left << sw << "\n";
        dbc << "BO_ " << right << " Right: 8 X\n" << cases[i].right << sw << "\n";
        if (i + 1 == sizeof(cases) / sizeof(cases[0])) {
            dbc << "VAL_ " << right << " S 1 \"On\" ;\n";
        }
        Output out = generate(dbc.str(), false, "layout");
        ASSERT_TRUE(out.ok) << i << " " << out.error;
        bool one = out.c.find("{ /* Right */") == std::string::npos;
        EXPECT_EQ(one, cases[i].same) << i;
    }

    std::string dlc =
            "BO_ 2565866513 Left: 8 X\n"
            " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n"
            "\n"
            "BO_ 2565866530 Right: 7 X\n"
            " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n"
            "\n";
    Output out = generate(dlc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* Right */"), std::string::npos);

    std::string extra =
            "BO_ 2565866513 Left: 8 X\n"
            " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n"
            "\n"
            "BO_ 2565866530 Right: 8 X\n"
            " SG_ S : 0|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ T : 8|8@1+ (1,0) [0|255] \"\" X\n"
            "\n";
    out = generate(extra);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.c.find("{ /* Right */"), std::string::npos);
}

TEST_F(DbcCGenTest, SpnNamesMuxBytesAndNoStrings) {
    std::string dbc =
            "BO_ 100 A-B: 8 X\n"
            " SG_ Mode M : 8|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ Z m1 : 24|4@1+ (1,0) [0|1] \"\" X\n"
            " SG_ A m0 : 24|4@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Be : 7|2@0+ (1.0,0) [0|3] \"\" X\n"
            " SG_ Signed : 32|8@1- (1,0) [-128|127] \"\" X\n"
            "\n"
            "BO_ 101 A_B: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|255] \"\" X\n"
            "\n"
            "BO_ 2565865233 Mux: 8 X\n"
            " SG_ Switch M : 0|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ B m1 : 8|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ A m0 : 8|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ Both m1 : 16|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Alt m0 : 16|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Wide m3 : 40|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Bare m0 : 48|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Tail m12 : 56|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 300 VinMsg: 16 X\n"
            " SG_ Vin : 0|80@1+ (1,0) [0|0] \"\" X\n"
            "\n"
            "BO_ 301 : 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 50 Ghost: 0 X\n"
            " SG_ Z : 0|0@1+ (1,0) [0|0] \"\" X\n"
            "\n"
            "BO_ 418382865 PlainExt: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "VAL_ 100 A 1 \"On\" 0 Off ;\n"
            "BA_ \"SPN\" SG_ 101 Speed 10;\n"
            "BA_ \"SPN\" SG_ 100 Missing 1;\n"
            "BA_ noquote;\n"
            "BA_ \"SPN;\n"
            "BA_ \"Other\" BO_ 100 1;\n"
            "BA_ \"SPN\" BO_;\n"
            "BA_ \"SPN\" SG_ 101 Speed;\n"
            "BA_ \"SPN\" BO_ 101 abc;\n"
            "BA_ \"J1939Default\" SG_ 100 A 1;\n"
            "\n";
    DbcParser parser;
    parser.setName("sample");
    EXPECT_FALSE(parser.parseFile(writeTemp(dbc)));

    dbc =
            "BO_ 100 A-B: 8 X\n"
            " SG_ Mode M : 8|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ Z m1 : 24|4@1+ (1,0) [0|1] \"\" X\n"
            " SG_ A m0 : 24|4@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Be : 7|2@0+ (1.0,0) [0|3] \"\" X\n"
            " SG_ Signed : 32|8@1- (1,0) [-128|127] \"\" X\n"
            "\n"
            "BO_ 101 A_B: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|255] \"\" X\n"
            "\n"
            "BO_ 2565865233 Mux: 8 X\n"
            " SG_ Switch M : 0|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ B m1 : 8|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ A m0 : 8|8@1+ (1,0) [0|255] \"\" X\n"
            " SG_ Both m1 : 16|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Alt m0 : 16|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Wide m3 : 40|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Bare m0 : 48|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Tail m12 : 56|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 300 VinMsg: 16 X\n"
            " SG_ Vin : 0|80@1+ (1,0) [0|0] \"\" X\n"
            "\n"
            "BO_ 301 : 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 50 Ghost: 0 X\n"
            " SG_ Z : 0|0@1+ (1,0) [0|0] \"\" X\n"
            "\n"
            "BO_ 418382865 PlainExt: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "VAL_ 2565865233 B 1 \"On\" ;\n"
            "VAL_ 100 A 1 \"On\" 0 Off ;\n"
            "BA_ \"SPN\" SG_ 101 Speed 10;\n"
            "BA_ \"SPN\" BO_ 101 42;\n"
            "BA_ noquote;\n"
            "BA_ \"SPN;\n"
            "BA_ \"Other\" BO_ 100 1;\n"
            "BA_ \"SPN\" BO_;\n"
            "BA_ \"SPN\" SG_ 101 Speed;\n"
            "BA_ \"SPN\" BO_ 101 abc;\n"
            "BA_ \"J1939Default\" SG_ 100 A 1;\n"
            "BA_ \"J1939Default\" BO_ 999999 1;\n"
            "\n";
    parser.setName("sample");
    // Unknown CAN id is an error even when the rest of the file is valid.
    EXPECT_FALSE(parser.parseFile(writeTemp(dbc)));

    auto cut = dbc.rfind("BA_ \"J1939Default\" BO_ 999999");
    dbc.resize(cut);
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_EQ(out.c.find("Ghost"), std::string::npos);
    EXPECT_NE(out.c.find("J1939_SIG_BYTES"), std::string::npos);
    EXPECT_NE(out.c.find("J1939_SIG_SIGNED"), std::string::npos);
    EXPECT_NE(out.c.find("J1939_SIG_LITTLE_ENDIAN | J1939_SIG_MULTIPLEXOR,"), std::string::npos);
    EXPECT_EQ(out.c.find("J1939_SIG_LITTLE_ENDIAN | J1939_SIG_MULTIPLEXOR | J1939_SIG_MULTIPLEXED"),
              std::string::npos);
    EXPECT_NE(out.h.find("#define A_B_CAN_ID 0x64u"), std::string::npos);
    EXPECT_NE(out.h.find("#define A_B_CAN_ID_2 0x65u"), std::string::npos);
    EXPECT_NE(out.h.find("#define unnamed_CAN_ID 0x12Du"), std::string::npos);
    EXPECT_NE(out.h.find("#define A_B_SPN 42u"), std::string::npos);
    EXPECT_NE(out.h.find("#define Mux_PGN "), std::string::npos);
    EXPECT_NE(out.h.find("#define PlainExt_PGN "), std::string::npos);

    auto pos_a = out.c.find("/* A */");
    auto pos_z = out.c.find("/* Z */");
    ASSERT_NE(pos_a, std::string::npos);
    ASSERT_NE(pos_z, std::string::npos);
    EXPECT_LT(pos_a, pos_z);

    auto mux = out.c.find("/* Switch */");
    auto a_m0 = out.c.find("/* A */", mux);
    auto b_m0 = out.c.find("/* B */", mux);
    ASSERT_NE(mux, std::string::npos);
    ASSERT_NE(a_m0, std::string::npos);
    ASSERT_NE(b_m0, std::string::npos);
    EXPECT_LT(a_m0, b_m0);
    auto alt = out.c.find("/* Alt */");
    auto both = out.c.find("/* Both */");
    ASSERT_NE(alt, std::string::npos);
    ASSERT_NE(both, std::string::npos);
    EXPECT_LT(alt, both);

    EXPECT_EQ(out.c.find("1.0f"), std::string::npos);
    EXPECT_EQ(out.c.find("\"On\""), std::string::npos);
    EXPECT_NE(out.c.find("0x4F, 0x6E"), std::string::npos);

    std::string shared_off;
    int speed_count = 0;
    std::string marker = "/* Speed */";
    for (size_t at = 0; (at = out.c.find(marker, at)) != std::string::npos; at += marker.size()) {
        auto line_end = out.c.find('\n', at);
        auto next_end = out.c.find('\n', line_end + 1);
        std::string init = out.c.substr(line_end + 1, next_end - line_end - 1);
        auto num = init.find_first_of("0123456789");
        auto stop = init.find('u', num);
        std::string off = init.substr(num, stop - num);
        if (shared_off.empty()) {
            shared_off = off;
        }
        EXPECT_EQ(off, shared_off) << init;
        speed_count++;
    }
    EXPECT_GE(speed_count, 2);

    DbcParser legacy;
    legacy.setName("sample");
    ASSERT_TRUE(legacy.parseFile(writeTemp(dbc)));
    legacy.generateRunTimeFormat();
    const PGN &mux_pgn = legacy.dbc().at(2565865233u);
    int named_b = 0;
    for (const auto &spn : mux_pgn.signals_) {
        if (spn.name_ == "B") {
            named_b++;
        }
    }
    EXPECT_EQ(named_b, 1);
    // Switch, A, B, Both, Alt, Wide, Bare, Tail. A shares bit 8 with B, Alt shares bit 16 with Both.
    EXPECT_EQ(mux_pgn.signals_.size(), 8u);
    int named_a = 0;
    int named_alt = 0;
    for (const auto &spn : mux_pgn.signals_) {
        if (spn.name_ == "A") {
            named_a++;
        }
        if (spn.name_ == "Alt") {
            named_alt++;
        }
    }
    EXPECT_EQ(named_a, 1);
    EXPECT_EQ(named_alt, 1);

    Output stripped = generate(dbc, true, "sample");
    ASSERT_TRUE(stripped.ok) << stripped.error;
    EXPECT_NE(stripped.c.find("J1939_NO_NAME"), std::string::npos);
    EXPECT_EQ(stripped.c.find("_strings"), std::string::npos);
    EXPECT_EQ(stripped.c.find("_name_index"), std::string::npos);
    EXPECT_NE(stripped.h.find("#define A_B_Speed_SPN 10u"), std::string::npos);
}

TEST_F(DbcCGenTest, ZeroLengthAndUnalignedWideSignalsFail) {
    Output zero = generate(
            "BO_ 100 M: 8 X\n"
            " SG_ Z : 0|0@1+ (1,0) [0|0] \"\" X\n"
            "\n");
    EXPECT_FALSE(zero.ok);
    EXPECT_EQ(zero.error, "signal Z has zero length");

    Output wide = generate(
            "BO_ 100 M: 16 X\n"
            " SG_ Vin : 0|70@1+ (1,0) [0|0] \"\" X\n"
            "\n");
    EXPECT_FALSE(wide.ok);
    EXPECT_NE(wide.error.find("Vin"), std::string::npos);
    EXPECT_NE(wide.error.find("not a whole number of aligned bytes"), std::string::npos);

    Output shift = generate(
            "BO_ 100 M: 16 X\n"
            " SG_ Vin : 4|80@1+ (1,0) [0|0] \"\" X\n"
            "\n");
    EXPECT_FALSE(shift.ok);
    EXPECT_NE(shift.error.find("Vin"), std::string::npos);
}

TEST_F(DbcCGenTest, EmptyDatabaseStillEmitsASymbol) {
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 50 Ghost: 0 X\n\n")));
    parser.setName("");
    std::ostringstream c;
    std::ostringstream h;
    std::string error;
    ASSERT_TRUE(parser.generateCOutput(c, h, false, error)) << error;
    EXPECT_NE(c.str().find("const j1939_db_t j1939_db"), std::string::npos);
    EXPECT_NE(c.str().find("j1939_strings"), std::string::npos);
    EXPECT_NE(c.str().find("0u,"), std::string::npos);
    EXPECT_NE(h.str().find("J1939_DATABASE_H"), std::string::npos);
    EXPECT_EQ(c.str().find("Ghost"), std::string::npos);
}

TEST_F(DbcCGenTest, SpnIndexOrdersDuplicatesAndDigitNames) {
    std::string dbc =
            "BO_ 70 2Fast: 8 X\n"
            " SG_ Bit : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 200 Same: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Speed : 16|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Other : 24|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 201 Other: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            " SG_ Late : 8|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BO_ 2565866513 Eng: 8 X\n"
            " SG_ Rpm : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BA_ \"SPN\" SG_ 200 Speed 50;\n"
            "BA_ \"SPN\" SG_ 200 Other 50;\n"
            "BA_ \"SPN\" SG_ 201 Speed 50;\n"
            "BA_ \"SPN\" SG_ 201 Late 7;\n"
            "BA_ \"SPN\" SG_ 2565866513 Rpm 190;\n"
            "BA_ \"SPN\" BO_ zz 1;\n"
            "BA_ \"SPN\" BO_ 200 5000000000;\n"
            "\n";
    Output out = generate(dbc);
    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.h.find("#define _2Fast_CAN_ID 0x46u"), std::string::npos);
    EXPECT_EQ(out.h.find("Same_SPN"), std::string::npos);

    auto index = out.c.find("_spn_index[]");
    ASSERT_NE(index, std::string::npos);
    auto index_end = out.c.find("};", index);
    std::string spns = out.c.substr(index, index_end - index);
    auto seven = spns.find("{ 7u, ");
    auto fifty_same = spns.find("{ 50u, 1u, 0u }");
    auto fifty_other_sig = spns.find("{ 50u, 1u, 2u }");
    auto fifty_other_msg = spns.find("{ 50u, 2u, 0u }");
    auto rpm = spns.find("{ 190u, ");
    ASSERT_NE(seven, std::string::npos);
    ASSERT_NE(fifty_same, std::string::npos);
    ASSERT_NE(fifty_other_sig, std::string::npos);
    ASSERT_NE(fifty_other_msg, std::string::npos);
    ASSERT_NE(rpm, std::string::npos);
    EXPECT_LT(seven, fifty_same);
    EXPECT_LT(fifty_same, fifty_other_sig);
    EXPECT_LT(fifty_other_sig, fifty_other_msg);
    EXPECT_LT(fifty_other_msg, rpm);

    int speed_comments = 0;
    for (size_t at = 0; (at = out.c.find("/* Speed */", at)) != std::string::npos; at += 11) {
        speed_comments++;
    }
    EXPECT_EQ(speed_comments, 3);
}

TEST_F(DbcCGenTest, UnknownSignalSpnFailsTheParse) {
    DbcParser parser;
    parser.setVerbose(false);
    std::string dbc =
            "BO_ 100 M: 8 X\n"
            " SG_ Speed : 0|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "BA_ \"SPN\" SG_ 100 Missing 5;\n";
    EXPECT_FALSE(parser.parseFile(writeTemp(dbc)));
}

TEST_F(DbcCGenTest, GeneratedCCompiles) {
    std::string dbc =
            "BO_ 96 M: 8 X\n"
            " SG_ S : 7|16@0- (1,0) [-10|10] \"\" X\n"
            " SG_ Vin : 16|80@1+ (1,0) [0|0] \"\" X\n"
            "\n"
            "BO_ 2565866513 EEC1: 8 X\n"
            " SG_ Speed : 24|16@1+ (0.125,0) [0|8031.875] \"rpm\" X\n"
            " SG_ Mode M : 0|4@1+ (1,0) [0|15] \"\" X\n"
            " SG_ Alt m1 : 8|8@1+ (1,0) [0|1] \"\" X\n"
            "\n"
            "VAL_ 2565866513 Mode 1 \"On\" 0 \"Off\" ;\n"
            "BA_ \"SPN\" SG_ 2565866513 Speed 190;\n"
            "\n";
    Output out = generate(dbc, false, "sample_j1939");
    ASSERT_TRUE(out.ok) << out.error;
    std::string dir = "/tmp/j1939_c_gen_XXXXXX";
    std::vector<char> dbuf(dir.begin(), dir.end());
    dbuf.push_back('\0');
    ASSERT_NE(mkdtemp(dbuf.data()), nullptr);
    std::string root(dbuf.data());
    temp_paths_.push_back(root + "/db.c");
    temp_paths_.push_back(root + "/db.h");
    temp_paths_.push_back(root + "/check.c");
    temp_paths_.push_back(root + "/db.so");
    temp_paths_.push_back(root + "/log.txt");
    temp_dirs_.push_back(root);
    {
        std::ofstream c(root + "/db.c");
        std::ofstream h(root + "/db.h");
        c << out.c;
        h << out.h;
    }
    std::ofstream check(root + "/check.c");
    check << "#include \"db.h\"\nint compile_db(void) { return (int)sample_j1939_db.message_count; }\n";
    check.close();
    std::string cmd = "cc -std=c11 -Wall -Wextra -Werror -shared -fPIC -I \"" + std::string(J1939_INCLUDE_DIR) +
                      "\" -I \"" + root + "\" \"" + root + "/check.c\" \"" + root + "/db.c\" -o \"" + root +
                      "/db.so\" > \"" + root + "/log.txt\" 2>&1";
    int status = std::system(cmd.c_str());
    if (status != 0) {
        std::ifstream log(root + "/log.txt");
        std::stringstream got;
        got << log.rdbuf();
        ADD_FAILURE() << got.str();
    }
}

TEST_F(DbcCGenTest, PortableAccessorsCompileAndRunInCAndCpp) {
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(writeTemp(
        "BO_ 100 Mixed: 16 X\n"
        " SG_ Intel : 3|13@1+ (0.5,-2) [0|4093] \"\" X\n"
        " SG_ Motorola : 23|12@0- (1,0) [-2048|2047] \"\" X\n"
        " SG_ Wide : 32|64@1- (1,0) [0|0] \"\" X\n"
        " SG_ Switch M : 96|8@1+ (1,0) [0|255] \"\" X\n"
        " SG_ A m0 : 104|8@1+ (1,0) [0|255] \"\" X\n"
        " SG_ B m1 : 104|8@1+ (1,0) [0|255] \"\" X\n"
        "\nBO_ 101 Omitted: 1 X\n SG_ Other : 0|8@1+ (1,0) [0|255] \"\" X\n"
        "\nBO_ 102 Bytes: 10 X\n SG_ Text : 0|80@1+ (1,0) [0|0] \"\" X\n"
        "\nVAL_ 100 Intel 3 \"Three\" 4 \"On-Off\" 5 \"On Off\" ;\n")));
    std::ostringstream header;
    std::string error;
    ASSERT_TRUE(parser.generateAccessorOutput(header, error)) << error;
    EXPECT_EQ(header.str().find("__attribute__"), std::string::npos);
    EXPECT_EQ(header.str().find("union"), std::string::npos);
    std::string source =
        "#define J1939_ACCESSORS_SELECT\n#define J1939_ACCESSORS_Mixed_100\n#define J1939_ACCESSORS_Bytes_102\n#include \"" + writeTemp(header.str()) + "\"\n" +
        "\n#ifdef j1939_Omitted_101_DLC\n#error Message selection failed\n#endif\n"
        "#include <assert.h>\nint main(void) {\n"
        "uint8_t p[16]; memset(p, 255, sizeof(p));\n"
        "j1939_Mixed_100_Intel_store_raw(p, 0x1234);\n"
        "assert(p[0] == 0xa7 && p[1] == 0x91 && p[2] == 255);\n"
        "assert(j1939_Mixed_100_Intel_raw(p) == 0x1234);\n"
        "assert(j1939_Mixed_100_Intel_phys(p) == 2328);\n"
        "assert(j1939_Mixed_100_Intel_Three == 3);\n"
        "assert(j1939_Mixed_100_Intel_On_Off == 4);\n"
        "assert(j1939_Mixed_100_Intel_On_Off_5 == 5);\n"
        "j1939_Mixed_100_Motorola_store_raw(p, 0xabc);\n"
        "assert(p[2] == 0xab && p[3] == 0xcf);\n"
        "assert(j1939_Mixed_100_Motorola_raw(p) == 0xabc);\n"
        "assert(j1939_Mixed_100_Motorola_phys(p) == -1348);\n"
        "j1939_Mixed_100_Wide_store_raw(p, UINT64_C(0x8000000000000000));\n"
        "assert(j1939_Mixed_100_Wide_raw(p) == UINT64_C(0x8000000000000000));\n"
        "assert(j1939_Mixed_100_Wide_phys(p) == -9223372036854775808.0);\n"
        "j1939_Mixed_100_A_store_raw(p, 42);\n"
        "assert(j1939_Mixed_100_B_raw(p) == 42);\n"
        "uint8_t text[10] = {1,2,3,4,5,6,7,8,9,10}, copy[10];\n"
        "j1939_Bytes_102_Text_set_bytes(p, text);\n"
        "j1939_Bytes_102_Text_get_bytes(p, copy);\n"
        "assert(memcmp(text, copy, 10) == 0);\nreturn 0; }\n";
    std::string path = writeTemp(source);
    for (const std::string compiler : {"cc -x c -std=c11", "c++ -x c++ -std=c++14"}) {
        std::string binary = path + ".bin";
        temp_paths_.push_back(binary);
        ASSERT_EQ(system((compiler + " -Wall -Wextra -Werror -pedantic '" + path + "' -o '" + binary + "'").c_str()), 0);
        EXPECT_EQ(system(("'" + binary + "'").c_str()), 0);
    }
}

TEST_F(DbcCGenTest, AccessorsRejectInvalidLayoutWithoutPartialOutput) {
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 Bad: 1 X\n SG_ TooWide : 7|16@0+ (1,0) [0|0] \"\" X\n\n")));
    std::ostringstream out;
    std::string error;
    EXPECT_FALSE(parser.generateAccessorOutput(out, error));
    EXPECT_TRUE(out.str().empty());
    EXPECT_FALSE(error.empty());
}

TEST_F(DbcCGenTest, AccessorsRejectZeroLengthUnalignedBytesAndIdentifierCollisions) {
    const std::vector<std::string> cases{
        " SG_ Empty : 0|0@1+ (1,0) [0|0] \"\" X\n",
        " SG_ Text : 1|80@1+ (1,0) [0|0] \"\" X\n",
        " SG_ Text : 0|81@1+ (1,0) [0|0] \"\" X\n",
        " SG_ Text : 7|80@0+ (1,0) [0|0] \"\" X\n",
        " SG_ A-B : 0|8@1+ (1,0) [0|0] \"\" X\n SG_ A_B : 8|8@1+ (1,0) [0|0] \"\" X\n",
        " SG_ Same : 0|8@1+ (1,0) [0|0] \"\" X\n SG_ Same : 8|8@1+ (1,0) [0|0] \"\" X\n",
    };
    for (const auto &signals : cases) {
        DbcParser parser;
        ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 Bad: 16 X\n" + signals + "\n")));
        std::ostringstream out;
        std::string error;
        EXPECT_FALSE(parser.generateAccessorOutput(out, error)) << signals;
        EXPECT_TRUE(out.str().empty());
        EXPECT_FALSE(error.empty());
    }
}

TEST_F(DbcCGenTest, GeneratedPgnRetainsMultiplexMetadata) {
    Message message("BO_ 100 Mux: 3 X\n");
    ASSERT_TRUE(message.addSignal(" SG_ Switch M : 0|8@1+ (1,0) [0|255] \"\" X\n"));
    ASSERT_TRUE(message.addSignal(" SG_ Nested m1M : 8|8@1+ (1,0) [0|255] \"\" X\n"));
    ASSERT_TRUE(message.addSignal(" SG_ Alternative m1 : 16|8@1+ (1,0) [0|255] \"\" X\n"));
    std::string source = "#include \"j1939_frame.h\"\n#include <cassert>\nconst PGN pgn = " + message.toPgnString() +
        ";\nint main() { assert(pgn.signals_[0].multiplexor_); assert(!pgn.signals_[0].multiplexed_);"
        "assert(pgn.signals_[1].multiplexor_ && pgn.signals_[1].multiplexed_);"
        "assert(pgn.signals_[2].multiplexed_ && pgn.signals_[2].mux_value_ == 1); }\n";
    std::string path = writeTemp(source);
    std::string binary = path + ".bin";
    temp_paths_.push_back(binary);
    ASSERT_EQ(system(("c++ -x c++ -std=c++14 -Wall -Wextra -Werror -I '" + std::string(J1939_INCLUDE_DIR) +
                      "' '" + path + "' -o '" + binary + "'").c_str()), 0);
    EXPECT_EQ(system(("'" + binary + "'").c_str()), 0);
}

// Literal payloads and arithmetic expectations, independent of either encoder.
TEST_F(DbcCGenTest, QuarryCPathsDecodeIndependentSingleFrameVectors) {
    DbcParser parser;
    parser.setName("vectors");
    ASSERT_TRUE(parser.parseFile(writeTemp(
        "BO_ 100 Vector: 8 X\n"
        " SG_ Intel : 3|13@1+ (0.5,-2) [0|4093] \"rpm\" X\n"
        " SG_ Motorola : 23|12@0- (0.25,10) [-502|521.75] \"deg\" X\n"
        " SG_ Switch M : 32|8@1+ (1,0) [0|255] \"\" X\n"
        " SG_ A m0 : 40|8@1+ (1,0) [0|255] \"\" X\n"
        " SG_ B m1 : 40|8@1+ (1,0) [0|255] \"\" X\n\n")));
    std::ostringstream c, h, accessors;
    std::string error;
    ASSERT_TRUE(parser.generateCOutput(c, h, false, error)) << error;
    ASSERT_TRUE(parser.generateAccessorOutput(accessors, error)) << error;
    std::string source = c.str() + "#include \"" + writeTemp(accessors.str()) + "\"\n" + R"C(
#include <assert.h>
int main(void) {
    const uint8_t p[8] = {0xa7,0x91,0xab,0xcf,0,42,0,0};
    const j1939_message_t *m = j1939_db_find(&vectors_db,100,0);
    const j1939_signal_t *i = j1939_signal_by_name(&vectors_db,m,"Intel");
    const j1939_signal_t *b = j1939_signal_by_name(&vectors_db,m,"Motorola");
    const j1939_signal_t *a = j1939_signal_by_name(&vectors_db,m,"A");
    const j1939_signal_t *alt = j1939_signal_by_name(&vectors_db,m,"B");
    uint64_t raw = 99;
    assert(i && b && a && alt);
    assert(j1939_signal_raw(p,2,i,&raw)==0 && raw==0x1234);
    assert(j1939_signal_phys(i,raw)==2328);
    assert(j1939_Vector_100_Intel_raw(p)==0x1234);
    assert(j1939_Vector_100_Intel_phys(p)==2328);
    assert(j1939_signal_raw(p,4,b,&raw)==0 && raw==0xabc);
    /* 0xabc is -1348 in signed 12-bit; -1348/4+10 = -327. */
    assert(j1939_signal_phys(b,raw)==-327);
    assert(j1939_Vector_100_Motorola_raw(p)==0xabc);
    assert(j1939_Vector_100_Motorola_phys(p)==-327);
    for (uint16_t n=0;n<4;++n) {
        raw=99;
        assert(j1939_signal_raw(p,n,b,&raw)==-1 && raw==99);
    }
    assert(j1939_signal_raw(p,1,i,&raw)==-1);
    assert(j1939_signal_raw(NULL,8,i,&raw)==-1);
    assert(j1939_signal_active(&vectors_db,m,p,8,a)==1);
    assert(j1939_signal_active(&vectors_db,m,p,8,alt)==0);
    assert(j1939_signal_active(&vectors_db,m,p,4,a)==-1);
    uint8_t switched[8] = {0xa7,0x91,0xab,0xcf,1,42,0,0};
    assert(j1939_signal_active(&vectors_db,m,switched,8,a)==0);
    assert(j1939_signal_active(&vectors_db,m,switched,8,alt)==1);
    /* Switch is present at DLC 5, but the selected branch is still truncated. */
    assert(j1939_signal_active(&vectors_db,m,switched,5,alt)==1);
    assert(j1939_signal_raw(switched,5,alt,&raw)==-1);
    switched[4]=2;
    assert(j1939_signal_active(&vectors_db,m,switched,8,a)==0);
    assert(j1939_signal_active(&vectors_db,m,switched,8,alt)==0);
    /* Accessors require a complete DLC and do not gate inactive alternatives. */
    assert(j1939_Vector_100_A_raw(p)==42 && j1939_Vector_100_B_raw(p)==42);
    j1939_signal_t wide = {0};
    wide.length=64; wide.flags=J1939_SIG_SIGNED; wide.scale=1;
    assert(j1939_signal_phys(&wide,UINT64_C(0x8000000000000000))==-9223372036854775808.0);
    assert(j1939_signal_phys(&wide,UINT64_MAX)==-1);
    return 0;
}
)C";
    std::string path = writeTemp(source);
    std::string binary = path + ".bin";
    temp_paths_.push_back(binary);
    ASSERT_EQ(system(("cc -x c -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined,address -I '" +
        std::string(J1939_INCLUDE_DIR) + "' '" + path + "' '" + std::string(J1939_INCLUDE_DIR) +
        "/j1939_db.c' -o '" + binary + "'").c_str()),0);
    EXPECT_EQ(system(("'" + binary + "'").c_str()),0);
}

TEST_F(DbcCGenTest, RejectsUnsupportedWireSemantics) {
    for (const std::string declaration : {
             "SG_MUL_VAL_ 100 A Switch 0-2;", "SIG_VALTYPE_ 100 A : 1;"}) {
        DbcParser parser;
        const auto path = writeTemp("BO_ 100 M: 8 X\n SG_ A : 0|8@1+ (1,0) [0|255] \"\" X\n\n" + declaration + "\n");
        ASSERT_TRUE(parser.parseFile(path)); // legacy tools still accept ignored declarations
        parser.generateRunTimeFormat();
        std::ostringstream out, c, h; std::string error;
        EXPECT_FALSE(parser.generateAccessorOutput(out,error));
        EXPECT_FALSE(parser.generateCOutput(c,h,false,error));
        EXPECT_TRUE(out.str().empty()); EXPECT_TRUE(c.str().empty()); EXPECT_TRUE(h.str().empty());
        DbcParser strict; strict.setModelExportMode();
        testing::internal::CaptureStderr(); EXPECT_FALSE(strict.parseFile(path));
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
}

TEST_F(DbcCGenTest, PortableCDecodesSignednessAndWidthBoundaries) {
    DbcParser parser;
    parser.setName("edges");
    ASSERT_TRUE(parser.parseFile(writeTemp(
        "BO_ 100 IntelSigned: 8 X\n SG_ V : 3|13@1- (0.5,-2) [-4098|4093] \"\" X\n\n"
        "BO_ 101 MotorolaUnsigned: 8 X\n SG_ V : 20|13@0+ (2,-3) [-3|16379] \"\" X\n\n"
        "BO_ 102 One: 8 X\n SG_ V : 7|1@0- (-2,3) [3|5] \"\" X\n\n"
        "BO_ 103 Intel64: 8 X\n SG_ V : 0|64@1- (1,0) [-1|1] \"\" X\n\n"
        "BO_ 104 Motorola64: 8 X\n SG_ V : 7|64@0- (1,0) [-1|1] \"\" X\n\n"
        "BO_ 105 Unsigned64: 8 X\n SG_ V : 0|64@1+ (1,0) [0|1] \"\" X\n\n")));
    std::ostringstream c, h, accessors;
    std::string error;
    ASSERT_TRUE(parser.generateCOutput(c,h,false,error)) << error;
    ASSERT_TRUE(parser.generateAccessorOutput(accessors,error)) << error;
    const std::string source = c.str() + "#include \"" + writeTemp(accessors.str()) + "\"\n" + R"C(
#include <assert.h>
#include <stdlib.h>
static void check(uint32_t id, const uint8_t p[8], uint16_t needed,
                  uint64_t expected, double physical) {
    const j1939_message_t *m = j1939_db_find(&edges_db,id,0);
    const j1939_signal_t *s = j1939_signal_by_name(&edges_db,m,"V");
    uint64_t raw = 0;
    assert(s);
    assert(j1939_signal_raw(p,needed,s,&raw)==0 && raw==expected);
    assert(j1939_signal_phys(s,raw)==physical);
    /* Allocate only the advertised length, so ASan detects an overread even
       when the decoder could otherwise read the rest of an eight-byte array. */
    for (uint16_t n=0;n<needed;++n) {
        uint8_t *short_payload = (uint8_t *)malloc(n ? n : 1);
        assert(short_payload);
        memcpy(short_payload,p,n);
        raw=123;
        assert(j1939_signal_raw(short_payload,n,s,&raw)==-1 && raw==123);
        free(short_payload);
    }
    assert(j1939_signal_raw(p,8,NULL,&raw)==-1);
    assert(j1939_signal_raw(p,8,s,NULL)==-1);
}
int main(void) {
    /* These constants are wire vectors, never output from either encoder.
       Intel: 0x1234 - 8192 = -3532. Motorola: low 5 bits 0x0b then 0xcf. */
    const uint8_t mixed[8] = {0xa7,0x91,0xab,0xcf,0x12,0x34,0x56,0x78};
    const uint8_t le_min[8] = {0,0,0,0,0,0,0,0x80};
    const uint8_t be_min[8] = {0x80,0,0,0,0,0,0,0};
    const uint8_t ones[8] = {255,255,255,255,255,255,255,255};
    const uint8_t zero[8] = {0};
    check(100,mixed,2,0x1234,-1768);
    check(101,mixed,4,0xbcf,6043);
    check(102,mixed,1,1,5);
    check(102,zero,1,0,3);
    check(103,le_min,8,UINT64_C(0x8000000000000000),-9223372036854775808.0);
    check(104,be_min,8,UINT64_C(0x8000000000000000),-9223372036854775808.0);
    check(103,ones,8,UINT64_MAX,-1);
    check(104,ones,8,UINT64_MAX,-1);
    check(105,ones,8,UINT64_MAX,18446744073709551616.0);
    assert(j1939_IntelSigned_100_V_raw(mixed)==0x1234);
    assert(j1939_IntelSigned_100_V_phys(mixed)==-1768);
    assert(j1939_MotorolaUnsigned_101_V_raw(mixed)==0xbcf);
    assert(j1939_MotorolaUnsigned_101_V_phys(mixed)==6043);
    assert(j1939_One_102_V_phys(mixed)==5 && j1939_One_102_V_phys(zero)==3);
    assert(j1939_Intel64_103_V_raw(le_min)==UINT64_C(0x8000000000000000));
    assert(j1939_Motorola64_104_V_raw(be_min)==UINT64_C(0x8000000000000000));
    assert(j1939_Intel64_103_V_phys(le_min)==-9223372036854775808.0);
    assert(j1939_Motorola64_104_V_phys(be_min)==-9223372036854775808.0);
    assert(j1939_Intel64_103_V_phys(ones)==-1 && j1939_Motorola64_104_V_phys(ones)==-1);
    assert(j1939_Unsigned64_105_V_raw(ones)==UINT64_MAX);
    /* Physical doubles round UINT64_MAX; consumers needing exact bits use raw. */
    assert(j1939_Unsigned64_105_V_phys(ones)==18446744073709551616.0);
    return 0;
}
)C";
    const std::string path = writeTemp(source);
    const std::string binary = path + ".bin";
    temp_paths_.push_back(binary);
    ASSERT_EQ(system(("cc -x c -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined,address -I '" +
        std::string(J1939_INCLUDE_DIR) + "' '" + path + "' '" + std::string(J1939_INCLUDE_DIR) +
        "/j1939_db.c' -o '" + binary + "'").c_str()),0);
    EXPECT_EQ(system(("'" + binary + "'").c_str()),0);
}

TEST_F(DbcCGenTest, PortableCBackendsRejectUnsupportedMultiplexSelection) {
    for (const std::string signals : {
        " SG_ A m1 : 8|8@1+ (1,0) [0|255] \"\" X\n",
        " SG_ A M : 0|8@1+ (1,0) [0|255] \"\" X\n SG_ B M : 8|8@1+ (1,0) [0|255] \"\" X\n",
        " SG_ A m1M : 0|8@1+ (1,0) [0|255] \"\" X\n",
        " SG_ A M : 0|8@1- (1,0) [-128|127] \"\" X\n",
        " SG_ A M : 0|1@1+ (1,0) [0|1] \"\" X\n SG_ B m2 : 8|8@1+ (1,0) [0|255] \"\" X\n",
        " SG_ A M : 0|0@1+ (1,0) [0|1] \"\" X\n",
        " SG_ A M : 0|72@1+ (1,0) [0|1] \"\" X\n"}) {
        DbcParser parser;
        ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 M: 8 X\n" + signals + "\n")));
        std::ostringstream out, c, h;
        out << "sentinel";
        c << "sentinel";
        h << "sentinel";
        std::string error;
        EXPECT_FALSE(parser.generateAccessorOutput(out,error));
        EXPECT_EQ(out.str(), "sentinel");
        EXPECT_NE(error.find("M"), std::string::npos);
        error.clear();
        EXPECT_FALSE(parser.generateCOutput(c,h,false,error));
        EXPECT_EQ(c.str(), "sentinel");
        EXPECT_EQ(h.str(), "sentinel");
        EXPECT_FALSE(error.empty());
    }
}

TEST_F(DbcCGenTest, PortableMuxTokensNeverBecomeUnconditional) {
    for (const std::string marker : {"m", "m99999", "m65536", "m4294967296", "m18446744073709551616", "m1junk", "m1M"}) {
        DbcParser parser;
        ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 M: 8 X\n SG_ Switch M : 0|24@1+ (1,0) [0|16777215] \"\" X\n SG_ Branch " + marker + " : 24|8@1+ (1,0) [0|255] \"\" X\n")));
        std::ostringstream a,c,h;std::string e;
        EXPECT_FALSE(parser.generateAccessorOutput(a,e)); EXPECT_FALSE(parser.generateCOutput(c,h,false,e));
        EXPECT_TRUE(a.str().empty()); EXPECT_TRUE(c.str().empty()); EXPECT_TRUE(h.str().empty());
    }
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 M: 8 X\n SG_ Switch M : 0|24@1+ (1,0) [0|16777215] \"\" X\n SG_ Branch m65535 : 24|8@1+ (1,0) [0|255] \"\" X\n")));
    std::ostringstream a,c,h;std::string e;
    EXPECT_TRUE(parser.generateAccessorOutput(a,e)); EXPECT_TRUE(parser.generateCOutput(c,h,false,e));
}

TEST_F(DbcCGenTest, AccessorsReportFailedOutputStream) {
    DbcParser parser; ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 S: 8 X\n"+std::string(kSig))));
    std::ostringstream out;out.setstate(std::ios::badbit);std::string e;
    EXPECT_FALSE(parser.generateAccessorOutput(out,e)); EXPECT_NE(e.find("stream"),std::string::npos);
}

TEST_F(DbcCGenTest, AccessorDecimalsAndSymbolsCompileWithIndependentExpectedValues) {
    DbcParser parser;parser.setName("numbers");
    ASSERT_TRUE(parser.parseFile(writeTemp(
        "BO_ 100 Numbers: 8 X\n"
        " SG_ Lead : 0|8@1+ (010,000) [0|255] \"\" X\n"
        " SG_ Eight : 8|8@1+ (08,+010) [0|255] \"\" X\n"
        " SG_ Signed : 16|8@1- (-010,-0008) [-128|127] \"\" X\n"
        " SG_ Fraction : 24|8@1+ (.125,-.25) [0|255] \"\" X\n"
        " SG_ Exponent : 32|8@1+ (01e+2,-02E+1) [0|255] \"\" X\n"
        " SG_ Huge : 40|8@1+ (18446744073709551616,0) [0|255] \"\" X\n"
        " SG_ Lead_Label : 48|8@1+ (1,0) [0|255] \"\" X\n"
        "\nVAL_ 100 Lead 0 \"raw\" 1 \"phys\" 2 \"store_raw\" 3 \"On-Off\" 4 \"On Off\" 5 \"raw_0\" 6 \"Label\" ;\n")));
    std::ostringstream a,c,h;std::string e;
    ASSERT_TRUE(parser.generateAccessorOutput(a,e))<<e;ASSERT_TRUE(parser.generateCOutput(c,h,false,e))<<e;
    const std::string header=writeTemp(a.str());
    const std::string source="#include \""+header+"\"\n"+c.str()+R"C(
#include <assert.h>
int main(void) {
 uint8_t p[8]={1,1,254,6,2,1,0,0};
 const double expected[]={10,18,12,0.5,180,18446744073709551616.0};
 assert(j1939_Numbers_100_Lead_phys(p)==expected[0]);
 assert(j1939_Numbers_100_Eight_phys(p)==expected[1]);
 assert(j1939_Numbers_100_Signed_phys(p)==expected[2]);
 assert(j1939_Numbers_100_Fraction_phys(p)==expected[3]);
 assert(j1939_Numbers_100_Exponent_phys(p)==expected[4]);
 assert(j1939_Numbers_100_Huge_phys(p)==expected[5]);
 assert(j1939_Numbers_100_Lead_Label_6==6);
 assert(j1939_Numbers_100_Lead_Label_raw(p)==0);
 assert(j1939_Numbers_100_Lead_raw(p)==1);
 j1939_Numbers_100_Lead_store_raw(p,1);
 assert(j1939_Numbers_100_Lead_raw_0==0);
 assert(j1939_Numbers_100_Lead_phys_1==1);
 assert(j1939_Numbers_100_Lead_store_raw_2==2);
 assert(j1939_Numbers_100_Lead_On_Off==3);
 assert(j1939_Numbers_100_Lead_On_Off_4==4);
 assert(j1939_Numbers_100_Lead_raw_0_5==5);
 for(unsigned i=0;i<6;++i) {
   const j1939_signal_t *s=&numbers_db.signals[i]; uint64_t raw;
   assert(j1939_signal_raw(p,8,s,&raw)==0);
   assert(j1939_signal_phys(s,raw)==expected[i]);
 }
 return 0;
}
)C";
    const std::string path=writeTemp(source), obj=path+".o";temp_paths_.push_back(obj);
    ASSERT_EQ(system(("cc -std=c11 -fsanitize=address,undefined -I '"+std::string(J1939_INCLUDE_DIR)+"' -c '"+std::string(J1939_INCLUDE_DIR)+"/j1939_db.c' -o '"+obj+"'").c_str()),0);
    for(const std::string compiler : {"cc -x c -std=c11", "c++ -x c++ -std=c++14"}) {
        const std::string bin=path+".bin";temp_paths_.push_back(bin);
        ASSERT_EQ(system((compiler+" -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I '"+std::string(J1939_INCLUDE_DIR)+"' '"+path+"' -x none '"+obj+"' -o '"+bin+"'").c_str()),0);
        EXPECT_EQ(system(("'"+bin+"'").c_str()),0);
    }
}

TEST_F(DbcCGenTest, MalformedDecimalsNeverFallBackToParsedPrefixes) {
    for (unsigned bad=0;bad<5;++bad) {
        std::vector<std::string> value{"1","0","0","1"};
        value[bad%4]=bad==4 ? "1.0f" : bad==0 ? "1.2.3" : "08foo";
        DbcParser parser;
        ASSERT_TRUE(parser.parseFile(writeTemp("BO_ 100 Decimal: 8 ECU\n SG_ A : 0|8@1+ ("+value[0]+","+value[1]+") ["+value[2]+"|"+value[3]+"] \"\" ECU\n")));
        std::ostringstream c,h;c<<"sentinel";h<<"sentinel";std::string error;
        EXPECT_FALSE(parser.generateCOutput(c,h,false,error));
        EXPECT_NE(error.find("Decimal.A"),std::string::npos);
        EXPECT_EQ(c.str(),"sentinel");EXPECT_EQ(h.str(),"sentinel");
    }
}
