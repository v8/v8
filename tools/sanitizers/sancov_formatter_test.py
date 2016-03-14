# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Requires python-coverage. Native python coverage version >= 3.7.1 should
# be installed to get the best speed.

import copy
import coverage
import logging
import os
import sys
import unittest


# Directory of this file.
LOCATION = os.path.dirname(os.path.abspath(__file__))

# V8 checkout directory.
BASE_DIR = os.path.dirname(os.path.dirname(LOCATION))

# Executable location.
BUILD_DIR = os.path.join(BASE_DIR, 'out', 'Release')

def abs_line(line):
  """Absolute paths as output by the llvm symbolizer."""
  return '%s/%s' % (BUILD_DIR, line)


#------------------------------------------------------------------------------

# Data for test_process_symbolizer_output. This simulates output from the
# llvm symbolizer. The paths are not normlized.
SYMBOLIZER_OUTPUT = (
  abs_line('../../src/foo.cc:87:7\n') +
  abs_line('../../src/foo.cc:92:0\n') + # Test sorting.
  abs_line('../../src/baz/bar.h:1234567:0\n') + # Test large line numbers.
  abs_line('../../src/foo.cc:92:0\n') + # Test duplicates.
  abs_line('../../src/baz/bar.h:0:0\n') + # Test subdirs.
  '/usr/include/cool_stuff.h:14:2\n' + # Test dropping absolute paths.
  abs_line('../../src/foo.cc:87:10\n') + # Test dropping character indexes.
  abs_line('../../third_party/icu.cc:0:0\n') + # Test dropping excluded dirs.
  abs_line('../../src/baz/bar.h:11:0\n')
)

# The expected post-processed output maps relative file names to line numbers.
# The numbers are sorted and unique.
EXPECTED_PROCESSED_OUTPUT = {
  'src/baz/bar.h': [0, 11, 1234567],
  'src/foo.cc': [87, 92],
}


#------------------------------------------------------------------------------

# Data for test_merge_instrumented_line_results. A list of absolute paths to
# all executables.
EXE_LIST = [
  '/path/to/d8',
  '/path/to/cctest',
  '/path/to/unittests',
]

# Post-processed llvm symbolizer output as returned by
# process_symbolizer_output. These are lists of this output for merging.
INSTRUMENTED_LINE_RESULTS = [
  {
    'src/baz/bar.h': [0, 3, 7],
    'src/foo.cc': [11],
  },
  {
    'src/baz/bar.h': [3, 7, 8],
    'src/baz.cc': [2],
    'src/foo.cc': [1, 92],
  },
  {
    'src/baz.cc': [1],
    'src/foo.cc': [92, 93],
  },
]

# This shows initial instrumentation. No lines are covered, hence,
# the coverage mask is 0 for all lines. The line tuples remain sorted by
# line number and contain no duplicates.
EXPECTED_INSTRUMENTED_LINES_DATA = {
  'version': 1,
  'tests': ['cctest', 'd8', 'unittests'],
  'files': {
    'src/baz/bar.h': [[0, 0], [3, 0], [7, 0], [8, 0]],
    'src/baz.cc': [[1, 0], [2, 0]],
    'src/foo.cc': [[1, 0], [11, 0], [92, 0], [93, 0]],
  },
}


#------------------------------------------------------------------------------

# Data for test_merge_covered_line_results. List of post-processed
# llvm-symbolizer output as a tuple including the executable name of each data
# set.
COVERED_LINE_RESULTS = [
  ({
     'src/baz/bar.h': [3, 7],
     'src/foo.cc': [11],
   }, 'd8'),
  ({
     'src/baz/bar.h': [3, 7],
     'src/baz.cc': [2],
     'src/foo.cc': [1],
   }, 'cctest'),
  ({
     'src/foo.cc': [92],
     'src/baz.cc': [2],
   }, 'unittests'),
]

# This shows initial instrumentation + coverage. The mask bits are:
# cctest: 1, d8: 2, unittests:4. So a line covered by cctest and unittests
# has a coverage mask of 0b101, e.g. line 2 in src/baz.cc.
EXPECTED_COVERED_LINES_DATA = {
  'version': 1,
  'tests': ['cctest', 'd8', 'unittests'],
  'files': {
    'src/baz/bar.h': [[0, 0b0], [3, 0b11], [7, 0b11], [8, 0b0]],
    'src/baz.cc': [[1, 0b0], [2, 0b101]],
    'src/foo.cc': [[1, 0b1], [11, 0b10], [92, 0b100], [93, 0b0]],
  },
}


class FormatterTests(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    sys.path.append(LOCATION)
    cls._cov = coverage.coverage(
        include=([os.path.join(LOCATION, 'sancov_formatter.py')]))
    cls._cov.start()
    import sancov_formatter
    global sancov_formatter

  @classmethod
  def tearDownClass(cls):
    cls._cov.stop()
    cls._cov.report()

  def test_process_symbolizer_output(self):
    result = sancov_formatter.process_symbolizer_output(SYMBOLIZER_OUTPUT)
    self.assertEquals(EXPECTED_PROCESSED_OUTPUT, result)

  def test_merge_instrumented_line_results(self):
    result = sancov_formatter.merge_instrumented_line_results(
      EXE_LIST, INSTRUMENTED_LINE_RESULTS)
    self.assertEquals(EXPECTED_INSTRUMENTED_LINES_DATA, result)

  def test_merge_covered_line_results(self):
    data = copy.deepcopy(EXPECTED_INSTRUMENTED_LINES_DATA)
    sancov_formatter.merge_covered_line_results(
      data, COVERED_LINE_RESULTS)
    self.assertEquals(EXPECTED_COVERED_LINES_DATA, data)
