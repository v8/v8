# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

from testrunner.local import testsuite
from testrunner.objects import testcase


class FuzzerTestSuite(testsuite.TestSuite):
  SUB_TESTS = ( 'parser', )

  def __init__(self, name, root):
    super(FuzzerTestSuite, self).__init__(name, root)

  def ListTests(self, context):
    tests = []
    for subtest in FuzzerTestSuite.SUB_TESTS:
      shell = '%s_fuzzer' % subtest
      for fname in os.listdir(os.path.join(self.root, subtest)):
        if not os.path.isfile(os.path.join(self.root, subtest, fname)):
          continue
        test = testcase.TestCase(self, '%s/%s' % (subtest, fname),
                                 override_shell=shell)
        tests.append(test)
    tests.sort()
    return tests

  def GetFlagsForTestCase(self, testcase, context):
    suite, name = testcase.path.split('/')
    return [os.path.join(self.root, suite, name)]


def GetSuite(name, root):
  return FuzzerTestSuite(name, root)
