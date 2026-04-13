# Note: The buildbots evaluate this file with CWD set to the parent
# directory and assume that the root of the checkout is in ./v8/, so
# all paths in here must match this assumption.

use_relative_paths = True

gclient_gn_args_file = 'build/config/gclient_args.gni'
gclient_gn_args = [
  'checkout_src_internal',
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

  'checkout_instrumented_libraries': False,
  'checkout_ittapi': False,
  # Checkout extra benchmarks.
  'checkout_benchmarks': False,

  # Fetch the internal v8 perf repository for additional benchmarks.
  'checkout_v8_perf': False,

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
  'chromium_url': 'https://chromium.googlesource.com',
  'chrome_internal_url': 'https://chrome-internal.googlesource.com',
  'download_gcmole': False,
  'download_jsfunfuzz': False,
  'download_prebuilt_bazel': False,
  'download_prebuilt_arm64_llvm_symbolizer': False,
  'check_v8_header_includes': False,

  # By default, download the fuchsia sdk from the public sdk directory.
  'fuchsia_sdk_cipd_prefix': 'fuchsia/sdk/core/',

  # Used for downloading the Fuchsia SDK without running hooks.
  'checkout_fuchsia_no_hooks': False,

  # V8 doesn't need src_internal, but some shared GN files use this variable.
  'checkout_src_internal': False,

  # reclient CIPD package version
  'reclient_version': 're_client_version:0.185.0.db415f21-gomaip',

  # Fetch configuration files required for the 'use_remoteexec' gn arg
  'download_remoteexec_cfg': False,

  # RBE instance to use for running remote builds
  'rbe_instance': Str('projects/rbe-chrome-untrusted/instances/default_instance'),

  # RBE project to download rewrapper config files for. Only needed if
  # different from the project used in 'rbe_instance'
  'rewrapper_cfg_project': Str(''),

  # This variable is overrided in Chromium's DEPS file.
  'build_with_chromium': False,

  # Repository URL
  'chromium_jetstream_git': 'https://chromium.googlesource.com/external/github.com/WebKit/JetStream.git',

  # GN CIPD package version.
  'gn_version': 'git_revision:ec56d4d935a0e2ab9d52b88dd00c93ec51233055',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:3@1.12.1.chromium.4',

  # siso CIPD package version
  'siso_version': 'git_revision:7bcc56206d234b3024df08ffd4ee6d40eea32623',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Fuchsia sdk
  # and whatever else without interference from each other.
  'fuchsia_version': 'version:31.20260327.5.1',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling partition_alloc_version
  # and whatever else without interference from each other.
  'partition_alloc_version': '7be1a61f68fd36d1c1c31ef87190a83ca1a64e92',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_build-tools_version
  # and whatever else without interference from each other.
  'android_sdk_build-tools_version': 'febJrTgiK9s1ANoUlc4Orn3--zs9GjGCj2vQc8g7OaMC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_emulator_version
  # and whatever else without interference from each other.
  'android_sdk_emulator_version': '9lGp8nTUCRRWGMnI_96HcKfzjnxEJKUcfvfwmA3wXNkC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_platform-tools_version
  # and whatever else without interference from each other.
  'android_sdk_platform-tools_version': 'qTD9QdBlBf3dyHsN1lJ0RH6AhHxR42Hmg2Ih-Vj4zIEC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_platforms_version
  # and whatever else without interference from each other.
  'android_sdk_platforms_version': 'WhtP32Q46ZHdTmgCgdauM3ws_H9iPoGKEZ_cPggcQ6wC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_tools-lint_version
  # and whatever else without interference from each other.
  'android_sdk_cmdline-tools_version': 'gekOVsZjseS1w9BXAT3FsoW__ByGDJYS9DgqesiwKYoC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling jetstream_3.0-custom_revision
  # and whatever else without interference from each other.
  'jetstream_3.0-custom_revision': '2145cedef4ca2777b792cb0059d3400ee2a6153c',
}

deps = {
  'build':
    Var('chromium_url') + '/chromium/src/build.git' + '@' + '2316930a88b15a036fa5f72e2b2c2ba08e2904a0',
  'buildtools':
    Var('chromium_url') + '/chromium/src/buildtools.git' + '@' + '11cc2bd83053cb790b7516aa3eb3f3935fb05a0e',
  'buildtools/linux64': {
    'packages': [
      {
        'package': 'gn/gn/linux-${{arch}}',
        'version': Var('gn_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'host_os == "linux" and host_cpu != "s390x" and host_os != "zos" and host_cpu != "ppc64"',
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
    'condition': '(host_os == "linux" or host_os == "mac" or host_os == "win") and host_cpu != "s390x" and host_os != "zos" and host_cpu != "ppc64" and (host_cpu != "arm64" or host_os == "mac")',
  },
  # TODO(498118202): Use checkout_benchmarks here too.
  'test/benchmarks/data':
    Var('chromium_url') + '/v8/deps/third_party/benchmarks.git' + '@' + '05d7188267b4560491ff9155c5ee13e207ecd65f',
  'test/benchmarks/JetStream3': {
    'url': Var('chromium_jetstream_git') + '@' + Var('jetstream_3.0-custom_revision'),
    'condition': 'checkout_benchmarks',
  },
  'third_party/v8-perf': {
    'url': Var('chrome_internal_url') + '/v8/v8-perf.git',
    'condition': 'checkout_v8_perf',
  },
  'test/mozilla/data':
    Var('chromium_url') + '/v8/deps/third_party/mozilla-tests.git' + '@' + 'f6c578a10ea707b1a8ab0b88943fe5115ce2b9be',
  'test/test262/data':
    Var('chromium_url') + '/external/github.com/tc39/test262.git' + '@' + '2b2ecead6e828dd9af13a9ec72065e645724a50f',
  'third_party/android_platform': {
    'url': Var('chromium_url') + '/chromium/src/third_party/android_platform.git' + '@' + 'e3919359f2387399042d31401817db4a02d756ec',
    'condition': 'checkout_android',
  },
  'third_party/android_sdk/public': {
      'packages': [
          {
              'package': 'chromium/third_party/android_sdk/public/build-tools/37.0.0',
              'version': Var('android_sdk_build-tools_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/emulator',
              'version': Var('android_sdk_emulator_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/platform-tools',
              'version': Var('android_sdk_platform-tools_version'),
          },
          {
              'package': 'chromium/third_party/android_sdk/public/platforms/android-37.0',
              'version': Var('android_sdk_platforms_version'),
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
        'version': 'KXOia11cm9lVdUdPlbGLu8sCz6Y4ey_HV2s8_8qeqhgC',
      },
    ],
    'condition': 'checkout_android',
    'dep_type': 'cipd',
  },
  'third_party/catapult': {
    'url': Var('chromium_url') + '/catapult.git' + '@' + '5a34891efa6e41c8aca8842386b8ee528963ffdf',
    'condition': 'checkout_android',
  },
  'third_party/clang-format/script':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/clang/tools/clang-format.git' + '@' + '08cce2b81a4d9801ebeaa8cdb43b241475a4d521',
  'third_party/colorama/src': {
    'url': Var('chromium_url') + '/external/colorama.git' + '@' + '3de9f013df4b470069d03d250224062e8cf15c49',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features/src': {
    'url': Var('chromium_url') + '/external/github.com/google/cpu_features.git' + '@' + '936b9ab5515dead115606559502e3864958f7f6e',
    'condition': 'checkout_android',
  },
  'third_party/depot_tools':
    Var('chromium_url') + '/chromium/tools/depot_tools.git' + '@' + 'bac00505a8c117dd48d465ee853c595981dd5e62',
  'third_party/dragonbox/src':
    Var('chromium_url') + '/external/github.com/jk-jeon/dragonbox.git' + '@' + 'beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44',
  'third_party/fp16/src':
    Var('chromium_url') + '/external/github.com/Maratyszcza/FP16.git' + '@' + '3d2de1816307bac63c16a297e8c4dc501b4076df',
  'third_party/fast_float/src':
    Var('chromium_url') + '/external/github.com/fastfloat/fast_float.git' + '@' + 'cb1d42aaa1e14b09e1452cfdef373d051b8c02a4',
  'third_party/fuchsia-gn-sdk': {
    'url': Var('chromium_url') + '/chromium/src/third_party/fuchsia-gn-sdk.git' + '@' + '1b201add6776a68bcb7b7e01e9d5a1ded7548d83',
    'condition': 'checkout_fuchsia',
  },
  'third_party/simdutf':
    Var('chromium_url') + '/chromium/src/third_party/simdutf' + '@' + 'f7356eed293f8208c40b3c1b344a50bd70971983',
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
    'url': Var('chromium_url') + '/chromium/src/third_party/google_benchmark.git' + '@' + 'abeba5d5e6db5bdf85261045e148f1db3fdc40ad',
  },
  'third_party/google_benchmark_chrome/src': {
    'url': Var('chromium_url') + '/external/github.com/google/benchmark.git' + '@' + '8abf1e701fbd88c8170f48fe0558247e2e5f8e7d',
  },
  'third_party/fuzztest':
    Var('chromium_url') + '/chromium/src/third_party/fuzztest.git' + '@' + '92a22e9802b8b6ff9fe5ae8ea3899dfb73a9651e',
  'third_party/fuzztest/src':
    Var('chromium_url') + '/external/github.com/google/fuzztest.git' + '@' + 'b5ca18f8418a9e1d2d2f0c55c66a1c093f9261eb',
  'third_party/googletest/src':
    Var('chromium_url') + '/external/github.com/google/googletest.git' + '@' + '4fe3307fb2d9f86d19777c7eb0e4809e9694dde7',
  'third_party/highway/src':
    Var('chromium_url') + '/external/github.com/google/highway.git' + '@' + '84379d1c73de9681b54fbe1c035a23c7bd5d272d',
  'third_party/icu':
    Var('chromium_url') + '/chromium/deps/icu.git' + '@' + 'ee5f27adc28bd3f15b2c293f726d14d2e336cbd5',
  'third_party/instrumented_libs': {
    'url': Var('chromium_url') + '/chromium/third_party/instrumented_libraries.git' + '@' + 'e8cb570a9a2ee9128e2214c73417ad2a3c47780b',
    'condition': 'checkout_instrumented_libraries',
  },
  'third_party/ittapi': {
    # Force checkout ittapi libraries to pass v8 header includes check on
    # bots that has check_v8_header_includes enabled.
    'url': Var('chromium_url') + '/external/github.com/intel/ittapi' + '@' + 'a3911fff01a775023a06af8754f9ec1e5977dd97',
    'condition': "checkout_ittapi or check_v8_header_includes",
  },
  'third_party/jinja2':
    Var('chromium_url') + '/chromium/src/third_party/jinja2.git' + '@' + 'c3027d884967773057bf74b957e3fea87e5df4d7',
  'third_party/jsoncpp/source':
    Var('chromium_url') + '/external/github.com/open-source-parsers/jsoncpp.git'+ '@' + '42e892d96e47b1f6e29844cc705e148ec4856448',
  'third_party/libc++/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxx.git' + '@' + '7ab65651aed6802d2599dcb7a73b1f82d5179d05',
  'third_party/libc++abi/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxxabi.git' + '@' + '8f11bb1d4438d0239d0dfc1bd9456a9f31629dda',
  'third_party/libpfm4':
    Var('chromium_url') + '/chromium/src/third_party/libpfm4.git' + '@' + 'd35e15a834ad4e05ba2f0a3c1a2b76fde99a0570',
  'third_party/libpfm4/src':
    Var('chromium_url') + '/external/git.code.sf.net/p/perfmon2/libpfm4.git' + '@' + '977a25bb3dfe45f653a6cee71ffaae9a92fc3095',
  'third_party/libunwind/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libunwind.git' + '@' + '6ca46ff28e3578c57cbead6f233969eb3dabc176',
  'third_party/llvm-libc/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libc.git' + '@' + '95fabdf43412fa079e11842600edb2766b5d2d46',
  'third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/clang-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'bd3a3d7e98cc9037daf24bd7b48f52ba0f0ea5e435c1c707e381034ee4b3c1d6',
        'size_bytes': 68803796,
        'generation': 1775108855393489,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Linux_x64/clang-tidy-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '580dcbef886d28480d0e1dc2b6d4287577fa01d08aadb13993045c4830d38fad',
        'size_bytes': 14398028,
        'generation': 1775108855773342,
        'condition': 'host_os == "linux" and checkout_clang_tidy',
      },
      {
        'object_name': 'Linux_x64/clangd-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'd77743e2b6bd827c080ec67dffce26753c1b0c039ec5736d066ba5aa097736e2',
        'size_bytes': 14639544,
        'generation': 1775108856105229,
        'condition': 'host_os == "linux" and checkout_clangd',
      },
      {
        'object_name': 'Linux_x64/llvm-code-coverage-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '2a1262a4f7893351bd6249b2f10ea2fd62064ad527d0572b41d9c469a2d6e191',
        'size_bytes': 2330560,
        'generation': 1775108856568087,
        'condition': 'host_os == "linux" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Linux_x64/llvmobjdump-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'ee43d3cc82df2affd7f290b0b99a65f22bdb1e6d4369323c35a008df550d4bcc',
        'size_bytes': 5798744,
        'generation': 1775108855922066,
        'condition': '(checkout_linux or checkout_mac or checkout_android) and host_os == "linux"',
      },
      {
        'object_name': 'Mac/clang-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'f600d79485ae1354b22e068149f624a8f53a15f59d48c249d544c2e11ed0eafb',
        'size_bytes': 54827088,
        'generation': 1775108859195599,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac/clang-mac-runtime-library-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '3a173578e5cc6757397d5e0c31cbf15bc35edc7a807e773c4cce82be217b0361',
        'size_bytes': 1012412,
        'generation': 1775108882604681,
        'condition': 'checkout_mac and not host_os == "mac"',
      },
      {
        'object_name': 'Mac/clang-tidy-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '12925a9b71f1a1709ff2a049105a353785a9cbb7bafb70fe226642f1b0ee227c',
        'size_bytes': 14286844,
        'generation': 1775108859499025,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac/clangd-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '50443c1df5e730305712f7ec625a77f8de4db6bf0337700093b540d279d3b95e',
        'size_bytes': 15456916,
        'generation': 1775108860055239,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clangd',
      },
      {
        'object_name': 'Mac/llvm-code-coverage-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'bbd27aabf53ceb2d5bcc456dad817ef9854706774073a7ff2d6d15a3651186a0',
        'size_bytes': 2376820,
        'generation': 1775108861016266,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac/llvmobjdump-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'a000b3e10a7fd9fe3d8e0ea660448362265988e0aebf3e169edccce580b5fcdb',
        'size_bytes': 5718704,
        'generation': 1775108860105704,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/clang-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '84c08af500d1695d2bc378e58bde7e697847597e8e35d0d35f00c603c0c7021b',
        'size_bytes': 45847348,
        'generation': 1775108885241255,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Mac_arm64/clang-tidy-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '9308c7d911f24f1da820b5e6d114cb72be85c8a8077a3b1e8a2b84c893cdebbb',
        'size_bytes': 12441916,
        'generation': 1775108885395968,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac_arm64/clangd-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'b36413059134c1e3cfdad8e2a5bc2d71129ccc472e3c7f36e10c2bf42bfb4144',
        'size_bytes': 12818368,
        'generation': 1775108885742159,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clangd',
      },
      {
        'object_name': 'Mac_arm64/llvm-code-coverage-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '143919e811987b59034b7fb4a1318d756a06bbcc0def2213f6fa0fab16cf98d1',
        'size_bytes': 1989380,
        'generation': 1775108886721636,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac_arm64/llvmobjdump-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '8e48488827c82749bc75e7bcd4209d9d780dfe45fa8cd29f96b3a8dbcd9213b9',
        'size_bytes': 5423800,
        'generation': 1775108886019106,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/clang-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '3de6f77fcf0b2194a1353dfe5048fcbae67f14e9a6ee090c4c432ee60bbd9e80',
        'size_bytes': 49580864,
        'generation': 1775108905186981,
        'condition': 'host_os == "win"',
      },
      {
        'object_name': 'Win/clang-tidy-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '907c8dc44e9a3bd7e8441c8580573a25f4ca412ccfc4eb4aa5c64d4ed53e5066',
        'size_bytes': 14476012,
        'generation': 1775108905754695,
        'condition': 'host_os == "win" and checkout_clang_tidy',
      },
      {
        'object_name': 'Win/clang-win-runtime-library-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '42cc02df5890c075d40a5694d485700bc5d47d8a6df0d601662fef712d8b9f72',
        'size_bytes': 2592964,
        'generation': 1775108927726013,
        'condition': 'checkout_win and not host_os == "win"',
      },
      {
        'object_name': 'Win/clangd-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': 'fc7c297e4c28c7ebe28b520806edf35bd204072bda4660d2cfbe5fd89691ec91',
        'size_bytes': 14863828,
        'generation': 1775108906103106,
       'condition': 'host_os == "win" and checkout_clangd',
      },
      {
        'object_name': 'Win/llvm-code-coverage-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '290a80496d49a5696960a8d0b2bb7737be3de2cf441b3ee24a7c3f0e6657d61d',
        'size_bytes': 2476152,
        'generation': 1775108907002072,
        'condition': 'host_os == "win" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/llvmobjdump-llvmorg-23-init-5669-g8a0be0bc-4.tar.xz',
        'sha256sum': '89a6e9303824ebbc88cd3f8ff5ee38266ba1e247877d9c61cbcbe196098acf38',
        'size_bytes': 5833644,
        'generation': 1775108906300539,
        'condition': '(checkout_linux or checkout_mac or checkout_android) and host_os == "win"',
      },
    ],
  },
  'third_party/logdog/logdog':
    Var('chromium_url') + '/infra/luci/luci-py/client/libs/logdog' + '@' + '62fe96d7fd97a62f21a4665d2e71f69e9eedb04e',
  'third_party/markupsafe':
    Var('chromium_url') + '/chromium/src/third_party/markupsafe.git' + '@' + '4256084ae14175d38a3ff7d739dca83ae49ccec6',
  'third_party/ninja': {
    'packages': [
      {
        'package': 'infra/3pp/tools/ninja/${{platform}}',
        'version': Var('ninja_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'host_cpu != "s390x" and host_os != "zos" and host_cpu != "ppc64"'
  },
  'third_party/partition_alloc': {
    'url': Var('chromium_url') + '/chromium/src/base/allocator/partition_allocator.git@' + Var('partition_alloc_version'),
    'condition': 'not build_with_chromium',
  },
  'third_party/perfetto':
    Var('chromium_url') + '/external/github.com/google/perfetto.git' + '@' + 'd6d4e7f478503496c39188e37ea7ba6ddca46760',
  'third_party/protobuf':
    Var('chromium_url') + '/chromium/src/third_party/protobuf.git' + '@' + '04bb12d7ca5e16c58b55012acb32f07c89dd4e2b',
  'third_party/re2/src':
    Var('chromium_url') + '/external/github.com/google/re2.git' + '@' + '972a15cedd008d846f1a39b2e88ce48d7f166cbd',
  'third_party/requests': {
      'url': Var('chromium_url') + '/external/github.com/kennethreitz/requests.git' + '@' + 'c7e0fc087ceeadb8b4c84a0953a422c474093d6d',
      'condition': 'checkout_android',
  },
  'tools/rust':
    Var('chromium_url') + '/chromium/src/tools/rust' + '@' + '69bb79a132d32d67b6f6820226b02e50f1945202',
  'tools/win':
    Var('chromium_url') + '/chromium/src/tools/win' + '@' + 'bfff5550ec231441c314c64878d860fdb6fdf174',
  'third_party/rust':
    Var('chromium_url') + '/chromium/src/third_party/rust' + '@' + '92a492cb44695d428b3baa7e13fb633170d94cc6',
  'third_party/rust-toolchain': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/rust-toolchain-6f54d591c3116ee7f8ce9321ddeca286810cc142-7-llvmorg-23-init-5669-g8a0be0bc.tar.xz',
        'sha256sum': 'afbb00d27b8f9f65e6a754fb21e80dff084993285cf7f3c0020dece59c5bd67a',
        'size_bytes': 271641712,
        'generation': 1773769777991797,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-toolchain-6f54d591c3116ee7f8ce9321ddeca286810cc142-7-llvmorg-23-init-5669-g8a0be0bc.tar.xz',
        'sha256sum': '70b86e82f1cb55777d40b5828ddcb80afea49510085290424b61251d22e9f959',
        'size_bytes': 259443552,
        'generation': 1773769780408342,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-toolchain-6f54d591c3116ee7f8ce9321ddeca286810cc142-7-llvmorg-23-init-5669-g8a0be0bc.tar.xz',
        'sha256sum': 'e2e19684f31b653ce9238f6303aec22576085528c294757a7157d4ab5e1926dc',
        'size_bytes': 242768940,
        'generation': 1773769782590875,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-toolchain-6f54d591c3116ee7f8ce9321ddeca286810cc142-7-llvmorg-23-init-5669-g8a0be0bc.tar.xz',
        'sha256sum': '37dd250549fed5a9765c3a88e3487409189e0c9c63b691fc77daa0b5f214bced',
        'size_bytes': 409536908,
        'generation': 1773769784773096,
        'condition': 'host_os == "win"',
      },
    ],
  },
  'third_party/siso/cipd': {
    'packages': [
      {
        'package': 'build/siso/${{platform}}',
        'version': Var('siso_version'),
      }
    ],
    'dep_type': 'cipd',
    'condition': 'not build_with_chromium and host_cpu != "s390x" and host_os != "zos" and host_cpu != "ppc64"',
  },
  'third_party/zlib':
    Var('chromium_url') + '/chromium/src/third_party/zlib.git'+ '@' + 'b80f1d1e5256ac25f6aea3f31f13d458981cb1f9',
  'tools/clang':
    Var('chromium_url') + '/chromium/src/tools/clang.git' + '@' + '772ec0f7db794d7c9bc4df748935ff13aaed36d7',
  'tools/protoc_wrapper':
    Var('chromium_url') + '/chromium/src/tools/protoc_wrapper.git' + '@' + '3438d4183bfc7c0d6850e8b970204cc8189f0323',
  'third_party/abseil-cpp': {
    'url': Var('chromium_url') + '/chromium/src/third_party/abseil-cpp.git' + '@' + 'dd4432fb02583b7a5b2b4fbf2012f7053e286ce0',
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
  '+third_party/dragonbox/src/include',
  '+third_party/fast_float/src/include',
  '+third_party/fdlibm',
  '+third_party/fp16/src/include',
  '+third_party/fuzztest',
  '+third_party/ittapi/include',
  '+third_party/simdutf',
  '+third_party/v8/codegen',
  '+third_party/vtune',
  '+hwy/highway.h',
  # Abseil features are allow-listed. Please use your best judgement when adding
  # to this set -- if in doubt, email v8-dev@. For general guidance, refer to
  # the Chromium guidelines (though note that some requirements in V8 may be
  # different to Chromium's):
  # https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++-features.md
  '+absl/container/flat_hash_map.h',
  '+absl/container/flat_hash_set.h',
  '+absl/container/btree_map.h',
  '+absl/functional/overload.h',
  '+absl/numeric/int128.h',
  '+absl/status',
  '+absl/strings/str_format.h',
  '+absl/synchronization/mutex.h',
  '+absl/time/time.h',
  # Some abseil features are explicitly banned.
  '-absl/types/any.h', # Requires RTTI.
  '-absl/types/flags', # Requires RTTI.
  '-absl/functional/function_ref.h', # Use base::FunctionRef
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
    'name': 'llvm_symbolizer',
    'pattern': '.',
    'condition': 'download_prebuilt_arm64_llvm_symbolizer',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--bucket', 'chromium-v8/llvm/arm64',
                '--no_resume',
                '-s', 'tools/sanitizers/linux/arm64/llvm-symbolizer.sha1',
                '--platform=linux*',
    ],
  },
  {
    'name': 'wasm_spec_tests',
    'pattern': '.',
    'action': [ 'python3',
                'third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
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
  'third_party/v8-perf',
]
