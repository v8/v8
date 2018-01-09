# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


"""
Pipeline

Test processors are chained together and communicate with each other by
calling previous/next processor in the chain.
     ----next_test()---->     ----next_test()---->
Proc1                    Proc2                    Proc3
     <---result_for()----     <---result_for()----

Subtests

When test processor needs to modify the test or create some variants of the
test it creates subtests and sends them to the next processor.
Each subtest has:
- procid - globally unique id that should contain id of the parent test and
          some suffix given by test processor, e.g. its name + subtest type.
- processor - which created it
- origin - pointer to the parent (sub)test
"""


class TestProc(object):
  def __init__(self):
    self._prev_proc = None
    self._next_proc = None

  def connect_to(self, next_proc):
    """Puts `next_proc` after itself in the chain."""
    next_proc._prev_proc = self
    self._next_proc = next_proc

  def next_test(self, test):
    """
    Method called by previous processor whenever it produces new test.
    This method shouldn't be called by anyone except previous processor.
    """
    raise NotImplementedError()

  def result_for(self, test, result, is_last):
    """
    Method called by next processor whenever it has result for some test.
    This method shouldn't be called by anyone except next processor.
    Args:
      test: test for which the `result` is
      result: result of calling test's outproc on the output
      is_last: for each test we've passed next processor may create subtests
               and pass results for all of them. `is_last` is set when it
               won't send any more results for subtests based on the `test`.
    """
    raise NotImplementedError()

  def heartbeat(self):
    if self._prev_proc:
      self._prev_proc.heartbeat()

  ### Communication
  def _send_test(self, test):
    """Helper method for sending test to the next processor."""
    return self._next_proc.next_test(test)

  def _send_result(self, test, result, is_last=True):
    """Helper method for sending result to the previous processor."""
    return self._prev_proc.result_for(test, result, is_last=is_last)



class TestProcObserver(TestProc):
  """Processor used for observing the data."""

  def next_test(self, test):
    self._on_next_test(test)
    self._send_test(test)

  def result_for(self, test, result, is_last):
    self._on_result_for(test, result, is_last)
    self._send_result(test, result, is_last)

  def heartbeat(self):
    self._on_heartbeat()
    super(TestProcObserver, self).heartbeat()

  def _on_next_test(self, test):
    """Method called after receiving test from previous processor but before
    sending it to the next one."""
    pass

  def _on_result_for(self, test, result, is_last):
    """Method called after receiving result from next processor but before
    sending it to the previous one."""
    pass

  def _on_heartbeat(self):
    pass


class TestProcProducer(TestProc):
  """Processor for creating subtests."""

  def __init__(self, name):
    super(TestProcProducer, self).__init__()
    self._name = name

  def next_test(self, test):
    return self._next_test(test)

  def result_for(self, subtest, result, is_last):
    test = self._get_subtest_origin(subtest)
    self._result_for(test, subtest, result, is_last)

  ### Implementation
  def _next_test(self, test):
    raise NotImplementedError()

  def _result_for(self, test, subtest, result, is_last):
    """
    result_for method extended with `subtest` parameter.

    Args
      test: test used by current processor to create the subtest.
      subtest: test for which the `result` is.
      other arguments are the same as for TestProc.result_for()
    """
    raise NotImplementedError()

  ### Managing subtests
  def _create_subtest(self, test, subtest_id):
    """Creates subtest with subtest id <processor name>-`subtest_id`."""
    return test.create_subtest(self, '%s-%s' % (self._name, subtest_id))

  def _get_subtest_origin(self, subtest):
    """Returns parent test that current processor used to create the subtest.
    None if there is no parent created by the current processor.
    """
    while subtest.processor and subtest.processor is not self:
      subtest = subtest.origin
    return subtest.origin
