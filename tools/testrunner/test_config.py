# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import random


# TODO(majeski): Move the rest of stuff from context
class TestConfig(object):
  def __init__(self, random_seed):
    # random_seed is always not None.
    self.random_seed = random_seed or self._gen_random_seed()

  def _gen_random_seed(self):
    seed = None
    while not seed:
      seed = random.SystemRandom().randint(-2147483648, 2147483647)
    return seed
