# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "RECLIENT", "ci_pair_factory", "in_branch_console", "main_multibranch_builder", "multibranch_builder")

in_category = in_branch_console("memory")
main_multibranch_builder_pair = ci_pair_factory(main_multibranch_builder)

in_category(
    "ASAN",
    main_multibranch_builder_pair(
        name = "V8 Linux64 ASAN",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Mac64 ASAN",
        triggered_by_gitiles = True,
        dimensions = {"os": "Mac", "cpu": "x86-64"},
    ),
    main_multibranch_builder_pair(
        name = "V8 Win64 ASAN",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
        close_tree = False,
    ),
)

in_category(
    "MSAN",
    main_multibranch_builder_pair(
        name = "V8 Linux - arm64 - sim - MSAN",
        dimensions = {"os": "Ubuntu-20.04", "cpu": "x86-64"},
        gclient_vars = [GCLIENT_VARS.INSTRUMENTED_LIBRARIES],
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
    ),
)

in_category(
    "TSAN",
    main_multibranch_builder(
        name = "V8 Linux64 TSAN - builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
    ),
    main_multibranch_builder(
        name = "V8 Linux64 TSAN",
        parent_builder = "V8 Linux64 TSAN - builder",
    ),
    main_multibranch_builder(
        name = "V8 Linux64 TSAN - isolates",
        parent_builder = "V8 Linux64 TSAN - builder",
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 TSAN - no-concurrent-marking",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    ),
    multibranch_builder(
        name = "V8 Linux64 TSAN - stress-incremental-marking",
        parent_builder = "V8 Linux64 TSAN - builder",
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
    ),
)

in_category(
    "UBSAN",
    main_multibranch_builder_pair(
        name = "V8 Linux64 UBSan",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
    ),
)

in_category(
    "CFI",
    main_multibranch_builder_pair(
        name = "V8 Linux - arm64 - sim - CFI",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
    ),
    main_multibranch_builder_pair(
        name = "V8 Linux64 - cfi",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.DEFAULT,
    ),
)
