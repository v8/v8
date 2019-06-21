// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/crash-key.h"
#include "components/crash/core/common/crash_key.h"

#include <string>
#include <sstream>

namespace v8 {
namespace internal {
namespace crash {

using CrashKeyInstance = crash_reporter::CrashKeyString<kKeySize>;
static CrashKeyInstance crash_keys[] = {
  {"v8-0", CrashKeyInstance::Tag::kArray},
  {"v8-1", CrashKeyInstance::Tag::kArray},
  {"v8-2", CrashKeyInstance::Tag::kArray},
  {"v8-3", CrashKeyInstance::Tag::kArray},
  {"v8-4", CrashKeyInstance::Tag::kArray},
  {"v8-5", CrashKeyInstance::Tag::kArray},
  {"v8-6", CrashKeyInstance::Tag::kArray},
  {"v8-7", CrashKeyInstance::Tag::kArray},
  {"v8-8", CrashKeyInstance::Tag::kArray},
  {"v8-9", CrashKeyInstance::Tag::kArray},
  {"v8-10", CrashKeyInstance::Tag::kArray},
  {"v8-11", CrashKeyInstance::Tag::kArray},
  {"v8-12", CrashKeyInstance::Tag::kArray},
  {"v8-13", CrashKeyInstance::Tag::kArray},
  {"v8-14", CrashKeyInstance::Tag::kArray},
  {"v8-15", CrashKeyInstance::Tag::kArray},
};

void AddCrashKey(int id, const char* name, uintptr_t value) {
  static int current = 0;
  if (current > kMaxCrashKeysCount) {
    return;
  }

  if (current == kMaxCrashKeysCount) {
    static crash_reporter::CrashKeyString<1> over("v8-too-many-keys");
    over.Set("1");
    current++;
    return;
  }

  auto& trace_key = crash_keys[current];

  std::stringstream stream;
  stream << name << " " << id << " 0x" << std::hex << value;
  trace_key.Set(stream.str().substr(0, kKeySize));

  current++;
}

}  // namespace crash
}  // namespace internal
}  // namespace v8
