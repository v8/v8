// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2023 the V8 project authors. All rights reserved.

#import <UIKit/UIKit.h>

#include "src/base/optional.h"
#include "test/common/google_test_runner_delegate.h"
#include "test/common/test_support_ios.h"

// Springboard will kill any iOS app that fails to check in after launch within
// a given time. Starting a UIApplication before invoking TestSuite::Run
// prevents this from happening.

// InitIOSRunHook saves the TestSuite and argc/argv, then invoking
// RunTestsFromIOSApp calls UIApplicationMain(), providing an application
// delegate class: ChromeUnitTestDelegate. The delegate implements
// application:didFinishLaunchingWithOptions: to invoke the TestSuite's Run
// method.

// Since the executable isn't likely to be a real iOS UI, the delegate puts up a
// window displaying the app name. If a bunch of apps using MainHook are being
// run in a row, this provides an indication of which one is currently running.
const char kEnableRunIOSUnittestsWithXCTest[] = "enable-run-ios-unittests-with-xctest";

static int (*g_test_suite)(void) = NULL;
static int g_argc;
static char** g_argv;
static v8::base::Optional<bool> g_is_xctest;

@interface UIApplication (Testing)
- (void)_terminateWithStatus:(int)status;
@end

// Xcode 6 introduced behavior in the iOS Simulator where the software
// keyboard does not appear if a hardware keyboard is connected. The following
// declaration allows this behavior to be overridden when the app starts up.
@interface UIKeyboardImpl
+ (instancetype)sharedInstance;
- (void)setAutomaticMinimizationEnabled:(BOOL)enabled;
- (void)setSoftwareKeyboardShownByTouch:(BOOL)enabled;
@end

// Can be used to easily check if the current application is being used for
// running tests.
@interface ChromeUnitTestApplication : UIApplication
- (BOOL)isRunningTests;
@end

@interface V8UnitTestDelegate : NSObject <GoogleTestRunnerDelegate> {
  UIWindow* _window;
}
- (void)runTests;
@end

@implementation V8UnitTestDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  // Xcode 6 introduced behavior in the iOS Simulator where the software
  // keyboard does not appear if a hardware keyboard is connected. The following
  // calls override this behavior by ensuring that the software keyboard is
  // always shown.
  [[UIKeyboardImpl sharedInstance] setAutomaticMinimizationEnabled:NO];
  if (@available(iOS 15, *)) {
  } else {
    [[UIKeyboardImpl sharedInstance] setSoftwareKeyboardShownByTouch:YES];
  }
  CGRect bounds = [[UIScreen mainScreen] bounds];

  _window = [[UIWindow alloc] initWithFrame:bounds];
  [_window setBackgroundColor:[UIColor whiteColor]];
  [_window makeKeyAndVisible];

  // Add a label with the app name.
  UILabel* label = [[UILabel alloc] initWithFrame:bounds];
  label.text = [[NSProcessInfo processInfo] processName];
  label.textAlignment = NSTextAlignmentCenter;
  [_window addSubview:label];

  // An NSInternalInconsistencyException is thrown if the app doesn't have a
  // root view controller. Set an empty one here.
  [_window setRootViewController:[[UIViewController alloc] init]];

  // Queue up the test run.
  if (!v8::internal::ShouldRunIOSUnittestsWithXCTest()) {
    // When running in XCTest mode, XCTest will invoke `runGoogleTest` directly.
    // Otherwise, schedule a call to `runTests`.
    [self performSelector:@selector(runTests) withObject:nil afterDelay:0.1];
  }

  return YES;
}

- (BOOL)supportsRunningGoogleTests {
  return v8::internal::ShouldRunIOSUnittestsWithXCTest();
}

- (int)runGoogleTests {
  int exitStatus = g_test_suite();

  return exitStatus;
}

- (void)runTests {
  DCHECK(!v8::internal::ShouldRunIOSUnittestsWithXCTest());

  int exitStatus = [self runGoogleTests];

  // If a test app is too fast, it will exit before Instruments has has a
  // a chance to initialize and no test results will be seen.
  // TODO(crbug.com/137010): Figure out how much time is actually needed, and
  // sleep only to make sure that much time has elapsed since launch.
  [NSThread sleepUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];

  // Use the hidden selector to try and cleanly take down the app (otherwise
  // things can think the app crashed even on a zero exit status).
  UIApplication* application = [UIApplication sharedApplication];
  [application _terminateWithStatus:exitStatus];

  exit(exitStatus);
}

@end

namespace v8 {
namespace internal {

void InitIOSArgs(int (*test_suite)(void), int argc, char* argv[]) {
  g_test_suite = test_suite;
  g_argc = argc;
  g_argv = argv;
}

int RunTestsFromIOSApp() {
  // When the tests main() is invoked it calls RunTestsFromIOSApp(). On its
  // invocation, this method fires up an iOS app via UIApplicationMain. The
  // TestSuite::Run will have be passed via InitIOSRunHook which will execute
  // the TestSuite once the UIApplication is ready.
  @autoreleasepool {
    return UIApplicationMain(g_argc, g_argv, nil, @"V8UnitTestDelegate");
  }
}

bool ShouldRunIOSUnittestsWithXCTest() {
  if (g_is_xctest.has_value()) {
    return g_is_xctest.value();
  }

  char** argv = g_argv;
  while (*argv != nullptr) {
    if (strstr(*argv, kEnableRunIOSUnittestsWithXCTest) != nullptr) {
      g_is_xctest = base::Optional<bool>(true);
      return true;
    }
    argv++;
  }
  g_is_xctest = base::Optional<bool>(false);
  return false;
}

}  // namespace internal
}  // namespace v8