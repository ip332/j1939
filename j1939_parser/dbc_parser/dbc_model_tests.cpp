#include "dbc_parser.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <locale>
#include <unistd.h>

namespace {
class DbcModelTest : public testing::Test {
protected:
    std::vector<std::string> paths;
    ~DbcModelTest() override { for (const auto &p : paths) std::remove(p.c_str()); }
    std::string file(const std::string &text) {
        char name[] = "/tmp/model_test_XXXXXX.dbc";
        int fd = mkstemps(name, 4);
        EXPECT_GE(fd, 0);
        if (fd < 0) return "";
        close(fd);
        std::ofstream out(name, std::ios::binary);
        out << text;
        paths.push_back(name);
        return name;
    }
    bool parse(DbcParser &parser, const std::string &text, std::string *diagnostic = nullptr) {
        parser.setModelExportMode();
        testing::internal::CaptureStderr();
        bool result = parser.parseFile(file(text));
        const std::string error = testing::internal::GetCapturedStderr();
        if (diagnostic) *diagnostic = error;
        return result;
    }
    const std::string bo = "BO_ 100 Sample: 8 ECU\n";
    const std::string sg = " SG_ S : 0|8@1+ (1,0) [0|255] \"\" Receiver\n";
};
}

TEST_F(DbcModelTest, RetainsDecimalUnitsAbsentAndExplicitZero) {
    DbcParser parser;
    ASSERT_TRUE(parse(parser, bo + " SG_ S : 3|13@1- (0.5000,-2) [-2048|2047] \"rev per min\" Receiver\n"
        " SG_ Other : 23|8@0+ (1,0) [0|255] \"\" Receiver\n"
        "BA_ \"SPN\" SG_ 100 S 0;\n"));
    std::ostringstream a, b;
    std::string error;
    ASSERT_TRUE(parser.generateModelOutput(a, error)) << error;
    ASSERT_TRUE(parser.generateModelOutput(b, error)) << error;
    EXPECT_EQ(a.str(), b.str());
    EXPECT_NE(a.str().find("scale: \"0.5000\""), std::string::npos);
    EXPECT_NE(a.str().find("unit: \"rev per min\""), std::string::npos);
    EXPECT_NE(a.str().find("spn: 0"), std::string::npos);
    EXPECT_EQ(a.str().find("null"), std::string::npos);
    EXPECT_NE(a.str().find("line: 2"), std::string::npos);
}

TEST_F(DbcModelTest, RejectsUnsupportedAmbiguousAndMalformedInputsWithoutOutput) {
    const std::vector<std::string> cases{
        bo + sg + "SIG_VALTYPE_ 100 S : 1;\n",
        bo + sg + "SG_MUL_VAL_ 100 S M 0-3;\n",
        bo + sg + "BA_ \"VFrameFormat\" BO_ 100 14;\n",
        bo + sg + "BA_DEF_DEF_ \"SPN\" 7;\n",
        bo + sg + "CM_ SG_ 100 S \"meaning\";\n",
        bo + sg + "BO_ 100 Second: 8 ECU\n" + sg,
        bo + sg + "BO_ 101 Sample: 8 ECU\n" + sg,
        bo + sg + " SG_ S : 8|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S : 0|8@1+ (1,0) [0|255] \"escaped\\unit\" Receiver\n",
        bo + " SG_ S m1M : 0|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S m99999999999999 : 0|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S m1X : 0|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S m1 : 0|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S M : 0|8@1- (1,0) [-128|127] \"\" Receiver\n",
        bo + " SG_ S M : 0|1@1+ (1,0) [0|1] \"\" Receiver\n SG_ A m2 : 8|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S M : 0|8@1+ (1,0) [0|255] \"\" Receiver\n SG_ A M : 8|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + sg + " SG_ Overlap : 4|8@1+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S : 63|2@1+ (1,0) [0|3] \"\" Receiver\n",
        bo + " SG_ S : 63|9@0+ (1,0) [0|511] \"\" Receiver\n",
        bo + " SG_ S : 0|0@1+ (1,0) [0|0] \"\" Receiver\n",
        bo + " SG_ S : 0|65@1+ (1,0) [0|0] \"\" Receiver\n",
        bo + " SG_ S : 0|8@2+ (1,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S : 0|8@1+ (nan,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S : 0|8@1+ (1e999,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S : 0|8@1+ (0,0) [0|255] \"\" Receiver\n",
        bo + " SG_ S : 0|8@1+ (1,0) [3|2] \"\" Receiver\n",
        bo + sg + "VAL_ 100 S -1 \"Negative\" ;\n",
        bo + sg + "VAL_ 100 S 256 \"Wide\" ;\n",
        bo + sg + "VAL_ 100 S 1 \"A\" 1 \"A\" ;\n",
        bo + sg + "VAL_ 100 S 1 \"A\" ;\nVAL_ 100 S 2 \"B\" ;\n",
        bo + sg + "VAL_ 100 Missing 1 \"A\" ;\n",
        bo + sg + "BA_ \"SPN\" SG_ 100 Missing 1;\n",
        bo + sg + "BA_ \"SPN\" SG_ 100 S 0;\nBA_ \"SPN\" SG_ 100 S 1;\n",
        "BO_ 2048 S: 8 ECU\n" + sg,
        "BO_ 2684354560 S: 8 ECU\n" + sg,
        "BO_ 2181038080 S: 8 ECU\n" + sg,
        "BO_ 100 S: 9 ECU\n" + sg,
        "BO_ 100 S: 0 ECU\n" + sg,
        "BO_ nonsense\n", bo + " SG_ broken\n", sg,
    };
    for (const auto &text : cases) {
        SCOPED_TRACE(text);
        DbcParser parser;
        std::string diagnostic;
        EXPECT_FALSE(parse(parser, text, &diagnostic));
        EXPECT_NE(diagnostic.find("model-v1:"), std::string::npos);
        EXPECT_NE(diagnostic.find(".dbc:"), std::string::npos);
        std::ostringstream out;
        out << "SENTINEL";
        std::string error;
        EXPECT_FALSE(parser.generateModelOutput(out, error));
        EXPECT_EQ(out.str(), "SENTINEL");
    }
}

TEST_F(DbcModelTest, RequiresStrictSessionAndRejectsNormalization) {
    DbcParser parser;
    ASSERT_TRUE(parser.parseFile(file(bo + sg)));
    std::ostringstream out;
    std::string error;
    EXPECT_FALSE(parser.generateModelOutput(out, error));
    EXPECT_TRUE(out.str().empty());
    DbcParser strict;
    ASSERT_TRUE(parse(strict, bo + sg));
    strict.removeDuplicates();
    EXPECT_FALSE(strict.generateModelOutput(out, error));
}

TEST_F(DbcModelTest, FixturesAndUint64EnumsAreExportable) {
    DbcParser parser;
    parser.setModelExportMode();
    ASSERT_TRUE(parser.parseFile(std::string(DBC_FOLDER) + "model_v1/single_frame.dbc"));
    ASSERT_TRUE(parser.parseFile(std::string(DBC_FOLDER) + "model_v1/multiplex.dbc"));
    std::ostringstream out;
    std::string error;
    ASSERT_TRUE(parser.generateModelOutput(out, error)) << error;
    EXPECT_NE(out.str().find("switch_name: \"Switch\" value: 0"), std::string::npos);
    EXPECT_NE(out.str().find("pgn: 61444"), std::string::npos);
    DbcParser wide;
    ASSERT_TRUE(parse(wide, bo + " SG_ S : 0|64@1+ (1,0) [0|18446744073709551615] \"\" Receiver\n"
        "VAL_ 100 S 18446744073709551615 \"Maximum\" ;\n"));
    std::ostringstream model;
    ASSERT_TRUE(wide.generateModelOutput(model, error));
    EXPECT_NE(model.str().find("raw: 18446744073709551615"), std::string::npos);
}


TEST_F(DbcModelTest, InvalidSecondSourcePoisonsExport) {
    DbcParser parser;
    ASSERT_TRUE(parse(parser, bo + sg));
    std::string diagnostic;
    EXPECT_FALSE(parse(parser, "unsupported_declaration\n", &diagnostic));
    std::ostringstream out;
    std::string error;
    EXPECT_FALSE(parser.generateModelOutput(out, error));
    EXPECT_TRUE(out.str().empty());
}

TEST_F(DbcModelTest, ExportIsIndependentOfGlobalCppNumericLocale) {
    struct Comma : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };
    struct Restore {
        std::locale original = std::locale();
        ~Restore() { std::locale::global(original); }
    } restore;
    const auto input = file(bo + " SG_ S : 0|8@1+ (0.125,-2) [0|29.875] \"unit text\" Receiver\n");
    DbcParser first, second;
    first.setModelExportMode(); second.setModelExportMode();
    ASSERT_TRUE(first.parseFile(input));
    std::ostringstream before, after;
    std::string error;
    ASSERT_TRUE(first.generateModelOutput(before, error));
    std::locale::global(std::locale(std::locale::classic(), new Comma));
    ASSERT_TRUE(second.parseFile(input));
    ASSERT_TRUE(second.generateModelOutput(after, error));
    EXPECT_EQ(before.str(), after.str());
}

TEST_F(DbcModelTest, RejectsHeaderAndAttributeErrorsWithSpecificDiagnostics) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"NS_ :\n\nNS_ :\n", "duplicate NS_"},
        {"VERSION \"one\"\nVERSION \"two\"\n", "duplicate VERSION"},
        {"BS_:\nBS_:\n", "duplicate BS_"},
        {"BU_:\nBU_: ECU\n", "duplicate BU_"},
        {"BU_: ECU ECU\n", "invalid or duplicate node"},
        {"BU_: 9ECU\n", "invalid or duplicate node"},
        {bo + sg + "VAL_ 101 S 0 \"Off\" ;\n", "VAL_ must reference"},
        {bo + sg + "BA_ \"SPN\" BO_ 101 1;\n", "SPN must reference"},
        {bo + sg + "BA_ \"SPN\" BO_ 100 1;\nBA_ \"SPN\" BO_ 100 2;\n", "duplicate message SPN"},
        {bo + sg + "BA_ \"SPN\" BO_ 100 S 1;\n", "malformed SPN"},
        {bo + sg + "BA_ \"SPN\" SG_ 100 1;\n", "malformed SPN"},
        {bo + sg + "BA_ \"J1939Default\" BO_ 101 0;\n", "J1939Default must reference"},
        {bo + sg + "BA_ \"J1939Default\" BO_ 100 0;\nBA_ \"J1939Default\" BO_ 100 1;\n", "duplicate J1939Default"},
        {bo + " SG_ S : 0|8@1+ (1,0) [0|255] \"\" ECU\n SG_ Other : 0|8@1+ (1,0) [0|255] \"\" ECU\n", "invalid signal"},
        {bo, "message has no signals"},
        {bo + sg + std::string(1, '\x01'), "only printable ASCII"},
        {bo + sg + std::string(1, '\x80'), "only printable ASCII"},
        {"BO_ 18446744073709551616 Huge: 8 ECU\n", "stoull"},
    };
    for (const auto &item : cases) {
        SCOPED_TRACE(item.first);
        DbcParser parser;
        std::string diagnostic;
        EXPECT_FALSE(parse(parser, item.first, &diagnostic));
        EXPECT_NE(diagnostic.find(item.second), std::string::npos) << diagnostic;
        std::ostringstream output;
        output << "unchanged";
        std::string error;
        EXPECT_FALSE(parser.generateModelOutput(output, error));
        EXPECT_EQ(output.str(), "unchanged");
        // A rejected source poisons the session, even if a later source is valid.
        EXPECT_FALSE(parser.parseFile(file(bo + sg)));
    }
}

TEST_F(DbcModelTest, EnforcesSourceAndSessionBoundaries) {
    const auto valid = file(bo + sg);
    const auto missing = file("");
    ASSERT_EQ(std::remove(missing.c_str()), 0);
    for (const auto &path : {std::string("bad name.dbc"), missing}) {
        DbcParser parser;
        parser.setModelExportMode();
        testing::internal::CaptureStderr();
        EXPECT_FALSE(parser.parseFile(path));
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
    DbcParser duplicate;
    duplicate.setModelExportMode();
    ASSERT_TRUE(duplicate.parseFile(valid));
    testing::internal::CaptureStderr();
    EXPECT_FALSE(duplicate.parseFile(valid));
    EXPECT_NE(testing::internal::GetCapturedStderr().find("duplicate source logical name"), std::string::npos);

    DbcParser normalized;
    normalized.setModelExportMode();
    ASSERT_TRUE(normalized.parseFile(valid));
    normalized.removeDuplicates();
    testing::internal::CaptureStderr();
    EXPECT_FALSE(normalized.parseFile(file("BO_ 101 Other: 8 ECU\n" + sg)));
    EXPECT_NE(testing::internal::GetCapturedStderr().find("cannot parse after PGN normalization"), std::string::npos);

    DbcParser late;
    ASSERT_TRUE(late.parseFile(valid));
    late.setModelExportMode();
    testing::internal::CaptureStderr();
    EXPECT_FALSE(late.parseFile(valid));
    EXPECT_NE(testing::internal::GetCapturedStderr().find("enable model export before"), std::string::npos);
}

TEST_F(DbcModelTest, ExportsEmptyHeadersWhitespaceAndFalseDefaultWithoutInventingMessages) {
    DbcParser empty;
    ASSERT_TRUE(parse(empty, "VERSION \"\"\r\nNS_ :\r\n\r\nBS_:\r\nBU_:\r\n"));
    std::ostringstream out;
    out << "unchanged";
    std::string error;
    EXPECT_FALSE(empty.generateModelOutput(out, error));
    EXPECT_NE(error.find("at least one message"), std::string::npos);
    EXPECT_EQ(out.str(), "unchanged");
    DbcParser parser;
    ASSERT_TRUE(parse(parser, "NS_ :\n CM_\n" + bo +
        "\tSG_ S : 0|8@1+ (1,0) [0|255] \"spaced unit\" ECU,Other\r\n"
        "BA_ \"J1939Default\" BO_ 100 0;\n"));
    std::ostringstream model;
    ASSERT_TRUE(parser.generateModelOutput(model, error));
    EXPECT_NE(model.str().find("j1939_default: false"), std::string::npos);
    EXPECT_NE(model.str().find("receivers: \"Other\""), std::string::npos);
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    EXPECT_FALSE(parser.generateModelOutput(failed, error));
    EXPECT_NE(error.find("output stream failed"), std::string::npos);
}

TEST_F(DbcModelTest, OrdersMuxAlternativesAndEscapesAllowedUnitTabs) {
    const std::string header = "BO_ 100 Mux: 8 ECU\n SG_ Switch M : 0|16@1+ (1,0) [0|65535] \"\" ECU\n";
    const std::string a = " SG_ A m1 : 16|8@1+ (1,0) [0|255] \"unit\ttext\" ECU\n";
    const std::string b = " SG_ B m65535 : 16|8@1+ (1,0) [0|255] \"\" ECU\n";
    for (const auto &body : {a + b, b + a}) {
        DbcParser parser;
        ASSERT_TRUE(parse(parser, header + body + "BA_ \"J1939Default\" BO_ 100 1;\n"));
        std::ostringstream model;
        std::string error;
        ASSERT_TRUE(parser.generateModelOutput(model, error));
        EXPECT_LT(model.str().find("name: \"A\""), model.str().find("name: \"B\""));
        EXPECT_NE(model.str().find("unit: \"unit\\011text\""), std::string::npos);
        EXPECT_NE(model.str().find("value: 65535"), std::string::npos);
        EXPECT_NE(model.str().find("j1939_default: true"), std::string::npos);
    }
}

TEST_F(DbcModelTest, AcceptsRelativeSourceNamesAndRejectsAnUnparsedSession) {
    DbcParser empty;
    empty.setModelExportMode();
    std::ostringstream out;
    std::string error;
    EXPECT_FALSE(empty.generateModelOutput(out,error));
    const auto path=file(bo+sg);
    char original[4096];
    ASSERT_NE(getcwd(original,sizeof(original)),nullptr);
    struct RestoreDirectory {
        const char *original;
        ~RestoreDirectory() { EXPECT_EQ(chdir(original),0); }
    } restore{original};
    ASSERT_EQ(chdir("/tmp"),0);
    DbcParser parser;parser.setModelExportMode();
    ASSERT_TRUE(parser.parseFile(path.substr(path.find_last_of('/')+1)));
    EXPECT_TRUE(parser.generateModelOutput(out,error))<<error;
}
