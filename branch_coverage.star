load("//lib.star", "GOMA", "v8_branch_coverage_builder")

v8_branch_coverage_builder(
    name = "V8 Linux - builder",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Linux"], "mastername": "client.v8", "set_gclient_var": "download_gcmole", "build_config": "Release", "binary_size_tracking": {"category": "linux32", "binary": "d8"}},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - debug builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8", "triggers": ["V8 Linux - debug", "V8 Linux - gc stress"]},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - debug",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - shared",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "binary_size_tracking": {"category": "linux32", "binary": "libv8.so"}},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - noi18n - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - verify csa",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - custom snapshot - debug builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8", "triggers": ["V8 Linux64 - custom snapshot - debug", "V8 Linux64 GC Stress - custom snapshot"]},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - internal snapshot",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - debug",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - custom snapshot - debug",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - debug - header includes",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - shared",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - verify csa",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Fuchsia - debug builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "target_platform": "fuchsia", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Presubmit",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by_gitiles = True,
    executable = {"name": "v8/presubmit"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win32 - builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "triggers": ["V8 Win32"], "mastername": "client.v8", "binary_size_tracking": {"category": "win32", "binary": "d8.exe"}},
    use_goma = GOMA.AST,
)
v8_branch_coverage_builder(
    name = "V8 Win32 - debug builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8", "triggers": ["V8 Win32 - debug"]},
    use_goma = GOMA.AST,
)
v8_branch_coverage_builder(
    name = "V8 Win32",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win32 - debug",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win64",
    triggered_by_gitiles = True,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "binary_size_tracking": {"category": "win64", "binary": "d8.exe"}},
    use_goma = GOMA.AST,
)
v8_branch_coverage_builder(
    name = "V8 Win64 - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8"},
    use_goma = GOMA.AST,
)
v8_branch_coverage_builder(
    name = "V8 Win64 - msvc",
    triggered_by_gitiles = True,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "use_goma": False, "mastername": "client.v8"},
    use_goma = GOMA.AST,
)
v8_branch_coverage_builder(
    name = "V8 Mac64",
    triggered_by_gitiles = True,
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "binary_size_tracking": {"category": "mac64", "binary": "d8"}},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Mac64 - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - gc stress",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 GC Stress - custom snapshot",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Mac64 GC Stress",
    triggered_by_gitiles = True,
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 ASAN",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN - builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "triggers": ["V8 Linux64 TSAN", "V8 Linux64 TSAN - concurrent marking", "V8 Linux64 TSAN - isolates"]},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN - concurrent marking",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN - isolates",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - CFI",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - MSAN",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "set_gclient_var": "checkout_instrumented_libraries"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Mac64 ASAN",
    triggered_by_gitiles = True,
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Win64 ASAN",
    triggered_by_gitiles = True,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.AST,
)
v8_branch_coverage_builder(
    name = "V8 Fuzzer",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux gcc",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
    use_goma = GOMA.NO,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 gcc - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
    use_goma = GOMA.NO,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - cfi",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 UBSan",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - pointer compression",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - arm64 - sim - pointer compression - builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8", "triggers": ["V8 Linux64 - arm64 - sim - pointer compression"]},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - arm64 - sim - pointer compression",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - vtunejit",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "checkout_ittapi"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - full debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Arm - builder",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Arm"], "mastername": "client.v8.ports", "target_arch": "arm", "build_config": "Release", "binary_size_tracking": {"category": "linux_arm32", "binary": "d8"}},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Arm - debug builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "triggers": ["V8 Arm - debug", "V8 Arm GC Stress"], "target_arch": "arm", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Android Arm - builder",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.ports", "target_arch": "arm", "build_config": "Release", "target_platform": "android", "binary_size_tracking": {"category": "android_arm32", "binary": "d8"}},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim - lite",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim - lite - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Arm",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 28800,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm - debug",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 27000,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm GC Stress",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 30600,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm64 - builder",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_arch": "arm", "target_bits": 64, "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Android Arm64 - builder",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Android Arm64 - N5X"], "mastername": "client.v8.ports", "target_arch": "arm", "build_config": "Release", "target_platform": "android", "binary_size_tracking": {"category": "android_arm64", "binary": "d8"}},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Android Arm64 - debug builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "target_platform": "android", "target_arch": "arm", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Android Arm64 - N5X",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - debug",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - gc stress",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 23400,
    properties = {"build_config": "Debug", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - mipsel - sim - builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8.ports", "triggers": ["V8 Linux - mipsel - sim"]},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - mips64el - sim - builder",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "mastername": "client.v8.ports", "triggers": ["V8 Linux - mips64el - sim"]},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - mipsel - sim",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - mips64el - sim",
    triggered_by_gitiles = False,
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - ppc64 - sim",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 19800,
    properties = {"build_config": "Release", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
v8_branch_coverage_builder(
    name = "V8 Linux - s390x - sim",
    triggered_by_gitiles = True,
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 19800,
    properties = {"build_config": "Release", "mastername": "client.v8.ports"},
    use_goma = GOMA.DEFAULT,
)
