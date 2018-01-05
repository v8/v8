# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from . import base


class LoadProc(base.TestProc):
  def load_tests(self, tests):
    loaded = set()
    for test in tests:
      if test.procid in loaded:
        print 'Warning: %s already obtained' % test.procid
        continue

      loaded.add(test.procid)
      self._send_test(test)

  def result_for(self, test, result, is_last):
    pass
