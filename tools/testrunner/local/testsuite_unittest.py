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
  def test_filter_testcases_by_status_first_pass(self):
    suite = TestSuite('foo', 'bar')
    suite.tests = [
      TestCase(suite, 'foo/bar'),
      TestCase(suite, 'baz/bar'),
    ]
    suite.rules = {
      '': {
        'foo/bar': set(['PASS', 'SKIP']),
        'baz/bar': set(['PASS', 'FAIL']),
      },
    }
    suite.wildcards = {
      '': {
        'baz/*': set(['PASS', 'SLOW']),
      },
    }
    suite.FilterTestCasesByStatus(warn_unused_rules=False)
    self.assertEquals(
        [TestCase(suite, 'baz/bar')],
        suite.tests,
    )
    self.assertEquals(set(['PASS', 'FAIL', 'SLOW']), suite.tests[0].outcomes)

  def test_filter_testcases_by_status_second_pass(self):
    suite = TestSuite('foo', 'bar')
    suite.tests = [
      TestCase(suite, 'foo/bar', variant='default'),
      TestCase(suite, 'foo/bar', variant='stress', flags=['-v']),
      TestCase(suite, 'baz/bar', variant='default'),
      TestCase(suite, 'baz/bar', variant='stress', flags=['-v']),
    ]

    # Contrived outcomes from filtering by variant-independent rules.
    suite.tests[0].outcomes = set(['PREV'])
    suite.tests[1].outcomes = set(['PREV'])
    suite.tests[2].outcomes = set(['PREV'])
    suite.tests[3].outcomes = set(['PREV'])

    suite.rules = {
      'default': {
        'foo/bar': set(['PASS', 'SKIP']),
        'baz/bar': set(['PASS', 'FAIL']),
      },
      'stress': {
        'baz/bar': set(['SKIP']),
      },
    }
    suite.wildcards = {
      'default': {
        'baz/*': set(['PASS', 'SLOW']),
      },
      'stress': {
        'foo/*': set(['PASS', 'SLOW']),
      },
    }
    suite.FilterTestCasesByStatus(warn_unused_rules=False, variants=True)
    self.assertEquals(
        [
          TestCase(suite, 'foo/bar', flags=['-v']),
          TestCase(suite, 'baz/bar'),
        ],
        suite.tests,
    )

    self.assertEquals(
        set(['PASS', 'SLOW', 'PREV']),
        suite.tests[0].outcomes,
    )
    self.assertEquals(
        set(['PASS', 'FAIL', 'SLOW', 'PREV']),
        suite.tests[1].outcomes,
    )


if __name__ == '__main__':
    unittest.main()
