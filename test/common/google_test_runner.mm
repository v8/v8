/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Copied from Chromium base/test/ios/google_test_runner.mm to avoid
// the //base dependency. The protocol is required to run iOS Unittest.

#import <UIKit/UIKit.h>
#import <XCTest/XCTest.h>

#import "test/common/google_test_runner_delegate.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface GoogleTestRunner : XCTestCase
@end

@implementation GoogleTestRunner

- (void)testRunGoogleTests {
  self.continueAfterFailure = false;

  id appDelegate = UIApplication.sharedApplication.delegate;
  XCTAssertTrue([appDelegate conformsToProtocol:@protocol(GoogleTestRunnerDelegate)]);

  id<GoogleTestRunnerDelegate> runnerDelegate =
      static_cast<id<GoogleTestRunnerDelegate>>(appDelegate);
  XCTAssertTrue(runnerDelegate.supportsRunningGoogleTests);
  XCTAssertTrue([runnerDelegate runGoogleTests] == 0);
}

@end