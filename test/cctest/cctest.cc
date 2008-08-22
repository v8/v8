// Copyright 2008 Google Inc. All Rights Reserved.
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

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "cctest.h"


CcTest* CcTest::first_ = NULL;


CcTest::CcTest(TestFunction* callback, const char* file, const char* name)
    : callback_(callback), name_(name), prev_(first_) {
  // Find the base name of this test (const_cast required on Windows).
  char *basename = strrchr(const_cast<char *>(file), '/');
  if (!basename) {
    basename = strrchr(const_cast<char *>(file), '\\');
  }
  if (!basename) {
    basename = strdup(file);
  } else {
    basename = strdup(basename + 1);
  }
  // Drop the extension, if there is one.
  char *extension = strrchr(basename, '.');
  if (extension) *extension = 0;
  // Install this test in the list of tests
  file_ = basename;
  prev_ = first_;
  first_ = this;
}


int main(int argc, char *argv[]) {
  CcTest* current = CcTest::first();
  while (current != NULL) {
    printf("%s/%s\n", current->file(), current->name());
    current = current->prev();
  }
}
