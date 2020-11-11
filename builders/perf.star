# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GOMA", "v8_builder")

def perf_builder(in_category, **kwargs):
    kwargs["close_tree"] = True
    properties = {"triggers_proxy": True, "builder_group": "client.v8.perf"}
    extra_properties = kwargs.pop("properties", {})
    properties.update(extra_properties)
    v8_builder(
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = properties,
        use_goma = GOMA.DEFAULT,
        in_console = "perf/%s" % in_category,
        **kwargs
    )

perf_builder(
    name = "V8 Arm - builder - perf",
    properties = {"target_arch": "arm"},
    in_category = "Arm",
)

perf_builder(
    name = "V8 Android Arm - builder - perf",
    properties = {"target_arch": "arm", "target_platform": "android"},
    in_category = "Arm",
)

perf_builder(
    name = "V8 Arm64 - builder - perf",
    properties = {"target_arch": "arm", "target_bits": 64},
    in_category = "Arm64",
)

perf_builder(
    name = "V8 Android Arm64 - builder - perf",
    properties = {"target_arch": "arm", "target_platform": "android"},
    in_category = "Arm64",
)

perf_builder(
    name = "V8 Linux - builder - perf",
    in_category = "Linux",
)

perf_builder(
    name = "V8 Linux64 - builder - perf",
    in_category = "Linux64",
)
