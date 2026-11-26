#include "dbc_parser.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <unistd.h>

namespace {
class DbcMetadataTest : public testing::Test {
protected:
    std::vector<std::string> paths;
    ~DbcMetadataTest() override { for (const auto &p : paths) std::remove(p.c_str()); }
    std::string file(const std::string &text) {
        char name[]="/tmp/metadata_XXXXXX.dbc";
        int fd=mkstemps(name,4);
        EXPECT_GE(fd,0);
        if(fd<0) return "";
        close(fd);
        std::ofstream out(name);out<<text;out.close();paths.push_back(name);return name;
    }
    bool parse(DbcParser &p,const std::string &path) {
        p.setName("sample");p.setModelExportMode();return p.parseFile(path);
    }
};
}

TEST_F(DbcMetadataTest, GeneratedCAndCppExposeInterpretationAndOriginalDefinitions) {
    DbcParser parser;
    ASSERT_TRUE(parse(parser,std::string(DBC_FOLDER)+"metadata_synthetic.dbc"));
    std::ostringstream c,h;std::string error;
    ASSERT_TRUE(parser.generateCOutput(c,h,false,error,true))<<error;
    std::ostringstream again,again_h;
    ASSERT_TRUE(parser.generateCOutput(again,again_h,false,error,true));
    EXPECT_EQ(c.str(),again.str());EXPECT_EQ(h.str(),again_h.str());
    const auto header=file(h.str());
    const auto path=file("#include \""+header+"\"\n"+c.str()+R"C(
#include <assert.h>
#include <string.h>
int main(void) {
 const j1939_db_metadata_t *m=&sample_metadata;
 const j1939_db_t *db=m->db;
 assert(db==&sample_db && db->message_count==2 && db->signal_count==7);
 assert(m->version==1 && m->source_count==1 && m->message_definition_count==3 && m->signal_definition_count==8);
 assert(strcmp(j1939_signal_unit(m,0),"rev per min")==0);
 assert(strcmp(j1939_signal_unit(m,1),"")==0);
 assert(strcmp(j1939_signal_unit(m,2),"deg C")==0);
 assert(strcmp(j1939_signal_unit(m,3),"")==0);
 assert(strcmp(j1939_signal_unit(m,5),"deg")==0);
 assert(strcmp(j1939_signal_unit(m,6),"deg C")==0);
 assert(j1939_signal_unit(m,2)==j1939_signal_unit(m,6)); /* one interned string */
 assert(strcmp(j1939_metadata_text(m,m->signals[0].scale_off),"010")==0);
 assert(strcmp(j1939_metadata_text(m,m->signals[0].offset_off),"+000")==0);
 assert(strcmp(j1939_metadata_text(m,m->signals[5].scale_off),"2.5e-1")==0);
 assert(strcmp(j1939_metadata_text(m,m->signals[6].offset_off),"-010")==0);
 assert(strcmp(j1939_metadata_text(m,m->signals[2].minimum_off),"-34")==0);
 assert(strcmp(j1939_metadata_text(m,m->signals[2].maximum_off),"29.75")==0);
 assert(db->signals[0].scale==10 && db->signals[0].offset==0);
 assert((db->signals[2].flags & J1939_SIG_SIGNED)!=0);
 assert((db->signals[0].flags & J1939_SIG_SIGNED)==0);
 assert((db->signals[5].flags & J1939_SIG_LITTLE_ENDIAN)==0);
 assert(db->signals[5].start_bit==39 && db->signals[5].length==12);
 assert(db->signals[2].mux_value==0 && db->signals[3].mux_value==1);
 assert(db->enums[1].value==255);
 assert(strcmp(db->strings+db->enums[1].name_off,"Synthetic marker")==0);
 uint8_t payload[8]={2,0,4,255,0,0,0,0};uint64_t raw=0;
 assert(j1939_signal_raw(payload,8,&db->signals[0],&raw)==0 && raw==2);
 assert(j1939_signal_phys(&db->signals[0],raw)==20);
 assert(j1939_signal_active(db,&db->messages[0],payload,8,&db->signals[2])==1);
 assert(j1939_signal_active(db,&db->messages[0],payload,8,&db->signals[3])==0);
 assert(m->signal_definitions[0].flags & J1939_META_HAS_SPN);
 assert(m->signal_definitions[0].spn==0 && !(m->signal_definitions[1].flags & J1939_META_HAS_SPN));
 assert(m->message_definitions[1].dbc_id==UINT32_C(2566849547));
 assert(m->message_definitions[2].dbc_id==UINT32_C(2566849598));
 assert(m->message_definitions[1].message_index==m->message_definitions[2].message_index);
 assert(m->message_definitions[1].flags & J1939_META_HAS_SPN);
 assert(m->message_definitions[2].flags & J1939_META_HAS_DEFAULT);
 assert(!(m->message_definitions[2].flags & J1939_META_DEFAULT));
 assert(strcmp(j1939_metadata_text(m,m->signal_definitions[7].name_off),"TemperatureAlias")==0);
 assert(m->signal_definitions[6].signal_index==m->signal_definitions[7].signal_index);
 assert(strcmp(j1939_metadata_text(m,m->sources[0].name_off),"metadata_synthetic.dbc")==0);
 assert(strcmp(j1939_metadata_text(m,m->sources[0].version_off),"metadata fixture 1")==0);
 assert(m->sources[0].byte_length==741 && m->sources[0].fnv1a64==UINT64_C(0x6054cdfc063a2d51));
 assert(m->message_definitions[0].line==3 && m->signal_definitions[0].line==4);
 assert(j1939_signal_unit(m,UINT16_MAX)==NULL && j1939_signal_unit(NULL,0)==NULL);
 assert(j1939_metadata_text(m,J1939_NO_NAME)==NULL);
 j1939_db_metadata_t wrong=*m;wrong.version=99;
 assert(j1939_signal_unit(&wrong,0)==NULL && j1939_metadata_text(&wrong,0)==NULL);
 wrong.version=1;wrong.db=NULL;
 assert(j1939_signal_unit(&wrong,0)==NULL && j1939_metadata_text(&wrong,0)==NULL);
 wrong.db=db;wrong.signals=NULL;assert(j1939_signal_unit(&wrong,0)==NULL);
 assert(j1939_metadata_text(NULL,0)==NULL);
 return 0;
}
)C");
    const auto object=path+".o";paths.push_back(object);
    ASSERT_EQ(system(("cc -std=c11 -fsanitize=address,undefined -I '"+std::string(J1939_INCLUDE_DIR)+"' -c '"+std::string(J1939_INCLUDE_DIR)+"/j1939_db.c' -o '"+object+"'").c_str()),0);
    for(const std::string compiler : {"cc -x c -std=c11","c++ -x c++ -std=c++14"}) {
        const auto binary=path+".bin";paths.push_back(binary);
        ASSERT_EQ(system((compiler+" -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I '"+std::string(J1939_INCLUDE_DIR)+"' '"+path+"' -x none '"+object+"' -o '"+binary+"'").c_str()),0);
        EXPECT_EQ(system(("'"+binary+"'").c_str()),0);
    }
}

TEST_F(DbcMetadataTest, MetadataRejectsLossyCollapseAndPreservesStreams) {
    const std::string prefix="BO_ 2566849547 First: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"unit A\" ECU\n";
    for(const auto &tail : {
        std::string("BO_ 2566849598 Second: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"unit B\" ECU\n"),
        std::string("BO_ 2566849598 Second: 8 ECU\n SG_ S : 0|8@1+ (1.0,0) [0|255] \"unit A\" ECU\n"),
        std::string("BO_ 2566849598 Second: 8 ECU\n SG_ S : 0|8@1+ (1,+0) [0|255] \"unit A\" ECU\n"),
        std::string("BO_ 2566849598 Second: 8 ECU\n SG_ S : 0|8@1+ (1,0) [00|255] \"unit A\" ECU\n"),
        std::string("BO_ 2566849598 Second: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|0255] \"unit A\" ECU\n")}) {
        DbcParser parser;ASSERT_TRUE(parse(parser,file(prefix+tail)));
        std::ostringstream c,h;std::string error;c<<"C";h<<"H";
        EXPECT_FALSE(parser.generateCOutput(c,h,false,error,true));
        EXPECT_NE(error.find("distinct units/decimal text"),std::string::npos)<<error;
        EXPECT_EQ(c.str(),"C");EXPECT_EQ(h.str(),"H");
        std::ostringstream old_c,old_h;EXPECT_TRUE(parser.generateCOutput(old_c,old_h,false,error));
    }
}

TEST_F(DbcMetadataTest, GenerationRequiresStrictCompleteMetadataAndCompatibleProfile) {
    const auto path=file("BO_ 100 Single: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"\" ECU\n");
    DbcParser legacy;ASSERT_TRUE(legacy.parseFile(path));
    std::ostringstream c,h;std::string error;
    EXPECT_FALSE(legacy.generateCOutput(c,h,false,error,true));
    DbcParser parser;ASSERT_TRUE(parse(parser,path));
    EXPECT_FALSE(parser.generateCOutput(c,h,true,error,true));
    parser.removeDuplicates();EXPECT_FALSE(parser.generateCOutput(c,h,false,error,true));
    EXPECT_TRUE(c.str().empty());EXPECT_TRUE(h.str().empty());
}

TEST_F(DbcMetadataTest, MetadataIsDeterministicAcrossSourceOrder) {
    const auto a=file("BO_ 100 A: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"\" ECU\n");
    const auto b=file("BO_ 2566849547 B: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"spaced units\" ECU\n");
    DbcParser first,second;ASSERT_TRUE(parse(first,a));ASSERT_TRUE(first.parseFile(b));
    ASSERT_TRUE(parse(second,b));ASSERT_TRUE(second.parseFile(a));
    std::ostringstream c,h,c2,h2;std::string error;
    ASSERT_TRUE(first.generateCOutput(c,h,false,error,true));
    struct Grouped : std::numpunct<char> {
        char do_thousands_sep() const override { return ','; }
        char do_decimal_point() const override { return ','; }
        std::string do_grouping() const override { return "\3"; }
    };
    struct RestoreLocale {
        std::locale original=std::locale();
        ~RestoreLocale() { std::locale::global(original); }
    } restore;
    std::locale::global(std::locale(std::locale::classic(),new Grouped));
    ASSERT_TRUE(second.generateCOutput(c2,h2,false,error,true));
    EXPECT_EQ(c.str(),c2.str());EXPECT_EQ(h.str(),h2.str());
}

TEST_F(DbcMetadataTest, MetadataReportsFailedOutputStreams) {
    DbcParser parser;ASSERT_TRUE(parse(parser,std::string(DBC_FOLDER)+"metadata_synthetic.dbc"));
    for(bool failed_header : {false,true}) {
        std::ostringstream c,h;std::string error;
        if (failed_header) h.setstate(std::ios::badbit);
        else c.setstate(std::ios::badbit);
        EXPECT_FALSE(parser.generateCOutput(c,h,false,error,true));
        EXPECT_NE(error.find("stream failed"),std::string::npos);
    }
}

TEST_F(DbcMetadataTest, SourceSpecificRowsKeepDistinctInterpretations) {
 for (unsigned variant : {0u,1u,2u,3u}) {
    DbcParser parser;
    std::string text="BO_ 2566849547 A: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"unit A\" ECU\n";
    text+="BO_ 2566849598 B: "+std::to_string(variant==1 ? 7 : 8)+" ECU\n SG_ S : 0|8@1+ (2,0) [0|510] \"unit B\" ECU\n";
    if (variant==2) text+=" SG_ T : 8|8@1+ (1,0) [0|255] \"\" ECU\n";
    if (variant) text+="BA_ \"J1939Default\" BO_ 2566849547 1;\n";
    ASSERT_TRUE(parse(parser,file(text)));
    std::ostringstream c,h;std::string error;
    ASSERT_TRUE(parser.generateCOutput(c,h,false,error,true))<<error;
    const auto path=file(c.str()+"\n#define HAS_DEFAULT "+std::to_string(variant!=0)+"\n"+R"C(
#include <assert.h>
#include <string.h>
int main(void) {
 const j1939_message_t *a=j1939_db_find(&sample_db,UINT32_C(0x18ff040b),1);
 const j1939_message_t *b=j1939_db_find(&sample_db,UINT32_C(0x18ff043e),1);
 assert(a && b && a!=b);
 assert(strcmp(j1939_signal_unit(&sample_metadata,a->signal_begin),"unit A")==0);
 assert(strcmp(j1939_signal_unit(&sample_metadata,b->signal_begin),"unit B")==0);
 assert(sample_db.signals[a->signal_begin].scale==1 && sample_db.signals[b->signal_begin].scale==2);
 assert(j1939_db_find(&sample_db,UINT32_C(0x18ff0401),1)==(HAS_DEFAULT ? a : NULL));
 assert(!!(sample_metadata.message_definitions[0].flags & J1939_META_DEFAULT)==HAS_DEFAULT);
 return 0;
}
)C");
    const auto binary=path+".bin";paths.push_back(binary);
    ASSERT_EQ(system(("cc -x c -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I '"+
        std::string(J1939_INCLUDE_DIR)+"' '"+path+"' '"+std::string(J1939_INCLUDE_DIR)+"/j1939_db.c' -o '"+binary+"'").c_str()),0);
    EXPECT_EQ(system(("'"+binary+"'").c_str()),0);
 }
}

TEST_F(DbcMetadataTest, EmptyOrPoisonedStrictSessionsCannotEmitACatalog) {
    for (unsigned state : {0u,1u,2u}) {
        DbcParser parser;parser.setModelExportMode();
        if(state==1) { ASSERT_TRUE(parser.parseFile(file("VERSION \"\"\n"))); }
        if(state==2) {
            ASSERT_TRUE(parser.parseFile(file("BO_ 100 A: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|255] \"\" ECU\n")));
            testing::internal::CaptureStderr();
            EXPECT_FALSE(parser.parseFile(file("unsupported\n")));
            EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
        }
        std::ostringstream c,h;std::string error;c<<"C";h<<"H";
        EXPECT_FALSE(parser.generateCOutput(c,h,false,error,true));
        EXPECT_EQ(c.str(),"C");EXPECT_EQ(h.str(),"H");
    }
}

TEST_F(DbcMetadataTest, LargeDecoderSignalTablesRejectBeforeOutput) {
    // The decoder and metadata share uint16 indices/counts. A switch plus
    // 65535 branches must reject even without a following message or name index.
    std::ostringstream text;
    text<<"BO_ 100 Large: 8 ECU\n SG_ Switch M : 0|16@1+ (1,0) [0|65535] \"\" ECU\n";
    for(unsigned i=0;i<65535;++i)
        text<<" SG_ S"<<i<<" m"<<i<<" : 16|8@1+ (1,0) [0|255] \"\" ECU\n";
    for(bool next_row : {false,true}) {
        std::string input=text.str();
        if(next_row) input+="\nBO_ 101 Next: 8 ECU\n SG_ T : 0|8@1+ (1,0) [0|255] \"\" ECU\n";
        DbcParser parser;ASSERT_TRUE(parser.parseFile(file(input)));
        std::ostringstream c,h;std::string error;c<<"C";h<<"H";
        EXPECT_FALSE(parser.generateCOutput(c,h,true,error));
        EXPECT_NE(error.find("uint16 index"),std::string::npos);
        EXPECT_EQ(c.str(),"C");EXPECT_EQ(h.str(),"H");
    }
}
