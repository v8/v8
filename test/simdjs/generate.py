#!/usr/bin/python
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to re-generate SimdJs.json from a SimdJs.json.template.

import json
import os
import re

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))

SKIP_FILES = [
    '../ecmascript_simd',
    'base',
    # TODO(bradnelson): Drop these when tests are fixed upstream.
    'aobench',
    'averageFloat32x4Load',
    'matrix-multiplication-load',
]

run_js = open(
    os.path.join(SCRIPT_DIR, 'data', 'src', 'benchmarks', 'run.js')).read()
tests = re.findall("load \\(\\'([^']+)[.]js\\'\\)", run_js)
tests = [t for t in tests if t not in SKIP_FILES]

output = {
  'name': 'SIMDJS',
  'run_count': 5,
  'units': 'ms',
  'total': True,
  'resources': [
    'test/simdjs/data/src/benchmarks/base.js',
    'test/simdjs/data/src/ecmascript_simd.js'
  ] + ['test/simdjs/data/src/benchmarks/%s.js' % t for t in tests],
  'flags': ['--harmony-object', 'test/simdjs/harness-adapt.js'],
  'path': ['../../'],
  'tests': [
    {
      'name': test,
      'main': 'test/simdjs/harness-finish.js',
      'flags': ['test/simdjs/data/src/benchmarks/%s.js' % test],
      'results_regexp': '%s\\([ ]*([0-9.]+)(ms)?\\)',
      'tests': [
        {'name': 'SIMD'},
        {'name': 'Non-SIMD'},
        {'name': 'Speedup', 'units': 'score'},
        {'name': 'Iterations', 'units': 'count'}
      ]
    }
  for test in tests],
}

with open(os.path.join(SCRIPT_DIR, 'SimdJS.json'), 'w') as fh:
  fh.write(json.dumps(output, separators=(',',': '), indent=2, sort_keys=True))
