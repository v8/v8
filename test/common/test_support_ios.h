// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2023 the V8 project authors. All rights reserved.

#ifndef V8_TEST_COMMON_TEST_SUPPORT_IOS_H_
#define V8_TEST_COMMON_TEST_SUPPORT_IOS_H_

namespace v8 {
namespace internal {

// Inits the initial args for tests on iOS.
void InitIOSArgs(int (*test_suite)(void), int argc, char* argv[]);

// Launches an iOS app that runs the tests in the suite passed to
// InitIOSRunHook.
int RunTestsFromIOSApp();

// Returns true if unittests should be run by the XCTest runner.
bool ShouldRunIOSUnittestsWithXCTest();

}  // namespace internal
}  // namespace v8

#endif  // V8_TEST_COMMON_TEST_SUPPORT_IOS_H_
