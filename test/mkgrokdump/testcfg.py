# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import difflib

from testrunner.local import testsuite
from testrunner.objects import outproc
from testrunner.objects import testcase


SHELL = 'mkgrokdump'

class TestSuite(testsuite.TestSuite):
  def __init__(self, *args, **kwargs):
    super(TestSuite, self).__init__(*args, **kwargs)

    v8_path = os.path.dirname(os.path.dirname(os.path.abspath(self.root)))
    self.expected_path = os.path.join(v8_path, 'tools', 'v8heapconst.py')

  def ListTests(self, context):
    test = self._create_test(SHELL)
    return [test]

  def _test_class(self):
    return TestCase


class TestCase(testcase.TestCase):
  def _get_variant_flags(self):
    return []

  def _get_statusfile_flags(self):
    return []

  def _get_mode_flags(self, ctx):
    return []

  def get_shell(self):
    return SHELL

  @property
  def output_proc(self):
    return OutProc(self.expected_outcomes, self.suite.expected_path)


class OutProc(outproc.OutProc):
  def __init__(self, expected_outcomes, expected_path):
    super(OutProc, self).__init__(expected_outcomes)
    self._expected_path = expected_path

  def _is_failure_output(self, output):
    with open(self._expected_path) as f:
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


def GetSuite(name, root):
  return TestSuite(name, root)
