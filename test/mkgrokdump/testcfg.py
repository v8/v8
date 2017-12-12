# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import difflib

from testrunner.local import testsuite
from testrunner.objects.testcase import TestCase


SHELL = 'mkgrokdump'

class MkGrokdump(testsuite.TestSuite):
  def ListTests(self, context):
    test = self._create_test(SHELL)
    return [test]

  def _test_class(self):
    return MkGrokTestCase

  def IsFailureOutput(self, testcase):
    output = testcase.output
    v8_path = os.path.dirname(os.path.dirname(os.path.abspath(self.root)))
    expected_path = os.path.join(v8_path, "tools", "v8heapconst.py")
    with open(expected_path) as f:
      expected = f.read()
    expected_lines = expected.splitlines()
    actual_lines = output.stdout.splitlines()
    diff = difflib.unified_diff(expected_lines, actual_lines, lineterm="",
                                fromfile="expected_path")
    diffstring = '\n'.join(diff)
    if diffstring is not "":
      if "generated from a non-shipping build" in output.stdout:
        return False
      if not "generated from a shipping build" in output.stdout:
        output.stdout = "Unexpected output:\n\n" + output.stdout
        return True
      output.stdout = diffstring
      return True
    return False


class MkGrokTestCase(TestCase):
  def _get_variant_flags(self):
    return []

  def _get_statusfile_flags(self):
    return []

  def _get_mode_flags(self, ctx):
    return []

  def _get_shell(self):
    return SHELL


def GetSuite(name, root):
  return MkGrokdump(name, root)
