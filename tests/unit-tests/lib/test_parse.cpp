// This file is part of BOINC.
// https://boinc.berkeley.edu
// Copyright (C) 2026 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

#ifndef _WIN32
#include <string>
#include <ios>
#include <cstring>
#include "gtest/gtest.h"
#endif
#include "common_defs.h"
#include "url.h"
#include "parse.h"


using namespace std;

namespace test_parse {
    class test_parse : public ::testing::Test {};

    TEST_F(test_parse, xml_unescape) {
        string test = "&lt;&gt;&quot;&apos;&amp;&#xD;&#xd;&#xA;&#xa;&#75;";
        string answer = "<>\"\'&\r\r\n\nK";
        xml_unescape(test);
        EXPECT_EQ(test, answer);

        //Note: this is to check that partial values don't pass strncmp for previously bad compares.
        test = "&quoYIKES&apoBOO";
        answer = "&quoYIKES&apoBOO";
        xml_unescape(test);
        EXPECT_EQ(test, answer);

        //Testing the ascii conversion unknown.
        test = "&#9s3;&#694312532&#;eq&#1234;&#75";
        answer = "&#9s3;&#694312532&#;eq&#1234;&#75";
        xml_unescape(test);
        EXPECT_EQ(test, answer);
    }

    TEST_F(test_parse, XML_PARSER) {

        MIOFILE mf;

        XML_PARSER xp(&mf);

        mf.init_buf_read("<?xml version=\"1.0\" encoding=\"ISO-8859-1\" ?>\n"
"<blah>\n"
"    <x>\n"
"    asdlfkj\n"
"      <x> fj</x>\n"
"    </x>\n"
"    <str>blah</str>\n"
"    <int>  6\n"
"    </int>\n"
"    <double>6.555</double>\n"
"    <bool>0</bool>\n"
"</blah>");

        EXPECT_TRUE(xp.parse_start("blah"));

        int success = false;
        int expects = 0;

        char name[64];
        strncpy(name, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", sizeof(name));

        int val;

        double x;

        bool flag;

        while (!xp.get_tag()) {
            if (!xp.is_tag) {
                continue;
            }
            if (xp.match_tag("/blah")) {
                success = true;
            } else if (xp.parse_str("str", name, 64)) {
                EXPECT_STREQ(name, "blah");
                expects++;
            } else if (xp.parse_int("int", val)) {
                EXPECT_EQ(val, 6);
                expects++;
            } else if (xp.parse_double("double", x)) {
                EXPECT_EQ(x, 6.555);
                expects++;
            } else if (xp.parse_bool("bool", flag)) {
                EXPECT_FALSE(flag);
                expects++;
            } else {
                xp.skip_unexpected(false, "xml test");
                EXPECT_STREQ(xp.parsed_tag, "x");
                expects++;
            }
        }

        EXPECT_TRUE(success);
        EXPECT_EQ(expects, 5);

    }

    TEST(test_xml_escape, xml_escape_basic) {
        char out[64];
        xml_escape("<&\"']]>", out, sizeof(out));
        EXPECT_STREQ(out, "&lt;&amp;&quot;&apos;]]&gt;");
    }

    TEST(test_xml_escape, xml_escape_control_and_high_bytes) {
        char out[64];
        const char in[] = {1, 'a', 9, (char)0xff, 0};
        xml_escape(in, out, sizeof(out));
        EXPECT_STREQ(out, "a&#9;&#255;");
    }

    // A short output buffer must stop at a complete entity, never in the
    // middle of one, and must always be NUL-terminated.
    //
    TEST(test_xml_escape, xml_escape_short_buffers) {
        const char* in = "<&\"'a]]>\x01\xffz";
        char full[256];
        xml_escape(in, full, sizeof(full));
        for (int len=1; len<=12; len++) {
            char out[32];
            memset(out, 0xAA, sizeof(out));
            xml_escape(in, out, len);
            EXPECT_LE(strlen(out), (size_t)len-1) << "len=" << len;
            EXPECT_EQ(std::string(out), std::string(full).substr(
                0, strlen(out)
            )) << "len=" << len;
            for (int k=len; k<32; k++) {
                EXPECT_EQ((unsigned char)out[k], 0xAA)
                    << "wrote past len=" << len;
            }
            // every '&' begins an entity, so it must be terminated
            const char* amp = strrchr(out, '&');
            if (amp) {
                EXPECT_NE(strchr(amp, ';'), nullptr)
                    << "partial entity at len=" << len;
            }
        }
    }
}
