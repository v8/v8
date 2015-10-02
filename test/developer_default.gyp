# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'conditions': [
    ['test_isolation_mode != "noop"', {
      'targets': [
        {
          'target_name': 'developer_default_run',
          'type': 'none',
          'dependencies': [
            'default.gyp:default_run',
            'unittests/unittests.gyp:unittests_run',
          ],
          'includes': [
            '../build/isolate.gypi',
          ],
          'sources': [
            'developer_default.isolate',
          ],
        },
      ],
    }],
  ],
}
