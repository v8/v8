# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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
