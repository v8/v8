# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GOMA", "branch_coverage_builder", "in_branch_console", "v8_builder")

def exceptions(*args):
    # foldable wrapper
    pass

exceptions(
    # These builders have some irregularities between branches
    v8_builder(
        name = "V8 Linux64 - builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux64", "V8 Linux64 - fyi"], "mastername": "client.v8", "track_build_dependencies": True, "build_config": "Release", "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
        use_goma = GOMA.DEFAULT,
        in_console = "main/Linux64",
    ),
    v8_builder(
        name = "V8 Linux64 - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "download_jsfunfuzz", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug", "V8 Linux64 - debug - fyi"]},
        use_goma = GOMA.DEFAULT,
        in_console = "main/Linux64",
    ),
    v8_builder(
        name = "V8 Fuchsia - builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "target_platform": "fuchsia", "mastername": "client.v8", "triggers": ["V8 Fuchsia"]},
        use_goma = GOMA.DEFAULT,
        in_console = "main/Fuchsia",
    ),
    v8_builder(
        name = "V8 Linux64 - builder",
        bucket = "ci.br.beta",
        triggered_by = ["v8-trigger-br-beta"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux64"], "mastername": "client.v8", "track_build_dependencies": True, "build_config": "Release", "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.beta/Linux64",
    ),
    v8_builder(
        name = "V8 Linux64 - debug builder",
        bucket = "ci.br.beta",
        triggered_by = ["v8-trigger-br-beta"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "download_jsfunfuzz", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug"]},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.beta/Linux64",
    ),
    v8_builder(
        name = "V8 Fuchsia - builder",
        bucket = "ci.br.beta",
        triggered_by = ["v8-trigger-br-beta"],
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "target_platform": "fuchsia", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.beta/Fuchsia",
    ),
    v8_builder(
        name = "V8 Linux64 - builder",
        bucket = "ci.br.stable",
        triggered_by = ["v8-trigger-br-stable"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux64"], "mastername": "client.v8", "track_build_dependencies": True, "build_config": "Release", "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.stable/Linux64",
    ),
    v8_builder(
        name = "V8 Linux64 - debug builder",
        bucket = "ci.br.stable",
        triggered_by = ["v8-trigger-br-stable"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "download_jsfunfuzz", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug"]},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.stable/Linux64",
    ),
    v8_builder(
        name = "V8 Fuchsia - builder",
        bucket = "ci.br.stable",
        triggered_by = ["v8-trigger-br-stable"],
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "target_platform": "fuchsia", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.stable/Fuchsia",
    ),
)

in_category = in_branch_console("main")

in_category(
    "Linux",
    branch_coverage_builder(
        name = "V8 Linux - builder",
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux"], "mastername": "client.v8", "set_gclient_var": "download_gcmole", "build_config": "Release", "binary_size_tracking": {"category": "linux32", "binary": "d8"}},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "triggers": ["V8 Linux - debug", "V8 Linux - gc stress"]},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux - full debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux - shared",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8", "binary_size_tracking": {"category": "linux32", "binary": "libv8.so"}},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux - noi18n - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux - verify csa",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux - vtunejit",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "checkout_ittapi"},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Linux64",
    branch_coverage_builder(
        name = "V8 Linux64 - custom snapshot - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "triggers": ["V8 Linux64 - custom snapshot - debug", "V8 Linux64 GC Stress - custom snapshot"]},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - internal snapshot",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - custom snapshot - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - debug - header includes",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - shared",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - verify csa",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - pointer compression",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Fuchsia",
    branch_coverage_builder(
        name = "V8 Fuchsia - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "target_platform": "fuchsia", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Windows",
    branch_coverage_builder(
        name = "V8 Win32 - builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"build_config": "Release", "triggers": ["V8 Win32"], "mastername": "client.v8", "binary_size_tracking": {"category": "win32", "binary": "d8.exe"}},
        use_goma = GOMA.AST,
    ),
    branch_coverage_builder(
        name = "V8 Win32 - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "triggers": ["V8 Win32 - debug"]},
        use_goma = GOMA.AST,
    ),
    branch_coverage_builder(
        name = "V8 Win32",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Win32 - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Win64",
        triggered_by_gitiles = True,
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8", "binary_size_tracking": {"category": "win64", "binary": "d8.exe"}},
        use_goma = GOMA.AST,
    ),
    branch_coverage_builder(
        name = "V8 Win64 - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8"},
        use_goma = GOMA.AST,
    ),
    branch_coverage_builder(
        name = "V8 Win64 - msvc",
        triggered_by_gitiles = True,
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"build_config": "Release", "use_goma": False, "mastername": "client.v8"},
        use_goma = GOMA.AST,
    ),
)

in_category(
    "Mac",
    branch_coverage_builder(
        name = "V8 Mac64",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8", "binary_size_tracking": {"category": "mac64", "binary": "d8"}},
        caches = [
            swarming.cache(
                path = "osx_sdk",
                name = "osx_sdk",
            ),
        ],
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Mac64 - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8"},
        caches = [
            swarming.cache(
                path = "osx_sdk",
                name = "osx_sdk",
            ),
        ],
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "GCStress",
    branch_coverage_builder(
        name = "V8 Linux - gc stress",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux64 GC Stress - custom snapshot",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Mac64 GC Stress",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8"},
        caches = [
            swarming.cache(
                path = "osx_sdk",
                name = "osx_sdk",
            ),
        ],
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Sanitizers",
    branch_coverage_builder(
        name = "V8 Linux64 ASAN",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 - cfi",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 TSAN - builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8", "triggers": ["V8 Linux64 TSAN", "V8 Linux64 TSAN - concurrent marking", "V8 Linux64 TSAN - isolates"]},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 TSAN",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux64 TSAN - concurrent marking",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux64 TSAN - isolates",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux - arm64 - sim - CFI",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux - arm64 - sim - MSAN",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8", "set_gclient_var": "checkout_instrumented_libraries"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 UBSan",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Mac64 ASAN",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        caches = [
            swarming.cache(
                path = "osx_sdk",
                name = "osx_sdk",
            ),
        ],
        use_goma = GOMA.DEFAULT,
    ),
    branch_coverage_builder(
        name = "V8 Win64 ASAN",
        triggered_by_gitiles = True,
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8"},
        use_goma = GOMA.AST,
    ),
)

in_category(
    "Misc",
    branch_coverage_builder(
        name = "V8 Presubmit",
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        triggered_by_gitiles = True,
        executable = {"name": "v8/presubmit"},
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Fuzzer",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"mastername": "client.v8"},
    ),
    branch_coverage_builder(
        name = "V8 Linux gcc",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
        use_goma = GOMA.NO,
    ),
    branch_coverage_builder(
        name = "V8 Linux64 gcc - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Debug", "mastername": "client.v8", "set_gclient_var": "check_v8_header_includes"},
        use_goma = GOMA.NO,
    ),
)
