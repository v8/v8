# Note: The buildbots evaluate this file with CWD set to the parent
# directory and assume that the root of the checkout is in ./v8/, so
# all paths in here must match this assumption.

use_relative_paths = True

gclient_gn_args_file = 'build/config/gclient_args.gni'
gclient_gn_args = [
  'android_ndk_version',
  'checkout_src_internal',
]

vars = {
  # The version of the NDK. Set here, to allow the autoroller to update this
  # value when updating the CIPD hash.
  'android_ndk_version': Str('2@30.0.15729638'),

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

  # Fetch the internal agents repository.
  'checkout_agents_internal': False,

  # CPython 3 CIPD package version for Siso hermetic toolchain.
  'cpython3_version': 'version:3@3.11.9.chromium.38',

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
  'gn_version': 'git_revision:cfcd774b98f3433e18b722f9a7ff06119825b8eb',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:3@1.12.1.chromium.4',

  # siso CIPD package version
  'siso_version': 'git_revision:d38036b64e81c5fdb5d25a92081e6f98a020163d',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Fuchsia sdk
  # and whatever else without interference from each other.
  'fuchsia_version': 'version:33.20260910.5.1',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling partition_alloc_version
  # and whatever else without interference from each other.
  'partition_alloc_version': '59c8c4f88fe20470be030c106f361d719127d068',

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
  'jetstream_3.0-custom_revision': 'ffde597c189c9c781986a6247a5844b1c04e4a8a',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling agents-internal
  # and whatever else without interference from each other.
  'agents_internal_revision': 'fbd26bb4531eaaecfc8cb80a6eab3c85a6e672dd',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling agents-public
  # and whatever else without interference from each other.
  'agents_public_revision': 'b61aca5681312269c2a2bb46ceb6b6f65fdd94dd',
}

deps = {
  'agents/shared': {
    'url': Var('chromium_url') + '/chromium/agents.git' + '@' + Var('agents_public_revision'),
  },
  'agents/internal': {
    'url': Var('chrome_internal_url') + '/chrome/agents-internal.git' + '@' + Var('agents_internal_revision'),
    'condition': 'checkout_agents_internal',
  },
  'build':
    Var('chromium_url') + '/chromium/src/build.git' + '@' + 'e6aa79b579ec7536ac9cbb92b47c1d3c2ecec984',
  'buildtools':
    Var('chromium_url') + '/chromium/src/buildtools.git' + '@' + 'c202b4a9dac30e789ed6e3b2354efa94357a56f3',
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
    'url': Var('chrome_internal_url') + '/v8/v8-perf.git' + '@' + 'HEAD',
    'condition': 'checkout_v8_perf',
  },
  'test/mozilla/data':
    Var('chromium_url') + '/v8/deps/third_party/mozilla-tests.git' + '@' + 'f6c578a10ea707b1a8ab0b88943fe5115ce2b9be',
  'test/test262/data':
    Var('chromium_url') + '/external/github.com/tc39/test262.git' + '@' + '72faf8ec1445c55149615e8b35187830783aba1a',
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
        'version': 'version:' + Var('android_ndk_version'),
      },
    ],
    'condition': 'checkout_android',
    'dep_type': 'cipd',
  },
  'third_party/catapult': {
    'url': Var('chromium_url') + '/catapult.git' + '@' + '471aa8aec31a4de79a96b72815180bc9dc71c420',
    'condition': 'checkout_android',
  },
  'third_party/clang-format/script':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/clang/tools/clang-format.git' + '@' + '70510081984cfcdb14a15b3e08dfe9776dc7ed37',
  'third_party/colorama/src': {
    'url': Var('chromium_url') + '/external/colorama.git' + '@' + '3de9f013df4b470069d03d250224062e8cf15c49',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features': {
    'url': Var('chromium_url') + '/chromium/src/third_party/cpu_features.git' + '@' + '4974e2063946d9dd2a417c27db139fd982ff447a',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features/src': {
    'url': Var('chromium_url') + '/external/github.com/google/cpu_features.git' + '@' + '81d13c49649f0714dd41fb56bb246398b6584085',
    'condition': 'checkout_android',
  },
  'third_party/depot_tools':
    Var('chromium_url') + '/chromium/tools/depot_tools.git' + '@' + '36a8df4ad006eaa0572fb446edb8145fe5403592',
  'third_party/dragonbox/src':
    Var('chromium_url') + '/external/github.com/jk-jeon/dragonbox.git' + '@' + 'beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44',
  'third_party/fp16/src':
    Var('chromium_url') + '/external/github.com/Maratyszcza/FP16.git' + '@' + '782eea126dc5c755827be751a099eb01826175cf',
  'third_party/fast_float/src':
    Var('chromium_url') + '/external/github.com/fastfloat/fast_float.git' + '@' + '34164f547b7df3f5d794ff67e9f885c36819ebfc',
  'third_party/fuchsia-gn-sdk': {
    'url': Var('chromium_url') + '/chromium/src/third_party/fuchsia-gn-sdk.git' + '@' + '019c2f57c0f022f63d4d56b4e6427d4f4ea91daa',
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
    'url': Var('chromium_url') + '/chromium/src/third_party/google_benchmark.git' + '@' + 'c3b654389bf74fac1f2d926d0439506f06c66751',
  },
  'third_party/google_benchmark_chrome/src': {
    'url': Var('chromium_url') + '/external/github.com/google/benchmark.git' + '@' + '8abf1e701fbd88c8170f48fe0558247e2e5f8e7d',
  },
  'third_party/fuzztest':
    Var('chromium_url') + '/chromium/src/third_party/fuzztest.git' + '@' + 'f0f143db954fb311d4e7ba39972d2dab9ba47e2a',
  'third_party/fuzztest/src':
    Var('chromium_url') + '/external/github.com/google/fuzztest.git' + '@' + '846bda6f291e26b7f8fb33c8de23796129bffd12',
  'third_party/googletest/src':
    Var('chromium_url') + '/external/github.com/google/googletest.git' + '@' + '4fe3307fb2d9f86d19777c7eb0e4809e9694dde7',
  'third_party/highway/src':
    Var('chromium_url') + '/external/github.com/google/highway.git' + '@' + '2607d3b5b0113992fe84d3848859eae13b3b52c1',
  'third_party/icu':
    Var('chromium_url') + '/chromium/deps/icu.git' + '@' + '6ebb40c594776cc2c21ea14df85a2a89a328b364',
  'third_party/instrumented_libs': {
    'url': Var('chromium_url') + '/chromium/third_party/instrumented_libraries.git' + '@' + 'd15c278eed5d38d9acf2d8054cf37baba93cef8e',
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
    Var('chromium_url') + '/external/github.com/open-source-parsers/jsoncpp.git'+ '@' + '3347a4b86bb914cb565f0cbda19c06e109513300',
  'third_party/libc++/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxx.git' + '@' + '97b436da4c33663581d394f4ee0a5977fc38c2f4',
  'third_party/libc++abi/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxxabi.git' + '@' + '14024f8f89421ba73a3d28c9b8f9fbd745d92dff',
  'third_party/libpfm4':
    Var('chromium_url') + '/chromium/src/third_party/libpfm4.git' + '@' + 'c93f7afce14329a1eecd3bb0cb5b22d84d5e6a10',
  'third_party/libpfm4/src':
    Var('chromium_url') + '/external/git.code.sf.net/p/perfmon2/libpfm4.git' + '@' + '6870a9f00412830ceaa7e4384bb92ee323e2a28f',
  'third_party/libunwind/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libunwind.git' + '@' + '24a407d5353ad38f948f107c7d6bc2bcbb4bca24',
  'third_party/llvm-libc/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libc.git' + '@' + '91b3328166da1db7db15cb49c714d59198e56cab',
  'third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/clang-android-runtime-library-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '9f3076dcb74c53198ccc5cb2db52d548f8c8edce382e4b499f66d5024f34fb11',
        'size_bytes': 6203280,
        'generation': 1789127816319422,
        'condition': 'checkout_android',
      },
      {
        'object_name': 'Linux_x64/clang-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '83f4a4ea3292bfc236d85575fa7af27d39180d88534bc3117b73ad1df34bb959',
        'size_bytes': 57243988,
        'generation': 1789127809113314,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Linux_x64/clang-tidy-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'a3a717113cbcdfdbdce6f8f178efe9dbd4352b69b28858cafe9dd12144b4d8b4',
        'size_bytes': 14947636,
        'generation': 1789127808973657,
        'condition': 'host_os == "linux" and checkout_clang_tidy',
      },
      {
        'object_name': 'Linux_x64/clangd-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'fe21bb9e363dad5cbf3c0e6eff3e38f14eb45b2c61a2642d8ed405f04fc8da1a',
        'size_bytes': 15091424,
        'generation': 1789127808995417,
        'condition': 'host_os == "linux" and checkout_clangd',
      },
      {
        'object_name': 'Linux_x64/llvm-code-coverage-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '8d4396305ff89df80d64ae5073c9aa0c62e587a17411e3fd900b4f2f502d8e17',
        'size_bytes': 2371104,
        'generation': 1789127809277865,
        'condition': 'host_os == "linux" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Linux_x64/llvmobjdump-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '4747ad1ae530a94d3d9ae1de3a0533bd82e4afaf588905500bcb65ad3626344e',
        'size_bytes': 5919816,
        'generation': 1789127809398773,
        'condition': '(checkout_linux or checkout_mac or checkout_android) and host_os == "linux"',
      },
      {
        'object_name': 'Mac/clang-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '5b893eb0dc07cb151fee5a19b40f159dd697ba21ca99d0f59e60a80e99acf278',
        'size_bytes': 56729348,
        'generation': 1789127818277683,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac/clang-mac-runtime-library-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'dd5c1da410296c96792bae2c3b79127ae1e53a853521e241088a229fa9a20b3b',
        'size_bytes': 981676,
        'generation': 1789127825117200,
        'condition': 'checkout_mac and not host_os == "mac"',
      },
      {
        'object_name': 'Mac/clang-tidy-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'd6ddd8ae0c92e46466964936812695cf9020b55dae38a96ba9ab73a49b54af3d',
        'size_bytes': 14927288,
        'generation': 1789127817821775,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac/clangd-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'fc78380523c4a1712e38dd54b2fa2e1c5d30af1abd14deaba44b9f7d11c6b5cc',
        'size_bytes': 16813268,
        'generation': 1789127817785803,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clangd',
      },
      {
        'object_name': 'Mac/llvm-code-coverage-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '69ef58d4655f5df294b3526aef7384568c2a169eeb80ec415aa7265ab4154058',
        'size_bytes': 2419748,
        'generation': 1789127818078978,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac/llvmobjdump-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '839275989dcef1f956ade1e49e93d78e378e57bc272bbdad3694ba28a3d83d9f',
        'size_bytes': 5902912,
        'generation': 1789127818020417,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/clang-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'b1d5d53f8dba60834fc3287ff53276fbed4273eee6a8f31a1ff7825f91db77c1',
        'size_bytes': 47539924,
        'generation': 1789127826521173,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Mac_arm64/clang-tidy-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '26ed86e035feb6a3085969aec14b6d8f58fa7db1630aa4315e907c85cdce688f',
        'size_bytes': 12996816,
        'generation': 1789127826637982,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac_arm64/clangd-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'dbea6965c94b6afeec10e07843a10011e37bcd0d380552740425ead52e6260ba',
        'size_bytes': 13314464,
        'generation': 1789127826581533,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clangd',
      },
      {
        'object_name': 'Mac_arm64/llvm-code-coverage-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '0280149cffa39f09823e21a2a5e3fc53b1d0e83e043647429d69f1547f42db31',
        'size_bytes': 2043396,
        'generation': 1789127826942850,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac_arm64/llvmobjdump-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'f2ea6f562955bbaca69358e83a194ee967886d36f8b5cf18fbae79d99450c4bd',
        'size_bytes': 5641596,
        'generation': 1789127826597258,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/clang-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '57e6aaa0edf5b34a8ac51603112ef346cb11059ccf3e3c91553abb9a7544f619',
        'size_bytes': 51840184,
        'generation': 1789127835497655,
        'condition': 'host_os == "win"',
      },
      {
        'object_name': 'Win/clang-tidy-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '8e2f3fa72b5c63c711083e9c528c9679842864c92e557b866b064364dbdbd95f',
        'size_bytes': 15132748,
        'generation': 1789127835458038,
        'condition': 'host_os == "win" and checkout_clang_tidy',
      },
      {
        'object_name': 'Win/clang-win-runtime-library-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '9f09d592579f262cfd4b57fe5cd9e17b071ec8f80fc9d6bb26c90612e476bdef',
        'size_bytes': 2645936,
        'generation': 1789127842749891,
        'condition': 'checkout_win and not host_os == "win"',
      },
      {
        'object_name': 'Win/clangd-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '05936ceb04719b8f117113dfd0914d863f9187bf7bf1fb2ddb6cda13aac15f4e',
        'size_bytes': 15431860,
        'generation': 1789127835571711,
       'condition': 'host_os == "win" and checkout_clangd',
      },
      {
        'object_name': 'Win/llvm-code-coverage-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': '9968f504eed6e4cb4b9d46d05ef742448107532c4f3811c79ff26fd20defd995',
        'size_bytes': 2523680,
        'generation': 1789127835702447,
        'condition': 'host_os == "win" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/llvmobjdump-llvmorg-24-init-7747-g62397f8b-27.tar.xz',
        'sha256sum': 'bac327770909f1a87080a3d82b54ad3857c9f037e77a2078aadcdbc294f18862',
        'size_bytes': 6005628,
        'generation': 1789127835514996,
        'condition': '(checkout_linux or checkout_mac or checkout_android) and host_os == "win"',
      },
    ],
  },
  'third_party/logdog/logdog':
    Var('chromium_url') + '/infra/luci/luci-py/client/libs/logdog' + '@' + '6d8ea2699c6e78b344b2fc4956322aa2c463af10',
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
    Var('chromium_url') + '/external/github.com/google/perfetto.git' + '@' + 'd3b1841f171c17161cb4faa1489052a09d8545d9',
  'third_party/protobuf':
    Var('chromium_url') + '/chromium/src/third_party/protobuf.git' + '@' + '398c57e93a383373546bc6ae5ac2052b6155f2ba',
  'third_party/re2/src':
    Var('chromium_url') + '/external/github.com/google/re2.git' + '@' + '972a15cedd008d846f1a39b2e88ce48d7f166cbd',
  'third_party/requests': {
      'url': Var('chromium_url') + '/external/github.com/kennethreitz/requests.git' + '@' + 'c7e0fc087ceeadb8b4c84a0953a422c474093d6d',
      'condition': 'checkout_android',
  },
  'tools/rust':
    Var('chromium_url') + '/chromium/src/tools/rust' + '@' + 'f4f5985e80629c586b1ba1874b6f75c8d9d830e4',
  'tools/win':
    Var('chromium_url') + '/chromium/src/tools/win' + '@' + '13cb6e5d223dc49eadd082d3aef4c2a5b0e4c0a0',
  'third_party/rust':
    Var('chromium_url') + '/chromium/src/third_party/rust' + '@' + '5d1eef11ee101253a9ba316fff0517f758ec12fd',
  'third_party/rust-toolchain': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/rust-toolchain-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': '793d75e2aade21579a3bf3be0df82fa569d66b5a4ad8c2e8eef64310e6e8202c',
        'size_bytes': 276475500,
        'generation': 1788957439577062,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-toolchain-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': 'cf72eab96c51d9340662fde2d71e1aa47f9e699edd109dba584403858099c7ee',
        'size_bytes': 264214704,
        'generation': 1788957443362069,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-toolchain-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': '35435e060b8ab5c3d53eee81ca2ac36116c176f81b357acf425ccb53d4dfebd9',
        'size_bytes': 248298456,
        'generation': 1788957446965952,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-toolchain-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': 'e596b726b6304a55c999db3559cc8248090d99fdbc43c7e871aad59fc21b8977',
        'size_bytes': 416524300,
        'generation': 1788957450588277,
        'condition': 'host_os == "win"',
      },
    ],
  },
  # libclang + Python bindings, for the tools/metagen harvest. Built from
  # the same LLVM revision as the clang and rust packages, so it stays in
  # step with the toolchain metagen borrows its resource-dir from; bump
  # this pin along with the clang/rust roll.
  #
  # The GCS object is named rust-libclang-*, but the entry uses the name
  # Chromium gives it (src/third_party/llvm-libclang, maintained there by
  # tools/clang/scripts/sync_deps.py) so //third_party/llvm-libclang
  # resolves in both trees.
  'third_party/llvm-libclang': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/rust-libclang-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': 'bddbd5de33e5289703e61a65d51de735fd6b589a81c8db47f98b3f7894600577',
        'size_bytes': 22893388,
        'generation': 1788957441556465,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-libclang-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': 'bbefd02baf8ceed4550cb7b64a0e9302fdf36dd8941c26eee8c17e54c0a9d35a',
        'size_bytes': 23302388,
        'generation': 1788957445103908,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-libclang-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': 'e25987c48c069da46e7f5d9af3e8f2cd72da36e5f0ab4c40bf1eb890ee83f5de',
        'size_bytes': 20693412,
        'generation': 1788957448711031,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-libclang-1edd55dcfcd573872c727fa3e086369a71661ee0-1-llvmorg-24-init-7747-g62397f8b.tar.xz',
        'sha256sum': '9078271f819db566faa3a5aeae51a61d052ffe8dbc7575f42a81df7659271b10',
        'size_bytes': 21660416,
        'generation': 1788957452493230,
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
  # Always download Linux x64 package regardless of host OS for RBE workers.
  'third_party/cpython3/linux-amd64': {
    'packages': [
      {
        'package': 'infra/3pp/tools/cpython3/linux-amd64',
        'version': Var('cpython3_version'),
      }
    ],
    'condition': 'not build_with_chromium and host_cpu != "s390x" and host_os != "zos" and host_cpu != "ppc64"',
    'dep_type': 'cipd',
  },
  # Host platform package.
  'third_party/cpython3/host': {
    'packages': [
      {
        'package': 'infra/3pp/tools/cpython3/${{platform}}',
        'version': Var('cpython3_version'),
      }
    ],
    'condition': 'not build_with_chromium and host_cpu != "s390x" and host_os != "zos" and host_cpu != "ppc64"',
    'dep_type': 'cipd',
  },
  'third_party/zlib':
    Var('chromium_url') + '/chromium/src/third_party/zlib.git'+ '@' + '285e94b8fa95ad3b7d16b80798ec8dce6febb8c8',
  'tools/clang':
    Var('chromium_url') + '/chromium/src/tools/clang.git' + '@' + 'cd850a16101606c8af1452ab6b505d3072409d74',
  'tools/protoc_wrapper':
    Var('chromium_url') + '/chromium/src/tools/protoc_wrapper.git' + '@' + 'e9dbe1bf6a2a5d2d4973725874259eed587cf18d',
  'third_party/abseil-cpp': {
    'url': Var('chromium_url') + '/chromium/src/third_party/abseil-cpp.git' + '@' + '2d78d7ce09549caca58fc28b442bbad3a19da432',
    'condition': 'not build_with_chromium',
  },
  'third_party/fadec/src': {
    'url': Var('chromium_url') + '/external/github.com/aengelke/fadec.git' + '@' + 'c9f78f532b9004de278489019ab2c6c28ae9746e',
  },
  'third_party/disarm/src': {
    'url': Var('chromium_url') + '/external/github.com/aengelke/disarm.git' + '@' + '2d960dae4b4fb29dba05cfc48210fbed423f3d2c',
  },
  'third_party/zoslib': {
    'url': Var('chromium_url') + '/external/github.com/ibmruntimes/zoslib.git' + '@' + '804b13554e8dd6972a591c1d6e532514fadd42a8',
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
  '+third_party/v8/codegen',
  '+third_party/vtune',
  '+hwy/highway.h',
  '+simdutf.h',
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
                '-vpython-spec', 'vpython.toml',
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
