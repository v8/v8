# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import itertools
import os
import re

from testrunner.local import testsuite
from testrunner.local import utils
from testrunner.objects import testcase


FLAGS_PATTERN = re.compile(r"//\s+Flags:(.*)")
INVALID_FLAGS = ["--enable-slow-asserts"]


class EmscriptenTestSuite(testsuite.TestSuite):
  def __init__(self, name, root):
    super(EmscriptenTestSuite, self).__init__(name, root)

  def ListTests(self, context):
    tests = []
    for dirname, dirs, files in os.walk(self.root):
      for dotted in [x for x in dirs if x.startswith('.')]:
        dirs.remove(dotted)
      dirs.sort()
      files.sort()
      for filename in files:
        if filename.endswith(".js"):
          testname = os.path.join(dirname[len(self.root) + 1:], filename[:-3])
          test = testcase.TestCase(self, testname)
          tests.append(test)
    return tests

  def GetFlagsForTestCase(self, testcase, context):
    source = self.GetSourceForTest(testcase)
    result = []
    flags_match = re.findall(FLAGS_PATTERN, source)
    for match in flags_match:
      result += match.strip().split()
    result += context.mode_flags
    result = [x for x in result if x not in INVALID_FLAGS]
    result.append(os.path.join(self.root, testcase.path + ".js"))
    return testcase.flags + result

  def GetSourceForTest(self, testcase):
    filename = os.path.join(self.root, testcase.path + self.suffix())
    with open(filename) as f:
      return f.read()

  def _IgnoreLine(self, string):
    """Ignore valgrind, NaCl and Android output."""
    return (string.startswith("==") or string.startswith("**") or
            string.startswith("ANDROID") or
            # These five patterns appear in normal Native Client output.
            string.startswith("DEBUG MODE ENABLED") or
            string.startswith("tools/nacl-run.py") or
            string.find("BYPASSING ALL ACL CHECKS") > 0 or
            string.find("Native Client module will be loaded") > 0 or
            string.find("NaClHostDescOpen:") > 0)

  def IsFailureOutput(self, output, testpath):
    expected_path = os.path.join(self.root, testpath + ".out")
    expected_lines = []
    # Can't use utils.ReadLinesFrom() here because it strips whitespace.
    with open(expected_path) as expected:
      expected_lines = expected.read().splitlines()
    raw_lines = output.stdout.splitlines()
    actual_lines = [ s for s in raw_lines if not self._IgnoreLine(s) ]
    env = { "basename": os.path.basename(testpath + ".js") }
    if len(expected_lines) != len(actual_lines):
      return True
    for (expected, actual) in itertools.izip_longest(
        expected_lines, actual_lines, fillvalue=''):
      if expected != actual:
        return True
    return False

  def StripOutputForTransmit(self, testcase):
    pass


def GetSuite(name, root):
  return EmscriptenTestSuite(name, root)
