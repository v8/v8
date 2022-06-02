#!/usr/bin/env python3
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Global system tests for V8 test runners and fuzzers.

This hooks up the framework under tools/testrunner testing high-level scenarios
with different test suite extensions and build configurations.
"""

# TODO(machenbach): Mock out util.GuessOS to make these tests really platform
# independent.
# TODO(machenbach): Move coverage recording to a global test entry point to
# include other unittest suites in the coverage report.
# TODO(machenbach): Coverage data from multiprocessing doesn't work.
# TODO(majeski): Add some tests for the fuzzers.

import collections
import contextlib
import json
import os
import shutil
import sys
import tempfile
import unittest

from io import StringIO

TOOLS_ROOT = os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))
sys.path.append(TOOLS_ROOT)
from testrunner import standard_runner
from testrunner import num_fuzzer
from testrunner.local import command
from testrunner.local import pool


TEST_DATA_ROOT = os.path.join(TOOLS_ROOT, 'testrunner', 'testdata')

Result = collections.namedtuple(
    'Result', ['stdout', 'stderr', 'returncode', 'json'])

Result.__str__ = lambda self: (
    '\nReturncode: %s\nStdout:\n%s\nStderr:\n%s\n' %
    (self.returncode, self.stdout, self.stderr))


@contextlib.contextmanager
def temp_dir():
  """Wrapper making a temporary directory available."""
  path = None
  try:
    path = tempfile.mkdtemp('v8_test_')
    yield path
  finally:
    if path:
      shutil.rmtree(path)


@contextlib.contextmanager
def temp_base(baseroot='testroot1'):
  """Wrapper that sets up a temporary V8 test root.

  Args:
    baseroot: The folder with the test root blueprint. Relevant files will be
        copied to the temporary test root, to guarantee a fresh setup with no
        dirty state.
  """
  basedir = os.path.join(TEST_DATA_ROOT, baseroot)
  with temp_dir() as tempbase:
    if not os.path.exists(basedir):
      yield tempbase
      return
    builddir = os.path.join(tempbase, 'out', 'build')
    testroot = os.path.join(tempbase, 'test')
    os.makedirs(builddir)
    shutil.copy(os.path.join(basedir, 'v8_build_config.json'), builddir)
    shutil.copy(os.path.join(basedir, 'd8_mocked.py'), builddir)

    for suite in os.listdir(os.path.join(basedir, 'test')):
      os.makedirs(os.path.join(testroot, suite))
      for entry in os.listdir(os.path.join(basedir, 'test', suite)):
        shutil.copy(
            os.path.join(basedir, 'test', suite, entry),
            os.path.join(testroot, suite))
    yield tempbase


@contextlib.contextmanager
def capture():
  """Wrapper that replaces system stdout/stderr an provides the streams."""
  oldout = sys.stdout
  olderr = sys.stderr
  try:
    stdout=StringIO()
    stderr=StringIO()
    sys.stdout = stdout
    sys.stderr = stderr
    yield stdout, stderr
  finally:
    sys.stdout = oldout
    sys.stderr = olderr


def run_tests(*args, baseroot='testroot1', config_overrides={}, **kwargs):
  """Executes the test runner with captured output."""
  with temp_base(baseroot=baseroot) as basedir:
    override_build_config(basedir, **config_overrides)
    json_out_path = None
    def resolve_arg(arg):
      """Some arguments come as function objects to be called (resolved)
      in the context of a temporary test configuration"""
      nonlocal json_out_path
      if arg == with_json_output:
        json_out_path = with_json_output(basedir)
        return json_out_path
      return arg
    resolved_args = [resolve_arg(arg) for arg in args]
    with capture() as (stdout, stderr):
      sys_args = ['--command-prefix', sys.executable] + resolved_args
      if kwargs.get('infra_staging', False):
        sys_args.append('--infra-staging')
      else:
        sys_args.append('--no-infra-staging')
      code = standard_runner.StandardTestRunner(basedir=basedir).execute(sys_args)
      json_out = clean_json_output(json_out_path, basedir)
      return Result(stdout.getvalue(), stderr.getvalue(), code, json_out)

def with_json_output(basedir):
  """ Function used as a placeholder where we need to resolve the value in the
  context of a temporary test configuration"""
  return os.path.join(basedir, 'out.json')

def clean_json_output(json_path, basedir):
  # Extract relevant properties of the json output.
  if not json_path:
    return None
  with open(json_path) as f:
    json_output = json.load(f)

  # Replace duration in actual output as it's non-deterministic. Also
  # replace the python executable prefix as it has a different absolute
  # path dependent on where this runs.
  def replace_variable_data(data):
    data['duration'] = 1
    data['command'] = ' '.join(
        ['/usr/bin/python'] + data['command'].split()[1:])
    data['command'] = data['command'].replace(basedir + '/', '')
  for data in json_output['slowest_tests']:
    replace_variable_data(data)
  for data in json_output['results']:
    replace_variable_data(data)
  json_output['duration_mean'] = 1
  # We need lexicographic sorting here to avoid non-deterministic behaviour
  # The original sorting key is duration, but in our fake test we have
  # non-deterministic durations before we reset them to 1
  def sort_key(x):
    return str(sorted(x.items()))
  json_output['slowest_tests'].sort(key=sort_key)

  return json_output


def override_build_config(basedir, **kwargs):
  """Override the build config with new values provided as kwargs."""
  if not kwargs:
    return
  path = os.path.join(basedir, 'out', 'build', 'v8_build_config.json')
  with open(path) as f:
    config = json.load(f)
    config.update(kwargs)
  with open(path, 'w') as f:
    json.dump(config, f)


class SystemTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    command.setup_testing()
    pool.setup_testing()

  def testPass(self):
    """Test running only passing tests in two variants.

    Also test printing durations.
    """
    result = run_tests(
        '--progress=verbose',
        '--variants=default,stress',
        '--time',
        'sweet/bananas',
        'sweet/raspberries',
    )
    self.assertIn('sweet/bananas default: PASS', result.stdout, result)
    # TODO(majeski): Implement for test processors
    # self.assertIn('Total time:', result.stderr, result)
    # self.assertIn('sweet/bananas', result.stderr, result)
    self.assertEqual(0, result.returncode, result)

  def testPassHeavy(self):
    """Test running with some tests marked heavy."""
    result = run_tests(
        '--progress=verbose',
        '--variants=nooptimization',
        '-j2',
        'sweet',
        baseroot='testroot3',
    )
    self.assertIn('7 tests ran', result.stdout, result)
    self.assertEqual(0, result.returncode, result)

  def testShardedProc(self):
    for shard in [1, 2]:
      result = run_tests(
          '--progress=verbose',
          '--variants=default,stress',
          '--shard-count=2',
          '--shard-run=%d' % shard,
          'sweet/blackberries',
          'sweet/raspberries',
          infra_staging=False,
      )
      # One of the shards gets one variant of each test.
      self.assertIn('2 tests ran', result.stdout, result)
      if shard == 1:
        self.assertIn('sweet/raspberries default', result.stdout, result)
        self.assertIn('sweet/raspberries stress', result.stdout, result)
        self.assertEqual(0, result.returncode, result)
      else:
        self.assertIn(
          'sweet/blackberries default: FAIL', result.stdout, result)
        self.assertIn(
          'sweet/blackberries stress: FAIL', result.stdout, result)
        self.assertEqual(1, result.returncode, result)

  @unittest.skip("incompatible with test processors")
  def testSharded(self):
    """Test running a particular shard."""
    for shard in [1, 2]:
      result = run_tests(
          '--progress=verbose',
          '--variants=default,stress',
          '--shard-count=2',
          '--shard-run=%d' % shard,
          'sweet/bananas',
          'sweet/raspberries',
      )
      # One of the shards gets one variant of each test.
      self.assertIn('Running 2 tests', result.stdout, result)
      self.assertIn('sweet/bananas', result.stdout, result)
      self.assertIn('sweet/raspberries', result.stdout, result)
      self.assertEqual(0, result.returncode, result)

  def testFail(self):
    """Test running only failing tests in two variants."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default,stress',
        'sweet/strawberries',
        infra_staging=False,
    )
    self.assertIn('sweet/strawberries default: FAIL', result.stdout, result)
    self.assertEqual(1, result.returncode, result)


  def check_cleaned_json_output(
      self, expected_results_name, actual_json_out, basedir=None):
    with open(os.path.join(TEST_DATA_ROOT, expected_results_name)) as f:
      expected_test_results = json.load(f)

    pretty_json = json.dumps(actual_json_out, indent=2, sort_keys=True)
    msg = None  # Set to pretty_json for bootstrapping.
    self.assertDictEqual(actual_json_out, expected_test_results, msg)

  def testFailWithRerunAndJSON(self):
    """Test re-running a failing test and output to json."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        '--rerun-failures-count=2',
        '--random-seed=123',
        '--json-test-results', with_json_output,
        'sweet/strawberries',
        infra_staging=False,
    )
    self.assertIn('sweet/strawberries default: FAIL', result.stdout, result)
    # With test processors we don't count reruns as separated failures.
    # TODO(majeski): fix it?
    self.assertIn('1 tests failed', result.stdout, result)
    self.assertEqual(0, result.returncode, result)

    # TODO(majeski): Previously we only reported the variant flags in the
    # flags field of the test result.
    # After recent changes we report all flags, including the file names.
    # This is redundant to the command. Needs investigation.
    self.check_cleaned_json_output(
        'expected_test_results1.json', result.json)

  def testFlakeWithRerunAndJSON(self):
    """Test re-running a failing test and output to json."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        '--rerun-failures-count=2',
        '--random-seed=123',
        '--json-test-results', with_json_output,
        'sweet',
        baseroot='testroot2',
        infra_staging=False,
    )
    self.assertIn('sweet/bananaflakes default: FAIL PASS', result.stdout, result)
    self.assertIn('=== sweet/bananaflakes (flaky) ===', result.stdout, result)
    self.assertIn('1 tests failed', result.stdout, result)
    self.assertIn('1 tests were flaky', result.stdout, result)
    self.assertEqual(0, result.returncode, result)
    self.check_cleaned_json_output('expected_test_results2.json', result.json)

  def testAutoDetect(self):
    """Fake a build with several auto-detected options.

    Using all those options at once doesn't really make much sense. This is
    merely for getting coverage.
    """
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        'sweet/bananas',
        config_overrides=dict(
          dcheck_always_on=True, is_asan=True, is_cfi=True,
          is_msan=True, is_tsan=True, is_ubsan_vptr=True, target_cpu='x86',
          v8_enable_i18n_support=False, v8_target_cpu='x86',
          v8_enable_verify_csa=False, v8_enable_lite_mode=False,
          v8_enable_pointer_compression=False,
          v8_enable_pointer_compression_shared_cage=False,
          v8_enable_shared_ro_heap=False,
          v8_enable_sandbox=False
        )
    )
    expect_text = (
        '>>> Autodetected:\n'
        'asan\n'
        'cfi_vptr\n'
        'dcheck_always_on\n'
        'msan\n'
        'no_i18n\n'
        'tsan\n'
        'ubsan_vptr\n'
        'webassembly\n'
        '>>> Running tests for ia32.release')
    self.assertIn(expect_text, result.stdout, result)
    self.assertEqual(0, result.returncode, result)
    # TODO(machenbach): Test some more implications of the auto-detected
    # options, e.g. that the right env variables are set.

  def testSkips(self):
    """Test skipping tests in status file for a specific variant."""
    result = run_tests(
        '--progress=verbose',
        '--variants=nooptimization',
        'sweet/strawberries',
        infra_staging=False,
    )
    self.assertIn('0 tests ran', result.stdout, result)
    self.assertEqual(2, result.returncode, result)

  def testRunSkips(self):
    """Inverse the above. Test parameter to keep running skipped tests."""
    result = run_tests(
        '--progress=verbose',
        '--variants=nooptimization',
        '--run-skipped',
        'sweet/strawberries',
    )
    self.assertIn('1 tests failed', result.stdout, result)
    self.assertIn('1 tests ran', result.stdout, result)
    self.assertEqual(1, result.returncode, result)

  def testDefault(self):
    """Test using default test suites, though no tests are run since they don't
    exist in a test setting.
    """
    result = run_tests(
        infra_staging=False,
    )
    self.assertIn('0 tests ran', result.stdout, result)
    self.assertEqual(2, result.returncode, result)

  def testNoBuildConfig(self):
    """Test failing run when build config is not found."""
    result = run_tests(baseroot='wrong_path')
    self.assertIn('Failed to load build config', result.stdout, result)
    self.assertEqual(5, result.returncode, result)

  def testInconsistentArch(self):
    """Test failing run when attempting to wrongly override the arch."""
    result = run_tests('--arch=ia32')
    self.assertIn(
        '--arch value (ia32) inconsistent with build config (x64).',
        result.stdout, result)
    self.assertEqual(5, result.returncode, result)

  def testWrongVariant(self):
    """Test using a bogus variant."""
    result = run_tests('--variants=meh')
    self.assertEqual(5, result.returncode, result)

  def testModeFromBuildConfig(self):
    """Test auto-detection of mode from build config."""
    result = run_tests('--outdir=out/build', 'sweet/bananas')
    self.assertIn('Running tests for x64.release', result.stdout, result)
    self.assertEqual(0, result.returncode, result)

  def testPredictable(self):
    """Test running a test in verify-predictable mode.

    The test will fail because of missing allocation output. We verify that and
    that the predictable flags are passed and printed after failure.
    """
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        'sweet/bananas',
        infra_staging=False,
        config_overrides=dict(v8_enable_verify_predictable=True),
    )
    self.assertIn('1 tests ran', result.stdout, result)
    self.assertIn('sweet/bananas default: FAIL', result.stdout, result)
    self.assertIn('Test had no allocation output', result.stdout, result)
    self.assertIn('--predictable --verify-predictable', result.stdout, result)
    self.assertEqual(1, result.returncode, result)

  def testSlowArch(self):
    """Test timeout factor manipulation on slow architecture."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        'sweet/bananas',
        config_overrides=dict(v8_target_cpu='arm64'),
    )
    # TODO(machenbach): We don't have a way for testing if the correct
    # timeout was used.
    self.assertEqual(0, result.returncode, result)

  def testRandomSeedStressWithDefault(self):
    """Test using random-seed-stress feature has the right number of tests."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        '--random-seed-stress-count=2',
        'sweet/bananas',
        infra_staging=False,
    )
    self.assertIn('2 tests ran', result.stdout, result)
    self.assertEqual(0, result.returncode, result)

  def testRandomSeedStressWithSeed(self):
    """Test using random-seed-stress feature passing a random seed."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        '--random-seed-stress-count=2',
        '--random-seed=123',
        'sweet/strawberries',
    )
    self.assertIn('2 tests ran', result.stdout, result)
    # We use a failing test so that the command is printed and we can verify
    # that the right random seed was passed.
    self.assertIn('--random-seed=123', result.stdout, result)
    self.assertEqual(1, result.returncode, result)

  def testSpecificVariants(self):
    """Test using NO_VARIANTS modifiers in status files skips the desire tests.

    The test runner cmd line configures 4 tests to run (2 tests * 2 variants).
    But the status file applies a modifier to each skipping one of the
    variants.
    """
    result = run_tests(
        '--progress=verbose',
        '--variants=default,stress',
        'sweet/bananas',
        'sweet/raspberries',
        config_overrides=dict(is_asan=True),
    )
    # Both tests are either marked as running in only default or only
    # slow variant.
    self.assertIn('2 tests ran', result.stdout, result)
    self.assertEqual(0, result.returncode, result)

  def testStatusFilePresubmit(self):
    """Test that the fake status file is well-formed."""
    with temp_base() as basedir:
      from testrunner.local import statusfile
      self.assertTrue(statusfile.PresubmitCheck(
          os.path.join(basedir, 'test', 'sweet', 'sweet.status')))

  def testDotsProgress(self):
    result = run_tests(
        '--progress=dots',
        'sweet/cherries',
        'sweet/bananas',
        '--no-sorting', '-j1', # make results order deterministic
        infra_staging=False,
    )
    self.assertIn('2 tests ran', result.stdout, result)
    self.assertIn('F.', result.stdout, result)
    self.assertEqual(1, result.returncode, result)

  def testMonoProgress(self):
    self._testCompactProgress('mono')

  def testColorProgress(self):
    self._testCompactProgress('color')

  def _testCompactProgress(self, name):
    result = run_tests(
        '--progress=%s' % name,
        'sweet/cherries',
        'sweet/bananas',
        infra_staging=False,
    )
    if name == 'color':
      expected = ('\033[34m%  28\033[0m|'
                  '\033[32m+   1\033[0m|'
                  '\033[31m-   1\033[0m]: Done')
    else:
      expected = '%  28|+   1|-   1]: Done'
    self.assertIn(expected, result.stdout)
    self.assertIn('sweet/cherries', result.stdout)
    self.assertIn('sweet/bananas', result.stdout)
    self.assertEqual(1, result.returncode, result)

  def testExitAfterNFailures(self):
    result = run_tests(
        '--progress=verbose',
        '--exit-after-n-failures=2',
        '-j1',
        'sweet/mangoes',       # PASS
        'sweet/strawberries',  # FAIL
        'sweet/blackberries',  # FAIL
        'sweet/raspberries',   # should not run
    )
    self.assertIn('sweet/mangoes default: PASS', result.stdout, result)
    self.assertIn('sweet/strawberries default: FAIL', result.stdout, result)
    self.assertIn('Too many failures, exiting...', result.stdout, result)
    self.assertIn('sweet/blackberries default: FAIL', result.stdout, result)
    self.assertNotIn('sweet/raspberries', result.stdout, result)
    self.assertIn('2 tests failed', result.stdout, result)
    self.assertIn('3 tests ran', result.stdout, result)
    self.assertEqual(1, result.returncode, result)

  def testNumFuzzer(self):
    sys_args = ['--command-prefix', sys.executable, '--outdir', 'out/build']

    with temp_base() as basedir:
      with capture() as (stdout, stderr):
        code = num_fuzzer.NumFuzzer(basedir=basedir).execute(sys_args)
        result = Result(stdout.getvalue(), stderr.getvalue(), code, None)

      self.assertEqual(0, result.returncode, result)

  def testRunnerFlags(self):
    """Test that runner-specific flags are passed to tests."""
    result = run_tests(
        '--progress=verbose',
        '--variants=default',
        '--random-seed=42',
        'sweet/bananas',
        '-v',
    )

    self.assertIn(
        '--test bananas --random-seed=42 --nohard-abort --testing-d8-test-runner',
        result.stdout, result)
    self.assertEqual(0, result.returncode, result)


if __name__ == '__main__':
  unittest.main()
