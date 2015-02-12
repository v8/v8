// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/version.h"

// These macros define the version number for the current version.
// NOTE these macros are used by some of the tool scripts and the build
// system so their names cannot be changed without changing the scripts.
#define MAJOR_VERSION     4
#define MINOR_VERSION     2
#define BUILD_NUMBER      68
#define PATCH_LEVEL       0
// Use 1 for candidates and 0 otherwise.
// (Boolean macro values are not supported by all preprocessors.)
#define IS_CANDIDATE_VERSION 0

// Define SONAME to have the build system put a specific SONAME into the
// shared library instead the generic SONAME generated from the V8 version
// number. This define is mainly used by the build system script.
#define SONAME            ""

#if IS_CANDIDATE_VERSION
#define CANDIDATE_STRING " (candidate)"
#else
#define CANDIDATE_STRING ""
#endif

#define SX(x) #x
#define S(x) SX(x)

#if PATCH_LEVEL > 0
#define VERSION_STRING                                                         \
  S(MAJOR_VERSION) "." S(MINOR_VERSION) "." S(BUILD_NUMBER) "." S(PATCH_LEVEL) \
      CANDIDATE_STRING
#else
#define VERSION_STRING \
  S(MAJOR_VERSION) "." S(MINOR_VERSION) "." S(BUILD_NUMBER) CANDIDATE_STRING
#endif

namespace v8 {
namespace internal {

int Version::major_ = MAJOR_VERSION;
int Version::minor_ = MINOR_VERSION;
int Version::build_ = BUILD_NUMBER;
int Version::patch_ = PATCH_LEVEL;
bool Version::candidate_ = (IS_CANDIDATE_VERSION != 0);
const char* Version::soname_ = SONAME;
const char* Version::version_string_ = VERSION_STRING;

// Calculate the V8 version string.
void Version::GetString(Vector<char> str) {
  const char* candidate = IsCandidate() ? " (candidate)" : "";
#ifdef USE_SIMULATOR
  const char* is_simulator = " SIMULATOR";
#else
  const char* is_simulator = "";
#endif  // USE_SIMULATOR
  if (GetPatch() > 0) {
    SNPrintF(str, "%d.%d.%d.%d%s%s",
             GetMajor(), GetMinor(), GetBuild(), GetPatch(), candidate,
             is_simulator);
  } else {
    SNPrintF(str, "%d.%d.%d%s%s",
             GetMajor(), GetMinor(), GetBuild(), candidate,
             is_simulator);
  }
}


// Calculate the SONAME for the V8 shared library.
void Version::GetSONAME(Vector<char> str) {
  if (soname_ == NULL || *soname_ == '\0') {
    // Generate generic SONAME if no specific SONAME is defined.
    const char* candidate = IsCandidate() ? "-candidate" : "";
    if (GetPatch() > 0) {
      SNPrintF(str, "libv8-%d.%d.%d.%d%s.so",
               GetMajor(), GetMinor(), GetBuild(), GetPatch(), candidate);
    } else {
      SNPrintF(str, "libv8-%d.%d.%d%s.so",
               GetMajor(), GetMinor(), GetBuild(), candidate);
    }
  } else {
    // Use specific SONAME.
    SNPrintF(str, "%s", soname_);
  }
}

} }  // namespace v8::internal
