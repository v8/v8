# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import signal

from . import base


class SignalProc(base.TestProcObserver):
  def __init__(self):
    super(SignalProc, self).__init__()
    self._ctrlc = False

    signal.signal(signal.SIGINT, self._on_ctrlc)

  def _on_next_test(self, _test):
    self._on_event()

  def _on_result_for(self, _test, _result):
    self._on_event()

  def _on_ctrlc(self, _signum, _stack_frame):
    print '>>> Ctrl-C detected, waiting for ongoing tests to finish...'
    self._ctrlc = True

  def _on_event(self):
    if self._ctrlc:
      self.stop()
