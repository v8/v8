# Copyright 2017 the V8 project authors. All rights reserved.')
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for //v8/include

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into depot_tools.
"""

import re


def _GetTryMasters(project, change):
  return {
    'master.tryserver.chromium.linux': {
      'linux_blink_rel': [],
     },
  }


def GetPreferredTryMasters(project, change):
  # TODO(jochen): Using the value of _GetTryMasters() instead of an empty
  # value here would cause 'git cl try' to include the site isolation trybots,
  # which would be nice. But it has the side effect of replacing, rather than
  # augmenting, the default set of try servers. Re-enable this when we figure
  # out a way to augment the default set.
  return {}


def PostUploadHook(cl, change, output_api):
  """git cl upload will call this hook after the issue is created/modified.

  This hook adds extra try bots to the CL description in order to run layout
  tests in addition to CQ try bots.
  """
  return output_api.EnsureCQIncludeTrybotsAreAdded(
    cl,
    [
      'master.tryserver.chromium.linux:linux_blink_rel'
    ],
    'Automatically added layout test trybots to run tests on CQ.')
