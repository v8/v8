# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from . import base
from ..local.variants import ALL_VARIANTS, ALL_VARIANT_FLAGS
from .result import GroupedResult


STANDARD_VARIANT = set(["default"])


class VariantProc(base.TestProcProducer):
  """Processor creating variants.

  For each test it keeps generator that returns variant, flags and id suffix.
  It produces variants one at a time, so it's waiting for the result of one
  variant to create another variant of the same test.
  It maintains the order of the variants passed to the init.

  There are some cases when particular variant of the test is not valid. To
  ignore subtests like that, StatusFileFilterProc should be placed somewhere
  after the VariantProc.
  """

  def __init__(self, variants):
    super(VariantProc, self).__init__('VariantProc')
    self._test_data = {} # procid: (generator, results)
    self._variant_gens = {}
    self._variants = variants

  def _next_test(self, test):
    test_data = gen, results = self._variants_gen(test), []
    self._test_data[test.procid] = test_data
    self._try_send_new_subtest(test, gen, results)

  def _result_for(self, test, subtest, result):
    gen, results = self._test_data[test.procid]
    results.append((subtest, result))
    self._try_send_new_subtest(test, gen, results)

  def _try_send_new_subtest(self, test, variants_gen, results):
    for variant, flags, suffix in variants_gen:
      subtest = self._create_subtest(test, '%s-%s' % (variant, suffix),
                                     variant=variant, flags=flags)
      self._send_test(subtest)
      return

    del self._test_data[test.procid]
    # TODO(majeski): Don't group tests if previous processors don't need them.
    result = GroupedResult.create(results)
    self._send_result(test, result)

  def _variants_gen(self, test):
    """Generator producing (variant, flags, procid suffix) tuples."""
    return self._get_variants_gen(test).gen(test)

  def _get_variants_gen(self, test):
    key = test.suite.name
    variants_gen = self._variant_gens.get(key)
    if not variants_gen:
      variants_gen = test.suite.get_variants_gen(self._variants)
      self._variant_gens[key] = variants_gen
    return variants_gen
