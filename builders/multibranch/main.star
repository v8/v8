# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "in_branch_console", "multibranch_builder", "v8_builder")

def main_multibranch_builder(**kwargs):
    props = kwargs.pop("properties", {})
    props["builder_group"] = "client.v8"
    kwargs["properties"] = props
    return multibranch_builder(**kwargs)

def main_multibranch_builder_pair(name, dimensions, builder_group = "client.v8", use_goma = GOMA.DEFAULT):
    return (
        main_multibranch_builder(
            name = name + " - builder",
            dimensions = dimensions,
            properties = {"builder_group": builder_group, "triggers": [name]},
            use_goma = use_goma,
        ) +
        main_multibranch_builder(
            name = name,
            triggered_by_gitiles = False,
            dimensions = {"host_class": "multibot"},
            properties = {"builder_group": builder_group},
        )
    )

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
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "triggers": ["V8 Linux64", "V8 Linux64 - fyi"], "track_build_dependencies": True, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
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
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug", "V8 Linux64 - debug - fyi"]},
        gclient_vars = [GCLIENT_VARS.JSFUNFUZZ],
        use_goma = GOMA.DEFAULT,
        in_console = "main/Linux64",
    ),
    v8_builder(
        name = "V8 Fuchsia - builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "target_platform": "fuchsia", "triggers": ["V8 Fuchsia"]},
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
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "triggers": ["V8 Linux64"], "track_build_dependencies": True, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
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
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug"]},
        gclient_vars = [GCLIENT_VARS.JSFUNFUZZ],
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.beta/Linux64",
    ),
    v8_builder(
        name = "V8 Fuchsia - builder",
        bucket = "ci.br.beta",
        triggered_by = ["v8-trigger-br-beta"],
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"target_platform": "fuchsia", "builder_group": "client.v8"},
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
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "triggers": ["V8 Linux64"], "track_build_dependencies": True, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
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
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug"]},
        gclient_vars = [GCLIENT_VARS.JSFUNFUZZ],
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.stable/Linux64",
    ),
    v8_builder(
        name = "V8 Fuchsia - builder",
        bucket = "ci.br.stable",
        triggered_by = ["v8-trigger-br-stable"],
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"target_platform": "fuchsia", "builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notifies = ["beta/stable notifier"],
        in_console = "br.stable/Fuchsia",
    ),
)

in_category = in_branch_console("main")

in_category(
    "Linux",
    main_multibranch_builder(
        name = "V8 Linux - builder",
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux"], "binary_size_tracking": {"category": "linux32", "binary": "d8"}},
        gclient_vars = [GCLIENT_VARS.GCMOLE],
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux - debug builder",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux - debug", "V8 Linux - gc stress"]},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux - full debug",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Linux - shared",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "linux32", "binary": "libv8.so"}},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux - noi18n - debug",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux - verify csa",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux - vtunejit",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.ITTAPI],
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Linux64",
    main_multibranch_builder(
        name = "V8 Linux64 - custom snapshot - debug builder",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux64 - custom snapshot - debug", "V8 Linux64 GC Stress - custom snapshot"]},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - internal snapshot",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - custom snapshot - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - debug - header includes",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - shared",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - verify csa",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - pointer compression",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Fuchsia",
    main_multibranch_builder(
        name = "V8 Fuchsia - debug builder",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"target_platform": "fuchsia", "builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Windows",
    main_multibranch_builder(
        name = "V8 Win32 - builder",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Win32"], "binary_size_tracking": {"category": "win32", "binary": "d8.exe"}},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder(
        name = "V8 Win32 - debug builder",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Win32 - debug"]},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder(
        name = "V8 Win32",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Win32 - debug",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Win64",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "win64", "binary": "d8.exe"}},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder(
        name = "V8 Win64 - debug",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder(
        name = "V8 Win64 - msvc",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"use_goma": False, "builder_group": "client.v8"},
        use_goma = GOMA.NO,
    ),
)

in_category(
    "Mac",
    main_multibranch_builder(
        name = "V8 Mac64",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "mac64", "binary": "d8"}},
    ),
    main_multibranch_builder(
        name = "V8 Mac64 - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
    ),
)

in_category(
    "GCStress",
    main_multibranch_builder(
        name = "V8 Linux - gc stress",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux64 GC Stress - custom snapshot",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Mac64 GC Stress",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
    ),
)

in_category(
    "Sanitizers",
    main_multibranch_builder(
        name = "V8 Linux64 ASAN",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - cfi",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 TSAN - builder",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"triggers": ["V8 Linux64 TSAN", "V8 Linux64 TSAN - stress-incremental-marking", "V8 Linux64 TSAN - isolates"]},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 TSAN",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    multibranch_builder(
        name = "V8 Linux64 TSAN - stress-incremental-marking",
        triggered_by_gitiles = False,
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Linux64 TSAN - isolates",
        triggered_by_gitiles = False,
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 TSAN - no-concurrent-marking",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    ),
    main_multibranch_builder(
        name = "V8 Linux - arm64 - sim - CFI",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux - arm64 - sim - MSAN",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.INSTRUMENTED_LIBRARIES],
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 UBSan",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Mac64 ASAN",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
    ),
    main_multibranch_builder(
        name = "V8 Win64 ASAN",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.ATS,
        close_tree = False,
    ),
)

in_category(
    "Misc",
    main_multibranch_builder(
        name = "V8 Presubmit",
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        executable = "recipe:v8/presubmit",
        dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NONE,
    ),
    main_multibranch_builder(
        name = "V8 Fuzzer",
        triggered_by_gitiles = False,
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Linux gcc",
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
        use_goma = GOMA.NO,
    ),
)
