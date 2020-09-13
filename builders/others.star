# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GOMA", "v8_builder")

v8_builder(
    name = "V8 Linux64 - pointer compression without dchecks",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"triggers_proxy": True, "build_config": "Release", "mastername": "client.v8"},
    use_goma = GOMA.DEFAULT,
)

v8_builder(
    name = "V8 iOS - sim",
    bucket = "ci.br.beta",
    dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    properties = {"build_config": "Release", "$depot_tools/osx_sdk": {"sdk_version": "11b52"}, "target_platform": "ios", "mastername": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
)
