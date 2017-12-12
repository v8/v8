# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

from testrunner.local import testsuite
from testrunner.objects.testcase import TestCase


class FuzzerVariantGenerator(testsuite.VariantGenerator):
  # Only run the fuzzer with standard variant.
  def FilterVariantsByTest(self, test):
    return self.standard_variant

  def GetFlagSets(self, test, variant):
    return testsuite.FAST_VARIANT_FLAGS[variant]


class FuzzerTestSuite(testsuite.TestSuite):
  SUB_TESTS = ( 'json', 'parser', 'regexp', 'wasm', 'wasm_async',
          'wasm_call', 'wasm_code', 'wasm_compile', 'wasm_data_section',
          'wasm_function_sigs_section', 'wasm_globals_section',
          'wasm_imports_section', 'wasm_memory_section', 'wasm_names_section',
          'wasm_types_section' )

  def ListTests(self, context):
    tests = []
    for subtest in FuzzerTestSuite.SUB_TESTS:
      for fname in os.listdir(os.path.join(self.root, subtest)):
        if not os.path.isfile(os.path.join(self.root, subtest, fname)):
          continue
        test = self._create_test('%s/%s' % (subtest, fname))
        tests.append(test)
    tests.sort()
    return tests

  def _test_class(self):
    return FuzzerTestCase

  def _VariantGeneratorFactory(self):
    return FuzzerVariantGenerator


class FuzzerTestCase(TestCase):
  def _get_files_params(self, ctx):
    suite, name = self.path.split('/')
    return [os.path.join(self.suite.root, suite, name)]

  def _get_variant_flags(self):
    return []

  def _get_statusfile_flags(self):
    return []

  def _get_mode_flags(self, ctx):
    return []

  def _get_shell(self):
    group, _ = self.path.split('/', 1)
    return 'v8_simple_%s_fuzzer' % group


def GetSuite(name, root):
  return FuzzerTestSuite(name, root)
