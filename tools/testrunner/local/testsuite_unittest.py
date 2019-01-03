#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

# Needed because the test runner contains relative imports.
TOOLS_PATH = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.append(TOOLS_PATH)

from testrunner.local.testsuite import TestSuite
from testrunner.objects.testcase import TestCase


class TestSuiteTest(unittest.TestCase):
  def test_fail_ok_outcome(self):
    suite = TestSuite('foo', 'bar')
    suite.rules = {
      '': {
        'foo/bar': set(['FAIL_OK']),
        'baz/bar': set(['FAIL']),
      },
    }
    suite.prefix_rules = {}
    suite.tests = [
      TestCase(suite, 'foo/bar', 'foo/bar'),
      TestCase(suite, 'baz/bar', 'baz/bar'),
    ]

    for t in suite.tests:
      self.assertEquals(['FAIL'], t.expected_outcomes)


if __name__ == '__main__':
    unittest.main()
