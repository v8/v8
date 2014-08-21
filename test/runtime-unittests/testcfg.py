# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil

from testrunner.local import commands
from testrunner.local import testsuite
from testrunner.local import utils
from testrunner.objects import testcase


class RuntimeUnitTestsSuite(testsuite.TestSuite):
  def __init__(self, name, root):
    super(RuntimeUnitTestsSuite, self).__init__(name, root)

  def ListTests(self, context):
    shell = os.path.abspath(os.path.join(context.shell_dir, self.shell()))
    if utils.IsWindows():
      shell += ".exe"
    output = commands.Execute(context.command_prefix +
                              [shell, "--gtest_list_tests"] +
                              context.extra_flags)
    if output.exit_code != 0:
      print output.stdout
      print output.stderr
      return []
    tests = []
    test_case = ''
    for line in output.stdout.splitlines():
      test_desc = line.strip().split()[0]
      if test_desc.endswith('.'):
        test_case = test_desc
      elif test_case and test_desc:
        test = testcase.TestCase(self, test_case + test_desc, dependency=None)
        tests.append(test)
    tests.sort()
    return tests

  def GetFlagsForTestCase(self, testcase, context):
    return (testcase.flags + ["--gtest_filter=" + testcase.path] +
            ["--gtest_random_seed=%s" % context.random_seed] +
            ["--gtest_print_time=0"] +
            context.mode_flags)

  def shell(self):
    return "runtime-unittests"


def GetSuite(name, root):
  return RuntimeUnitTestsSuite(name, root)
