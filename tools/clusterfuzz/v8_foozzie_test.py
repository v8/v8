#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import random
import subprocess
import sys
import unittest

import v8_commands
import v8_foozzie
import v8_fuzz_config
import v8_suppressions

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FOOZZIE = os.path.join(BASE_DIR, 'v8_foozzie.py')
TEST_DATA = os.path.join(BASE_DIR, 'testdata')


class ConfigTest(unittest.TestCase):
  def testExperiments(self):
    """Test that probabilities add up to 100 and that all config names exist.
    """
    EXPERIMENTS = v8_fuzz_config.FOOZZIE_EXPERIMENTS
    CONFIGS = v8_foozzie.CONFIGS
    assert sum(x[0] for x in EXPERIMENTS) == 100
    assert all(map(lambda x: x[1] in CONFIGS, EXPERIMENTS))
    assert all(map(lambda x: x[2] in CONFIGS, EXPERIMENTS))
    assert all(map(lambda x: x[3].endswith('d8'), EXPERIMENTS))

  def testConfig(self):
    """Smoke test how to choose experiments.

    When experiment distribution changes this test might change, too.
    """
    self.assertEqual(
        [
          '--first-config=ignition',
          '--second-config=ignition_turbo_opt',
          '--second-d8=d8',
          '--second-config-extra-flags=--stress-scavenge=100',
          '--second-config-extra-flags=--no-regexp-tier-up',
          '--second-config-extra-flags=--no-enable-ssse3',
          '--second-config-extra-flags=--no-enable-bmi2',
          '--second-config-extra-flags=--no-enable-lzcnt',
        ],
        v8_fuzz_config.Config('foo', random.Random(42)).choose_foozzie_flags(),
    )


class UnitTest(unittest.TestCase):
  def testDiff(self):
    def diff_fun(one, two, skip=False):
      suppress = v8_suppressions.get_suppression(
          'x64', 'ignition', 'x64', 'ignition_turbo', skip)
      return suppress.diff_lines(one.splitlines(), two.splitlines())

    one = ''
    two = ''
    diff = None, None
    self.assertEquals(diff, diff_fun(one, two))

    one = 'a \n  b\nc();'
    two = 'a \n  b\nc();'
    diff = None, None
    self.assertEquals(diff, diff_fun(one, two))

    # Ignore line before caret, caret position and error message.
    one = """
undefined
weird stuff
      ^
somefile.js: TypeError: undefined is not a function
  undefined
"""
    two = """
undefined
other weird stuff
            ^
somefile.js: TypeError: baz is not a function
  undefined
"""
    diff = None, None
    self.assertEquals(diff, diff_fun(one, two))

    one = """
Still equal
Extra line
"""
    two = """
Still equal
"""
    diff = '- Extra line', None
    self.assertEquals(diff, diff_fun(one, two))

    one = """
Still equal
"""
    two = """
Still equal
Extra line
"""
    diff = '+ Extra line', None
    self.assertEquals(diff, diff_fun(one, two))

    one = """
undefined
somefile.js: TypeError: undefined is not a constructor
"""
    two = """
undefined
otherfile.js: TypeError: undefined is not a constructor
"""
    diff = """- somefile.js: TypeError: undefined is not a constructor
+ otherfile.js: TypeError: undefined is not a constructor""", None
    self.assertEquals(diff, diff_fun(one, two))

    # Test that skipping suppressions works.
    one = """
v8-foozzie source: foo
23:TypeError: bar is not a function
"""
    two = """
v8-foozzie source: foo
42:TypeError: baz is not a function
"""
    self.assertEquals((None, 'foo'), diff_fun(one, two))
    diff = """- 23:TypeError: bar is not a function
+ 42:TypeError: baz is not a function""", 'foo'
    self.assertEquals(diff, diff_fun(one, two, skip=True))

  def testOutputCapping(self):
    def output(stdout, is_crash):
      exit_code = -1 if is_crash else 0
      return v8_commands.Output(
          exit_code=exit_code, timed_out=False, stdout=stdout, pid=0)

    def check(stdout1, stdout2, is_crash1, is_crash2, capped_lines1,
              capped_lines2):
      output1 = output(stdout1, is_crash1)
      output2 = output(stdout2, is_crash2)
      self.assertEquals(
          (capped_lines1.splitlines(), capped_lines2.splitlines()),
          v8_suppressions.get_lines_capped(output1, output2))

    # No capping, already equal.
    check('1\n2', '1\n2', True, True, '1\n2', '1\n2')
    # No crash, no capping.
    check('1\n2', '1\n2\n3', False, False, '1\n2', '1\n2\n3')
    check('1\n2\n3', '1\n2', False, False, '1\n2\n3', '1\n2')
    # Cap smallest if all runs crash.
    check('1\n2', '1\n2\n3', True, True, '1\n2', '1\n2')
    check('1\n2\n3', '1\n2', True, True, '1\n2', '1\n2')
    # Cap the non-crashy run.
    check('1\n2\n3', '1\n2', False, True, '1\n2', '1\n2')
    check('1\n2', '1\n2\n3', True, False, '1\n2', '1\n2')
    # The crashy run has more output.
    check('1\n2\n3', '1\n2', True, False, '1\n2\n3', '1\n2')
    check('1\n2', '1\n2\n3', False, True, '1\n2', '1\n2\n3')
    # Keep output difference when capping.
    check('1\n2', '3\n4\n5', True, True, '1\n2', '3\n4')
    check('1\n2\n3', '4\n5', True, True, '1\n2', '4\n5')


def cut_verbose_output(stdout):
  # This removes first lines containing d8 commands.
  return '\n'.join(stdout.split('\n')[4:])


def run_foozzie(second_d8_dir, *extra_flags, **kwargs):
  second_config = 'ignition_turbo'
  if 'second_config' in kwargs:
    second_config = 'jitless'
  return subprocess.check_output([
    sys.executable, FOOZZIE,
    '--random-seed', '12345',
    '--first-d8', os.path.join(TEST_DATA, 'baseline', 'd8.py'),
    '--second-d8', os.path.join(TEST_DATA, second_d8_dir, 'd8.py'),
    '--first-config', 'ignition',
    '--second-config', second_config,
    os.path.join(TEST_DATA, 'fuzz-123.js'),
  ] + list(extra_flags))


class SystemTest(unittest.TestCase):
  """This tests the whole correctness-fuzzing harness with fake build
  artifacts.

  Overview of fakes:
    baseline: Example foozzie output including a syntax error.
    build1: Difference to baseline is a stack trace differece expected to
            be suppressed.
    build2: Difference to baseline is a non-suppressed output difference
            causing the script to fail.
    build3: As build1 but with an architecture difference as well.
  """
  def testSyntaxErrorDiffPass(self):
    stdout = run_foozzie('build1', '--skip-sanity-checks')
    self.assertEquals('# V8 correctness - pass\n', cut_verbose_output(stdout))
    # Default comparison includes suppressions.
    self.assertIn('v8_suppressions.js', stdout)
    # Default comparison doesn't include any specific mock files.
    self.assertNotIn('v8_mock_archs.js', stdout)
    self.assertNotIn('v8_mock_webassembly.js', stdout)

  def testDifferentOutputFail(self):
    with open(os.path.join(TEST_DATA, 'failure_output.txt')) as f:
      expected_output = f.read()
    with self.assertRaises(subprocess.CalledProcessError) as ctx:
      run_foozzie('build2', '--skip-sanity-checks',
                  '--first-config-extra-flags=--flag1',
                  '--first-config-extra-flags=--flag2=0',
                  '--second-config-extra-flags=--flag3')
    e = ctx.exception
    self.assertEquals(v8_foozzie.RETURN_FAIL, e.returncode)
    self.assertEquals(expected_output, cut_verbose_output(e.output))

  def testSanityCheck(self):
    with open(os.path.join(TEST_DATA, 'sanity_check_output.txt')) as f:
      expected_output = f.read()
    with self.assertRaises(subprocess.CalledProcessError) as ctx:
      run_foozzie('build2')
    e = ctx.exception
    self.assertEquals(v8_foozzie.RETURN_FAIL, e.returncode)
    self.assertEquals(expected_output, e.output)

  def testDifferentArch(self):
    """Test that the architecture-specific mocks are passed to both runs when
    we use executables with different architectures.
    """
    # Build 3 simulates x86, while the baseline is x64.
    stdout = run_foozzie('build3', '--skip-sanity-checks')
    lines = stdout.split('\n')
    # TODO(machenbach): Don't depend on the command-lines being printed in
    # particular lines.
    self.assertIn('v8_mock_archs.js', lines[1])
    self.assertIn('v8_mock_archs.js', lines[3])

  def testJitless(self):
    """Test that webassembly is mocked out when comparing with jitless."""
    stdout = run_foozzie(
        'build1', '--skip-sanity-checks', second_config='jitless')
    lines = stdout.split('\n')
    # TODO(machenbach): Don't depend on the command-lines being printed in
    # particular lines.
    self.assertIn('v8_mock_webassembly.js', lines[1])
    self.assertIn('v8_mock_webassembly.js', lines[3])

  def testSkipSuppressions(self):
    """Test that the suppressions file is not passed when skipping
    suppressions.
    """
    # Compare baseline with baseline. This passes as there is no difference.
    stdout = run_foozzie(
        'baseline', '--skip-sanity-checks', '--skip-suppressions')
    self.assertNotIn('v8_suppressions.js', stdout)

    # Compare with a build that usually suppresses a difference. Now we fail
    # since we skip suppressions.
    with self.assertRaises(subprocess.CalledProcessError) as ctx:
      run_foozzie(
          'build1', '--skip-sanity-checks', '--skip-suppressions')
    e = ctx.exception
    self.assertEquals(v8_foozzie.RETURN_FAIL, e.returncode)
    self.assertNotIn('v8_suppressions.js', e.output)


if __name__ == '__main__':
  unittest.main()
