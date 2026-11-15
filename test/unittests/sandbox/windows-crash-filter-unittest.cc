// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/sandbox/sandbox.h"
#include "src/sandbox/testing.h"
#include "src/sandbox/windows-crash-filter.h"
#include "test/unittests/test-utils.h"

#if defined(V8_OS_WIN) && defined(V8_ENABLE_SANDBOX)

namespace v8 {
namespace internal {
namespace {

constexpr Address kSandboxStart = 0x100000000ULL;
constexpr Address kSandboxEnd = 0x110000000ULL;

EXCEPTION_RECORD MakeAccessViolation(ULONG_PTR access_type,
                                     ULONG_PTR fault_address) {
  EXCEPTION_RECORD record{};
  record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
  record.NumberParameters = 2;
  record.ExceptionInformation[0] = access_type;
  record.ExceptionInformation[1] = fault_address;
  return record;
}

TEST(WindowsSandboxCrashFilterTest, ClassifiesFaultInsideSandbox) {
  constexpr ULONG_PTR kAccessTypes[] = {0, 1, 8};
  for (ULONG_PTR access_type : kAccessTypes) {
    EXCEPTION_RECORD record =
        MakeAccessViolation(access_type, kSandboxStart + 0x1000);
    EXPECT_TRUE(IsHarmlessWindowsSandboxAccessViolation(
        &record, kSandboxStart, kSandboxEnd));
  }
}

TEST(WindowsSandboxCrashFilterTest, ClassifiesZeroSegmentFault) {
  EXCEPTION_RECORD record = MakeAccessViolation(1, 0x10000);
  EXPECT_TRUE(IsHarmlessWindowsSandboxAccessViolation(
      &record, kSandboxStart, kSandboxEnd));
}

TEST(WindowsSandboxCrashFilterTest, RejectsFaultOutsideSafeRanges) {
  EXCEPTION_RECORD record =
      MakeAccessViolation(1, kSandboxEnd + 0x1000);
  EXPECT_FALSE(IsHarmlessWindowsSandboxAccessViolation(
      &record, kSandboxStart, kSandboxEnd));
}

TEST(WindowsSandboxCrashFilterTest, RejectsUnknownAccessType) {
  EXCEPTION_RECORD record = MakeAccessViolation(2, kSandboxStart + 0x1000);
  EXPECT_FALSE(IsHarmlessWindowsSandboxAccessViolation(
      &record, kSandboxStart, kSandboxEnd));
}

TEST(WindowsSandboxCrashFilterTest, RejectsMalformedExceptionRecords) {
  EXCEPTION_RECORD record = MakeAccessViolation(1, kSandboxStart + 0x1000);
  record.NumberParameters = 1;
  EXPECT_FALSE(IsHarmlessWindowsSandboxAccessViolation(
      &record, kSandboxStart, kSandboxEnd));
}

TEST(WindowsSandboxCrashFilterTest, RejectsNonAccessViolationExceptions) {
  EXCEPTION_RECORD record = MakeAccessViolation(1, kSandboxStart + 0x1000);
  record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
  EXPECT_FALSE(IsHarmlessWindowsSandboxAccessViolation(
      &record, kSandboxStart, kSandboxEnd));
}


using WindowsSandboxCrashFilterIntegrationTest = TestWithIsolate;

// Exercise the registered VEH end-to-end in a child process. The sandbox
// reservation is inaccessible, so reading its base must raise an access
// violation that the filter classifies as harmless.
TEST_F(WindowsSandboxCrashFilterIntegrationTest,
       FiltersSandboxAccessViolation) {
  Sandbox* sandbox = Sandbox::current();
  ASSERT_EXIT(
      {
        SandboxTesting::Enable(SandboxTesting::Mode::kForTesting);
        volatile uint8_t value =
            *reinterpret_cast<volatile uint8_t*>(sandbox->base());
        (void)value;
      },
      testing::ExitedWithCode(0), "");
}

TEST_F(WindowsSandboxCrashFilterIntegrationTest,
       FuzzingModeUsesNonZeroExitCode) {
  Sandbox* sandbox = Sandbox::current();
  ASSERT_EXIT(
      {
        SandboxTesting::Enable(SandboxTesting::Mode::kForFuzzing);
        volatile uint8_t value =
            *reinterpret_cast<volatile uint8_t*>(sandbox->base());
        (void)value;
      },
      testing::ExitedWithCode(1), "");
}

}  // namespace
}  // namespace internal
}  // namespace v8

#endif  // V8_OS_WIN && V8_ENABLE_SANDBOX
