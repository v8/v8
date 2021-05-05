# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "defaults_triggered", "defaults_try", "v8_builder")

#TODO(almuthanna): get rid of kwargs and specify default values
def try_ng_pair(name, use_cas = False, experimental = False, **kwargs):
    triggered_timeout = kwargs.pop("triggered_timeout", None)
    cq_tg = kwargs.pop("cq_properties_trigger", None)
    cq_td = kwargs.pop("cq_properties_triggered", None)
    cq_exp = dict(cq_tg)
    kwargs.setdefault("properties", {})["triggers"] = [name + "_ng_triggered"]
    if experimental:
        cq_tg["includable_only"] = "true"
        cq_td["includable_only"] = "true"
    v8_builder(
        defaults_try,
        name = name + "_ng",
        bucket = "try",
        cq_properties = cq_tg,
        in_list = "tryserver",
        **kwargs
    )
    v8_builder(
        defaults_triggered,
        name = name + "_ng_triggered",
        bucket = "try.triggered",
        execution_timeout = triggered_timeout,
        cq_properties = cq_td,
        in_list = "tryserver",
    )
    if experimental:
        cq_exp["experiment_percentage"] = 100
        kwargs["properties"]["triggers"] = None
        v8_builder(
            defaults_try,
            name = name + "_exp",
            bucket = "try",
            cq_properties = cq_exp,
            in_list = "tryserver",
            **kwargs
        )

try_ng_pair(
    name = "v8_android_arm64_n5x_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    properties = {"target_platform": "android", "target_arch": "arm"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_fuchsia_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    properties = {"target_platform": "fuchsia"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_arm64_pointer_compression_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_asan_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_cfi_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_dbg",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_dict_tracking_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_gc_stress_custom_snapshot_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_heap_sandbox_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_fuzzilli",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_fyi_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_msan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"gclient_vars": {"checkout_instrumented_libraries": "True"}},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_nodcheck_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_perfetto_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_pointer_compression_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_riscv64_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_tsan_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
    experimental = True,
)

try_ng_pair(
    name = "v8_linux64_tsan_no_cm_rel",
    cq_properties_trigger = {"location_regexp": [".+/[+]/src/compiler/js-heap-broker.cc", ".+/[+]/src/compiler/heap-refs.h"], "cancel_stale": False},
    cq_properties_triggered = {"location_regexp": [".+/[+]/src/compiler/js-heap-broker.cc", ".+/[+]/src/compiler/heap-refs.h"], "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_tsan_isolates_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_ubsan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux64_verify_csa_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_gc_stress_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm64_cfi_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm_lite_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_arm_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 2400,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_gc_stress_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_gcc_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    execution_timeout = 2700,
    gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
    use_goma = GOMA.NO,
)

try_ng_pair(
    name = "v8_linux_nodcheck_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 2400,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_noi18n_rel",
    cq_properties_trigger = {"location_regexp": [".+/[+]/.*intl.*", ".+/[+]/.*test262.*"], "cancel_stale": False},
    cq_properties_triggered = {"location_regexp": [".+/[+]/.*intl.*", ".+/[+]/.*test262.*"], "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 2400,
    gclient_vars = [GCLIENT_VARS.GCMOLE],
    use_goma = GOMA.DEFAULT,
)

optional_rel_cq_properties = {
    "location_regexp": [
        ".+/[+]/src/codegen/ia32/(macro-)?assembler-ia32.(h|cc)",
        ".+/[+]/src/codegen/shared-ia32-x64/macro-assembler-shared-ia32-x64.(h|cc)",
    ],
    "cancel_stale": False,
}

try_ng_pair(
    name = "v8_linux_optional_rel",
    cq_properties_trigger = optional_rel_cq_properties,
    cq_properties_triggered = optional_rel_cq_properties,
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_linux_verify_csa_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    execution_timeout = 2400,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_asan_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_gc_stress_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    execution_timeout = 3600,
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac64_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_full_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_sim_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_sim_dbg",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_mac_arm64_sim_nodcheck_rel",
    triggered_timeout = 7200,
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Mac-10.15"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_odroid_arm_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Ubuntu", "cpu": "x86-64"},
    properties = {"target_arch": "arm"},
    use_goma = GOMA.DEFAULT,
)

try_ng_pair(
    name = "v8_win64_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win64_msvc_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    properties = {"use_goma": False},
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win64_rel",
    cq_properties_trigger = {"cancel_stale": False},
    cq_properties_triggered = {"cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win_dbg",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win_rel",
    cq_properties_trigger = {"cancel_stale": False, "includable_only": "true"},
    cq_properties_triggered = {"cancel_stale": False, "includable_only": "true"},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    use_goma = GOMA.ATS,
)

try_ng_pair(
    name = "v8_win64_asan_rel",
    cq_properties_trigger = {"includable_only": "true", "cancel_stale": False},
    cq_properties_triggered = {"includable_only": "true", "cancel_stale": False},
    dimensions = {"os": "Windows-10", "cpu": "x86-64"},
    execution_timeout = 3600,
    use_goma = GOMA.ATS,
)
