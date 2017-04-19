# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import difflib

from testrunner.local import testsuite
from testrunner.objects import testcase


class MkGrokdump(testsuite.TestSuite):

  def __init__(self, name, root):
    super(MkGrokdump, self).__init__(name, root)

  def ListTests(self, context):
    test = testcase.TestCase(self, self.shell())
    return [test]

  def GetFlagsForTestCase(self, testcase, context):
    return []

  def IsFailureOutput(self, testcase):
    output = testcase.output
    v8_path = os.path.dirname(os.path.dirname(os.path.abspath(self.root)))
    expected_path = os.path.join(v8_path, "tools", "v8heapconst.py")
    with open(expected_path) as f:
      expected = f.read()
    if expected != output.stdout:
      if "generated from a non-shipping build" in output.stdout:
        return False
      assert "generated from a shipping build" in output.stdout
      expected_lines = expected.splitlines()
      actual_lines = output.stdout.splitlines()
      output.stdout = "%s differs from mkgrokdump output:\n\n" % expected_path
      output.stdout += '\n'.join(difflib.unified_diff(expected_lines, actual_lines))
      return True
    return False

  def shell(self):
    return "mkgrokdump"

def GetSuite(name, root):
  return MkGrokdump(name, root)
