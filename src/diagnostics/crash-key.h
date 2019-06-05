// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Conflicts between v8/src/base and base prevent from including
// components/crash/core/common/crash_key.h into most of v8's files, so have to
// provide wrappers to localize the include to v8/src/base/crash-key.cc only.

#ifndef V8_DIAGNOSTICS_CRASH_KEY_H_
#define V8_DIAGNOSTICS_CRASH_KEY_H_

#include <stdint.h>

namespace v8 {
namespace internal {
namespace crash {

// Crash keys must be statically allocated so we'll have a few slots for
// pointer values and will log if we run out of space. The pointer value will
// be combined with the given name and id. Names should be sufficiently short
// to fit key_size limit.
// The crash key in the dump will look similar to:
//   {"v8-0", "isolate 0 0x21951a41d90"}
// (we assume a pointer is being logged and we convert it to hex).
constexpr int kKeySize = 64;
constexpr int kMaxCrashKeysCount = 16;

void AddCrashKey(int id, const char* name, uintptr_t value);

}  // namespace crash
}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_CRASH_KEY_H_
