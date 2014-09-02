# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'v8_code': 1,
  },
  'includes': ['../../build/toolchain.gypi', '../../build/features.gypi'],
  'targets': [
    {
      'target_name': 'compiler-unittests',
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
        'change-lowering-unittest.cc',
        'common-operator-unittest.cc',
        'compiler-unittests.cc',
        'compiler-unittests.h',
        'graph-unittest.cc',
        'graph-unittest.h',
        'instruction-selector-unittest.cc',
        'instruction-selector-unittest.h',
        'machine-operator-reducer-unittest.cc',
        'machine-operator-unittest.cc',
        'simplified-operator-reducer-unittest.cc',
      ],
      'conditions': [
        ['v8_target_arch=="arm"', {
          'sources': [  ### gcmole(arch:arm) ###
            'arm/instruction-selector-arm-unittest.cc',
          ],
        }],
        ['v8_target_arch=="arm64"', {
          'sources': [  ### gcmole(arch:arm64) ###
            'arm64/instruction-selector-arm64-unittest.cc',
          ],
        }],
        ['v8_target_arch=="ia32"', {
          'sources': [  ### gcmole(arch:ia32) ###
            'ia32/instruction-selector-ia32-unittest.cc',
          ],
        }],
        ['v8_target_arch=="x64"', {
          'sources': [  ### gcmole(arch:x64) ###
            'x64/instruction-selector-x64-unittest.cc',
          ],
        }],
        ['component=="shared_library"', {
          # compiler-unittests can't be built against a shared library, so we
          # need to depend on the underlying static target in that case.
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
        ['os_posix == 1', {
          # TODO(svenpanne): This is a temporary work-around to fix the warnings
          # that show up because we use -std=gnu++0x instead of -std=c++11.
          'cflags!': [
            '-pedantic',
          ],
        }],
      ],
    },
  ],
}
