#!/usr/bin/env python
#
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from os.path import join
import itertools
import json
import math
import multiprocessing
import os
import random
import shlex
import sys
import time

# Adds testrunner to the path hence it has to be imported at the beggining.
import base_runner

from testrunner.local import execution
from testrunner.local import progress
from testrunner.local import testsuite
from testrunner.local import utils
from testrunner.local import verbose
from testrunner.objects import context


DEFAULT_TESTS = ["mjsunit", "webkit"]
TIMEOUT_DEFAULT = 60

# Double the timeout for these:
SLOW_ARCHS = ["arm",
              "mipsel"]
DISTRIBUTION_MODES = ["smooth", "random"]


class GCFuzzer(base_runner.BaseTestRunner):
  def __init__(self):
    super(GCFuzzer, self).__init__()

  class RandomDistribution:
    def __init__(self, seed=None):
      seed = seed or random.randint(1, sys.maxint)
      print "Using random distribution with seed %d" % seed
      self._random = random.Random(seed)

    def Distribute(self, n, m):
      if n > m:
        n = m
      return self._random.sample(xrange(1, m + 1), n)

  class SmoothDistribution:
    """Distribute n numbers into the interval [1:m].
    F1: Factor of the first derivation of the distribution function.
    F2: Factor of the second derivation of the distribution function.
    With F1 and F2 set to 0, the distribution will be equal.
    """
    def __init__(self, factor1=2.0, factor2=0.2):
      self._factor1 = factor1
      self._factor2 = factor2

    def Distribute(self, n, m):
      if n > m:
        n = m
      if n <= 1:
        return [ 1 ]

      result = []
      x = 0.0
      dx = 1.0
      ddx = self._factor1
      dddx = self._factor2
      for i in range(0, n):
        result += [ x ]
        x += dx
        dx += ddx
        ddx += dddx

      # Project the distribution into the interval [0:M].
      result = [ x * m / result[-1] for x in result ]

      # Equalize by n. The closer n is to m, the more equal will be the
      # distribution.
      for (i, x) in enumerate(result):
        # The value of x if it was equally distributed.
        equal_x = i / float(n - 1) * float(m - 1) + 1

        # Difference factor between actual and equal distribution.
        diff = 1 - (x / equal_x)

        # Equalize x dependent on the number of values to distribute.
        result[i] = int(x + (i + 1) * diff)
      return result


  def _distribution(self, options):
    if options.distribution_mode == "random":
      return self.RandomDistribution(options.seed)
    if options.distribution_mode == "smooth":
      return self.SmoothDistribution(options.distribution_factor1,
                                     options.distribution_factor2)


  def _add_parser_options(self, parser):
    parser.add_option("--command-prefix",
                      help="Prepended to each shell command used to run a test",
                      default="")
    parser.add_option("--coverage", help=("Exponential test coverage "
                      "(range 0.0, 1.0) - 0.0: one test, 1.0 all tests (slow)"),
                      default=0.4, type="float")
    parser.add_option("--coverage-lift", help=("Lifts test coverage for tests "
                      "with a low memory size reached (range 0, inf)"),
                      default=20, type="int")
    parser.add_option("--distribution-factor1", help=("Factor of the first "
                      "derivation of the distribution function"), default=2.0,
                      type="float")
    parser.add_option("--distribution-factor2", help=("Factor of the second "
                      "derivation of the distribution function"), default=0.7,
                      type="float")
    parser.add_option("--distribution-mode", help=("How to distribute flag "
                      "values for a given test (smooth|random)"),
                      default="smooth")
    parser.add_option("--dump-results-file", help="Dump maximum limit reached")
    parser.add_option("--extra-flags",
                      help="Additional flags to pass to each test command",
                      default="")
    parser.add_option("--isolates", help="Whether to test isolates",
                      default=False, action="store_true")
    parser.add_option("-j", help="The number of parallel tasks to run",
                      default=0, type="int")
    parser.add_option("-p", "--progress",
                      help=("The style of progress indicator"
                            " (verbose, dots, color, mono)"),
                      choices=progress.PROGRESS_INDICATORS.keys(),
                      default="mono")
    parser.add_option("--shard-count",
                      help="Split testsuites into this number of shards",
                      default=1, type="int")
    parser.add_option("--shard-run",
                      help="Run this shard from the split up tests.",
                      default=1, type="int")
    parser.add_option("--seed", help="The seed for the random distribution",
                      type="int")
    parser.add_option("-t", "--timeout", help="Timeout in seconds",
                      default= -1, type="int")
    parser.add_option("--random-seed", default=0,
                      help="Default seed for initializing random generator")
    parser.add_option("--fuzzer-random-seed", default=0,
                      help="Default seed for initializing fuzzer random "
                      "generator")
    parser.add_option("--stress-compaction", default=False, action="store_true",
                      help="Enable stress_compaction_percentage flag")
    return parser


  def _process_options(self, options):
    # Special processing of other options, sorted alphabetically.
    options.command_prefix = shlex.split(options.command_prefix)
    options.extra_flags = shlex.split(options.extra_flags)
    if options.j == 0:
      options.j = multiprocessing.cpu_count()
    while options.random_seed == 0:
      options.random_seed = random.SystemRandom().randint(-2147483648,
                                                          2147483647)
    while options.fuzzer_random_seed == 0:
      options.fuzzer_random_seed = random.SystemRandom().randint(-2147483648,
                                                                 2147483647)
    if not options.distribution_mode in DISTRIBUTION_MODES:
      print "Unknown distribution mode %s" % options.distribution_mode
      return False
    if options.distribution_factor1 < 0.0:
      print ("Distribution factor1 %s is out of range. Defaulting to 0.0"
          % options.distribution_factor1)
      options.distribution_factor1 = 0.0
    if options.distribution_factor2 < 0.0:
      print ("Distribution factor2 %s is out of range. Defaulting to 0.0"
          % options.distribution_factor2)
      options.distribution_factor2 = 0.0
    if options.coverage < 0.0 or options.coverage > 1.0:
      print ("Coverage %s is out of range. Defaulting to 0.4"
          % options.coverage)
      options.coverage = 0.4
    if options.coverage_lift < 0:
      print ("Coverage lift %s is out of range. Defaulting to 0"
          % options.coverage_lift)
      options.coverage_lift = 0
    return True

  def _shard_tests(self, tests, shard_count, shard_run):
    if shard_count < 2:
      return tests
    if shard_run < 1 or shard_run > shard_count:
      print "shard-run not a valid number, should be in [1:shard-count]"
      print "defaulting back to running all tests"
      return tests
    count = 0
    shard = []
    for test in tests:
      if count % shard_count == shard_run - 1:
        shard.append(test)
      count += 1
    return shard

  def _do_execute(self, options, args):
    suite_paths = utils.GetSuitePaths(join(base_runner.BASE_DIR, "test"))

    if len(args) == 0:
      suite_paths = [ s for s in suite_paths if s in DEFAULT_TESTS ]
    else:
      args_suites = set()
      for arg in args:
        suite = arg.split(os.path.sep)[0]
        if not suite in args_suites:
          args_suites.add(suite)
      suite_paths = [ s for s in suite_paths if s in args_suites ]

    suites = []
    for root in suite_paths:
      suite = testsuite.TestSuite.LoadTestSuite(
          os.path.join(base_runner.BASE_DIR, "test", root))
      if suite:
        suites.append(suite)

    try:
      return self._execute(args, options, suites)
    except KeyboardInterrupt:
      return 2


  def _calculate_n_tests(self, m, options):
    """Calculates the number of tests from m points with exponential coverage.
    The coverage is expected to be between 0.0 and 1.0.
    The 'coverage lift' lifts the coverage for tests with smaller m values.
    """
    c = float(options.coverage)
    l = float(options.coverage_lift)
    return int(math.pow(m, (m * c + l) / (m + l)))


  def _execute(self, args, options, suites):
    print(">>> Running tests for %s.%s" % (self.build_config.arch,
                                           self.mode_name))

    dist = self._distribution(options)

    # Populate context object.
    timeout = options.timeout
    if timeout == -1:
      # Simulators are slow, therefore allow a longer default timeout.
      if self.build_config.arch in SLOW_ARCHS:
        timeout = 2 * TIMEOUT_DEFAULT;
      else:
        timeout = TIMEOUT_DEFAULT;

    timeout *= self.mode_options.timeout_scalefactor
    ctx = context.Context(self.build_config.arch,
                          self.mode_options.execution_mode,
                          self.outdir,
                          self.mode_options.flags, options.verbose,
                          timeout, options.isolates,
                          options.command_prefix,
                          options.extra_flags,
                          False,  # Keep i18n on by default.
                          options.random_seed,
                          True,  # No sorting of test cases.
                          0,  # Don't rerun failing tests.
                          0,  # No use of a rerun-failing-tests maximum.
                          False,  # No predictable mode.
                          False,  # No no_harness mode.
                          False,  # Don't use perf data.
                          False)  # Coverage not supported.

    num_tests = self._load_tests(args, options, suites, ctx)
    if num_tests == 0:
      print "No tests to run."
      return 0

    test_backup = dict(map(lambda s: (s, s.tests), suites))

    print('>>> Collection phase')
    for s in suites:
      analysis_flags = [
        # > 100% to not influence default incremental marking, but we need this
        # flag to print reached incremental marking limit.
        '--stress_incremental_marking_percentage', '200',
        '--trace_incremental_marking',
      ]
      s.tests = map(lambda t: t.CopyAddingFlags(t.variant, analysis_flags),
                    s.tests)

    progress_indicator = progress.PROGRESS_INDICATORS[options.progress]()
    runner = execution.Runner(suites, progress_indicator, ctx)
    exit_code = runner.Run(options.j)

    print('>>> Analysis phase')
    test_results = dict()
    for s in suites:
      for t in s.tests:
        max_limit = self._get_max_limit_reached(t)
        if max_limit:
          test_results[t.path] = max_limit

    if options.dump_results_file:
      with file("%s.%d.txt" % (options.dump_results_file, time.time()),
                "w") as f:
        f.write(json.dumps(test_results))

    num_tests = 0
    for s in suites:
      s.tests = []
      for t in test_backup[s]:
        max_percent = test_results.get(t.path, 0)
        if not max_percent or max_percent < 1.0:
          continue
        max_percent = int(max_percent)

        # Calculate distribution.
        im_count = self._calculate_n_tests(max_percent, options)
        im_distribution = dist.Distribute(im_count, max_percent)
        if options.stress_compaction:
          compaction_count = self._calculate_n_tests(100, options)
          compaction_distribution = dist.Distribute(compaction_count, 100)
          distribution = itertools.product(
            im_distribution, compaction_distribution)
        else:
          # 0 disables the second flag.
          distribution = itertools.product(im_distribution, [0])

        if options.verbose:
          distribution = list(distribution)
          print "%s %s (max=%.02f)" % (t.path, distribution, max_percent)
        for im, compaction in distribution:
          fuzzing_flags = [
            "--stress_incremental_marking_percentage", str(im),
            "--stress_compaction_percentage", str(compaction),
          ]
          if options.random_seed:
            fuzzing_flags += [
              '--fuzzer_random_seed', str(options.fuzzer_random_seed)
            ]
          s.tests.append(t.CopyAddingFlags(t.variant, fuzzing_flags))
      num_tests += len(s.tests)

    if num_tests == 0:
      print "No tests to run."
      return 0

    print(">>> Fuzzing phase (%d test cases)" % num_tests)
    progress_indicator = progress.PROGRESS_INDICATORS[options.progress]()
    runner = execution.Runner(suites, progress_indicator, ctx)

    return runner.Run(options.j) or exit_code

  def _load_tests(self, args, options, suites, ctx):
    # Find available test suites and read test cases from them.
    variables = {
      "arch": self.build_config.arch,
      "asan": self.build_config.asan,
      "byteorder": sys.byteorder,
      "dcheck_always_on": self.build_config.dcheck_always_on,
      "deopt_fuzzer": False,
      "gc_fuzzer": True,
      "gc_stress": False,
      "gcov_coverage": self.build_config.gcov_coverage,
      "isolates": options.isolates,
      "mode": self.mode_options.status_mode,
      "msan": self.build_config.msan,
      "no_harness": False,
      "no_i18n": self.build_config.no_i18n,
      "no_snap": self.build_config.no_snap,
      "novfp3": False,
      "predictable": self.build_config.predictable,
      "simulator": utils.UseSimulator(self.build_config.arch),
      "simulator_run": False,
      "system": utils.GuessOS(),
      "tsan": self.build_config.tsan,
      "ubsan_vptr": self.build_config.ubsan_vptr,
    }

    num_tests = 0
    test_id = 0
    for s in suites:
      s.ReadStatusFile(variables)
      s.ReadTestCases(ctx)
      if len(args) > 0:
        s.FilterTestCasesByArgs(args)
      s.FilterTestCasesByStatus(False)
      num_tests += len(s.tests)
      for t in s.tests:
        t.id = test_id
        test_id += 1

    return num_tests

  # Parses test stdout and returns what was the highest reached percent of the
  # incremental marking limit (0-100).
  # Skips values above 100% since they already trigger incremental marking.
  @staticmethod
  def _get_max_limit_reached(test):
    def is_im_line(l):
      return 'IncrementalMarking' in l and '% of the memory limit reached' in l

    def line_to_percent(l):
      return filter(lambda part: '%' in part, l.split(' '))[0]

    def percent_str_to_float(s):
      return float(s[:-1])

    if not (test.output and test.output.stdout):
      return None

    im_lines = filter(is_im_line, test.output.stdout.splitlines())
    percents_str = map(line_to_percent, im_lines)
    percents = map(percent_str_to_float, percents_str)

    # Skip >= 100%.
    percents = filter(lambda p: p < 100, percents)

    if not percents:
      return None
    return max(percents)


if __name__ == '__main__':
  sys.exit(GCFuzzer().execute())
