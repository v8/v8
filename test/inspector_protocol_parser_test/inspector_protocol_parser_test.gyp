# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{ 'variables': {
    'protocol_path': '../../third_party/WebKit/Source/platform/inspector_protocol',
  },
  'targets': [
    { 'target_name': 'inspector_protocol_parser_test',
      'type': 'executable',
      'dependencies': [
        '../../src/inspector/inspector.gyp:inspector_protocol',
        '../../testing/gmock.gyp:gmock',
        '../../testing/gtest.gyp:gtest',
      ],
      'include_dirs+': [
        '../..',
        '<(protocol_path)/../..',
      ],
      'defines': [
        'V8_INSPECTOR_USE_STL',
      ],
      'sources': [
        '<(protocol_path)/ParserTest.cpp',
        'RunTests.cpp',
      ]
    },
  ],
  'conditions': [
    ['test_isolation_mode != "noop"', {
      'targets': [
        {
          'target_name': 'inspector_protocol_parser_test_run',
          'type': 'none',
          'dependencies': [
            'inspector_protocol_parser_test',
          ],
          'includes': [
            '../../gypfiles/features.gypi',
            '../../gypfiles/isolate.gypi',
          ],
          'sources': [
            'inspector_protocol_parser_test.isolate',
          ],
        },
      ],
    }],
  ],
}
