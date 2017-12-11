# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

from testrunner.local import command
from testrunner.local import utils
from testrunner.local.testsuite import TestSuite, StandardVariantGenerator
from testrunner.objects import testcase


class GoogleTestSuite(TestSuite):
  def __init__(self, name, root):
    super(GoogleTestSuite, self).__init__(name, root)

  def ListTests(self, context):
    shell = os.path.abspath(
      os.path.join(context.shell_dir, self.GetShellForTestCase(None)))
    if utils.IsWindows():
      shell += ".exe"

    output = None
    for i in xrange(3): # Try 3 times in case of errors.
      cmd = command.Command(
        cmd_prefix=context.command_prefix,
        shell=shell,
        args=['--gtest_list_tests'] + context.extra_flags)
      output = cmd.execute()
      if output.exit_code == 0:
        break
      print "Test executable failed to list the tests (try %d).\n\nCmd:" % i
      print cmd
      print "\nStdout:"
      print output.stdout
      print "\nStderr:"
      print output.stderr
      print "\nExit code: %d" % output.exit_code
    else:
      raise Exception("Test executable failed to list the tests.")

    tests = []
    test_case = ''
    for line in output.stdout.splitlines():
      test_desc = line.strip().split()[0]
      if test_desc.endswith('.'):
        test_case = test_desc
      elif test_case and test_desc:
        test = testcase.TestCase(self, test_case + test_desc)
        tests.append(test)
    tests.sort(key=lambda t: t.path)
    return tests

  def GetParametersForTestCase(self, testcase, context):
    flags = (
      testcase.flags +
      ["--gtest_filter=" + testcase.path] +
      ["--gtest_random_seed=%s" % context.random_seed] +
      ["--gtest_print_time=0"] +
      context.mode_flags)
    return [], flags, {}

  def _VariantGeneratorFactory(self):
    return StandardVariantGenerator

  def GetShellForTestCase(self, testcase):
    return self.name


def GetSuite(name, root):
  return GoogleTestSuite(name, root)
