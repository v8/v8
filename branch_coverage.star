load("//lib.star", "v8_branch_coverage_builder")

v8_branch_coverage_builder(
    name = "V8 Linux - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Linux"], "mastername": "client.v8", "set_gclient_var": "download_gcmole", "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "binary_size_tracking": {"category": "linux32", "binary": "d8"}},
)
v8_branch_coverage_builder(
    name = "V8 Linux - debug builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "triggers": ["V8 Linux - debug", "V8 Linux - gc stress"]},
)
v8_branch_coverage_builder(
    name = "V8 Linux",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - debug",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - shared",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "binary_size_tracking": {"category": "linux32", "binary": "libv8.so"}},
)
v8_branch_coverage_builder(
    name = "V8 Linux - noi18n - debug",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - verify csa",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - custom snapshot - debug builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "triggers": ["V8 Linux64 - custom snapshot - debug", "V8 Linux64 GC Stress - custom snapshot"]},
)
v8_branch_coverage_builder(
    name = "V8 Linux64",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - internal snapshot",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - debug",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - custom snapshot - debug",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - debug - header includes",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - shared",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - verify csa",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Fuchsia - debug builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "target_platform": "fuchsia", "mastername": "client.v8", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_branch_coverage_builder(
    name = "V8 Presubmit",
    executable = {"name": "v8/presubmit"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win32 - builder",
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "triggers": ["V8 Win32"], "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8", "binary_size_tracking": {"category": "win32", "binary": "d8.exe"}},
)
v8_branch_coverage_builder(
    name = "V8 Win32 - debug builder",
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8", "triggers": ["V8 Win32 - debug"]},
)
v8_branch_coverage_builder(
    name = "V8 Win32",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win32 - debug",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win64",
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8", "binary_size_tracking": {"category": "win64", "binary": "d8.exe"}},
)
v8_branch_coverage_builder(
    name = "V8 Win64 - debug",
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Win64 - msvc",
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "use_goma": False, "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Mac64",
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "binary_size_tracking": {"category": "mac64", "binary": "d8"}},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_branch_coverage_builder(
    name = "V8 Mac64 - debug",
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_branch_coverage_builder(
    name = "V8 Linux - gc stress",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 GC Stress - custom snapshot",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Mac64 GC Stress",
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_branch_coverage_builder(
    name = "V8 Linux64 ASAN",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "triggers": ["V8 Linux64 TSAN", "V8 Linux64 TSAN - concurrent marking", "V8 Linux64 TSAN - isolates"]},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN - concurrent marking",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 TSAN - isolates",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - CFI",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - MSAN",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "set_gclient_var": "checkout_instrumented_libraries"},
)
v8_branch_coverage_builder(
    name = "V8 Mac64 ASAN",
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_branch_coverage_builder(
    name = "V8 Win64 ASAN",
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Fuzzer",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux gcc",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "use_goma": False, "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 gcc - debug",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "use_goma": False, "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - cfi",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 UBSan",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - pointer compression",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - arm64 - sim - pointer compression - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "triggers": ["V8 Linux64 - arm64 - sim - pointer compression"]},
)
v8_branch_coverage_builder(
    name = "V8 Linux64 - arm64 - sim - pointer compression",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - vtunejit",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "set_gclient_var": "checkout_ittapi"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - full debug",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_branch_coverage_builder(
    name = "V8 Arm - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Arm"], "mastername": "client.v8.ports", "target_arch": "arm", "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "binary_size_tracking": {"category": "linux_arm32", "binary": "d8"}},
)
v8_branch_coverage_builder(
    name = "V8 Arm - debug builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "triggers": ["V8 Arm - debug", "V8 Arm GC Stress"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "target_arch": "arm", "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Android Arm - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports", "target_arch": "arm", "build_config": "Release", "target_platform": "android", "binary_size_tracking": {"category": "android_arm32", "binary": "d8"}},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim - debug",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim - lite",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm - sim - lite - debug",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 28800,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm - debug",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 27000,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm GC Stress",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 30600,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Arm64 - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "target_arch": "arm", "target_bits": 64, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Android Arm64 - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Android Arm64 - N5X"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports", "target_arch": "arm", "build_config": "Release", "target_platform": "android", "binary_size_tracking": {"category": "android_arm64", "binary": "d8"}},
)
v8_branch_coverage_builder(
    name = "V8 Android Arm64 - debug builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "target_platform": "android", "target_arch": "arm", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Android Arm64 - N5X",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - debug",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - arm64 - sim - gc stress",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 23400,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - mipsel - sim - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports", "triggers": ["V8 Linux - mipsel - sim"]},
)
v8_branch_coverage_builder(
    name = "V8 Linux - mips64el - sim - builder",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports", "triggers": ["V8 Linux - mips64el - sim"]},
)
v8_branch_coverage_builder(
    name = "V8 Linux - mipsel - sim",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - mips64el - sim",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - ppc64 - sim",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 19800,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
v8_branch_coverage_builder(
    name = "V8 Linux - s390x - sim",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 19800,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.ports"},
)
