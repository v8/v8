# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import itertools

from ..local import statusfile


class OutProc(object):
  def __init__(self, test):
    # TODO(majeski): Get only necessary fields from the test.
    self._test = test

  def get_outcome(self, output):
    if output.HasCrashed():
      return statusfile.CRASH
    elif output.HasTimedOut():
      return statusfile.TIMEOUT
    elif self._has_failed(output):
      return statusfile.FAIL
    else:
      return statusfile.PASS

  def _has_failed(self, output):
    execution_failed = self._is_failure_output(output)
    if self._is_negative():
      return not execution_failed
    return execution_failed

  def _is_failure_output(self, output):
    # TODO(majeski): Move implementation.
    return self._test.suite.IsFailureOutput(self._test, output)

  def _is_negative(self):
    # TODO(majeski): Move implementation.
    return self._test.suite.IsNegativeTest(self._test)


class ExpectedOutProc(OutProc):
  def __init__(self, expected_filename):
    self._expected_filename = expected_filename

  def _is_failure_output(self, output):
    with open(self._expected_filename, 'r') as f:
      expected_lines = f.readlines()

    for act_iterator in self._act_block_iterator(output):
      for expected, actual in itertools.izip_longest(
          self._expected_iterator(expected_lines),
          act_iterator,
          fillvalue=''
      ):
        if expected != actual:
          return True
      return False

  def _act_block_iterator(self, output):
    """Iterates over blocks of actual output lines."""
    lines = output.stdout.splitlines()
    start_index = 0
    found_eqeq = False
    for index, line in enumerate(lines):
      # If a stress test separator is found:
      if line.startswith('=='):
        # Iterate over all lines before a separator except the first.
        if not found_eqeq:
          found_eqeq = True
        else:
          yield self._actual_iterator(lines[start_index:index])
        # The next block of output lines starts after the separator.
        start_index = index + 1
    # Iterate over complete output if no separator was found.
    if not found_eqeq:
      yield self._actual_iterator(lines)

  def _actual_iterator(self, lines):
    return self._iterator(lines, self._ignore_actual_line)

  def _expected_iterator(self, lines):
    return self._iterator(lines, self._ignore_expected_line)

  def _ignore_actual_line(self, line):
    """Ignore empty lines, valgrind output, Android output and trace
    incremental marking output.
    """
    if not line:
      return True
    return (line.startswith('==') or
            line.startswith('**') or
            line.startswith('ANDROID') or
            '[IncrementalMarking]' in line or
            # FIXME(machenbach): The test driver shouldn't try to use slow
            # asserts if they weren't compiled. This fails in optdebug=2.
            line == 'Warning: unknown flag --enable-slow-asserts.' or
            line == 'Try --help for options')

  def _ignore_expected_line(self, line):
    return not line

  def _iterator(self, lines, ignore_predicate):
    for line in lines:
      line = line.strip()
      if not ignore_predicate(line):
        yield line

  def _is_negative(self):
    return False
