#!/usr/bin/env python
# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

from testrunner.local import testsuite, statusfile


class TestLoader(testsuite.TestLoader):
  pass

class TestSuite(testsuite.TestSuite):
  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return testsuite.TestCase

  def ListTests(self):
    fast = self._test_loader._create_test("fast", self)
    slow = self._test_loader._create_test("slow", self)
    slow._statusfile_outcomes.append(statusfile.SLOW)
    yield fast
    yield slow

def GetSuite(*args, **kwargs):
  return TestSuite(*args, **kwargs)
