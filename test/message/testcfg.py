# Copyright 2008 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import itertools
import os
import re

from testrunner.local import testsuite
from testrunner.local import utils
from testrunner.objects import outproc
from testrunner.objects import testcase


INVALID_FLAGS = ["--enable-slow-asserts"]
MODULE_PATTERN = re.compile(r"^// MODULE$", flags=re.MULTILINE)


class TestSuite(testsuite.TestSuite):
  def ListTests(self, context):
    tests = []
    for dirname, dirs, files in os.walk(self.root):
      for dotted in [x for x in dirs if x.startswith('.')]:
        dirs.remove(dotted)
      dirs.sort()
      files.sort()
      for filename in files:
        if filename.endswith(".js"):
          fullpath = os.path.join(dirname, filename)
          relpath = fullpath[len(self.root) + 1 : -3]
          testname = relpath.replace(os.path.sep, "/")
          test = self._create_test(testname)
          tests.append(test)
    return tests

  def _test_class(self):
    return TestCase

  def CreateVariantGenerator(self, variants):
    return super(TestSuite, self).CreateVariantGenerator(
        variants + ["preparser"])


class TestCase(testcase.TestCase):
  def __init__(self, *args, **kwargs):
    super(TestCase, self).__init__(*args, **kwargs)

    source = self.get_source()
    self._source_files = self._parse_source_files(source)
    self._source_flags = self._parse_source_flags(source)
    self._outproc = OutProc(os.path.join(self.suite.root, self.path),
                            self._expected_fail())

  def _parse_source_files(self, source):
    files = []
    if MODULE_PATTERN.search(source):
      files.append("--module")
    files.append(os.path.join(self.suite.root, self.path + ".js"))
    return files

  def _expected_fail(self):
    path = self.path
    while path:
      head, tail = os.path.split(path)
      if tail == 'fail':
        return True
      path = head
    return False

  def _get_cmd_params(self, ctx):
    params = super(TestCase, self)._get_cmd_params(ctx)
    return [p for p in params if p not in INVALID_FLAGS]

  def _get_files_params(self, ctx):
    return self._source_files

  def _get_source_flags(self):
    return self._source_flags

  def _get_source_path(self):
    return os.path.join(self.suite.root, self.path + self._get_suffix())

  @property
  def output_proc(self):
    return self._outproc


class OutProc(outproc.OutProc):
  def __init__(self, basepath, expected_fail):
    self._basepath = basepath
    self._expected_fail = expected_fail

  def _is_failure_output(self, output):
    fail = output.exit_code != 0
    if fail != self._expected_fail:
      return True

    expected_lines = []
    # Can't use utils.ReadLinesFrom() here because it strips whitespace.
    with open(self._basepath + '.out') as f:
      for line in f:
        if line.startswith("#") or not line.strip():
          continue
        expected_lines.append(line)
    raw_lines = output.stdout.splitlines()
    actual_lines = [ s for s in raw_lines if not self._ignore_line(s) ]
    if len(expected_lines) != len(actual_lines):
      return True

    env = {
      'basename': os.path.basename(self._basepath + '.js'),
    }
    for (expected, actual) in itertools.izip_longest(
        expected_lines, actual_lines, fillvalue=''):
      pattern = re.escape(expected.rstrip() % env)
      pattern = pattern.replace('\\*', '.*')
      pattern = pattern.replace('\\{NUMBER\\}', '\d+(?:\.\d*)?')
      pattern = '^%s$' % pattern
      if not re.match(pattern, actual):
        return True
    return False

  def _ignore_line(self, string):
    """Ignore empty lines, valgrind output, Android output."""
    return (
      not string or
      not string.strip() or
      string.startswith("==") or
      string.startswith("**") or
      string.startswith("ANDROID")
    )


def GetSuite(name, root):
  return TestSuite(name, root)
