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
  'reclient_version': 're_client_version:0.177.1.e58c0145-gomaip',

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
  'gn_version': 'git_revision:c0a46c5e8c316010baf1a0eb2d2ee5a86f73e4c2',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:3@1.12.1.chromium.4',

  # siso CIPD package version
  'siso_version': 'git_revision:0dd559fac7db9d72c9cbee525ab596b8a10b79d0',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Fuchsia sdk
  # and whatever else without interference from each other.
  'fuchsia_version': 'version:27.20250402.5.1',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling partition_alloc_version
  # and whatever else without interference from each other.
  'partition_alloc_version': '932177a9b8f06e7f478fcb22ac7b7184908738a8',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_build-tools_version
  # and whatever else without interference from each other.
  'android_sdk_build-tools_version': 'y3EsZLg4bxPmpW0oYsAHylywNyMnIwPS3kh1VbQLAFAC',
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
  'android_sdk_platform-tools_version': 'mjFmRj7k_XR9yj60pYbr9mG38FyEbU5oWdU56bZQ5cwC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_platforms_version
  # and whatever else without interference from each other.
  'android_sdk_platforms_version': '_YHemUrK49JrE7Mctdf5DDNOHu1VKBx_PTcWnZ-cbOAC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_sources_version
  # and whatever else without interference from each other.
  'android_sdk_sources_version': 'qfTSF99e29-w3eIVPpfcif0Em5etyvxuicTDTntWHQMC',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling android_sdk_tools-lint_version
  # and whatever else without interference from each other.
  'android_sdk_cmdline-tools_version': 'gekOVsZjseS1w9BXAT3FsoW__ByGDJYS9DgqesiwKYoC',
}

deps = {
  'build':
    Var('chromium_url') + '/chromium/src/build.git' + '@' + 'c246251934490d46fc8710d3820a49ff8caf1e16',
  'buildtools':
    Var('chromium_url') + '/chromium/src/buildtools.git' + '@' + '2e85fadf9fc1fa9a00b7a9d7f467474ad4484c97',
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
    Var('chromium_url') + '/external/github.com/tc39/test262.git' + '@' + 'cf4c281a3857d88f7165bc022b059132d71ef0ce',
  'third_party/android_platform': {
    'url': Var('chromium_url') + '/chromium/src/third_party/android_platform.git' + '@' + '98aee46efb1cc4e09fa0e3ecaa6b19dc258645fa',
    'condition': 'checkout_android',
  },
  'third_party/android_sdk/public': {
      'packages': [
          {
              'package': 'chromium/third_party/android_sdk/public/build-tools/36.0.0',
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
              'package': 'chromium/third_party/android_sdk/public/platforms/android-36',
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
  'third_party/catapult': {
    'url': Var('chromium_url') + '/catapult.git' + '@' + '3ea4639ac5703fc483bbb7117362006418ef8ee5',
    'condition': 'checkout_android',
  },
  'third_party/clang-format/script':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/clang/tools/clang-format.git' + '@' + '37f6e68a107df43b7d7e044fd36a13cbae3413f2',
  'third_party/colorama/src': {
    'url': Var('chromium_url') + '/external/colorama.git' + '@' + '3de9f013df4b470069d03d250224062e8cf15c49',
    'condition': 'checkout_android',
  },
  'third_party/cpu_features/src': {
    'url': Var('chromium_url') + '/external/github.com/google/cpu_features.git' + '@' + '936b9ab5515dead115606559502e3864958f7f6e',
    'condition': 'checkout_android',
  },
  'third_party/depot_tools':
    Var('chromium_url') + '/chromium/tools/depot_tools.git' + '@' + 'f40ddcd8d51626fb7be3ab3c418b3f3be801623f',
  'third_party/fp16/src':
    Var('chromium_url') + '/external/github.com/Maratyszcza/FP16.git' + '@' + '0a92994d729ff76a58f692d3028ca1b64b145d91',
  'third_party/fast_float/src':
    Var('chromium_url') + '/external/github.com/fastfloat/fast_float.git' + '@' + 'cb1d42aaa1e14b09e1452cfdef373d051b8c02a4',
  'third_party/fuchsia-gn-sdk': {
    'url': Var('chromium_url') + '/chromium/src/third_party/fuchsia-gn-sdk.git' + '@' + 'e678aca1bad7a1c9a38620b2e328281bc68f6357',
    'condition': 'checkout_fuchsia',
  },
  'third_party/simdutf':
    Var('chromium_url') + '/chromium/src/third_party/simdutf' + '@' + '40d1fa26cd5ca221605c974e22c001ca2fb12fde',
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
    'url': Var('chromium_url') + '/chromium/src/third_party/google_benchmark.git' + '@' + '917e1208b42fdce63511e401067677ffee3a5c7d',
  },
  'third_party/google_benchmark_chrome/src': {
    'url': Var('chromium_url') + '/external/github.com/google/benchmark.git' + '@' + '761305ec3b33abf30e08d50eb829e19a802581cc',
  },
  'third_party/fuzztest':
    Var('chromium_url') + '/chromium/src/third_party/fuzztest.git' + '@' + 'df29ed1355d06c486e17fc421767ff01af050ca4',
  'third_party/fuzztest/src':
    Var('chromium_url') + '/external/github.com/google/fuzztest.git' + '@' + 'e98f567d3ea5fc61fb8957792448255acca4dc99',
  'third_party/googletest/src':
    Var('chromium_url') + '/external/github.com/google/googletest.git' + '@' + '52204f78f94d7512df1f0f3bea1d47437a2c3a58',
  'third_party/highway/src':
    Var('chromium_url') + '/external/github.com/google/highway.git' + '@' + '00fe003dac355b979f36157f9407c7c46448958e',
  'third_party/icu':
    Var('chromium_url') + '/chromium/deps/icu.git' + '@' + 'c9fb4b3a6fb54aa8c20a03bbcaa0a4a985ffd34b',
  'third_party/instrumented_libs': {
    'url': Var('chromium_url') + '/chromium/third_party/instrumented_libraries.git' + '@' + '69015643b3f68dbd438c010439c59adc52cac808',
    'condition': 'checkout_instrumented_libraries',
  },
  'third_party/ittapi': {
    # Force checkout ittapi libraries to pass v8 header includes check on
    # bots that has check_v8_header_includes enabled.
    'url': Var('chromium_url') + '/external/github.com/intel/ittapi' + '@' + 'a3911fff01a775023a06af8754f9ec1e5977dd97',
    'condition': "checkout_ittapi or check_v8_header_includes",
  },
  'third_party/jinja2':
    Var('chromium_url') + '/chromium/src/third_party/jinja2.git' + '@' + '5e1ee241ab04b38889f8d517f2da8b3df7cfbd9a',
  'third_party/jsoncpp/source':
    Var('chromium_url') + '/external/github.com/open-source-parsers/jsoncpp.git'+ '@' + '42e892d96e47b1f6e29844cc705e148ec4856448',
  'third_party/libc++/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxx.git' + '@' + '6f6dc922b8e0c96b7cd49873c7bdcdd46219a55c',
  'third_party/libc++abi/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxxabi.git' + '@' + '78140a7276d52a537615d79da1a91b8890cb8287',
  'third_party/libunwind/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libunwind.git' + '@' + 'e2e6f2a67e9420e770b014ce9bba476fa2ab9874',
  'third_party/llvm-libc/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libc.git' + '@' + '7f56bd496d12e4d65cfcee697a700781d0137f5d',
  'third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/clang-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '17d9277d32a87f856c6d0a5ee3f662015d423d876315d7736ca7b05b2b6f047e',
        'size_bytes': 54610924,
        'generation': 1743178947242029,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Linux_x64/clang-tidy-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '426b5f0daf964c3e1a9b9e7626311e8544a3e4768c8ef80d31464894b0fead68',
        'size_bytes': 13573888,
        'generation': 1743178947383618,
        'condition': 'host_os == "linux" and checkout_clang_tidy',
      },
      {
        'object_name': 'Linux_x64/clangd-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': 'e419c7db3d30a163fdd8692cfeef26a1fb22db039f2f6ab42b930edfc4e5fd43',
        'size_bytes': 13829552,
        'generation': 1743178947404834,
        'condition': 'host_os == "linux" and checkout_clangd',
      },
      {
        'object_name': 'Linux_x64/llvm-code-coverage-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '935be59242458aa5849133c5a47b4ae11f6d8cae92401188e7db070dcae11326',
        'size_bytes': 2303256,
        'generation': 1743178947639495,
        'condition': 'host_os == "linux" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Linux_x64/llvmobjdump-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '3617952c541889b46806f66223bb4e8342e4c714938e7c83c7b9ca27b8e94cec',
        'size_bytes': 5699348,
        'generation': 1743178947497618,
        'condition': '(checkout_linux or checkout_mac or checkout_android and host_os != "mac")',
      },
      {
        'object_name': 'Mac/clang-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '690ae86dbd1c3689713db944059e7249760bdc30d4e06fdee79851780d62b255',
        'size_bytes': 51731764,
        'generation': 1743178948995628,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac/clang-mac-runtime-library-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '6e5b19f3ef4f2a84b2137f64c66cf7dc559c399ee070b69c26791ad4530dc84a',
        'size_bytes': 980732,
        'generation': 1743178955913911,
        'condition': 'checkout_mac and not host_os == "mac"',
      },
      {
        'object_name': 'Mac/clang-tidy-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '05c89e445986aece87cf514b0e877c6cb1b847ac32c8fe24baf5386fa2874956',
        'size_bytes': 13595068,
        'generation': 1743178949025853,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac/clangd-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '27945ba4a22b9e59b0c28dd299919a1da3a18a2d76e10f30a5e0e72242af6b55',
        'size_bytes': 15018860,
        'generation': 1743178949033402,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clangd',
      },
      {
        'object_name': 'Mac/llvm-code-coverage-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '3eb87f93194f7fe3177d4796c8d6376421811538747d1e65c825654ec4ecceea',
        'size_bytes': 2257340,
        'generation': 1743178949227666,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac_arm64/clang-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': 'd4478bb335f9555fe6b0c888a4c0f5d48695081b02ce662cfe7a125f3f501eca',
        'size_bytes': 43908296,
        'generation': 1743178957140254,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Mac_arm64/clang-tidy-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '06bd6ba46d4c0fc51e018d53c41f1ab81f7222f99dde95a10d57cb7f6c340eb7',
        'size_bytes': 11774556,
        'generation': 1743178957285392,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac_arm64/clangd-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '1cb1d20b1d9bcf5bfab05f333c1cd21ff723a463f8b3a84c3b1f79758fc8d834',
        'size_bytes': 12058388,
        'generation': 1743178957310593,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clangd',
      },
      {
        'object_name': 'Mac_arm64/llvm-code-coverage-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '932bd7fb9dd4b8da2a59880f71f9f9fb4c66b071a4e481b19bbc340b9336dd11',
        'size_bytes': 1978748,
        'generation': 1743178957577801,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/clang-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '6f486adfb406b0fcb9d3b83485ed4fca3467a3565d67baf0d9fd822721b780a1',
        'size_bytes': 46895260,
        'generation': 1743178966458891,
        'condition': 'host_os == "win"',
      },
      {
        'object_name': 'Win/clang-tidy-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '3fd9808e08070f84920d22ad537e1f2445bcf5773c134ddef28b14cfe46a67e1',
        'size_bytes': 13459292,
        'generation': 1743178966633313,
        'condition': 'host_os == "win" and checkout_clang_tidy',
      },
      {
        'object_name': 'Win/clang-win-runtime-library-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': 'bcf2e1a93afb20f384dbbcc484815989cb0aa2e593f2235251498c97d7f22493',
        'size_bytes': 2477288,
        'generation': 1743178973852867,
        'condition': 'checkout_win and not host_os == "win"',
      },
      {
        'object_name': 'Win/clangd-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '12978a7e84b777aa10857e13f1e5e999061091126891631bf29df486f4bb2770',
        'size_bytes': 13891176,
        'generation': 1743178966674825,
       'condition': 'host_os == "win" and checkout_clangd',
      },
      {
        'object_name': 'Win/llvm-code-coverage-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '14842204c68030e276bb054bd5058a561be025d1ee1a0909943ea4f8c31cd715',
        'size_bytes': 2367868,
        'generation': 1743178966817841,
        'condition': 'host_os == "win" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/llvmobjdump-llvmorg-21-init-6681-g5b36835d-1.tar.xz',
        'sha256sum': '6c505137fcc3879c3f0ad3562ec1f530a4b83f725d2a91363d1cfdcc3536252e',
        'size_bytes': 5673896,
        'generation': 1743178966726617,
        'condition': 'checkout_linux or checkout_mac or checkout_android and host_os == "win"',
      },
    ],
  },
  'third_party/logdog/logdog':
    Var('chromium_url') + '/infra/luci/luci-py/client/libs/logdog' + '@' + '0b2078a90f7a638d576b3a7c407d136f2fb62399',
  'third_party/markupsafe':
    Var('chromium_url') + '/chromium/src/third_party/markupsafe.git' + '@' + '9f8efc8637f847ab1ba984212598e6fb9cf1b3d4',
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
  'third_party/partition_alloc': {
    'url': Var('chromium_url') + '/chromium/src/base/allocator/partition_allocator.git@' + Var('partition_alloc_version'),
    'condition': 'not build_with_chromium',
  },
  'third_party/perfetto':
    Var('android_url') + '/platform/external/perfetto.git' + '@' + '40b529923598b739b2892a536a7692eedbed5685',
  'third_party/protobuf':
    Var('chromium_url') + '/chromium/src/third_party/protobuf.git' + '@' + 'de1750a8cedfcf807e3d61f714198d45fd2652a1',
  'third_party/re2/src':
    Var('chromium_url') + '/external/github.com/google/re2.git' + '@' + 'c84a140c93352cdabbfb547c531be34515b12228',
  'third_party/requests': {
      'url': Var('chromium_url') + '/external/github.com/kennethreitz/requests.git' + '@' + 'c7e0fc087ceeadb8b4c84a0953a422c474093d6d',
      'condition': 'checkout_android',
  },
  'tools/rust':
    Var('chromium_url') + '/chromium/src/tools/rust' + '@' + 'b549758ca9a8bac940560fcdcfdc6b5564303d90',
  'third_party/rust':
    Var('chromium_url') + '/chromium/src/third_party/rust' + '@' + '9d81b90077932ba74a122f3cfcf33a468d0b09c7',
  'third_party/rust-toolchain': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/rust-toolchain-3f690c2257b7080cd3a8cce64e082fc972148990-1-llvmorg-21-init-6681-g5b36835d.tar.xz',
        'sha256sum': 'f3132b0b1fa412c2fb35e068e47b3fade679a1446d9a08b67c403d143931f2ec',
        'size_bytes': 118335848,
        'generation': 1743178941814849,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-toolchain-3f690c2257b7080cd3a8cce64e082fc972148990-1-llvmorg-21-init-6681-g5b36835d.tar.xz',
        'sha256sum': '093ad50477633cb98719ace92995ad0e11dfa491b97835997a30fc1078d63cbe',
        'size_bytes': 111607824,
        'generation': 1743178943094518,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-toolchain-3f690c2257b7080cd3a8cce64e082fc972148990-1-llvmorg-21-init-6681-g5b36835d.tar.xz',
        'sha256sum': 'e5db937e64017390f52c1cb68445ed5101855e0b9318c7de71dfaea37649b4cb',
        'size_bytes': 101058976,
        'generation': 1743178944397528,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-toolchain-3f690c2257b7080cd3a8cce64e082fc972148990-1-llvmorg-21-init-6681-g5b36835d.tar.xz',
        'sha256sum': '3f8d34895c880301e2805164b7b5e4e487769b65d5fcadb4384a10228f559c85',
        'size_bytes': 192615112,
        'generation': 1743178945657202,
        'condition': 'host_os == "win"',
      },
    ],
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
    Var('chromium_url') + '/chromium/src/third_party/zlib.git'+ '@' + 'ce5a169f50174a2be18c38cd7b2a1bf858649e58',
  'tools/clang':
    Var('chromium_url') + '/chromium/src/tools/clang.git' + '@' + '374799d77820014d176c4fafd1bb30bda600197c',
  'tools/protoc_wrapper':
    Var('chromium_url') + '/chromium/src/tools/protoc_wrapper.git' + '@' + 'dbcbea90c20ae1ece442d8ef64e61c7b10e2b013',
  'third_party/abseil-cpp': {
    'url': Var('chromium_url') + '/chromium/src/third_party/abseil-cpp.git' + '@' + 'd9e0233d6708820f0670a43a2c1c1c7f10aaf01a',
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
  # https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++11.md
  '+absl/container/flat_hash_map.h',
  '+absl/container/flat_hash_set.h',
  '+absl/container/btree_map.h',
  '+absl/status',
  '+absl/synchronization/mutex.h',
  '+absl/time/time.h',
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
