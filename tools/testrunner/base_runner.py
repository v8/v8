# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import os
import sys


# Add testrunner to the path.
sys.path.insert(
  0,
  os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))


class BaseTestRunner(object):
  def __init__(self):
    pass

  def execute(self):
    return self._do_execute()

  def _do_execute(self):
    raise NotImplementedError()
