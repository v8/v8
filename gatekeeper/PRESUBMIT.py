# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Check that gatekeeper configs are proper json."""

import json
import os

def _CommonChecks(input_api, output_api):
  """Checks common to both upload and commit."""
  results = []
  base_path = input_api.PresubmitLocalPath()
  for file in os.listdir(base_path):
    if not file.endswith('.json'):
      continue
    try:
      with open(os.path.join(base_path, file)) as f:
        json.load(f)
    except Exception as e:
      results.append(output_api.PresubmitError(
          'Failed loading %s: %s' % (file, str(e))))
  return results


def CheckChangeOnUpload(input_api, output_api):
  return _CommonChecks(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return _CommonChecks(input_api, output_api)
