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
  'android_ndk_version': Str('2@30.0.16248370'),

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
  'boringssl_url': 'https://boringssl.googlesource.com',
  'chromium_url': 'https://chromium.googlesource.com',
  'chrome_internal_url': 'https://chrome-internal.googlesource.com',
  'download_gcmole': False,
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
  'chromium_speedometer_git': 'https://chromium.googlesource.com/external/github.com/WebKit/Speedometer.git',

  # GN CIPD package version.
  'gn_version': 'git_revision:e04c7ff10dc0955050fbdbae6b46d856663d7d1f',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:3@1.12.1.chromium.4',

  # siso CIPD package version
  'siso_version': 'git_revision:3025be9c477069426f08b8235e9545d5b20afd0f',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Fuchsia sdk
  # and whatever else without interference from each other.
  'fuchsia_version': 'version:34.20261123.5.1',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling partition_alloc_version
  # and whatever else without interference from each other.
  'partition_alloc_version': 'cd7802bab125c3b5bdfbf2e805abc1110edd2ec9',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_build-tools_version
  # and whatever else without interference from each other.
  'android_sdk_build-tools_version': 'version_37.0.0',
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
  'android_sdk_platforms_version': 'JsUGMsJK2pkQxVKr7EcJbxbwjJEIWjEPLqcbv7ifdmcC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_tools-lint_version
  # and whatever else without interference from each other.
  'android_sdk_cmdline-tools_version': 'gekOVsZjseS1w9BXAT3FsoW__ByGDJYS9DgqesiwKYoC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling jetstream_3.0-custom_revision
  # and whatever else without interference from each other.
  'jetstream_3.0-custom_revision': 'ffde597c189c9c781986a6247a5844b1c04e4a8a',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling agents-common
  # and whatever else without interference from each other.
  'agents_common_revision': '281f7364f86aef21ac620bc0b770d477de7c76a1',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling speedometer_2.1_revision
  # and whatever else without interference from each other.
  'speedometer_2.1_revision': '1386415be8fef2f6b6bbdbe1828872471c5d802a',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling speedometer_3.1_revision
  # and whatever else without interference from each other.
  'speedometer_3.1_revision': '1386415be8fef2f6b6bbdbe1828872471c5d802a',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling speedometer_main_revision
  # and whatever else without interference from each other.
  'speedometer_main_revision': '1386415be8fef2f6b6bbdbe1828872471c5d802a',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling agents-internal
  # and whatever else without interference from each other.
  'agents_internal_revision': '5bbf8f7c3b5cbc477770c7a63071a2ca4937b35b',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling agents-public
  # and whatever else without interference from each other.
  'agents_public_revision': 'f3fecac8a714ae5512abb3a635387c7a01da8666',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling agents-testing
  # and whatever else without interference from each other.
  'agents_testing_revision': 'b1f0d9a76768b01c6b17f0e123768baffd84e24d',
}

deps = {
  'agents/common': {
    'url': Var('chromium_url') + '/chromium/src/agents/common.git' + '@' + Var('agents_common_revision'),
  },
  'agents/shared': {
    'url': Var('chromium_url') + '/chromium/agents.git' + '@' + Var('agents_public_revision'),
  },
  'agents/testing': {
    'url': Var('chromium_url') + '/chromium/src/agents/testing.git' + '@' + Var('agents_testing_revision'),
  },
  'agents/internal': {
    'url': Var('chrome_internal_url') + '/chrome/agents-internal.git' + '@' + Var('agents_internal_revision'),
    'condition': 'checkout_agents_internal',
  },
  'build':
    Var('chromium_url') + '/chromium/src/build.git' + '@' + 'f71636db2fdbb0edce7ffcf76877ee6097de72f1',
  'buildtools':
    Var('chromium_url') + '/chromium/src/buildtools.git' + '@' + '9411a4f3bf18c99b30c1a817b5bc6280a6530a5c',
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
  # TODO(498118202): Use checkout_benchmarks here too.
  'test/benchmarks/data':
    Var('chromium_url') + '/v8/deps/third_party/benchmarks.git' + '@' + '05d7188267b4560491ff9155c5ee13e207ecd65f',
  'test/benchmarks/JetStream3': {
    'url': Var('chromium_jetstream_git') + '@' + Var('jetstream_3.0-custom_revision'),
    'condition': 'checkout_benchmarks',
  },
  'test/benchmarks/Speedometer2': {
    'url': Var('chromium_speedometer_git') + '@' + Var('speedometer_2.1_revision'),
    'condition': 'checkout_benchmarks',
  },
  'test/benchmarks/Speedometer3': {
    'url': Var('chromium_speedometer_git') + '@' + Var('speedometer_3.1_revision'),
    'condition': 'checkout_benchmarks',
  },
  'test/benchmarks/SpeedometerMain': {
    'url': Var('chromium_speedometer_git') + '@' + Var('speedometer_main_revision'),
    'condition': 'checkout_benchmarks',
  },
  'third_party/v8-perf': {
    'url': Var('chrome_internal_url') + '/v8/v8-perf.git' + '@' + 'HEAD',
    'condition': 'checkout_v8_perf',
  },
  'test/mozilla/data':
    Var('chromium_url') + '/v8/deps/third_party/mozilla-tests.git' + '@' + 'f6c578a10ea707b1a8ab0b88943fe5115ce2b9be',
  'test/test262/data':
    Var('chromium_url') + '/external/github.com/tc39/test262.git' + '@' + '2a1ade303f72d3de80565c921d685222405c32ce',
  'third_party/android_platform': {
    'url': Var('chromium_url') + '/chromium/src/third_party/android_platform.git' + '@' + '4cabfaca9bdfe8d1711fe4954a67f435eb3f4d09',
    'condition': 'checkout_android',
  },
  'third_party/android_sdk/public': {
      'packages': [
          {
              'package': 'chromium/third_party/android_sdk/public/build-tools/37.0.0/${{os}}',
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
        'package': 'chromium/third_party/android_toolchain/android_toolchain/${{platform}}',
        'version': 'version:' + Var('android_ndk_version'),
      },
    ],
    'condition': 'checkout_android and '
                 '((host_os == "linux" and host_cpu == "x64") or '
                 '(host_os == "mac" and host_cpu == "arm64"))',
    'dep_type': 'cipd',
  },
  'third_party/catapult': {
    'url': Var('chromium_url') + '/catapult.git' + '@' + 'c293f274909cbd4c7e34b2ac765bc1c880addca9',
    'condition': 'checkout_android',
  },
  'third_party/clang-format/script':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/clang/tools/clang-format.git' + '@' + '65b9a76faeba5f10c1f331ad729b3bae89defbbb',
  'third_party/colorama/src': {
    'url': Var('chromium_url') + '/external/colorama.git' + '@' + '3de9f013df4b470069d03d250224062e8cf15c49',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features': {
    'url': Var('chromium_url') + '/chromium/src/third_party/cpu_features.git' + '@' + '378c196c1dc411059833778ab6b2ab2801f8c9ab',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features/src': {
    'url': Var('chromium_url') + '/external/github.com/google/cpu_features.git' + '@' + '81d13c49649f0714dd41fb56bb246398b6584085',
    'condition': 'checkout_android',
  },
  'third_party/depot_tools':
    Var('chromium_url') + '/chromium/tools/depot_tools.git' + '@' + 'ee90e3f95824aa10ed7a912fe08e16e45eb62004',
  'third_party/dragonbox/src':
    Var('chromium_url') + '/external/github.com/jk-jeon/dragonbox.git' + '@' + 'beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44',
  'third_party/fp16/src':
    Var('chromium_url') + '/external/github.com/Maratyszcza/FP16.git' + '@' + '782eea126dc5c755827be751a099eb01826175cf',
  'third_party/fast_float/src':
    Var('chromium_url') + '/external/github.com/fastfloat/fast_float.git' + '@' + '517e6b8e008341fc4ebf80cc709d8b51610a888e',
  'third_party/fuchsia-gn-sdk': {
    'url': Var('chromium_url') + '/chromium/src/third_party/fuchsia-gn-sdk.git' + '@' + '8488f5b23e737d08f2888cf0bb0d55fe7d313092',
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
    'url': Var('chromium_url') + '/chromium/src/third_party/google_benchmark.git' + '@' + '57f4041d982b6900be1f525cfe909212a14d95c6',
  },
  'third_party/google_benchmark_chrome/src': {
    'url': Var('chromium_url') + '/external/github.com/google/benchmark.git' + '@' + 'a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c',
  },
  'third_party/fuzztest':
    Var('chromium_url') + '/chromium/src/third_party/fuzztest.git' + '@' + 'b794d413a5943fe6c52c0d06e47bbd29b085caff',
  'third_party/fuzztest/src':
    Var('chromium_url') + '/external/github.com/google/fuzztest.git' + '@' + 'b7ef234c594beeb90ef6089fa30d6b3c0c65e82b',
  'third_party/googletest/src':
    Var('chromium_url') + '/external/github.com/google/googletest.git' + '@' + 'a46a6012a133c7c0929fdae55b321c2fe65f3c5f',
  'third_party/highway/src':
    Var('chromium_url') + '/external/github.com/google/highway.git' + '@' + '2607d3b5b0113992fe84d3848859eae13b3b52c1',
  'third_party/icu':
    Var('chromium_url') + '/chromium/deps/icu.git' + '@' + 'd3c612f4414600bd06a68db89944df130b5bc248',
  'third_party/instrumented_libs': {
    'url': Var('chromium_url') + '/chromium/third_party/instrumented_libraries.git' + '@' + 'fd1b8502e69c84a128d2b49259472ab93f7ea8ec',
    'condition': 'checkout_instrumented_libraries',
  },
  'third_party/ittapi': {
    # Force checkout ittapi libraries to pass v8 header includes check on
    # bots that has check_v8_header_includes enabled.
    'url': Var('chromium_url') + '/external/github.com/intel/ittapi' + '@' + 'a3911fff01a775023a06af8754f9ec1e5977dd97',
    'condition': "checkout_ittapi or check_v8_header_includes",
  },
  'third_party/jinja2':
    Var('chromium_url') + '/chromium/src/third_party/jinja2.git' + '@' + 'fde3c16799bd5937e4f15aedc9037c89f96236b9',
  'third_party/jsoncpp/src':
    Var('chromium_url') + '/external/github.com/open-source-parsers/jsoncpp.git'+ '@' + '93acc7b155048f1a66c43235cc205519f1e7a6ea',
  'third_party/libc++/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxx.git' + '@' + '2c7b4048084397454ff423895c23c40a86dbf214',
  'third_party/libc++abi/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxxabi.git' + '@' + '8e42446dd61148a66220123297964bd3856e7cf9',
  'third_party/libpfm4':
    Var('chromium_url') + '/chromium/src/third_party/libpfm4.git' + '@' + 'e02968592ac424c7f45441249e7175d4549a5e3a',
  'third_party/libpfm4/src':
    Var('chromium_url') + '/external/git.code.sf.net/p/perfmon2/libpfm4.git' + '@' + '186a658c3e1e49b0ee3da414a7db8ba971a00cb5',
  'third_party/libunwind/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libunwind.git' + '@' + '9e44ea2ff4f53a60c2a3d2b6f511d7bab9ab12f0',
  'third_party/llvm-libc/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libc.git' + '@' + 'f95dd12ba6da85518ff2eaa384bdff3716612f1a',
  'third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/clang-android-runtime-library-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '64727394243fcab5a1c2765da26d8f1e8ebbd9fe7f35d0cab3b3a0da9fde7d4a',
        'size_bytes': 6181828,
        'generation': 1795478778225891,
        'condition': 'checkout_android',
      },
      {
        'object_name': 'Linux_x64/clang-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'a3fbdb1855e705711d5fb5892f88cc69a8f0cd9a4ee04a0850c89b7d2b8b179f',
        'size_bytes': 59066320,
        'generation': 1795478775605938,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Linux_x64/clang-tidy-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '36775600bb383b755ae465984bdafda01043dd7ac61083cbab902a6ec27b024a',
        'size_bytes': 15220244,
        'generation': 1795478775715724,
        'condition': 'host_os == "linux" and checkout_clang_tidy',
      },
      {
        'object_name': 'Linux_x64/clangd-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'e0de5f4f8845a131af2061eff24ba4563255914220145d05ab4a7b924896b851',
        'size_bytes': 15583152,
        'generation': 1795478775875977,
        'condition': 'host_os == "linux" and checkout_clangd',
      },
      {
        'object_name': 'Linux_x64/llvm-code-coverage-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '86732aeed8b70985b77f614213f88685ebb569e5da35d524442cf15279c82385',
        'size_bytes': 2435668,
        'generation': 1795478776072148,
        'condition': 'host_os == "linux" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Linux_x64/llvmobjdump-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '192e516a17b549336da0c1991feebb5945164c7d9e35003c71de47a496ae3a1a',
        'size_bytes': 6024552,
        'generation': 1795478775918517,
        'condition': '(checkout_linux or checkout_mac or checkout_android) and host_os == "linux"',
      },
      {
        'object_name': 'Mac/clang-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'a5fa83d990ea74c1f90c44ce063deb53fa90b39e4eeab0c9b1170dddcab33157',
        'size_bytes': 55567376,
        'generation': 1795478784445219,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac/clang-mac-runtime-library-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'c8c40924e87d77bc81e4b7fe914e2e5ffcc0ba26bb0e5cef9dbfb7f1c9b9d365',
        'size_bytes': 1286412,
        'generation': 1795478791497407,
        'condition': 'checkout_mac and not host_os == "mac"',
      },
      {
        'object_name': 'Mac/clang-tidy-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '9cd2a83dbccb2cab4fe50a5ee282db91ccf5812321d73a69c66285ec05d6b606',
        'size_bytes': 14481732,
        'generation': 1795478784536728,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac/clangd-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'adf9d309e14fbf41621acd303cbdb1eb09e13063ece7161c6a643ea85224da1b',
        'size_bytes': 15583700,
        'generation': 1795478784642991,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clangd',
      },
      {
        'object_name': 'Mac/llvm-code-coverage-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '2efdec6e0ecf9805b99dce21a2dbb1c0973ad542c456f7c22f76df7a27dbd84b',
        'size_bytes': 2315360,
        'generation': 1795478784842581,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac/llvmobjdump-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'b59bca56abaa08663e82781a248b1402da20c6dbb1abe4241c7d806745202724',
        'size_bytes': 5597588,
        'generation': 1795478784732320,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/clang-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'fa0844933f1afeca7f35866e865beed5e7806d5eec15db941ef8e4c5141f2f24',
        'size_bytes': 45625592,
        'generation': 1795478792867551,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Mac_arm64/clang-tidy-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '60742ca1f3bd526ddead079039808f2c872b8f38713aba895c964eff5e8b7ba5',
        'size_bytes': 12359740,
        'generation': 1795478792976527,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac_arm64/clangd-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': 'b858d19a2fe2bb80a9fea92780d258ad9101511cac7eec82816cafdbfdf1b099',
        'size_bytes': 12333420,
        'generation': 1795478793090638,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clangd',
      },
      {
        'object_name': 'Mac_arm64/llvm-code-coverage-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '28d07e2a7e0594678e01febf05d0643bb8f6e82b6a460129837527236be40063',
        'size_bytes': 1902196,
        'generation': 1795478793241857,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac_arm64/llvmobjdump-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '2e6f70ac5a2f05743f45499b36887a86933aed585c5a7626879efe0d83981487',
        'size_bytes': 5282828,
        'generation': 1795478793129620,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/clang-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '621d50c0f30b036c6a63c216e3048b8551983b103344523448143da927e3c3d2',
        'size_bytes': 53390252,
        'generation': 1795478801750733,
        'condition': 'host_os == "win"',
      },
      {
        'object_name': 'Win/clang-tidy-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '7f06cf789c7ed97f9e0634559392844652cb8a1d9335922f7c3bc3b8cd931410',
        'size_bytes': 15535600,
        'generation': 1795478801910632,
        'condition': 'host_os == "win" and checkout_clang_tidy',
      },
      {
        'object_name': 'Win/clang-win-runtime-library-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '5aa388fc2eb08fc26272f4d20f766de868e407181f940dee0d1fbb22c5f5f3d4',
        'size_bytes': 2714456,
        'generation': 1795478808882265,
        'condition': 'checkout_win and not host_os == "win"',
      },
      {
        'object_name': 'Win/clangd-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '3596d131f50d7d5dce14898d6eee747aaab042438d237a35d72fcdc554641dfd',
        'size_bytes': 15969244,
        'generation': 1795478802031503,
       'condition': 'host_os == "win" and checkout_clangd',
      },
      {
        'object_name': 'Win/llvm-code-coverage-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '3261cff4039624b9cd51ad7d9b4fd42d129147c840a61edb4684df7bcdb3c10f',
        'size_bytes': 2617712,
        'generation': 1795478802200524,
        'condition': 'host_os == "win" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/llvmobjdump-llvmorg-24-init-17044-gc3223914-68.tar.xz',
        'sha256sum': '2ec95a789c636d0a0fdbcb4a7fc90e7cbc0e45eda840d57a2407e20e0389d429',
        'size_bytes': 6132588,
        'generation': 1795478802113233,
        'condition': '(checkout_linux or checkout_mac or checkout_android) and host_os == "win"',
      },
    ],
  },
  'third_party/logdog/logdog':
    Var('chromium_url') + '/infra/luci/luci-py/client/libs/logdog' + '@' + 'dd1f72c4fefbd4a1fd3b6bdb5d56afede3612347',
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
    Var('chromium_url') + '/external/github.com/google/perfetto.git' + '@' + 'e6a2946f0b3e0d604db6eeec82328e2b293e32e6',
  'third_party/protobuf':
    Var('chromium_url') + '/chromium/src/third_party/protobuf.git' + '@' + '3813fd5b3029807583e5d2549a40d78fa499c3e8',
  'third_party/re2/src':
    Var('chromium_url') + '/external/github.com/google/re2.git' + '@' + '2da0056814cf180480a19f5cf811e7e1c054bf6d',
  'third_party/requests': {
      'url': Var('chromium_url') + '/external/github.com/kennethreitz/requests.git' + '@' + 'c2b307dbefe21177af03f9feb37181a89a799fcc',
      'condition': 'checkout_android',
  },
  'tools/rust':
    Var('chromium_url') + '/chromium/src/tools/rust' + '@' + '6a137d2ff7dea84a372fd50e148c6fea044ec398',
  'tools/win':
    Var('chromium_url') + '/chromium/src/tools/win' + '@' + 'd72db9e3040567a7589e00c0ad7ddf25c3b070bc',
  'third_party/rust':
    Var('chromium_url') + '/chromium/src/third_party/rust' + '@' + '147c52e552e54a6d950a918c6245c04459f562bc',
  'third_party/rust-toolchain': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/rust-toolchain-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': '73e395668def3e0a488f5edffd3ccaeb86a99f0476ed06c04930c6895094b212',
        'size_bytes': 287866428,
        'generation': 1794385348294495,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-toolchain-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': '0888daf1c1215ac4556028d7e6df7d5fa0c57336bff64c570a57f93fef0da565',
        'size_bytes': 275787508,
        'generation': 1794385352157176,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-toolchain-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': '64a506650c18e57eeae11d493c41faf3a712885b7b65d993f4c76eac7f0498b7',
        'size_bytes': 259550672,
        'generation': 1794385356196372,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-toolchain-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': 'c3c1f2e465ed50a67e2ba5c5579c3df46e2fb445ccb73d063869087972d84e1c',
        'size_bytes': 427637256,
        'generation': 1794385360031394,
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
        'object_name': 'Linux_x64/rust-libclang-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': 'bdfa70274badca548d73b95f353ebc6451c96c6ada1e561c6da880f857f21bb6',
        'size_bytes': 23186936,
        'generation': 1794385350228067,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-libclang-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': 'daa332f90f0237bd1389cfa482957eee5f08cd341c42b3c0f7ec9ee96cf90544',
        'size_bytes': 23333728,
        'generation': 1794385354028288,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-libclang-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': '4bfdfe46686aa3c1f8f6fd9447fac5e37e699bccf213138b279c99a71c732a03',
        'size_bytes': 20988176,
        'generation': 1794385358033896,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-libclang-96a18bf5ffa2b24f9cf3d1c731a9eb121c1acad9-5-llvmorg-24-init-17044-gc3223914.tar.xz',
        'sha256sum': '7b4dd46967b0a84b83ab6aa6f3999ca18b15926c5f05a481445e47aaed2558e2',
        'size_bytes': 21906524,
        'generation': 1794385361865408,
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
    Var('chromium_url') + '/chromium/src/third_party/zlib.git'+ '@' + '401d1853956d408bfd327ee5e54c1c7b67086f87',
  'tools/clang':
    Var('chromium_url') + '/chromium/src/tools/clang.git' + '@' + '25de098d3d73de7c1c853526e1f99ba0b8d55cd0',
  'tools/protoc_wrapper':
    Var('chromium_url') + '/chromium/src/tools/protoc_wrapper.git' + '@' + '5c77c0c9fdebb2d87378bcea183e4c6937fd8c53',
  'tools/python': {
    'url': Var('chromium_url') + '/chromium/src/tools/python.git' + '@' + '54763b0a68ecca4842b8ef32793b82811337a67b',
    'condition': 'checkout_android',
  },
  'third_party/abseil-cpp': {
    'url': Var('chromium_url') + '/chromium/src/third_party/abseil-cpp.git' + '@' + '4e36995a11b51ea24047fba9a35ffcdb8a7bf2d3',
    'condition': 'not build_with_chromium',
  },
  'third_party/boringssl/src': {
    'url': Var('boringssl_url') + '/boringssl.git' + '@' + '2e33b24cdb5b021c07990dc6d0191ae4a1480cfb',
    'condition': 'not build_with_chromium',
  },
  'third_party/nasm': {
    'url': Var('chromium_url') + '/chromium/deps/nasm.git' + '@' + '54b1dcf7995006337c54f6862fd6f77a01e23e4e',
    'condition': 'not build_with_chromium',
  },
  'third_party/fadec/src': {
    'url': Var('chromium_url') + '/external/github.com/aengelke/fadec.git' + '@' + 'c9f78f532b9004de278489019ab2c6c28ae9746e',
  },
  'third_party/disarm/src': {
    'url': Var('chromium_url') + '/external/github.com/aengelke/disarm.git' + '@' + '8770f6d6f0c62e7e008f4bf0e04d22197ecb98e3',
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
  '+openssl',
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
