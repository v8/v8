# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

from testrunner.local import testsuite
from testrunner.local import utils
from testrunner.objects import outproc
from testrunner.objects import testcase

PROTOCOL_TEST_JS = "protocol-test.js"
EXPECTED_SUFFIX = "-expected.txt"
RESOURCES_FOLDER = "resources"

class TestSuite(testsuite.TestSuite):
  def ListTests(self, context):
    tests = []
    for dirname, dirs, files in os.walk(
        os.path.join(self.root), followlinks=True):
      for dotted in [x for x in dirs if x.startswith('.')]:
        dirs.remove(dotted)
      if dirname.endswith(os.path.sep + RESOURCES_FOLDER):
        continue
      dirs.sort()
      files.sort()
      for filename in files:
        if filename.endswith(".js") and filename != PROTOCOL_TEST_JS:
          fullpath = os.path.join(dirname, filename)
          relpath = fullpath[len(self.root) + 1 : -3]
          testname = relpath.replace(os.path.sep, "/")
          test = self._create_test(testname)
          tests.append(test)
    return tests

  def _test_class(self):
    return TestCase


class TestCase(testcase.TestCase):
  def __init__(self, *args, **kwargs):
    super(TestCase, self).__init__(*args, **kwargs)

    self._source_flags = self._parse_source_flags()
    self._outproc = outproc.ExpectedOutProc(
        os.path.join(self.suite.root, self.path) + EXPECTED_SUFFIX)

  def _get_files_params(self, ctx):
    return [
      os.path.join(self.suite.root, PROTOCOL_TEST_JS),
      os.path.join(self.suite.root, self.path + self._get_suffix()),
    ]

  def _get_source_flags(self):
    return self._source_flags

  def _get_source_path(self):
    return os.path.join(self.suite.root, self.path + self._get_suffix())

  def get_shell(self):
    return 'inspector-test'

  def get_output_proc(self):
    return self._outproc


def GetSuite(name, root):
  return TestSuite(name, root)
