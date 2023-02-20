# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "CQ", "GCLIENT_VARS", "GOMA", "RECLIENT", "RECLIENT_JOBS", "v8_builder")

def try_builder(
        name,
        bucket = "try",
        cq_properties = CQ.NONE,
        cq_branch_properties = CQ.NONE,
        **kwargs):
    # All unspecified branch trybots are per default optional.
    if (cq_properties != CQ.NONE and cq_branch_properties == CQ.NONE):
        cq_branch_properties = CQ.OPTIONAL
    v8_builder(
        name = name,
        bucket = bucket,
        cq_properties = cq_properties,
        cq_branch_properties = cq_branch_properties,
        in_list = "tryserver",
        **kwargs
    )

def presubmit_builder(
        project,
        cq_properties = CQ.NONE,
        cq_branch_properties = CQ.NONE,
        timeout = 8 * 60):
    try_builder(
        name = "%s_presubmit" % project,
        bucket = "try",
        cq_properties = cq_properties,
        cq_branch_properties = cq_branch_properties,
        executable = "recipe:run_presubmit",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        execution_timeout = timeout + 2 * 60,
        properties = {"runhooks": True, "timeout": timeout},
        priority = 25,
    )

presubmit_builder(
    "v8",
    cq_properties = CQ.BLOCK_NO_REUSE,
    cq_branch_properties = CQ.BLOCK_NO_REUSE,
)
presubmit_builder("crossbench", timeout = 900)

try_builder(
    name = "v8_android_arm_compile_rel",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"target_platform": "android", "target_arch": "arm"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_android_arm64_compile_dbg",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"target_platform": "android", "target_arch": "arm"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_full_presubmit",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    executable = "recipe:v8/presubmit",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
)

try_builder(
    name = "v8_ios_simulator",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac"},
    execution_timeout = 3600,
    properties = {"$depot_tools/osx_sdk": {"sdk_version": "12d4e"}, "target_platform": "ios"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux64_bazel",
    bucket = "try",
    # TODO(crbug.com/v8/13748): Enable again after bazel build is fixed.
    # cq_properties = CQ.BLOCK,
    cq_properties = CQ.OPTIONAL,
    cq_branch_properties = CQ.OPTIONAL,
    executable = "recipe:v8/bazel",
    dimensions = {"os": "Ubuntu-20.04", "cpu": "x86-64"},
    execution_timeout = 3600,
)

try_builder(
    name = "v8_linux64_coverage",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"enable_swarming": False, "gclient_vars": {"checkout_clang_coverage_tools": "True"}},
    execution_timeout = 7200,
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux64_gcc_compile_dbg",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-20.04", "cpu": "x86-64"},
    execution_timeout = 5400,
    use_goma = GOMA.NO,
)

try_builder(
    name = "v8_linux64_gcc_light_compile_dbg",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    # TODO(https://crbug.com/v8/13005): Block branch CQ as soon as M105 is
    # extended stable.
    cq_branch_properties = CQ.EXP_100_PERCENT,
    dimensions = {"os": "Ubuntu-20.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"default_targets": ["v8_gcc_light"]},
    use_goma = GOMA.NO,
)

try_builder(
    name = "v8_linux64_gcov_coverage",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"enable_swarming": False, "clobber": True, "coverage": "gcov"},
    execution_timeout = 10800,
    use_goma = GOMA.NO,
)

try_builder(
    name = "v8_linux64_header_includes_dbg",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux64_no_wasm_compile_rel",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux64_shared_compile_rel",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux_arm_lite_compile_dbg",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux_blink_rel",
    bucket = "try",
    cq_properties = CQ.EXP_5_PERCENT,
    executable = "recipe:chromium_trybot",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 4400,
    build_numbers = True,
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
    disable_resultdb_exports = True,
)

try_builder(
    name = "v8_linux_chromium_gn_rel",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    executable = "recipe:chromium_trybot",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    build_numbers = True,
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
    reclient_jobs = RECLIENT_JOBS.J150,
    disable_resultdb_exports = True,
)

try_builder(
    name = "v8_linux_mips64el_compile_rel",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux_noi18n_compile_dbg",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux_shared_compile_rel",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux_torque_compare",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"default_targets": ["compare_torque_runs"]},
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_linux_vtunejit",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    gclient_vars = [GCLIENT_VARS.ITTAPI],
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT_UNTRUSTED,
)

try_builder(
    name = "v8_test_tools",
    bucket = "try",
    cq_properties = CQ.on_files("tools/clusterfuzz/js_fuzzer/.+"),
    executable = "recipe:v8/test_tools",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
)

try_builder(
    name = "v8_win_msvc_light_compile_dbg",
    bucket = "try",
    cq_properties = CQ.OPTIONAL,
    cq_branch_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"default_targets": ["d8"], "use_goma": False},
    use_goma = GOMA.NO,
)

try_builder(
    name = "v8_win64_msvc_light_compile_rel",
    bucket = "try",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"default_targets": ["d8"], "use_goma": False},
    use_goma = GOMA.NO,
)

try_builder(
    name = "v8_flako",
    bucket = "try.triggered",
    executable = "recipe:v8/flako",
    execution_timeout = 14400,
)

try_builder(
    name = "v8_verify_flakes",
    bucket = "try.triggered",
    executable = "recipe:v8/verify_flakes",
    execution_timeout = 16200,
    schedule = "with 3h interval",
)
