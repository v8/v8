load("//lib.star", "v8_try_builder")

v8_try_builder(
    name = "v8_full_presubmit",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    executable = {"name": "v8/presubmit"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
)
v8_try_builder(
    name = "v8_ios_simulator",
    bucket = "try",
    dimensions = {"os": "Mac-10.13"},
    properties = {"build_config": "Release", "$depot_tools/osx_sdk": {"sdk_version": "11b52"}, "target_platform": "ios", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux_gcc_compile_rel",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2700,
    properties = {"build_config": "Release", "default_targets": ["d8"], "use_goma": False, "set_gclient_var": "check_v8_header_includes"},
)
v8_try_builder(
    name = "v8_linux_shared_compile_rel",
    bucket = "try",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux64_gcc_compile_dbg",
    bucket = "try",
    cq_properties = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 2700,
    properties = {"build_config": "Debug", "default_targets": ["d8"], "use_goma": False, "set_gclient_var": "check_v8_header_includes"},
)
v8_try_builder(
    name = "v8_linux64_header_includes_dbg",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}, "set_gclient_var": "check_v8_header_includes"},
)
v8_try_builder(
    name = "v8_linux_mipsel_compile_rel",
    bucket = "try",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux_mips64el_compile_rel",
    bucket = "try",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_android_arm_compile_rel",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "android", "target_arch": "arm", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_android_arm64_compile_dbg",
    bucket = "try",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "target_platform": "android", "target_arch": "arm", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_presubmit",
    bucket = "try",
    cq_properties = {"disable_reuse": "true", "cancel_stale": False},
    executable = {"name": "run_presubmit"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 600,
    properties = {"runhooks": True, "repo_name": "v8"},
    priority = 25,
)
v8_try_builder(
    name = "v8_win_compile_dbg",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_win64_msvc_compile_rel",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"build_config": "Release", "use_goma": False, "$build/goma": {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux_chromium_gn_rel",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    executable = {"name": "chromium_trybot"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    build_numbers = True,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux_blink_rel",
    bucket = "try",
    cq_properties = {"experiment_percentage": 10, "cancel_stale": False},
    executable = {"name": "chromium_trybot"},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    execution_timeout = 4400,
    build_numbers = True,
    properties = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_fuchsia_compile_rel",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "target_platform": "fuchsia", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux64_shared_compile_rel",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux_noi18n_compile_dbg",
    bucket = "try",
    cq_properties = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_mac64_compile_full_dbg",
    bucket = "try",
    cq_properties = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.13"},
    properties = {"build_config": "Debug", "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
v8_try_builder(
    name = "v8_linux_torque_compare",
    bucket = "try",
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"build_config": "Release", "default_targets": ["compare_torque_runs"], "$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}},
)
