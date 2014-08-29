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
      'target_name': 'base',
      'type': 'static_library',
      'variables': {
        'optimize': 'max',
      },
      'include_dirs+': [
        '../..',
      ],
      'sources': [
        'atomicops.h',
        'atomicops_internals_arm64_gcc.h',
        'atomicops_internals_arm_gcc.h',
        'atomicops_internals_atomicword_compat.h',
        'atomicops_internals_mac.h',
        'atomicops_internals_mips_gcc.h',
        'atomicops_internals_tsan.h',
        'atomicops_internals_x86_gcc.cc',
        'atomicops_internals_x86_gcc.h',
        'atomicops_internals_x86_msvc.h',
        'bits.h',
        'build_config.h',
        'cpu.cc',
        'cpu.h',
        'flags.h',
        'lazy-instance.h',
        'logging.cc',
        'logging.h',
        'macros.h',
        'once.cc',
        'once.h',
        'platform/elapsed-timer.h',
        'platform/time.cc',
        'platform/time.h',
        'platform/condition-variable.cc',
        'platform/condition-variable.h',
        'platform/mutex.cc',
        'platform/mutex.h',
        'platform/platform.h',
        'platform/semaphore.cc',
        'platform/semaphore.h',
        'safe_conversions.h',
        'safe_conversions_impl.h',
        'safe_math.h',
        'safe_math_impl.h',
        'sys-info.cc',
        'sys-info.h',
        'utils/random-number-generator.cc',
        'utils/random-number-generator.h',
      ],
      'conditions': [
        ['want_separate_host_toolset==1', {
          'toolsets': ['host', 'target'],
        }, {
          'toolsets': ['target'],
        }],
        ['OS=="linux"', {
            'link_settings': {
              'libraries': [
                '-lrt'
              ]
            },
            'sources': [
              'platform/platform-linux.cc',
              'platform/platform-posix.cc'
            ],
          }
        ],
        ['OS=="android"', {
            'sources': [
              'platform/platform-posix.cc'
            ],
            'conditions': [
              ['host_os=="mac"', {
                'target_conditions': [
                  ['_toolset=="host"', {
                    'sources': [
                      'platform/platform-macos.cc'
                    ]
                  }, {
                    'sources': [
                      'platform/platform-linux.cc'
                    ]
                  }],
                ],
              }, {
                # TODO(bmeurer): What we really want here, is this:
                #
                # 'link_settings': {
                #   'target_conditions': [
                #     ['_toolset=="host"', {
                #       'libraries': [
                #         '-lrt'
                #       ]
                #     }]
                #   ]
                # },
                #
                # but we can't do this right now, as the AOSP does not support
                # linking against the host librt, so we need to work around this
                # for now, using the following hack (see platform/time.cc):
                'target_conditions': [
                  ['_toolset=="host"', {
                    'defines': [
                      'V8_LIBRT_NOT_AVAILABLE=1',
                    ],
                  }],
                ],
                'sources': [
                  'platform/platform-linux.cc'
                ]
              }],
            ],
          },
        ],
        ['OS=="qnx"', {
            'link_settings': {
              'target_conditions': [
                ['_toolset=="host" and host_os=="linux"', {
                  'libraries': [
                    '-lrt'
                  ],
                }],
                ['_toolset=="target"', {
                  'libraries': [
                    '-lbacktrace'
                  ],
                }],
              ],
            },
            'sources': [
              'platform/platform-posix.cc',
              'qnx-math.h',
            ],
            'target_conditions': [
              ['_toolset=="host" and host_os=="linux"', {
                'sources': [
                  'platform/platform-linux.cc'
                ],
              }],
              ['_toolset=="host" and host_os=="mac"', {
                'sources': [
                  'platform/platform-macos.cc'
                ],
              }],
              ['_toolset=="target"', {
                'sources': [
                  'platform/platform-qnx.cc'
                ],
              }],
            ],
          },
        ],
        ['OS=="freebsd"', {
            'link_settings': {
              'libraries': [
                '-L/usr/local/lib -lexecinfo',
            ]},
            'sources': [
              'platform/platform-freebsd.cc',
              'platform/platform-posix.cc'
            ],
          }
        ],
        ['OS=="openbsd"', {
            'link_settings': {
              'libraries': [
                '-L/usr/local/lib -lexecinfo',
            ]},
            'sources': [
              'platform/platform-openbsd.cc',
              'platform/platform-posix.cc'
            ],
          }
        ],
        ['OS=="netbsd"', {
            'link_settings': {
              'libraries': [
                '-L/usr/pkg/lib -Wl,-R/usr/pkg/lib -lexecinfo',
            ]},
            'sources': [
              'platform/platform-openbsd.cc',
              'platform/platform-posix.cc'
            ],
          }
        ],
        ['OS=="solaris"', {
            'link_settings': {
              'libraries': [
                '-lnsl',
            ]},
            'sources': [
              'platform/platform-solaris.cc',
              'platform/platform-posix.cc'
            ],
          }
        ],
        ['OS=="mac"', {
          'sources': [
            'platform/platform-macos.cc',
            'platform/platform-posix.cc'
          ]},
        ],
        ['OS=="win"', {
          'defines': [
            '_CRT_RAND_S'  # for rand_s()
          ],
          'variables': {
            'gyp_generators': '<!(echo $GYP_GENERATORS)',
          },
          'conditions': [
            ['gyp_generators=="make"', {
              'variables': {
                'build_env': '<!(uname -o)',
              },
              'conditions': [
                ['build_env=="Cygwin"', {
                  'sources': [
                    'platform/platform-cygwin.cc',
                    'platform/platform-posix.cc'
                  ],
                }, {
                  'sources': [
                    'platform/platform-win32.cc',
                    'win32-headers.h',
                    'win32-math.cc',
                    'win32-math.h'
                  ],
                }],
              ],
              'link_settings':  {
                'libraries': [ '-lwinmm', '-lws2_32' ],
              },
            }, {
              'sources': [
                'platform/platform-win32.cc',
                'win32-headers.h',
                'win32-math.cc',
                'win32-math.h'
              ],
              'msvs_disabled_warnings': [4351, 4355, 4800],
              'link_settings':  {
                'libraries': [ '-lwinmm.lib', '-lws2_32.lib' ],
              },
            }],
          ],
        }],
      ],
    },
    {
      'target_name': 'base-unittests',
      'type': 'executable',
      'dependencies': [
        '../../testing/gtest.gyp:gtest',
        '../../testing/gtest.gyp:gtest_main',
        'base',
      ],
      'include_dirs': [
        '../..',
      ],
      'sources': [  ### gcmole(all) ###
        'bits-unittest.cc',
        'cpu-unittest.cc',
        'flags-unittest.cc',
        'platform/condition-variable-unittest.cc',
        'platform/mutex-unittest.cc',
        'platform/platform-unittest.cc',
        'platform/semaphore-unittest.cc',
        'platform/time-unittest.cc',
        'sys-info-unittest.cc',
        'utils/random-number-generator-unittest.cc',
      ],
      'conditions': [
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
