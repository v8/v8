// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_WINDOWS_CRASH_FILTER_H_
#define V8_SANDBOX_WINDOWS_CRASH_FILTER_H_

#include "src/common/globals.h"

#if defined(V8_OS_WIN) && defined(V8_ENABLE_SANDBOX)
#include <windows.h>

namespace v8 {
namespace internal {

// Returns true only for well-formed access violations whose faulting address
// lies in the sandbox reservation or the platform's reserved zero segment.
// This helper is deliberately pure so its classification rules can be tested
// without installing a process-wide exception handler.
bool IsHarmlessWindowsSandboxAccessViolation(const EXCEPTION_RECORD* record,
                                             Address sandbox_start,
                                             Address sandbox_end);

// Installs the Windows VEH crash filter. The handler is registered last so
// V8's existing first-priority Wasm trap handler gets the first opportunity
// to consume WebAssembly bounds-check exceptions.
void InstallWindowsCrashFilter(Address sandbox_start, Address sandbox_end,
                                bool fuzzing_mode);
void UninstallWindowsCrashFilter();

}  // namespace internal
}  // namespace v8
#endif  // V8_OS_WIN && V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_WINDOWS_CRASH_FILTER_H_
