load("//lib.star", "v8_builder")

v8_builder(
    name = "V8 Linux64 - builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Linux64", "V8 Linux64 - fyi"], "mastername": "client.v8", "track_build_dependencies": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
)
v8_builder(
    name = "V8 Linux64 - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "set_gclient_var": "download_jsfunfuzz", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug", "V8 Linux64 - debug - fyi"]},
)
v8_builder(
    name = "V8 Fuchsia - builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "fuchsia", "mastername": "client.v8", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "triggers": ["V8 Fuchsia"]},
)
v8_builder(
    name = "V8 Linux64 - pointer compression without dchecks",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_builder(
    name = "V8 iOS - sim",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.14", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$depot_tools/osx_sdk": {"sdk_version": "11b52"}, "target_platform": "ios", "mastername": "client.v8", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Linux64 - debug - perfetto - builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "triggers": ["V8 Linux64 - debug - perfetto"]},
)
v8_builder(
    name = "V8 Linux64 - debug - perfetto",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_builder(
    name = "V8 Linux64 - Fuzzilli",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_builder(
    name = "V8 Linux64 - fyi",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_builder(
    name = "V8 Linux64 - debug - fyi",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8"},
)
v8_builder(
    name = "V8 Linux64 - gcov coverage",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"use_goma": False, "enable_swarming": False, "mastername": "client.v8", "clobber": True, "coverage": "gcov", "build_config": "Release"},
)
v8_builder(
    name = "V8 Linux - predictable",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_builder(
    name = "V8 Linux64 - reverse jsargs",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
)
v8_builder(
    name = "V8 Fuchsia",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    properties = {"mastername": "client.v8"},
)
v8_builder(
    name = "V8 Mac64 - full debug",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Blink Mac",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    execution_timeout = 10800,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Blink Win",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 10800,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Blink Linux",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_concurrent_invocations = 3,
        max_batch_size = 1,
    ),
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Blink Linux Debug",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.LOGARITHMIC_BATCHING_KIND,
        log_base = 2.0,
    ),
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Blink Linux Future",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "Linux Debug Builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"host_class": "large_disk", "os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Linux GN",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Android GN (dbg)",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "Linux ASAN Builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"host_class": "large_disk", "os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 18000,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "Win V8 FYI Release (NVIDIA)",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 10800,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "Mac V8 FYI Release (Intel)",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "Linux V8 FYI Release (NVIDIA)",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "Linux V8 FYI Release - pointer compression (NVIDIA)",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "Android V8 FYI Release (Nexus 5X)",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "chromium_integration"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 10800,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Linux64 - node.js integration ng",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    executable = {"name": "v8/node_integration_ng"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "v8_tot": True, "mastername": "client.v8.fyi"},
)
v8_builder(
    name = "V8 Clusterfuzz Win64 ASAN - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bitness": "64", "bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Win64 ASAN - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bitness": "64", "bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Mac64 ASAN - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Clusterfuzz Mac64 ASAN - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 NumFuzz"], "mastername": "client.v8.clusterfuzz", "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 NumFuzz - debug"], "mastername": "client.v8.clusterfuzz", "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 ASAN no inline - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan-no-inline"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 ASAN - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 ASAN arm64 - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-arm64-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux ASAN arm - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-asan", "name": "d8-arm-asan"}, "build_config": "Debug", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux MSAN no origins",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "set_gclient_var": "checkout_instrumented_libraries", "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "clusterfuzz_archive": {"bucket": "v8-msan", "name": "d8-msan-no-origins"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux MSAN chained origins",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "set_gclient_var": "checkout_instrumented_libraries", "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "clusterfuzz_archive": {"bucket": "v8-msan", "name": "d8-msan-chained-origins"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 CFI - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-cfi", "name": "d8-cfi"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 TSAN - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.clusterfuzz", "triggers": ["V8 NumFuzz - TSAN"]},
)
v8_builder(
    name = "V8 Clusterfuzz Linux64 UBSan - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"mastername": "client.v8.clusterfuzz", "clobber": True, "clusterfuzz_archive": {"bucket": "v8-ubsan", "name": "d8-ubsan"}, "build_config": "Release", "default_targets": ["v8_clusterfuzz"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 NumFuzz",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.clusterfuzz"},
)
v8_builder(
    name = "V8 NumFuzz - debug",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.clusterfuzz"},
)
v8_builder(
    name = "V8 NumFuzz - TSAN",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"mastername": "client.v8.clusterfuzz"},
)
v8_builder(
    name = "Linux - Future",
    bucket = "ci",
    triggered_by = ["chromium-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.chromium"},
)
v8_builder(
    name = "Linux - Future (dbg)",
    bucket = "ci",
    triggered_by = ["chromium-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.chromium"},
)
v8_builder(
    name = "Linux V8 API Stability",
    bucket = "ci",
    triggered_by = ["chromium-trigger"],
    executable = {"name": "chromium"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.chromium"},
)
v8_builder(
    name = "V8 Official Arm32",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 32, "target_arch": "arm"},
)
v8_builder(
    name = "V8 Official Arm64",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64, "target_arch": "arm"},
)
v8_builder(
    name = "V8 Official Android Arm32",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 32, "build_config": "Release", "target_platform": "android", "target_arch": "arm"},
)
v8_builder(
    name = "V8 Official Android Arm64",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64, "build_config": "Release", "target_platform": "android", "target_arch": "arm"},
)
v8_builder(
    name = "V8 Official Linux32",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 32},
)
v8_builder(
    name = "V8 Official Linux32 Debug",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 32},
)
v8_builder(
    name = "V8 Official Linux64",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64},
)
v8_builder(
    name = "V8 Official Linux64 Debug",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64},
)
v8_builder(
    name = "V8 Official Win32",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 32},
)
v8_builder(
    name = "V8 Official Win32 Debug",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 32},
)
v8_builder(
    name = "V8 Official Win64",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64},
)
v8_builder(
    name = "V8 Official Win64 Debug",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64},
)
v8_builder(
    name = "V8 Official Mac64",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Official Mac64 Debug",
    bucket = "ci",
    triggered_by = ["v8-trigger-official"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    executable = {"name": "v8/archive"},
    dimensions = {"os": "Mac-10.13", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.official", "target_bits": 64},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Arm - builder - perf",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "target_arch": "arm", "mastername": "client.v8.perf"},
)
v8_builder(
    name = "V8 Arm64 - builder - perf",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "mastername": "client.v8.perf", "target_arch": "arm", "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "target_bits": 64},
)
v8_builder(
    name = "V8 Android Arm - builder - perf",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.perf", "target_arch": "arm", "build_config": "Release", "target_platform": "android"},
)
v8_builder(
    name = "V8 Android Arm64 - builder - perf",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.perf", "target_arch": "arm", "build_config": "Release", "target_platform": "android"},
)
v8_builder(
    name = "V8 Linux - builder - perf",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.perf"},
)
v8_builder(
    name = "V8 Linux64 - builder - perf",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8.perf"},
)

v8_builder(
    name = "v8_flako",
    bucket = "try.triggered",
    executable = {"name": "v8/flako"},
    execution_timeout = 14400,
)
v8_builder(
    name = "V8 Linux64 - builder",
    bucket = "ci.br.beta",
    triggered_by = ["v8-trigger-br-beta"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Linux64"], "mastername": "client.v8", "track_build_dependencies": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
)
v8_builder(
    name = "V8 Linux64 - debug builder",
    bucket = "ci.br.beta",
    triggered_by = ["v8-trigger-br-beta"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "set_gclient_var": "download_jsfunfuzz", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug"]},
)
v8_builder(
    name = "V8 Fuchsia - builder",
    bucket = "ci.br.beta",
    triggered_by = ["v8-trigger-br-beta"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "fuchsia", "mastername": "client.v8", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_builder(
    name = "V8 iOS - sim",
    bucket = "ci.br.beta",
    dimensions = {"os": "Mac-10.14", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$depot_tools/osx_sdk": {"sdk_version": "11b52"}, "target_platform": "ios", "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
v8_builder(
    name = "V8 Linux64 - builder",
    bucket = "ci.br.stable",
    triggered_by = ["v8-trigger-br-stable"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers": ["V8 Linux64"], "mastername": "client.v8", "track_build_dependencies": True, "build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "binary_size_tracking": {"category": "linux64", "binary": "d8"}},
)
v8_builder(
    name = "V8 Linux64 - debug builder",
    bucket = "ci.br.stable",
    triggered_by = ["v8-trigger-br-stable"],
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "mastername": "client.v8", "set_gclient_var": "download_jsfunfuzz", "triggers": ["V8 Fuzzer", "V8 Linux64 - debug"]},
)
v8_builder(
    name = "V8 Fuchsia - builder",
    bucket = "ci.br.stable",
    triggered_by = ["v8-trigger-br-stable"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "fuchsia", "mastername": "client.v8", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
