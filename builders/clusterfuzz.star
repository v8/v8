# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "greedy_batching_of_1", "in_console", "v8_builder")

def clusterfuzz_builder(properties, close_tree = True, use_goma = GOMA.DEFAULT, **kwargs):
    properties["builder_group"] = "client.v8.clusterfuzz"
    properties["default_targets"] = ["v8_clusterfuzz"]
    return v8_builder(
        bucket = "ci",
        close_tree = close_tree,
        properties = properties,
        triggered_by = ["v8-trigger"],
        triggering_policy = greedy_batching_of_1,
        use_goma = use_goma,
        **kwargs
    )

in_category = in_console("clusterfuzz")

in_category(
    "Windows",
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Win64 ASAN - release builder",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bitness": "64", "bucket": "v8-asan", "name": "d8-asan"}},
        use_goma = GOMA.ATS,
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Win64 ASAN - debug builder",
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bitness": "64", "bucket": "v8-asan", "name": "d8-asan"}},
        use_goma = GOMA.ATS,
    ),
)

in_category(
    "Mac",
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Mac64 ASAN - release builder",
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Mac64 ASAN - debug builder",
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}},
    ),
)

in_category(
    "Linux",
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8"}},
        use_goma = GOMA.DEFAULT,
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8"}},
        use_goma = GOMA.DEFAULT,
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 ASAN - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 ASAN arm64 - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-arm64-asan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 ASAN no inline - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan-no-inline"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 CFI - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-cfi", "name": "d8-cfi"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 TSAN - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clusterfuzz_archive": {"bucket": "v8-tsan", "name": "d8-tsan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 UBSan - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-ubsan", "name": "d8-ubsan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clusterfuzz_archive": {"bitness": "32", "bucket": "v8-asan", "name": "d8"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux ASAN - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bitness": "32", "bucket": "v8-asan", "name": "d8-asan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux ASAN arm - debug builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-arm-asan"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux ASAN no inline - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bitness": "32", "bucket": "v8-asan", "name": "d8-asan-no-inline"}},
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux MSAN chained origins",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-msan", "name": "d8-msan-chained-origins"}},
        gclient_vars = [GCLIENT_VARS.INSTRUMENTED_LIBRARIES],
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux MSAN no origins",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-msan", "name": "d8-msan-no-origins"}},
        gclient_vars = [GCLIENT_VARS.INSTRUMENTED_LIBRARIES],
    ),
    clusterfuzz_builder(
        name = "V8 Clusterfuzz Linux64 ASAN sandbox testing - release builder",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan-sandbox-testing"}},
    ),
)

in_category(
    "Fuzzers",
    v8_builder(
        name = "V8 NumFuzz",
        parent_builder = "V8 Clusterfuzz Linux64 - release builder",
        bucket = "ci",
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8.clusterfuzz", "disable_auto_bisect": True},
        close_tree = False,
        notifies = ["NumFuzz maintainer"],
    ),
    v8_builder(
        name = "V8 NumFuzz - debug",
        parent_builder = "V8 Clusterfuzz Linux64 - debug builder",
        bucket = "ci",
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8.clusterfuzz", "disable_auto_bisect": True},
        close_tree = False,
        notifies = ["NumFuzz maintainer"],
    ),
    v8_builder(
        name = "V8 NumFuzz - TSAN",
        parent_builder = "V8 Clusterfuzz Linux64 TSAN - release builder",
        bucket = "ci",
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8.clusterfuzz", "disable_auto_bisect": True},
        close_tree = False,
        notifies = ["NumFuzz maintainer"],
    ),
)
