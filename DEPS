# Note: The buildbots evaluate this file with CWD set to the parent
# directory and assume that the root of the checkout is in ./v8/, so
# all paths in here must match this assumption.

use_relative_paths = True

gclient_gn_args_file = 'build/config/gclient_args.gni'
gclient_gn_args = [
]

vars = {
  # Fetches only the SDK boot images which match at least one of the whitelist
  # entries in a comma-separated list.
  #
  # Available images:
  #   Emulation:
  #   - terminal.qemu-x64
  #   - terminal.qemu-arm64
  #   - workstation.qemu-x64-release
  #   Hardware:
  #   - minimal.x64
  #   - core.x64-dfv2
  #
  # Since the images are hundreds of MB, default to only downloading the image
  # most commonly useful for developers. Bots and developers that need to use
  # other images (e.g., qemu.arm64) can override this with additional images.
  'checkout_fuchsia_boot_images': "terminal.x64",
  'checkout_fuchsia_product_bundles': '"{checkout_fuchsia_boot_images}" != ""',

  'checkout_centipede_deps': False,
  'checkout_instrumented_libraries': False,
  'checkout_ittapi': False,

  # Fetch the prebuilt binaries for llvm-cov and llvm-profdata. Needed to
  # process the raw profiles produced by instrumented targets (built with
  # the gn arg 'use_clang_coverage').
  'checkout_clang_coverage_tools': False,

  # Fetch clang-tidy into the same bin/ directory as our clang binary.
  'checkout_clang_tidy': False,

  # Fetch clangd into the same bin/ directory as our clang binary.
  'checkout_clangd': False,

  # Fetch and build V8 builtins with PGO profiles
  'checkout_v8_builtins_pgo_profiles': False,

  'android_url': 'https://android.googlesource.com',
  'boringssl_url': 'https://boringssl.googlesource.com',
  'chromium_url': 'https://chromium.googlesource.com',
  'download_gcmole': False,
  'download_jsfunfuzz': False,
  'download_prebuilt_bazel': False,
  'check_v8_header_includes': False,

  # By default, download the fuchsia sdk from the public sdk directory.
  'fuchsia_sdk_cipd_prefix': 'fuchsia/sdk/core/',

  # Used for downloading the Fuchsia SDK without running hooks.
  'checkout_fuchsia_no_hooks': False,

  # reclient CIPD package version
  'reclient_version': 're_client_version:0.158.0.ddc2270e-gomaip',

  # Fetch configuration files required for the 'use_remoteexec' gn arg
  'download_remoteexec_cfg': False,

  # RBE instance to use for running remote builds
  'rbe_instance': Str('projects/rbe-chrome-untrusted/instances/default_instance'),

  # RBE project to download rewrapper config files for. Only needed if
  # different from the project used in 'rbe_instance'
  'rewrapper_cfg_project': Str(''),

  # This variable is overrided in Chromium's DEPS file.
  'build_with_chromium': False,

  # GN CIPD package version.
  'gn_version': 'git_revision:9e0c7b7cefb270ce9d340777e4379c9fed9ea9df',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:2@1.12.1.chromium.4',

  # siso CIPD package version
  'siso_version': 'git_revision:250f32bbb04e51117d17db783c9535dd956f7d44',

  # luci-go CIPD package version.
  'luci_go': 'git_revision:5845c6112f584c023a47776b31c3b6f08f562fb8',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Fuchsia sdk
  # and whatever else without interference from each other.
  'fuchsia_version': 'version:23.20240823.0.1',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_build-tools_version
  # and whatever else without interference from each other.
  'android_sdk_build-tools_version': 'DxwAZ3hD551Neu6ycuW5CPnXFrdleRBd93oX1eB_m9YC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_emulator_version
  # and whatever else without interference from each other.
  'android_sdk_emulator_version': '9lGp8nTUCRRWGMnI_96HcKfzjnxEJKUcfvfwmA3wXNkC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_extras_version
  # and whatever else without interference from each other.
  'android_sdk_extras_version': 'bY55nDqO6FAm6FkGIj09sh2KW9oqAkCGKjYok5nUvBMC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_patcher_version
  # and whatever else without interference from each other.
  'android_sdk_patcher_version': 'I6FNMhrXlpB-E1lOhMlvld7xt9lBVNOO83KIluXDyA0C',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_platform-tools_version
  # and whatever else without interference from each other.
  'android_sdk_platform-tools_version': 'WihaseZR6cojZbkzIqwGhpTp92ztaGfqq8njBU8eTXYC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_platforms_version
  # and whatever else without interference from each other.
  'android_sdk_platforms_version': 'kIXA-9XuCfOESodXEdOBkW5f1ytrGWdbp3HFp1I8A_0C',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_sources_version
  # and whatever else without interference from each other.
  'android_sdk_sources_version': 'qfTSF99e29-w3eIVPpfcif0Em5etyvxuicTDTntWHQMC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_tools-lint_version
  # and whatever else without interference from each other.
  'android_sdk_cmdline-tools_version': 'fv6JzkTqfxfIbmsRC8u1b2y0EQO7yQDMDzY3-g0NVu4C',
}

deps = {
  'build':
    Var('chromium_url') + '/chromium/src/build.git' + '@' + '6672ff7714bb19cac39bbf1d130fa5699fca8c47',
  'buildtools':
    Var('chromium_url') + '/chromium/src/buildtools.git' + '@' + '0932737f617c4e23f4ae4f530c48d887bca4f1b6',
  'buildtools/linux64': {
    'packages': [
      {
        'package': 'gn/gn/linux-${{arch}}',
        'version': Var('gn_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'host_os == "linux" and host_cpu != "s390" and host_os != "zos" and host_cpu != "ppc"',
  },
  'buildtools/mac': {
    'packages': [
      {
        'package': 'gn/gn/mac-${{arch}}',
        'version': Var('gn_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'host_os == "mac"',
  },
  'buildtools/win': {
    'packages': [
      {
        'package': 'gn/gn/windows-amd64',
        'version': Var('gn_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'host_os == "win"',
  },
  'buildtools/reclient': {
    'packages': [
      {
        'package': 'infra/rbe/client/${{platform}}',
        'version': Var('reclient_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': '(host_os == "linux" or host_os == "mac" or host_os == "win") and host_cpu != "s390" and host_os != "zos" and host_cpu != "ppc" and (host_cpu != "arm64" or host_os == "mac")',
  },
  'test/benchmarks/data':
    Var('chromium_url') + '/v8/deps/third_party/benchmarks.git' + '@' + '05d7188267b4560491ff9155c5ee13e207ecd65f',
  'test/mozilla/data':
    Var('chromium_url') + '/v8/deps/third_party/mozilla-tests.git' + '@' + 'f6c578a10ea707b1a8ab0b88943fe5115ce2b9be',
  'test/test262/data':
    Var('chromium_url') + '/external/github.com/tc39/test262.git' + '@' + '941813e1f0e64ae9a5c4c5c8075f49ff89b0c642',
  'third_party/android_platform': {
    'url': Var('chromium_url') + '/chromium/src/third_party/android_platform.git' + '@' + '6337c445f9963ec3914e7e0c5787941d07b46509',
    'condition': 'checkout_android',
  },
  'third_party/android_sdk/public': {
      'packages': [
          {
              'package': 'chromium/third_party/android_sdk/public/build-tools/35.0.0',
              'version': Var('android_sdk_build-tools_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/emulator',
              'version': Var('android_sdk_emulator_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/extras',
              'version': Var('android_sdk_extras_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/patcher',
              'version': Var('android_sdk_patcher_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/platform-tools',
              'version': Var('android_sdk_platform-tools_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/platforms/android-35',
              'version': Var('android_sdk_platforms_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/sources/android-30',
              'version': Var('android_sdk_sources_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/cmdline-tools',
              'version': Var('android_sdk_cmdline-tools_version'),
          },
      ],
      'condition': 'checkout_android',
      'dep_type': 'cipd',
  },
  'third_party/android_toolchain/ndk': {
    'packages': [
      {
        'package': 'chromium/third_party/android_toolchain/android_toolchain',
        'version': 'Idl-vYnWGnM8K3XJhM3h6zjYVDXlnljVz3FE00V9IM8C',
      },
    ],
    'condition': 'checkout_android',
    'dep_type': 'cipd',
  },
  'third_party/boringssl': {
    'url': Var('chromium_url') + '/chromium/src/third_party/boringssl.git' + '@' + '3b818f4d644a181593e120cdb0b6e85fa5a0ea44',
    'condition': "checkout_centipede_deps",
  },
  'third_party/boringssl/src': {
    'url': Var('boringssl_url') + '/boringssl.git' + '@' +  'f1b043c28352a4e79114324ca2e86df33922e843',
    'condition': "checkout_centipede_deps",
  },
  'third_party/catapult': {
    'url': Var('chromium_url') + '/catapult.git' + '@' + '4917becfcf533ef31071fde42fa13ec417c71930',
    'condition': 'checkout_android',
  },
  'third_party/clang-format/script':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/clang/tools/clang-format.git' + '@' + '3c0acd2d4e73dd911309d9e970ba09d58bf23a62',
  'third_party/colorama/src': {
    'url': Var('chromium_url') + '/external/colorama.git' + '@' + '3de9f013df4b470069d03d250224062e8cf15c49',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features/src': {
    'url': Var('chromium_url') + '/external/github.com/google/cpu_features.git' + '@' + '936b9ab5515dead115606559502e3864958f7f6e',
    'condition': 'checkout_android',
  },
  'third_party/depot_tools':
    Var('chromium_url') + '/chromium/tools/depot_tools.git' + '@' + 'f5e10923392588205925c036948e111f72b80271',
  'third_party/fp16/src':
    Var('chromium_url') + '/external/github.com/Maratyszcza/FP16.git' + '@' + '0a92994d729ff76a58f692d3028ca1b64b145d91',
  'third_party/fast_float/src':
    Var('chromium_url') + '/external/github.com/fastfloat/fast_float.git' + '@' + '3e57d8dcfb0a04b5a8a26b486b54490a2e9b310f',
  'third_party/fuchsia-gn-sdk': {
    'url': Var('chromium_url') + '/chromium/src/third_party/fuchsia-gn-sdk.git' + '@' + '0ea3c0308526d07eae3a6277b442b8ea6185a264',
    'condition': 'checkout_fuchsia',
  },
  # Exists for rolling the Fuchsia SDK. Check out of the SDK should always
  # rely on the hook running |update_sdk.py| script below.
  'third_party/fuchsia-sdk/sdk': {
      'packages': [
          {
              'package': Var('fuchsia_sdk_cipd_prefix') + '${{platform}}',
              'version': Var('fuchsia_version'),
          },
      ],
      'condition': 'checkout_fuchsia_no_hooks',
      'dep_type': 'cipd',
  },
  'third_party/google_benchmark_chrome': {
    'url': Var('chromium_url') + '/chromium/src/third_party/google_benchmark.git' + '@' + 'f049b96d7a50ae19f2748aae7fba7bde705bcd8c',
  },
  'third_party/google_benchmark_chrome/src': {
    'url': Var('chromium_url') + '/external/github.com/google/benchmark.git' + '@' + '344117638c8ff7e239044fd0fa7085839fc03021',
  },
  'third_party/fuzztest':
    Var('chromium_url') + '/chromium/src/third_party/fuzztest.git' + '@' + '69fe98bf87d80fdc773481ae5180c63e431a13a1',
  'third_party/fuzztest/src':
    Var('chromium_url') + '/external/github.com/google/fuzztest.git' + '@' + '32eb84a95951fa3a0148fb3e6a1a02f830ded136',
  'third_party/googletest/src':
    Var('chromium_url') + '/external/github.com/google/googletest.git' + '@' + 'ff233bdd4cac0a0bf6e5cd45bda3406814cb2796',
  'third_party/highway/src':
    Var('chromium_url') + '/external/github.com/google/highway.git' + '@' + '8295336dd70f1201d42c22ab5b0861de38cf8fbf',
  'third_party/icu':
    Var('chromium_url') + '/chromium/deps/icu.git' + '@' + '9408c6fd4a39e6fef0e1c4077602e1c83b15f3fb',
  'third_party/instrumented_libs': {
    'url': Var('chromium_url') + '/chromium/third_party/instrumented_libraries.git' + '@' + 'bb6dbcf2df7a9beb34c3773ef4df161800e3aed9',
    'condition': 'checkout_instrumented_libraries',
  },
  'third_party/ittapi': {
    # Force checkout ittapi libraries to pass v8 header includes check on
    # bots that has check_v8_header_includes enabled.
    'url': Var('chromium_url') + '/external/github.com/intel/ittapi' + '@' + 'a3911fff01a775023a06af8754f9ec1e5977dd97',
    'condition': "checkout_ittapi or check_v8_header_includes",
  },
  'third_party/jinja2':
    Var('chromium_url') + '/chromium/src/third_party/jinja2.git' + '@' + '2f6f2ff5e4c1d727377f5e1b9e1903d871f41e74',
  'third_party/jsoncpp/source':
    Var('chromium_url') + '/external/github.com/open-source-parsers/jsoncpp.git'+ '@' + '42e892d96e47b1f6e29844cc705e148ec4856448',
  'third_party/libc++/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxx.git' + '@' + '0f818c09a7fd9a6bac4947cdca35912b5666c4eb',
  'third_party/libc++abi/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxxabi.git' + '@' + 'a834cb253992175f4a896c5fb1352ecd91abf11e',
  'third_party/libunwind/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libunwind.git' + '@' + 'dc70138c3e68e2f946585f134e20815851e26263',
  'third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/clang-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '32ac9d9864a6bd99242f1a97778b3a074ac1151ce3eca369903f2ef5337c787a',
        'size_bytes': 52250752,
        'generation': 1723267014378582,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Linux_x64/clang-tidy-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '5fd3fb73ceef73593fa09f8228283aec9f7798b648bf450e87f071a097be213b',
        'size_bytes': 13217676,
        'generation': 1723267014627839,
        'condition': 'host_os == "linux" and checkout_clang_tidy',
      },
      {
        'object_name': 'Linux_x64/clangd-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '6e1fe97f8b7131591233d8a2df1ff289ffb878c3fc6834c978a86273f7c00b6b',
        'size_bytes': 26125984,
        'generation': 1723267014767012,
        'condition': 'host_os == "linux" and checkout_clangd',
      },
      {
        'object_name': 'Linux_x64/llvm-code-coverage-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'ef317481472926d3e2a82e2d2a02cde78685002b4c9923df476108906022d792',
        'size_bytes': 2374748,
        'generation': 1723267015213805,
        'condition': 'host_os == "linux" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Linux_x64/llvmobjdump-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '02be68f7c7c7bf679e1abff2745306b8385275017c89b2b13f638a941785f8c5',
        'size_bytes': 5386480,
        'generation': 1723267014930087,
        'condition': '(checkout_linux or checkout_mac or checkout_android and host_os != "mac")',
      },
      {
        'object_name': 'Mac/clang-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '5df8a609a7d2511343fbc20af1de3ed1682c3703fc074f21af1bf8bc2f58e491',
        'size_bytes': 47200408,
        'generation': 1723267016534642,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac/clang-mac-runtime-library-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'ffc72ff3fca85f31340c164aab480bd2babfaa6219ff12e93b81f0056309da55',
        'size_bytes': 869616,
        'generation': 1723267034708598,
        'condition': 'checkout_mac and not host_os == "mac"',
      },
      {
        'object_name': 'Mac/clang-tidy-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'd02b9a39491d2ff3a291778de87b2a72f5885d01a8093518cb5612b97353ac2a',
        'size_bytes': 12805336,
        'generation': 1723267016822831,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac/clangd-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'ce09141be75350f0f932fcda14d8b906d2869674d79ef5a10a3e60a9a8d3ccee',
        'size_bytes': 26372428,
        'generation': 1723267016957114,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clangd',
      },
      {
        'object_name': 'Mac/llvm-code-coverage-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'c0ac62ff01f1ce6e5d30134cb0f83fd8eabf858dfb33d07209a6b40d8f1ae789',
        'size_bytes': 2248664,
        'generation': 1723267017743181,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac_arm64/clang-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'f4a384ecdaa051ba4786c9e6c46f9101a751b1a5c5ad4bf0d217c4ba71e0ff30',
        'size_bytes': 42737720,
        'generation': 1723267036349494,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Mac_arm64/clang-tidy-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '2769378fd2891af945f8d29b5eaf05b4ab0450b2d086539df1c78c684e64be14',
        'size_bytes': 11740656,
        'generation': 1723267036269250,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac_arm64/clangd-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'a35dfb99903a487e06d685712e461125978c76ba8eaa99e9f5742e63d3e67444',
        'size_bytes': 23470088,
        'generation': 1723267036383208,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clangd',
      },
      {
        'object_name': 'Mac_arm64/llvm-code-coverage-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '574a0d7c560aae964d8bdcd85f0145077b1324e79eee4a3dd1636ab7aefc59e5',
        'size_bytes': 2010540,
        'generation': 1723267036758678,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/clang-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'e255af29c29a741cf39c3000b612466ff805a99766d26ac86ec2afcb4ca0c922',
        'size_bytes': 44019080,
        'generation': 1723267056892790,
        'condition': 'host_os == "win"',
      },
      {
        'object_name': 'Win/clang-tidy-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'a86aa579fc90a053b94874a8c79daeb7f3bbd77107fb460c6d0a7959cefb7e61',
        'size_bytes': 13055812,
        'generation': 1723267057185720,
        'condition': 'host_os == "win" and checkout_clang_tidy',
      },
      {
        'object_name': 'Win/clang-win-runtime-library-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '6d89f358769ef50d008194e0ab9e8d4d80b8d6ffc0095ed44aef925d900aa743',
        'size_bytes': 2873772,
        'generation': 1723267074433695,
        'condition': 'checkout_win and not host_os == "win"',
      },
      {
        'object_name': 'Win/clangd-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'e2b69a726f794005a333ae66a0ef5c0258872a19bc4506eff23f23fdee75ba5c',
        'size_bytes': 25053884,
        'generation': 1723267057351794,
       'condition': 'host_os == "win" and checkout_clangd',
      },
      {
        'object_name': 'Win/llvm-code-coverage-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': 'e68e7c7ecbc7b2fc4f7ec3e97565a7f12bab1d195d22bc76959f3a88b1462ac1',
        'size_bytes': 2376020,
        'generation': 1723267057803475,
        'condition': 'host_os == "win" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/llvmobjdump-llvmorg-20-init-1009-g7088a5ed-10.tar.xz',
        'sha256sum': '801714415847b8efea7252b1072b8647f92ba0e946480b3db9b156900e42ab55',
        'size_bytes': 5392812,
        'generation': 1723267057506056,
        'condition': 'checkout_linux or checkout_mac or checkout_android and host_os == "win"',
      },
    ],
  },
  'third_party/logdog/logdog':
    Var('chromium_url') + '/infra/luci/luci-py/client/libs/logdog' + '@' + '0b2078a90f7a638d576b3a7c407d136f2fb62399',
  'third_party/markupsafe':
    Var('chromium_url') + '/chromium/src/third_party/markupsafe.git' + '@' + '6638e9b0a79afc2ff7edd9e84b518fe7d5d5fea9',
  'third_party/ninja': {
    'packages': [
      {
        'package': 'infra/3pp/tools/ninja/${{platform}}',
        'version': Var('ninja_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'host_cpu != "s390" and host_os != "zos" and host_cpu != "ppc"'
  },
  'third_party/perfetto':
    Var('android_url') + '/platform/external/perfetto.git' + '@' + '6fc824d618d2f06b5d9cd8655ba0419b6b3b366e',
  'third_party/protobuf':
    Var('chromium_url') + '/chromium/src/third_party/protobuf.git' + '@' + 'da2fe725b80ac0ba646fbf77d0ce5b4ac236f823',
  'third_party/re2/src':
    Var('chromium_url') + '/external/github.com/google/re2.git' + '@' + '6dcd83d60f7944926bfd308cc13979fc53dd69ca',
  'third_party/requests': {
      'url': Var('chromium_url') + '/external/github.com/kennethreitz/requests.git' + '@' + 'c7e0fc087ceeadb8b4c84a0953a422c474093d6d',
      'condition': 'checkout_android',
  },
  'third_party/siso': {
    'packages': [
      {
        'package': 'infra/build/siso/${{platform}}',
        'version': Var('siso_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'not build_with_chromium and host_cpu != "s390" and host_os != "zos" and host_cpu != "ppc"',
  },
  'third_party/zlib':
    Var('chromium_url') + '/chromium/src/third_party/zlib.git'+ '@' + 'd3aea2341cdeaf7e717bc257a59aa7a9407d318a',
  'tools/clang':
    Var('chromium_url') + '/chromium/src/tools/clang.git' + '@' + 'f7fd8eedc41c89d3667f38e9e2fbf87b2b73c0a8',
  'tools/luci-go': {
      'packages': [
        {
          'package': 'infra/tools/luci/isolate/${{platform}}',
          'version': Var('luci_go'),
        },
        {
          'package': 'infra/tools/luci/swarming/${{platform}}',
          'version': Var('luci_go'),
        },
      ],
      'condition': 'host_cpu != "s390" and host_os != "zos" and host_os != "aix"',
      'dep_type': 'cipd',
  },
  'tools/protoc_wrapper':
    Var('chromium_url') + '/chromium/src/tools/protoc_wrapper.git' + '@' + 'dbcbea90c20ae1ece442d8ef64e61c7b10e2b013',
  'third_party/abseil-cpp': {
    'url': Var('chromium_url') + '/chromium/src/third_party/abseil-cpp.git' + '@' + '1b7ed5a1932647009b72fdad8e0e834d55cf40d8',
    'condition': 'not build_with_chromium',
  },
  'third_party/zoslib': {
    'url': Var('chromium_url') + '/external/github.com/ibmruntimes/zoslib.git' + '@' + '1e68de6e37efced3738a88536fccb6bbfe2d70b2',
    'condition': 'host_os == "zos"',
  }
}

include_rules = [
  # Everybody can use some things.
  '+include',
  '+unicode',
  '+third_party/fdlibm',
  '+third_party/ittapi/include',
  '+third_party/fast_float/src/include',
  '+third_party/fp16/src/include',
  '+third_party/v8/codegen',
  '+third_party/fuzztest',
  # Abseil features are allow-listed. Please use your best judgement when adding
  # to this set -- if in doubt, email v8-dev@. For general guidance, refer to
  # the Chromium guidelines (though note that some requirements in V8 may be
  # different to Chromium's):
  # https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++11.md
  '+absl/container/flat_hash_map.h',
  '+absl/container/flat_hash_set.h',
  '+absl/container/btree_map.h',
  '+absl/types/optional.h',
  '+absl/types/variant.h',
  '+absl/status',
  # Some abseil features are explicitly banned.
  '-absl/types/any.h', # Requires RTTI.
  '-absl/types/flags', # Requires RTTI.
]

# checkdeps.py shouldn't check for includes in these directories:
skip_child_includes = [
  'build',
  'third_party',
]

hooks = [
  {
    # Ensure that the DEPS'd "depot_tools" has its self-update capability
    # disabled.
    'name': 'disable_depot_tools_selfupdate',
    'pattern': '.',
    'action': [
        'python3',
        'third_party/depot_tools/update_depot_tools_toggle.py',
        '--disable',
    ],
  },
  {
    # This clobbers when necessary (based on get_landmines.py). It must be the
    # first hook so that other things that get/generate into the output
    # directory will not subsequently be clobbered.
    'name': 'landmines',
    'pattern': '.',
    'action': [
        'python3',
        'build/landmines.py',
        '--landmine-scripts',
        'tools/get_landmines.py',
    ],
  },
  {
    'name': 'bazel',
    'pattern': '.',
    'condition': 'download_prebuilt_bazel',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--bucket', 'chromium-v8-prebuilt-bazel/linux',
                '--no_resume',
                '-s', 'tools/bazel/bazel.sha1',
                '--platform=linux*',
    ],
  },
  # Pull dsymutil binaries using checked-in hashes.
  {
    'name': 'dsymutil_mac_arm64',
    'pattern': '.',
    'condition': 'host_os == "mac" and host_cpu == "arm64"',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-browser-clang',
                '-s', 'tools/clang/dsymutil/bin/dsymutil.arm64.sha1',
                '-o', 'tools/clang/dsymutil/bin/dsymutil',
    ],
  },
  {
    'name': 'dsymutil_mac_x64',
    'pattern': '.',
    'condition': 'host_os == "mac" and host_cpu == "x64"',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-browser-clang',
                '-s', 'tools/clang/dsymutil/bin/dsymutil.x64.sha1',
                '-o', 'tools/clang/dsymutil/bin/dsymutil',
    ],
  },
  {
    'name': 'gcmole',
    'pattern': '.',
    'condition': 'download_gcmole',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--bucket', 'chrome-v8-gcmole',
                '-u', '--no_resume',
                '-s', 'tools/gcmole/gcmole-tools.tar.gz.sha1',
                '--platform=linux*',
    ],
  },
  {
    'name': 'jsfunfuzz',
    'pattern': '.',
    'condition': 'download_jsfunfuzz',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--bucket', 'chrome-v8-jsfunfuzz',
                '-u', '--no_resume',
                '-s', 'tools/jsfunfuzz/jsfunfuzz.tar.gz.sha1',
                '--platform=linux*',
    ],
  },
  {
    'name': 'wasm_spec_tests',
    'pattern': '.',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '-u',
                '--bucket', 'v8-wasm-spec-tests',
                '-s', 'test/wasm-spec-tests/tests.tar.gz.sha1',
    ],
  },
  {
    'name': 'wasm_js',
    'pattern': '.',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '-u',
                '--bucket', 'v8-wasm-spec-tests',
                '-s', 'test/wasm-js/tests.tar.gz.sha1',
    ],
  },
  {
    # Case-insensitivity for the Win SDK. Must run before win_toolchain below.
    'name': 'ciopfs_linux',
    'pattern': '.',
    'condition': 'checkout_win and host_os == "linux"',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-browser-clang/ciopfs',
                '-s', 'build/ciopfs.sha1',
    ]
  },
  {
    # Update the Windows toolchain if necessary.
    'name': 'win_toolchain',
    'pattern': '.',
    'condition': 'checkout_win',
    'action': ['python3', 'build/vs_toolchain.py', 'update', '--force'],
  },
  {
    # Update the Mac toolchain if necessary.
    'name': 'mac_toolchain',
    'pattern': '.',
    'condition': 'checkout_mac',
    'action': ['python3', 'build/mac_toolchain.py'],
  },
  {
    # Update LASTCHANGE.
    'name': 'lastchange',
    'pattern': '.',
    'action': ['python3', 'build/util/lastchange.py',
               '-o', 'build/util/LASTCHANGE'],
  },
  {
    'name': 'Download Fuchsia SDK from GCS',
    'pattern': '.',
    'condition': 'checkout_fuchsia',
    'action': [
      'python3',
      'build/fuchsia/update_sdk.py',
      '--cipd-prefix={fuchsia_sdk_cipd_prefix}',
      '--version={fuchsia_version}',
    ],
  },
  {
    'name': 'Download Fuchsia system images',
    'pattern': '.',
    'condition': 'checkout_fuchsia and checkout_fuchsia_product_bundles',
    'action': [
      'python3',
      'build/fuchsia/update_product_bundles.py',
      '{checkout_fuchsia_boot_images}',
    ],
  },
  {
    # Mac does not have llvm-objdump, download it for cross builds in Fuchsia.
    'name': 'llvm-objdump',
    'pattern': '.',
    'condition': 'host_os == "mac" and checkout_fuchsia',
    'action': ['python3', 'tools/clang/scripts/update.py',
               '--package=objdump'],
  },
  {
    'name': 'vpython3_common',
    'pattern': '.',
    'action': [ 'vpython3',
                '-vpython-spec', '.vpython3',
                '-vpython-tool', 'install',
    ],
  },
  {
    'name': 'check_v8_header_includes',
    'pattern': '.',
    'condition': 'check_v8_header_includes',
    'action': [
      'python3',
      'tools/generate-header-include-checks.py',
    ],
  },
  {
    'name': 'checkout_v8_builtins_pgo_profiles',
    'pattern': '.',
    'condition': 'checkout_v8_builtins_pgo_profiles',
    'action': [
      'python3',
      'tools/builtins-pgo/download_profiles.py',
      'download',
      '--quiet',
    ],
  },
  {
    # Clean up build dirs for crbug.com/1337238.
    # After a libc++ roll and revert, .ninja_deps would get into a state
    # that breaks Ninja on Windows.
    # TODO(crbug.com/1337238): Remove in a month or so.
    'name': 'del_ninja_deps_cache',
    'pattern': '.',
    'condition': 'host_os == "win"',
    'action': ['python3', 'build/del_ninja_deps_cache.py'],
  },
  # Configure remote exec cfg files
  {
    'name': 'download_and_configure_reclient_cfgs',
    'pattern': '.',
    'condition': 'download_remoteexec_cfg and not build_with_chromium',
    'action': ['python3',
               'buildtools/reclient_cfgs/configure_reclient_cfgs.py',
               '--rbe_instance',
               Var('rbe_instance'),
               '--reproxy_cfg_template',
               'reproxy.cfg.template',
               '--rewrapper_cfg_project',
               Var('rewrapper_cfg_project'),
               '--quiet',
               ],
  },
  {
    'name': 'configure_reclient_cfgs',
    'pattern': '.',
    'condition': 'not download_remoteexec_cfg and not build_with_chromium',
    'action': ['python3',
               'buildtools/reclient_cfgs/configure_reclient_cfgs.py',
               '--rbe_instance',
               Var('rbe_instance'),
               '--reproxy_cfg_template',
               'reproxy.cfg.template',
               '--rewrapper_cfg_project',
               Var('rewrapper_cfg_project'),
               '--skip_remoteexec_cfg_fetch',
               ],
  },
  # Configure Siso for developer builds.
  {
    'name': 'configure_siso',
    'pattern': '.',
    'condition': 'not build_with_chromium',
    'action': ['python3',
               'build/config/siso/configure_siso.py',
               '--rbe_instance',
               Var('rbe_instance'),
               ],
  },
]

recursedeps = [
  'build',
  'buildtools',
  'third_party/instrumented_libs',
]
