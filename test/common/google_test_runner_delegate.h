// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2023 the V8 project authors. All rights reserved.

#ifndef V8_TEST_COMMON_GOOGLE_TEST_RUNNER_DELEGATE_H_
#define V8_TEST_COMMON_GOOGLE_TEST_RUNNER_DELEGATE_H_

// Copied from Chromium base/test/ios/google_test_runner_delegate.h
// to avoid the //base dependency. This protocol is required
// to run iOS Unittest.
@protocol GoogleTestRunnerDelegate

// Returns YES if this delegate supports running GoogleTests via a call to
// |runGoogleTests|.
@property(nonatomic, readonly, assign) BOOL supportsRunningGoogleTests;

// Runs GoogleTests and returns the final exit code.
- (int)runGoogleTests;

@end

#endif  // V8_TEST_COMMON_GOOGLE_TEST_RUNNER_DELEGATE_H_
