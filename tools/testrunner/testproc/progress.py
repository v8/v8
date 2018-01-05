# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys

from . import base


def print_failure_header(test):
  if test.output_proc.negative:
    negative_marker = '[negative] '
  else:
    negative_marker = ''
  print "=== %(label)s %(negative)s===" % {
    'label': test,
    'negative': negative_marker,
  }


class ResultsTracker(base.TestProcObserver):
  def __init__(self, count_subtests):
    super(ResultsTracker, self).__init__()
    self.failed = 0
    self.remaining = 0
    self.total = 0
    self.count_subtests = count_subtests

  def _on_next_test(self, test):
    self.total += 1
    self.remaining += 1
    # TODO(majeski): If count_subtests is set get number of subtests from the
    # next proc.

  def _on_result_for(self, test, result, is_last):
    if not is_last and not self.count_subtests:
      return

    self.remaining -= 1
    if result.has_unexpected_output:
      self.failed += 1


class ProgressIndicator(base.TestProcObserver):
  def starting(self):
    pass

  def finished(self):
    pass


class SimpleProgressIndicator(ProgressIndicator):
  def __init__(self):
    super(SimpleProgressIndicator, self).__init__()

    self._failed = []
    self._total = 0

  def _on_next_test(self, test):
    # TODO(majeski): Collect information about subtests, e.g. for each test
    # we create multiple variants.
    self._total += 1

  def _on_result_for(self, test, result, is_last):
    if result.has_unexpected_output:
      self._failed.append((test, result.output))

  def starting(self):
    print 'Running %i tests' % self._total

  def finished(self):
    crashed = 0
    print
    for test, output in self._failed:
      print_failure_header(test)
      if output.stderr:
        print "--- stderr ---"
        print output.stderr.strip()
      if output.stdout:
        print "--- stdout ---"
        print output.stdout.strip()
      print "Command: %s" % test.cmd.to_string()
      if output.HasCrashed():
        print "exit code: %d" % output.exit_code
        print "--- CRASHED ---"
        crashed += 1
      if output.HasTimedOut():
        print "--- TIMEOUT ---"
    if len(self._failed) == 0:
      print "==="
      print "=== All tests succeeded"
      print "==="
    else:
      print
      print "==="
      print "=== %i tests failed" % len(self._failed)
      if crashed > 0:
        print "=== %i tests CRASHED" % crashed
      print "==="


class VerboseProgressIndicator(SimpleProgressIndicator):
  def _on_result_for(self, test, result, is_last):
    super(VerboseProgressIndicator, self)._on_result_for(test, result, is_last)
    if result.has_unexpected_output:
      if result.output.HasCrashed():
        outcome = 'CRASH'
      else:
        outcome = 'FAIL'
    else:
      outcome = 'pass'
    print 'Done running %s: %s' % (test, outcome)
    sys.stdout.flush()


class JsonTestProgressIndicator(ProgressIndicator):
  def __init__(self, json_test_results, arch, mode, random_seed):
    super(JsonTestProgressIndicator, self).__init__()
    self.json_test_results = json_test_results
    self.arch = arch
    self.mode = mode
    self.random_seed = random_seed
    self.results = []
    self.tests = []

  def _on_result_for(self, test, result, is_last):
    output = result.output
    # Buffer all tests for sorting the durations in the end.
    self.tests.append((test, output.duration))

    # TODO(majeski): Previously we included reruns here. If we still want this
    # json progress indicator should be placed just before execution.
    if not result.has_unexpected_output:
      # Omit tests that run as expected.
      return

    self.results.append({
      "name": str(test),
      "flags": test.cmd.args,
      "command": test.cmd.to_string(relative=True),
      "run": -100, # TODO(majeski): do we need this?
      "stdout": output.stdout,
      "stderr": output.stderr,
      "exit_code": output.exit_code,
      "result": test.output_proc.get_outcome(output),
      "expected": test.expected_outcomes,
      "duration": output.duration,

      # TODO(machenbach): This stores only the global random seed from the
      # context and not possible overrides when using random-seed stress.
      "random_seed": self.random_seed,
      "target_name": test.get_shell(),
      "variant": test.variant,
    })

  def finished(self):
    complete_results = []
    if os.path.exists(self.json_test_results):
      with open(self.json_test_results, "r") as f:
        # Buildbot might start out with an empty file.
        complete_results = json.loads(f.read() or "[]")

    duration_mean = None
    if self.tests:
      # Get duration mean.
      duration_mean = (
          sum(duration for (_, duration) in self.tests) /
          float(len(self.tests)))

    # Sort tests by duration.
    self.tests.sort(key=lambda (_, duration): duration, reverse=True)
    slowest_tests = [
      {
        "name": str(test),
        "flags": test.cmd.args,
        "command": test.cmd.to_string(relative=True),
        "duration": duration,
        "marked_slow": test.is_slow,
      } for (test, duration) in self.tests[:20]
    ]

    complete_results.append({
      "arch": self.arch,
      "mode": self.mode,
      "results": self.results,
      "slowest_tests": slowest_tests,
      "duration_mean": duration_mean,
      "test_total": len(self.tests),
    })

    with open(self.json_test_results, "w") as f:
      f.write(json.dumps(complete_results))
