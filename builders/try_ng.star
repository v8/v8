# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "//lib/lib.star",
    "CQ",
    "GCLIENT_VARS",
    "GOMA",
    "defaults_triggered",
    "defaults_try",
    "v8_builder",
)

#TODO(almuthanna): get rid of kwargs and specify default values
def try_ng_pair(
        name,
        cq_properties = CQ.NONE,
        cq_branch_properties = CQ.NONE,
        **kwargs):
    triggered_timeout = kwargs.pop("triggered_timeout", None)
    kwargs.setdefault("properties", {})["triggers"] = [
        "%s_ng_triggered" % name,
    ]

    # All unspecified branch trybots are per default optional.
    if (cq_properties != CQ.NONE and cq_branch_properties == CQ.NONE):
        cq_branch_properties = CQ.OPTIONAL

    # Copy properties as they are modified below.
    cq_tg = dict(cq_properties)
    cq_td = dict(cq_properties)
    cq_exp = dict(cq_properties)
    cq_branch_tg = dict(cq_branch_properties)
    cq_branch_td = dict(cq_branch_properties)
    cq_branch_exp = dict(cq_branch_properties)

    # Triggered builders don't support experiments. Therefore we create
    # a separate experimental builder below. The trigger pair will remain
    # as opt-in trybots.
    for prop in (cq_tg, cq_td, cq_branch_tg, cq_branch_td):
        if "experiment_percentage" in prop:
            prop.pop("experiment_percentage")
            prop["includable_only"] = "true"

    v8_builder(
        defaults_try,
        name = name + "_ng",
        bucket = "try",
        cq_properties = cq_tg,
        cq_branch_properties = cq_branch_tg,
        in_list = "tryserver",
        **kwargs
    )
    v8_builder(
        defaults_triggered,
        name = name + "_ng_triggered",
        bucket = "try.triggered",
        execution_timeout = triggered_timeout,
        cq_properties = cq_td,
        cq_branch_properties = cq_branch_td,
        in_list = "tryserver",
    )
    if "experiment_percentage" in cq_properties:
        kwargs["properties"]["triggers"] = None
        v8_builder(
            defaults_try,
            name = name + "_exp",
            bucket = "try",
            cq_properties = cq_exp,
            cq_branch_properties = cq_branch_exp,
            in_list = "tryserver",
            **kwargs
        )

try_ng_pair(
    name = "v8_android_arm64_n5x_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"target_platform": "android", "target_arch": "arm"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_fuchsia_rel",
    cq_properties = CQ.EXP_100_PERCENT,
    cq_branch_properties = CQ.EXP_100_PERCENT,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"target_platform": "fuchsia"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_arm64_pointer_compression_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_asan_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_cfi_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_cppgc_non_default_dbg_ng",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_dbg",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_dict_tracking_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_disable_runtime_call_stats_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_external_code_space_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_gc_stress_custom_snapshot_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_heap_sandbox_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_fuzzilli",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_fyi_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_msan_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"gclient_vars": {"checkout_instrumented_libraries": "True"}},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_nodcheck_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_perfetto_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_pointer_compression_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_single_generation_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_riscv64_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_loong64_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_tsan_rel",
    cq_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_tsan_no_cm_rel",
    cq_properties = CQ.on_files(
        ".+/[+]/src/compiler/js-heap-broker.(h|cc)",
        ".+/[+]/src/compiler/heap-refs.(h|cc)",
    ),
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_tsan_isolates_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_ubsan_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_verify_csa_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_gc_stress_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_sim_heap_sandbox_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_cfi_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm_lite_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_gc_stress_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_gcc_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
    use_goma = GOMA.NO,
)

try_ng_pair(
    name = "v8_linux_nodcheck_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_noi18n_rel",
    cq_properties = CQ.on_files(".+/[+]/.*intl.*", ".+/[+]/.*test262.*"),
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    gclient_vars = [GCLIENT_VARS.GCMOLE],
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_optional_rel",
    cq_properties = CQ.on_files(
        ".+/[+]/src/codegen/shared-ia32-x64/macro-assembler-shared-ia32-x64.(h|cc)",
        ".+/[+]/src/codegen/x64/(macro-)?assembler-x64.(h|cc)",
        ".+/[+]/src/codegen/x64/sse-instr.h",
        ".+/[+]/src/compiler/backend/x64/code-generator-x64.cc",
        ".+/[+]/src/wasm/baseline/x64/liftoff-assembler-x64.h",
    ),
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_verify_csa_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 2400,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_dbg",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_asan_rel",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_gc_stress_dbg",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_rel",
    triggered_timeout = 7200,
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_rel",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_dbg",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_full_dbg",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_sim_rel",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_sim_dbg",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_sim_nodcheck_rel",
    triggered_timeout = 7200,
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_numfuzz",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_numfuzz_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_numfuzz_tsan",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_odroid_arm_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    properties = {"target_arch": "arm"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_win64_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win64_msvc_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"use_goma": False},
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win64_rel",
    cq_properties = CQ.BLOCK,
    cq_branch_properties = CQ.BLOCK,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win_dbg",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win64_asan_rel",
    cq_properties = CQ.OPTIONAL,
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.ATS,
)
