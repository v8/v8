# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GOMA", "in_console", "v8_builder")

in_category = in_console("clusterfuzz")

in_category(
    "Windows",
    v8_builder(
        name = "V8 Clusterfuzz Win64 ASAN - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bitness": "64", "bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.AST,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Win64 ASAN - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bitness": "64", "bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.AST,
    ),
)

in_category(
    "Mac",
    v8_builder(
        name = "V8 Clusterfuzz Mac64 ASAN - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"]},
        caches = [
            swarming.cache(
                path = "osx_sdk",
                name = "osx_sdk",
            ),
        ],
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Mac64 ASAN - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"]},
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
    "Linux",
    v8_builder(
        name = "V8 Clusterfuzz Linux64 - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"triggers": ["V8 NumFuzz"], "mastername": "client.v8.clusterfuzz", "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"triggers": ["V8 NumFuzz - debug"], "mastername": "client.v8.clusterfuzz", "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 ASAN no inline - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan-no-inline"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 ASAN - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 ASAN arm64 - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-arm64-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux ASAN arm - debug builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-arm-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux MSAN no origins",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "set_gclient_var": "checkout_instrumented_libraries", "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "clusterfuzz_archive": {"bucket": "v8-msan", "name": "d8-msan-no-origins"}},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux MSAN chained origins",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "set_gclient_var": "checkout_instrumented_libraries", "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "clusterfuzz_archive": {"bucket": "v8-msan", "name": "d8-msan-chained-origins"}},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 CFI - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-cfi", "name": "d8-cfi"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 TSAN - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"build_config": "Release", "default_targets": ["v8_clusterfuzz"], "mastername": "client.v8.clusterfuzz", "triggers": ["V8 NumFuzz - TSAN"]},
        use_goma = GOMA.DEFAULT,
    ),
    v8_builder(
        name = "V8 Clusterfuzz Linux64 UBSan - release builder",
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-ubsan", "name": "d8-ubsan"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"]},
        use_goma = GOMA.DEFAULT,
    ),
)

in_category(
    "Fuzzers",
    v8_builder(
        name = "V8 NumFuzz",
        bucket = "ci",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"mastername": "client.v8.clusterfuzz"},
    ),
    v8_builder(
        name = "V8 NumFuzz - debug",
        bucket = "ci",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"mastername": "client.v8.clusterfuzz"},
    ),
    v8_builder(
        name = "V8 NumFuzz - TSAN",
        bucket = "ci",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"mastername": "client.v8.clusterfuzz"},
    ),
)
