# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "ci_pair_factory", "greedy_batching_of_1", "in_branch_console", "main_multibranch_builder")

in_category = in_branch_console("main")
main_multibranch_builder_pair = ci_pair_factory(main_multibranch_builder)

in_category(
    "Linux",
    main_multibranch_builder_pair(
        name = "V8 Linux",
        triggering_policy = greedy_batching_of_1,
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "linux32", "binary": "d8"}},
        gclient_vars = [GCLIENT_VARS.GCMOLE],
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux - debug",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux - full debug",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
        close_tree = False,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - heap sandbox - debug",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        first_branch_version = "10.4",
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux - shared",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "linux32", "binary": "libv8.so"}},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - no wasm - builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"track_build_dependencies": True, "binary_size_tracking": {"category": "linux64_no_wasm", "binary": "d8"}},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux - noi18n - debug",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux - verify csa",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    # this is only a builder
    main_multibranch_builder(
        name = "V8 Linux - vtunejit",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.ITTAPI],
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Linux64",
    main_multibranch_builder(
        name = "V8 Linux64 - builder",
        triggering_policy = greedy_batching_of_1,
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"track_build_dependencies": True, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - debug builder",
        triggering_policy = greedy_batching_of_1,
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.JSFUNFUZZ],
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - custom snapshot - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64",
        parent_builder = "V8 Linux64 - builder",
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - internal snapshot",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - debug",
        parent_builder = "V8 Linux64 - debug builder",
    ),
    main_multibranch_builder(
        name = "V8 Linux64 - custom snapshot - debug",
        parent_builder = "V8 Linux64 - custom snapshot - debug builder",
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - debug - header includes",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - shared",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - verify csa",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - pointer compression",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Fuchsia",
    main_multibranch_builder(
        name = "V8 Fuchsia - builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"target_platform": "fuchsia"},
        use_goma = GOMA.DEFAULT,
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Fuchsia - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"target_platform": "fuchsia"},
        use_goma = GOMA.DEFAULT,
        close_tree = False,
    ),
)

in_category(
    "Windows",
    main_multibranch_builder(
        name = "V8 Win32 - builder",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "win32", "binary": "d8.exe"}},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder(
        name = "V8 Win32 - debug builder",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder(
        name = "V8 Win32",
        parent_builder = "V8 Win32 - builder",
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Win32 - debug",
        parent_builder = "V8 Win32 - debug builder",
        close_tree = False,
    ),
    main_multibranch_builder_pair(
        name = "V8 Win64",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "win64", "binary": "d8.exe"}},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder_pair(
        name = "V8 Win64 - debug",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        use_goma = GOMA.ATS,
    ),
    main_multibranch_builder_pair(
        name = "V8 Win64 - msvc",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"use_goma": False},
        use_goma = GOMA.NO,
        close_tree = False,
    ),
)

in_category(
    "Mac",
    main_multibranch_builder(
        name = "V8 Mac64 - builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"binary_size_tracking": {"category": "mac64", "binary": "d8"}},
    ),
    main_multibranch_builder(
        name = "V8 Mac64",
        parent_builder = "V8 Mac64 - builder",
    ),
    main_multibranch_builder(
        name = "V8 Mac64 - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    ),
    main_multibranch_builder(
        name = "V8 Mac64 - debug",
        parent_builder = "V8 Mac64 - debug builder",
    ),
    main_multibranch_builder(
        name = "V8 Mac - arm64 - release builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    ),
    #TODO(liviurau): Naming pattern mismatch prevents the use of `_pair` here
    main_multibranch_builder(
        name = "V8 Mac - arm64 - release",
        parent_builder = "V8 Mac - arm64 - release builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        close_tree = True,
    ),
    main_multibranch_builder(
        name = "V8 Mac - arm64 - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    ),
    #TODO(liviurau): Naming pattern mismatch prevents the use of `_pair` here
    main_multibranch_builder(
        name = "V8 Mac - arm64 - debug",
        parent_builder = "V8 Mac - arm64 - debug builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        close_tree = True,
    ),
    main_multibranch_builder(
        name = "V8 Mac - arm64 - sim - release builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    ),
    main_multibranch_builder(
        name = "V8 Mac - arm64 - sim - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    ),
)

in_category(
    "GCStress",
    main_multibranch_builder(
        name = "V8 Linux - gc stress",
        parent_builder = "V8 Linux - debug builder",
    ),
    main_multibranch_builder(
        name = "V8 Linux64 GC Stress - custom snapshot",
        parent_builder = "V8 Linux64 - custom snapshot - debug builder",
    ),
    main_multibranch_builder(
        name = "V8 Mac64 GC Stress",
        parent_builder = "V8 Mac64 - debug builder",
    ),
)

in_category(
    "Misc",
    main_multibranch_builder(
        name = "V8 Presubmit",
        triggering_policy = greedy_batching_of_1,
        executable = "recipe:v8/presubmit",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.NONE,
    ),
    main_multibranch_builder(
        name = "V8 Fuzzer",
        parent_builder = "V8 Linux64 - debug builder",
        execution_timeout = 19800,
        close_tree = False,
    ),
    main_multibranch_builder(
        name = "V8 Test Tools",
        executable = "recipe:v8/test_tools",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    ),
)
