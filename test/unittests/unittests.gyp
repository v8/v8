# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': ['../../build/toolchain.gypi', '../../build/features.gypi'],
  'targets': [
    {
      'target_name': 'unittests',
      'type': 'executable',
      'dependencies': [
        '../../testing/gmock.gyp:gmock',
        '../../testing/gtest.gyp:gtest',
        '../../tools/gyp/v8.gyp:v8_libplatform',
      ],
      'include_dirs': [
        '../..',
      ],
      'sources': [  ### gcmole(all) ###
        'compiler/test-instruction-selector.cc',
        'compiler/test-instruction-selector.h',
        'test-isolate.cc',
        'test-isolate.h',
        'test-zone.cc',
        'test-zone.h',
        'unittests.cc',
        'unittests.h',
      ],
      'conditions': [
        ['v8_target_arch=="arm"', {
          'sources': [  ### gcmole(arch:arm) ###
            'compiler/arm/test-instruction-selector-arm.cc',
          ]
        }],
        ['component=="shared_library"', {
          # unittests can't be built against a shared library, so we need to
          # depend on the underlying static target in that case.
          'conditions': [
            ['v8_use_snapshot=="true"', {
              'dependencies': ['../../tools/gyp/v8.gyp:v8_snapshot'],
            },
            {
              'dependencies': [
                '../../tools/gyp/v8.gyp:v8_nosnapshot',
              ],
            }],
          ],
        }, {
          'dependencies': ['../../tools/gyp/v8.gyp:v8'],
        }],
      ],
    },
  ],
}
