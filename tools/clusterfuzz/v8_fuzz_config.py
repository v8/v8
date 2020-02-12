# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import random

THIS_DIR = os.path.dirname(os.path.abspath(__file__))

# List of configuration experiments for correctness fuzzing.
# List of <probability>, <1st config name>, <2nd config name>, <2nd d8>.
# Probabilities must add up to 100.
with open(os.path.join(THIS_DIR, 'v8_fuzz_experiments.json')) as f:
  FOOZZIE_EXPERIMENTS = json.load(f)

# Additional flag experiments. List of tuples like
# (<likelihood to use flags in [0,1)>, <flag>).
with open(os.path.join(THIS_DIR, 'v8_fuzz_flags.json')) as f:
  ADDITIONAL_FLAGS = json.load(f)


class Config(object):
  def __init__(self, name, rng=None):
    """
    Args:
      name: Name of the used fuzzer.
      rng: Random number generator for generating experiments.
      random_seed: Random-seed used for d8 throughout one fuzz session.
    """
    self.name = name
    self.rng = rng or random.Random()

  def choose_foozzie_flags(self):
    """Randomly chooses a configuration from FOOZZIE_EXPERIMENTS.

    Returns: List of flags to pass to v8_foozzie.py fuzz harness.
    """

    # Add additional flags to second config based on experiment percentages.
    extra_flags = []
    for p, flag in ADDITIONAL_FLAGS:
      if self.rng.random() < p:
        extra_flags.append('--second-config-extra-flags=%s' % flag)

    # Calculate flags determining the experiment.
    acc = 0
    threshold = self.rng.random() * 100
    for prob, first_config, second_config, second_d8 in FOOZZIE_EXPERIMENTS:
      acc += prob
      if acc > threshold:
        return [
          '--first-config=' + first_config,
          '--second-config=' + second_config,
          '--second-d8=' + second_d8,
        ] + extra_flags
    assert False
