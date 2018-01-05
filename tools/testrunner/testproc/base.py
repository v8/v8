# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


class TestProc(object):
  def __init__(self):
    self._prev_proc = None
    self._next_proc = None

  def connect_to(self, next_proc):
    next_proc._prev_proc = self
    self._next_proc = next_proc

  def next_test(self, test):
    raise NotImplementedError()

  def result_for(self, test, result, is_last):
    raise NotImplementedError()

  ### Communication
  def _send_test(self, test):
    return self._next_proc.next_test(test)

  def _send_result(self, test, result, is_last=True):
    return self._prev_proc.result_for(test, result, is_last=is_last)



class TestProcObserver(TestProc):
  def next_test(self, test):
    self._on_next_test(test)
    self._send_test(test)

  def result_for(self, test, result, is_last):
    self._on_result_for(test, result, is_last)
    self._send_result(test, result, is_last)

  def _on_next_test(self, test):
    pass

  def _on_result_for(self, test, result, is_last):
    pass


class TestProcProducer(TestProc):
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
    raise NotImplementedError()

  ### Managing subtests
  def _create_subtest(self, test, subtest_id):
    return test.create_subtest(self, '%s-%s' % (self._name, subtest_id))

  def _get_subtest_origin(self, subtest):
    while subtest.processor and subtest.processor is not self:
      subtest = subtest.origin
    return subtest.origin
