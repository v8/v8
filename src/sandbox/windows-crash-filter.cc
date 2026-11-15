// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/windows-crash-filter.h"

#if defined(V8_OS_WIN) && defined(V8_ENABLE_SANDBOX)

#include <windows.h>

#include "src/base/logging.h"
#include "src/sandbox/sandbox-zero-segment.h"

namespace v8 {
namespace internal {
namespace {

PVOID volatile g_sandbox_start = nullptr;
PVOID volatile g_sandbox_end = nullptr;
volatile LONG g_harmless_exit_code = 0;
PVOID g_registered_handler = nullptr;

// Keep this callback intentionally small. It must not allocate, take locks,
// call into the V8 heap, or inspect memory at the faulting address.
LONG WINAPI WindowsSandboxCrashFilter(EXCEPTION_POINTERS* exception) {
  if (exception == nullptr || exception->ExceptionRecord == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const Address sandbox_start = reinterpret_cast<Address>(
      InterlockedCompareExchangePointer(&g_sandbox_start, nullptr, nullptr));
  const Address sandbox_end = reinterpret_cast<Address>(
      InterlockedCompareExchangePointer(&g_sandbox_end, nullptr, nullptr));

  if (!IsHarmlessWindowsSandboxAccessViolation(
          exception->ExceptionRecord, sandbox_start, sandbox_end)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // TerminateProcess is the Windows equivalent of the async-signal-safe _exit
  // used by the POSIX crash filter. Do not perform logging or cleanup here:
  // the process is already in an exception-dispatch path.
  const DWORD exit_code =
      static_cast<DWORD>(InterlockedCompareExchange(&g_harmless_exit_code, 0,
                                                    0));
  if (TerminateProcess(GetCurrentProcess(), exit_code)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // If termination fails, preserve normal Windows exception processing.
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

bool IsHarmlessWindowsSandboxAccessViolation(const EXCEPTION_RECORD* record,
                                             Address sandbox_start,
                                             Address sandbox_end) {
  if (record == nullptr ||
      record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      record->NumberParameters < 2 ||
      record->NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS) {
    return false;
  }

  // Windows documents 0 = read, 1 = write, and 8 = execute.
  const ULONG_PTR access_type = record->ExceptionInformation[0];
  if (access_type != 0 && access_type != 1 && access_type != 8) {
    return false;
  }

  const Address fault_address =
      static_cast<Address>(record->ExceptionInformation[1]);

  // A fault within the sandbox reservation is considered a harmless crash
  // while sandbox testing is enabled. The first 4GB plus its documented
  // padding is also reserved to mitigate Smi/HeapObject confusion.
  const bool inside_sandbox =
      sandbox_start < sandbox_end && sandbox_start <= fault_address &&
      fault_address < sandbox_end;
  const bool inside_zero_segment = fault_address < kMinZeroSegmentSize;
  return inside_sandbox || inside_zero_segment;
}

void InstallWindowsCrashFilter(Address sandbox_start, Address sandbox_end,
                                bool fuzzing_mode) {
  CHECK(g_registered_handler == nullptr);
  CHECK_LT(sandbox_start, sandbox_end);

  InterlockedExchangePointer(&g_sandbox_start,
                             reinterpret_cast<PVOID>(sandbox_start));
  InterlockedExchangePointer(&g_sandbox_end,
                             reinterpret_cast<PVOID>(sandbox_end));
  InterlockedExchange(&g_harmless_exit_code, fuzzing_mode ? 1 : 0);

  // FALSE appends this handler after first-priority handlers. In particular,
  // V8's existing Wasm trap handler is registered with TRUE and must get the
  // first opportunity to handle its own access violations.
  g_registered_handler =
      AddVectoredExceptionHandler(FALSE, WindowsSandboxCrashFilter);
  CHECK(g_registered_handler != nullptr);
}

void UninstallWindowsCrashFilter() {
  if (g_registered_handler == nullptr) return;

  PVOID handler = g_registered_handler;
  g_registered_handler = nullptr;
  CHECK(RemoveVectoredExceptionHandler(handler) != 0);

  InterlockedExchangePointer(&g_sandbox_start, nullptr);
  InterlockedExchangePointer(&g_sandbox_end, nullptr);
  InterlockedExchange(&g_harmless_exit_code, 0);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OS_WIN && V8_ENABLE_SANDBOX
