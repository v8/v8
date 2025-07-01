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
  'download_prebuilt_arm64_llvm_symbolizer': False,
  'check_v8_header_includes': False,

  # By default, download the fuchsia sdk from the public sdk directory.
  'fuchsia_sdk_cipd_prefix': 'fuchsia/sdk/core/',

  # Used for downloading the Fuchsia SDK without running hooks.
  'checkout_fuchsia_no_hooks': False,

  # reclient CIPD package version
  'reclient_version': 're_client_version:0.179.0.28341fc7-gomaip',

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
  'gn_version': 'git_revision:97b68a0bb62b7528bc3491c7949d6804223c2b82',

  # ninja CIPD package version
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:3@1.12.1.chromium.4',

  # siso CIPD package version
  'siso_version': 'git_revision:4ac6aa83563aed91e78947b2b466eb6d49d51a21',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Fuchsia sdk
  # and whatever else without interference from each other.
  'fuchsia_version': 'version:28.20250626.4.1',

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling partition_alloc_version
  # and whatever else without interference from each other.
  'partition_alloc_version': '79b418050876e0c4187a80f668e81ce956b32bf2',

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
    Var('chromium_url') + '/chromium/src/build.git' + '@' + '7e93b7d484e73d88e7d6de65a27f1e807dba7422',
  'buildtools':
    Var('chromium_url') + '/chromium/src/buildtools.git' + '@' + 'f7862e515c44313f6942517fef6d7718d92283d3',
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
    Var('chromium_url') + '/external/github.com/tc39/test262.git' + '@' + 'a073f479f80b336256b7fc4e04700c827293e2fe',
  'third_party/android_platform': {
    'url': Var('chromium_url') + '/chromium/src/third_party/android_platform.git' + '@' + 'e97e62b0b5f26315a0cd58ff8772a2483107158e',
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
        'version': 'KXOia11cm9lVdUdPlbGLu8sCz6Y4ey_HV2s8_8qeqhgC',
      },
    ],
    'condition': 'checkout_android',
    'dep_type': 'cipd',
  },
  'third_party/catapult': {
    'url': Var('chromium_url') + '/catapult.git' + '@' + '92a076097780315c2a9551cdc1bd4389b3f4cce5',
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
    Var('chromium_url') + '/chromium/tools/depot_tools.git' + '@' + '5cc29c7a217801cfa7b7b0aa23144300b49b750d',
  'third_party/dragonbox/src':
    Var('chromium_url') + '/external/github.com/jk-jeon/dragonbox.git' + '@' + '6c7c925b571d54486b9ffae8d9d18a822801cbda',
  'third_party/fp16/src':
    Var('chromium_url') + '/external/github.com/Maratyszcza/FP16.git' + '@' + '0a92994d729ff76a58f692d3028ca1b64b145d91',
  'third_party/fast_float/src':
    Var('chromium_url') + '/external/github.com/fastfloat/fast_float.git' + '@' + 'cb1d42aaa1e14b09e1452cfdef373d051b8c02a4',
  'third_party/fuchsia-gn-sdk': {
    'url': Var('chromium_url') + '/chromium/src/third_party/fuchsia-gn-sdk.git' + '@' + '764129cb937b746fcc2cf965c2d9b8b3076488e4',
    'condition': 'checkout_fuchsia',
  },
  'third_party/simdutf':
    Var('chromium_url') + '/chromium/src/third_party/simdutf' + '@' + 'a1046f20f7099b4f7dd72d7127bb4dc05252ec5c',
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
    'url': Var('chromium_url') + '/chromium/src/third_party/google_benchmark.git' + '@' + 'd6e7f141ed7c93a66890f3750ab634b8b52057a5',
  },
  'third_party/google_benchmark_chrome/src': {
    'url': Var('chromium_url') + '/external/github.com/google/benchmark.git' + '@' + '761305ec3b33abf30e08d50eb829e19a802581cc',
  },
  'third_party/fuzztest':
    Var('chromium_url') + '/chromium/src/third_party/fuzztest.git' + '@' + 'e0ad2518abf4089c4e9e69abd48166fc28a8b9ef',
  'third_party/fuzztest/src':
    Var('chromium_url') + '/external/github.com/google/fuzztest.git' + '@' + '45a1c3ad5ac3de58c8e9a3f89036e3f954820d4c',
  'third_party/googletest/src':
    Var('chromium_url') + '/external/github.com/google/googletest.git' + '@' + 'c67de117379f4d1c889c7581a0a76aa0979c2083',
  'third_party/highway/src':
    Var('chromium_url') + '/external/github.com/google/highway.git' + '@' + '00fe003dac355b979f36157f9407c7c46448958e',
  'third_party/icu':
    Var('chromium_url') + '/chromium/deps/icu.git' + '@' + 'b929596baebf0ab4ac7ec07f38365db4c50a559d',
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
    Var('chromium_url') + '/chromium/src/third_party/jinja2.git' + '@' + 'c3027d884967773057bf74b957e3fea87e5df4d7',
  'third_party/jsoncpp/source':
    Var('chromium_url') + '/external/github.com/open-source-parsers/jsoncpp.git'+ '@' + '42e892d96e47b1f6e29844cc705e148ec4856448',
  'third_party/libc++/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxx.git' + '@' + 'b5d83e70a1537fbce965e6319877c76794daaa80',
  'third_party/libc++abi/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libcxxabi.git' + '@' + '02570151a883c93800bd336774e26d2bad8fee0d',
  'third_party/libunwind/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libunwind.git' + '@' + '5bbf35ae6801f579c523893176789774c0726e22',
  'third_party/llvm-libc/src':
    Var('chromium_url') + '/external/github.com/llvm/llvm-project/libc.git' + '@' + '00de13db4f2f71ffa23bfd8a41615fbcb68fc657',
  'third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/clang-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'fd19544f167b5dd5a12024b9021b9b772c014905a4673b8d75ce67f0d06c73cd',
        'size_bytes': 55845940,
        'generation': 1750690373685358,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Linux_x64/clang-tidy-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '489558a5c372ead5836fe67cbd2f46f6d5a524703093aee97b3c99efb53b9e8f',
        'size_bytes': 13657968,
        'generation': 1750690373970645,
        'condition': 'host_os == "linux" and checkout_clang_tidy',
      },
      {
        'object_name': 'Linux_x64/clangd-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'b8e29fa10198eda874644132c7c8911612ac73971c51946c8771aa6c39a4c26f',
        'size_bytes': 13879664,
        'generation': 1750690374148306,
        'condition': 'host_os == "linux" and checkout_clangd',
      },
      {
        'object_name': 'Linux_x64/llvm-code-coverage-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'fd0330e7380cf08638f4a3a893191cb4fb7c8a13122368bf8ed31bec3a7ae8e4',
        'size_bytes': 2313332,
        'generation': 1750690374633463,
        'condition': 'host_os == "linux" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Linux_x64/llvmobjdump-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '791ffbc93fc075b33d97ab04c622b0c91a6f20f40bc76d72fb14c35e52b7d494',
        'size_bytes': 5675208,
        'generation': 1750690374293292,
        'condition': '(checkout_linux or checkout_mac or checkout_android and host_os != "mac")',
      },
      {
        'object_name': 'Mac/clang-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'a24ad8caf0730c1d2abac758889217db88de1cdbfdfc0f1afc06eb900f441e9d',
        'size_bytes': 52434160,
        'generation': 1750690376393610,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac/clang-mac-runtime-library-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '7ece76451d184aaee9ef3f778f98aac917c0bca62ac1562ddab0013e2da7ebc1',
        'size_bytes': 995312,
        'generation': 1750690396883403,
        'condition': 'checkout_mac and not host_os == "mac"',
      },
      {
        'object_name': 'Mac/clang-tidy-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '444e50776e84916582fa3833fa9e8f62e599b1e691c385fdc9af19139a33796a',
        'size_bytes': 13751072,
        'generation': 1750690376720844,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac/clangd-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'b3ae223ff917978c8b88490d5cf130ad4369163c5d7724a55c1e9f792d26a0ba',
        'size_bytes': 15150056,
        'generation': 1750690376890897,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clangd',
      },
      {
        'object_name': 'Mac/llvm-code-coverage-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '43e4196cda83f0b5e42998582982f210396c8b0375c448b0038e664948822a8d',
        'size_bytes': 2282936,
        'generation': 1750690377374578,
        'condition': 'host_os == "mac" and host_cpu == "x64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Mac_arm64/clang-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'e22cd3cda320fb169bb4a654c2d74290985bbd437e108305dc76cfc1ac796959',
        'size_bytes': 44341520,
        'generation': 1750690398921553,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Mac_arm64/clang-tidy-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'a21513a81f81462faf07c3ae3b7e36ae4a8f2cb804f662420bc9fe5dd89934ac',
        'size_bytes': 11876208,
        'generation': 1750690398940597,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_tidy',
      },
      {
        'object_name': 'Mac_arm64/clangd-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '374cccbd42e1c955703cc9a7839bfc1dde0d685f7ee7e613b4122903d2b569cc',
        'size_bytes': 12141224,
        'generation': 1750690399093769,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clangd',
      },
      {
        'object_name': 'Mac_arm64/llvm-code-coverage-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '641ee3e630385b7010e903808db52cd863ac2f1cfb7a5e1b62f124768bcb6c33',
        'size_bytes': 1988508,
        'generation': 1750690399733904,
        'condition': 'host_os == "mac" and host_cpu == "arm64" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/clang-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '6ae063abb9365d9dbabb807f732f27bc80586fabb8d9ec0f1fe7e709c3a79f84',
        'size_bytes': 47395032,
        'generation': 1750690422812176,
        'condition': 'host_os == "win"',
      },
      {
        'object_name': 'Win/clang-tidy-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'ac55e2350c829f0400a9ff19de4beb1828c555403913785abdf2dec854900910',
        'size_bytes': 13495284,
        'generation': 1750690423085786,
        'condition': 'host_os == "win" and checkout_clang_tidy',
      },
      {
        'object_name': 'Win/clang-win-runtime-library-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '04068cb779f3964d720ea2b2ca593207052cefdf3b1863620cdb9f764d000d55',
        'size_bytes': 2506704,
        'generation': 1750690443942059,
        'condition': 'checkout_win and not host_os == "win"',
      },
      {
        'object_name': 'Win/clangd-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'faadb216f376a82de6ae317a3bdadb9a10a1a1b60742d950fd079aad1ce9d6d6',
        'size_bytes': 13906324,
        'generation': 1750690423257789,
       'condition': 'host_os == "win" and checkout_clangd',
      },
      {
        'object_name': 'Win/llvm-code-coverage-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': '89e38a25c54ce3ceb1a4e8d595905d88f79e2e1793bf64c153f7d4105cf2643c',
        'size_bytes': 2383136,
        'generation': 1750690423799057,
        'condition': 'host_os == "win" and checkout_clang_coverage_tools',
      },
      {
        'object_name': 'Win/llvmobjdump-llvmorg-21-init-16348-gbd809ffb-1.tar.xz',
        'sha256sum': 'b307a743893e42d5b3429441256dbd7d93c701917f1053e5f346b1f87c308a4d',
        'size_bytes': 5651276,
        'generation': 1750690423419140,
        'condition': 'checkout_linux or checkout_mac or checkout_android and host_os == "win"',
      },
    ],
  },
  'third_party/logdog/logdog':
    Var('chromium_url') + '/infra/luci/luci-py/client/libs/logdog' + '@' + '0b2078a90f7a638d576b3a7c407d136f2fb62399',
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
    'condition': 'host_cpu != "s390" and host_os != "zos" and host_cpu != "ppc"'
  },
  'third_party/partition_alloc': {
    'url': Var('chromium_url') + '/chromium/src/base/allocator/partition_allocator.git@' + Var('partition_alloc_version'),
    'condition': 'not build_with_chromium',
  },
  'third_party/perfetto':
    Var('android_url') + '/platform/external/perfetto.git' + '@' + '40b529923598b739b2892a536a7692eedbed5685',
  'third_party/protobuf':
    Var('chromium_url') + '/chromium/src/third_party/protobuf.git' + '@' + '83a68fb3c5ea5db85ead48e31555657cf9b442d9',
  'third_party/re2/src':
    Var('chromium_url') + '/external/github.com/google/re2.git' + '@' + '8ea5841693c6c0b837c6ed2189217e8f8d6fee9c',
  'third_party/requests': {
      'url': Var('chromium_url') + '/external/github.com/kennethreitz/requests.git' + '@' + 'c7e0fc087ceeadb8b4c84a0953a422c474093d6d',
      'condition': 'checkout_android',
  },
  'tools/rust':
    Var('chromium_url') + '/chromium/src/tools/rust' + '@' + 'c52b68f9dc6986af8573e3151a99b009311dd75f',
  'third_party/rust':
    Var('chromium_url') + '/chromium/src/third_party/rust' + '@' + '079791274d7d7a206e8e2d1cae76c5f4449ba1fa',
  'third_party/rust-toolchain': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [
      {
        'object_name': 'Linux_x64/rust-toolchain-22be76b7e259f27bf3e55eb931f354cd8b69d55f-3-llvmorg-21-init-16348-gbd809ffb.tar.xz',
        'sha256sum': '5f8e9ad847e5bf586e0de1bb563c9a49e05ad36edfad5037900d7510004fc577',
        'size_bytes': 138573136,
        'generation': 1750840933611077,
        'condition': 'host_os == "linux"',
      },
      {
        'object_name': 'Mac/rust-toolchain-22be76b7e259f27bf3e55eb931f354cd8b69d55f-3-llvmorg-21-init-16348-gbd809ffb.tar.xz',
        'sha256sum': '357db812ca0a518ef0fc4394ddc859d68f23384931294412b7424bb3aabb3c09',
        'size_bytes': 132392604,
        'generation': 1750840935469331,
        'condition': 'host_os == "mac" and host_cpu == "x64"',
      },
      {
        'object_name': 'Mac_arm64/rust-toolchain-22be76b7e259f27bf3e55eb931f354cd8b69d55f-3-llvmorg-21-init-16348-gbd809ffb.tar.xz',
        'sha256sum': 'd3cb60c6388e86d3d1a0c46c539f1ea0ed1ff48cf907dc21b2cb5ff441b23c03',
        'size_bytes': 120354192,
        'generation': 1750840937280735,
        'condition': 'host_os == "mac" and host_cpu == "arm64"',
      },
      {
        'object_name': 'Win/rust-toolchain-22be76b7e259f27bf3e55eb931f354cd8b69d55f-3-llvmorg-21-init-16348-gbd809ffb.tar.xz',
        'sha256sum': '7e804f3a8bef4c8ca32d3368ca7564e5c12b684899453d9a522bdd05b1f1df7b',
        'size_bytes': 195000356,
        'generation': 1750840939064273,
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
    Var('chromium_url') + '/chromium/src/third_party/zlib.git'+ '@' + '4028ebf8710ee39d2286cb0f847f9b95c59f84d8',
  'tools/clang':
    Var('chromium_url') + '/chromium/src/tools/clang.git' + '@' + '9d5b043d87010932b62a2a411a3969223655088b',
  'tools/protoc_wrapper':
    Var('chromium_url') + '/chromium/src/tools/protoc_wrapper.git' + '@' + '502dcad5230ec1da544e7720b15d91782f040d9f',
  'third_party/abseil-cpp': {
    'url': Var('chromium_url') + '/chromium/src/third_party/abseil-cpp.git' + '@' + 'dd86c62796721703269070183115c6196e03e71b',
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
  # https://chromium.googlesource.com/chromium/src/+/main/styleguide/c++/c++11.md
  '+absl/container/flat_hash_map.h',
  '+absl/container/flat_hash_set.h',
  '+absl/container/btree_map.h',
  '+absl/functional/overload.h',
  '+absl/status',
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
]
