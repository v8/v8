// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "v8.h"
#include "cctest.h"

using namespace v8::internal;

DEFINE_bool(bool_flag, true, "bool_flag");
DEFINE_int(int_flag, 13, "int_flag");
DEFINE_float(float_flag, 2.5, "float-flag");
DEFINE_string(string_flag, "Hello, world!", "string-flag");


// This test must be executed first!
TEST(Default) {
  CHECK(FLAG_bool_flag);
  CHECK_EQ(13, FLAG_int_flag);
  CHECK_EQ(2.5, FLAG_float_flag);
  CHECK_EQ(0, strcmp(FLAG_string_flag, "Hello, world!"));
}


static void SetFlagsToDefault() {
  for (Flag* f = FlagList::list(); f != NULL; f = f->next()) {
    f->SetToDefault();
  }
  TestDefault();
}


TEST(Flags1) {
  FlagList::Print(__FILE__, false);
  FlagList::Print(NULL, true);
}


TEST(Flags2) {
  SetFlagsToDefault();
  int argc = 7;
  const char* argv[] = { "Test2", "-nobool-flag", "notaflag", "--int_flag=77",
                         "-float_flag=.25", "--string_flag", "no way!" };
  CHECK_EQ(0, FlagList::SetFlagsFromCommandLine(&argc,
                                                const_cast<char **>(argv),
                                                false));
  CHECK_EQ(7, argc);
  CHECK(!FLAG_bool_flag);
  CHECK_EQ(77, FLAG_int_flag);
  CHECK_EQ(.25, FLAG_float_flag);
  CHECK_EQ(0, strcmp(FLAG_string_flag, "no way!"));
}


TEST(Flags2b) {
  SetFlagsToDefault();
  const char* str =
      " -nobool-flag notaflag   --int_flag=77 -float_flag=.25  "
      "--string_flag   no_way!  ";
  CHECK_EQ(0, FlagList::SetFlagsFromString(str, strlen(str)));
  CHECK(!FLAG_bool_flag);
  CHECK_EQ(77, FLAG_int_flag);
  CHECK_EQ(.25, FLAG_float_flag);
  CHECK_EQ(0, strcmp(FLAG_string_flag, "no_way!"));
}


TEST(Flags3) {
  SetFlagsToDefault();
  int argc = 8;
  const char* argv[] =
      { "Test3", "--bool_flag", "notaflag", "--int_flag", "-666",
        "--float_flag", "-12E10", "-string-flag=foo-bar" };
  CHECK_EQ(0, FlagList::SetFlagsFromCommandLine(&argc,
                                                const_cast<char **>(argv),
                                                true));
  CHECK_EQ(2, argc);
  CHECK(FLAG_bool_flag);
  CHECK_EQ(-666, FLAG_int_flag);
  CHECK_EQ(-12E10, FLAG_float_flag);
  CHECK_EQ(0, strcmp(FLAG_string_flag, "foo-bar"));
}


TEST(Flags3b) {
  SetFlagsToDefault();
  const char* str =
      "--bool_flag notaflag --int_flag -666 --float_flag -12E10 "
      "-string-flag=foo-bar";
  CHECK_EQ(0, FlagList::SetFlagsFromString(str, strlen(str)));
  CHECK(FLAG_bool_flag);
  CHECK_EQ(-666, FLAG_int_flag);
  CHECK_EQ(-12E10, FLAG_float_flag);
  CHECK_EQ(0, strcmp(FLAG_string_flag, "foo-bar"));
}


TEST(Flags4) {
  SetFlagsToDefault();
  int argc = 3;
  const char* argv[] = { "Test4", "--bool_flag", "--foo" };
  CHECK_EQ(2, FlagList::SetFlagsFromCommandLine(&argc,
                                                const_cast<char **>(argv),
                                                true));
  CHECK_EQ(3, argc);
}


TEST(Flags4b) {
  SetFlagsToDefault();
  const char* str = "--bool_flag --foo";
  CHECK_EQ(2, FlagList::SetFlagsFromString(str, strlen(str)));
}


TEST(Flags5) {
  SetFlagsToDefault();
  int argc = 2;
  const char* argv[] = { "Test5", "--int_flag=\"foobar\"" };
  CHECK_EQ(1, FlagList::SetFlagsFromCommandLine(&argc,
                                                const_cast<char **>(argv),
                                                true));
  CHECK_EQ(2, argc);
}


TEST(Flags5b) {
  SetFlagsToDefault();
  const char* str = "                     --int_flag=\"foobar\"";
  CHECK_EQ(1, FlagList::SetFlagsFromString(str, strlen(str)));
}


TEST(Flags6) {
  SetFlagsToDefault();
  int argc = 4;
  const char* argv[] = { "Test5", "--int-flag", "0", "--float_flag" };
  CHECK_EQ(3, FlagList::SetFlagsFromCommandLine(&argc,
                                                const_cast<char **>(argv),
                                                true));
  CHECK_EQ(4, argc);
}


TEST(Flags6b) {
  SetFlagsToDefault();
  const char* str = "              --int-flag 0       --float_flag    ";
  CHECK_EQ(3, FlagList::SetFlagsFromString(str, strlen(str)));
}
