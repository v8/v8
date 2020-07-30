load("//lib.star", "v8_try_ng_pair")

v8_try_ng_pair(
    name = "v8_linux_gc_stress_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_gcc_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2700,
    properties = {"build_config": "Release", "use_goma": False, "set_gclient_var": "check_v8_header_includes"},
)
v8_try_ng_pair(
    name = "v8_linux64_msan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "set_gclient_var": "checkout_instrumented_libraries"},
)
v8_try_ng_pair(
    name = "v8_linux64_tsan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm64_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm64_gc_stress_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_mac64_asan_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.13"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_mac64_gc_stress_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.13"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_win_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_win64_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_android_arm64_n5x_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "android", "target_arch": "arm", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_fuchsia_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "fuchsia", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_arm64_pointer_compression_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_asan_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_cfi_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_dbg",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_fuzzilli",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_gc_stress_custom_snapshot_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_fyi_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_nodcheck_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_perfetto_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_pointer_compression_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_reverse_jsargs_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_tsan_isolates_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_ubsan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux64_verify_csa_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm64_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm64_cfi_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm_lite_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_arm_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_nodcheck_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_noi18n_rel",
    cq_properties_trigger = {"location_regexp": [".+/[+]/.*intl.*", ".+/[+]/.*test262.*"], "cancel_stale": False},
    cq_properties_triggered = {"location_regexp": [".+/[+]/.*intl.*", ".+/[+]/.*test262.*"], "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "set_gclient_var": "download_gcmole"},
)
v8_try_ng_pair(
    name = "v8_linux_optional_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_linux_verify_csa_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_mac64_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Mac-10.13"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_mac64_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.13"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_odroid_arm_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "target_arch": "arm"},
)
v8_try_ng_pair(
    name = "v8_win64_asan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_win64_msvc_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "use_goma": False, "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_win64_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_ng_pair(
    name = "v8_win_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
