// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Noop implementation of crash keys to be used by targets that don't support
// crashpad.

#include "src/diagnostics/crash-key.h"

namespace v8 {
namespace internal {
namespace crash {

void AddCrashKey(int id, const char* name, uintptr_t value) {
}

}  // namespace crash
}  // namespace internal
}  // namespace v8
