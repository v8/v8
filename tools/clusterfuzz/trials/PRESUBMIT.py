# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json

try:
  basestring       # Python 2
except NameError:  # Python 3
  basestring = str


def _CheckTrialsConfig(input_api, output_api):
  def FilterFile(affected_file):
    return input_api.FilterSourceFile(
        affected_file,
        files_to_check=(r'.+clusterfuzz_trials_config\.json',))

  results = []
  for f in input_api.AffectedFiles(
      file_filter=FilterFile, include_deletes=False):
    with open(f.AbsoluteLocalPath()) as j:
      try:
        trials = json.load(j)
        for trial in trials:
          if not all (k in trial for k in ('app_args', 'app_name', 'probability')):
            results.append('trial %s is not configured correctly' % trial)
          if trial['app_name'] != 'd8':
            results.append('trial %s has an incorrect app_name' % trial)
          if not isinstance(trial['probability'], float):
            results.append('trial %s probability is not a float' % trial)
          if not (0 <= trial['probability'] <= 1):
            results.append('trial %s has invalid probability value' % trial)
          if not isinstance(trial['app_args'], basestring) or not trial['app_args']:
            results.append('trial %s should have a non-empty string for app_args' % trial)
      except Exception as e:
        results.append(
            'JSON validation failed for %s. Error:\n%s' % (f.LocalPath(), e))

  return [output_api.PresubmitError(r) for r in results]

def _CommonChecks(input_api, output_api):
  """Checks common to both upload and commit."""
  checks = [
    _CheckTrialsConfig,
  ]

  return sum([check(input_api, output_api) for check in checks], [])

def CheckChangeOnCommit(input_api, output_api):
  return _CommonChecks(input_api, output_api)

def CheckChangeOnUpload(input_api, output_api):
  return _CommonChecks(input_api, output_api)
