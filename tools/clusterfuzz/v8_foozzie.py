#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
V8 correctness fuzzer launcher script.
"""

# for py2/py3 compatibility
from __future__ import print_function

import argparse
import hashlib
import itertools
import os
import random
import re
import sys
import traceback

import v8_commands
import v8_suppressions

CONFIGS = dict(
  default=[],
  ignition=[
    '--turbo-filter=~',
    '--noopt',
    '--liftoff',
    '--no-wasm-tier-up',
  ],
  ignition_asm=[
    '--turbo-filter=~',
    '--noopt',
    '--validate-asm',
    '--stress-validate-asm',
  ],
  ignition_eager=[
    '--turbo-filter=~',
    '--noopt',
    '--no-lazy',
    '--no-lazy-inner-functions',
  ],
  ignition_no_ic=[
    '--turbo-filter=~',
    '--noopt',
    '--liftoff',
    '--no-wasm-tier-up',
    '--no-use-ic',
    '--no-lazy-feedback-allocation',
  ],
  ignition_turbo=[],
  ignition_turbo_no_ic=[
    '--no-use-ic',
  ],
  ignition_turbo_opt=[
    '--always-opt',
    '--no-liftoff',
    '--no-wasm-tier-up',
    '--no-lazy-feedback-allocation'
  ],
  ignition_turbo_opt_eager=[
    '--always-opt',
    '--no-lazy',
    '--no-lazy-inner-functions',
    '--no-lazy-feedback-allocation',
  ],
  jitless=[
    '--jitless',
  ],
  slow_path=[
    '--force-slow-path',
  ],
  slow_path_opt=[
    '--always-opt',
    '--force-slow-path',
    '--no-lazy-feedback-allocation',
  ],
  trusted=[
    '--no-untrusted-code-mitigations',
  ],
  trusted_opt=[
    '--always-opt',
    '--no-untrusted-code-mitigations',
    '--no-lazy-feedback-allocation',
  ],
)

# Return codes.
RETURN_PASS = 0
RETURN_FAIL = 2

BASE_PATH = os.path.dirname(os.path.abspath(__file__))
SANITY_CHECKS = os.path.join(BASE_PATH, 'v8_sanity_checks.js')

# Output for suppressed failure case.
FAILURE_HEADER_TEMPLATE = """#
# V8 correctness failure
# V8 correctness configs: %(configs)s
# V8 correctness sources: %(source_key)s
# V8 correctness suppression: %(suppression)s
"""

# Extended output for failure case. The 'CHECK' is for the minimizer.
FAILURE_TEMPLATE = FAILURE_HEADER_TEMPLATE + """#
# CHECK
#
# Compared %(first_config_label)s with %(second_config_label)s
#
# Flags of %(first_config_label)s:
%(first_config_flags)s
# Flags of %(second_config_label)s:
%(second_config_flags)s
#
# Difference:
%(difference)s%(source_file_text)s
#
### Start of configuration %(first_config_label)s:
%(first_config_output)s
### End of configuration %(first_config_label)s
#
### Start of configuration %(second_config_label)s:
%(second_config_output)s
### End of configuration %(second_config_label)s
"""

SOURCE_FILE_TEMPLATE = """
#
# Source file:
%s"""


FUZZ_TEST_RE = re.compile(r'.*fuzz(-\d+\.js)')
SOURCE_RE = re.compile(r'print\("v8-foozzie source: (.*)"\);')

# The number of hex digits used from the hash of the original source file path.
# Keep the number small to avoid duplicate explosion.
ORIGINAL_SOURCE_HASH_LENGTH = 3

# Placeholder string if no original source file could be determined.
ORIGINAL_SOURCE_DEFAULT = 'none'


def parse_args():
  parser = argparse.ArgumentParser()
  parser.add_argument(
    '--random-seed', type=int, required=True,
    help='random seed passed to both runs')
  parser.add_argument(
      '--first-config', help='first configuration', default='ignition')
  parser.add_argument(
      '--second-config', help='second configuration', default='ignition_turbo')
  parser.add_argument(
      '--first-config-extra-flags', action='append', default=[],
      help='Additional flags to pass to the run of the first configuration')
  parser.add_argument(
      '--second-config-extra-flags', action='append', default=[],
      help='Additional flags to pass to the run of the second configuration')
  parser.add_argument(
      '--first-d8', default='d8',
      help='optional path to first d8 executable, '
           'default: bundled in the same directory as this script')
  parser.add_argument(
      '--second-d8',
      help='optional path to second d8 executable, default: same as first')
  parser.add_argument(
      '--skip-sanity-checks', default=False, action='store_true',
      help='skip sanity checks for testing purposes')
  parser.add_argument('testcase', help='path to test case')
  options = parser.parse_args()

  # Ensure we have a test case.
  assert (os.path.exists(options.testcase) and
          os.path.isfile(options.testcase)), (
      'Test case %s doesn\'t exist' % options.testcase)

  # Use first d8 as default for second d8.
  options.second_d8 = options.second_d8 or options.first_d8

  # Ensure we make a sane comparison.
  if (options.first_d8 == options.second_d8 and
      options.first_config == options.second_config):
    parser.error('Need either executable or config difference.')

  assert options.first_config in CONFIGS
  assert options.second_config in CONFIGS

  return options


def get_meta_data(content):
  """Extracts original-source-file paths from test case content."""
  sources = []
  for line in content.splitlines():
    match = SOURCE_RE.match(line)
    if match:
      sources.append(match.group(1))
  return {'sources': sources}


def content_bailout(content, ignore_fun):
  """Print failure state and return if ignore_fun matches content."""
  bug = (ignore_fun(content) or '').strip()
  if bug:
    print(FAILURE_HEADER_TEMPLATE % dict(
        configs='', source_key='', suppression=bug))
    return True
  return False


def pass_bailout(output, step_number):
  """Print info and return if in timeout or crash pass states."""
  if output.HasTimedOut():
    # Dashed output, so that no other clusterfuzz tools can match the
    # words timeout or crash.
    print('# V8 correctness - T-I-M-E-O-U-T %d' % step_number)
    return True
  if output.HasCrashed():
    print('# V8 correctness - C-R-A-S-H %d' % step_number)
    return True
  return False


def fail_bailout(output, ignore_by_output_fun):
  """Print failure state and return if ignore_by_output_fun matches output."""
  bug = (ignore_by_output_fun(output.stdout) or '').strip()
  if bug:
    print(FAILURE_HEADER_TEMPLATE % dict(
        configs='', source_key='', suppression=bug))
    return True
  return False


def print_difference(
    options, source_key, first_command, second_command,
    first_config_output, second_config_output, difference, source=None):
  # The first three entries will be parsed by clusterfuzz. Format changes
  # will require changes on the clusterfuzz side.
  first_config_label = '%s,%s' % (first_command.arch, options.first_config)
  second_config_label = '%s,%s' % (second_command.arch, options.second_config)
  source_file_text = SOURCE_FILE_TEMPLATE % source if source else ''
  print((FAILURE_TEMPLATE % dict(
      configs='%s:%s' % (first_config_label, second_config_label),
      source_file_text=source_file_text,
      source_key=source_key,
      suppression='', # We can't tie bugs to differences.
      first_config_label=first_config_label,
      second_config_label=second_config_label,
      first_config_flags=' '.join(first_command.flags),
      second_config_flags=' '.join(second_command.flags),
      first_config_output=
          first_config_output.stdout.decode('utf-8', 'replace'),
      second_config_output=
          second_config_output.stdout.decode('utf-8', 'replace'),
      source=source,
      difference=difference.decode('utf-8', 'replace'),
  )).encode('utf-8', 'replace'))


def main():
  options = parse_args()

  # Set up runtime arguments.
  first_config_flags = (CONFIGS[options.first_config] +
                        options.first_config_extra_flags)
  second_config_flags = (CONFIGS[options.second_config] +
                         options.second_config_extra_flags)

  first_cmd = v8_commands.Command(
      'first', options.first_d8, options.random_seed, first_config_flags)
  second_cmd = v8_commands.Command(
      'second', options.second_d8, options.random_seed, second_config_flags)

  # Suppressions are architecture and configuration specific.
  suppress = v8_suppressions.get_suppression(
      first_cmd.arch, options.first_config,
      second_cmd.arch, options.second_config,
  )

  # Static bailout based on test case content or metadata.
  with open(options.testcase) as f:
    content = f.read()
  if content_bailout(get_meta_data(content), suppress.ignore_by_metadata):
    return RETURN_FAIL
  if content_bailout(content, suppress.ignore_by_content):
    return RETURN_FAIL

  # Sanity checks. Run both configurations with the sanity-checks file only and
  # bail out early if different.
  if not options.skip_sanity_checks:
    first_config_output = first_cmd.run(SANITY_CHECKS)
    second_config_output = second_cmd.run(SANITY_CHECKS)
    difference, _ = suppress.diff(
        first_config_output.stdout, second_config_output.stdout)
    if difference:
      # Special source key for sanity checks so that clusterfuzz dedupes all
      # cases on this in case it's hit.
      source_key = 'sanity check failed'
      print_difference(
          options, source_key, first_cmd, second_cmd,
          first_config_output, second_config_output, difference)
      return RETURN_FAIL

  first_config_output = first_cmd.run(options.testcase, verbose=True)

  # Early bailout based on first run's output.
  if pass_bailout(first_config_output, 1):
    return RETURN_PASS

  second_config_output = second_cmd.run(options.testcase, verbose=True)

  # Bailout based on second run's output.
  if pass_bailout(second_config_output, 2):
    return RETURN_PASS

  difference, source = suppress.diff(
      first_config_output.stdout, second_config_output.stdout)

  if source:
    source_key = hashlib.sha1(source).hexdigest()[:ORIGINAL_SOURCE_HASH_LENGTH]
  else:
    source_key = ORIGINAL_SOURCE_DEFAULT

  if difference:
    # Only bail out due to suppressed output if there was a difference. If a
    # suppression doesn't show up anymore in the statistics, we might want to
    # remove it.
    if fail_bailout(first_config_output, suppress.ignore_by_output1):
      return RETURN_FAIL
    if fail_bailout(second_config_output, suppress.ignore_by_output2):
      return RETURN_FAIL

    print_difference(
        options, source_key, first_cmd, second_cmd,
        first_config_output, second_config_output, difference, source)
    return RETURN_FAIL

  # TODO(machenbach): Figure out if we could also return a bug in case there's
  # no difference, but one of the line suppressions has matched - and without
  # the match there would be a difference.

  print('# V8 correctness - pass')
  return RETURN_PASS


if __name__ == "__main__":
  try:
    result = main()
  except SystemExit:
    # Make sure clusterfuzz reports internal errors and wrong usage.
    # Use one label for all internal and usage errors.
    print(FAILURE_HEADER_TEMPLATE % dict(
        configs='', source_key='', suppression='wrong_usage'))
    result = RETURN_FAIL
  except MemoryError:
    # Running out of memory happens occasionally but is not actionable.
    print('# V8 correctness - pass')
    result = RETURN_PASS
  except Exception as e:
    print(FAILURE_HEADER_TEMPLATE % dict(
        configs='', source_key='', suppression='internal_error'))
    print('# Internal error: %s' % e)
    traceback.print_exc(file=sys.stdout)
    result = RETURN_FAIL

  sys.exit(result)
