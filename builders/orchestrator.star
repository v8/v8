# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Experimental builders for building an orchestrator prototype.

load("//lib/lib.star", "GOMA", "v8_builder")

v8_builder(
    name = "v8_linux64_rel",
    bucket = "try",
    dimensions = {"host_class": "multibot"},
    executable = "recipe:v8/orchestrator",
    in_list = "tryserver",
    caches = [
        swarming.cache(
            path = "builder",
            name = "v8_builder_cache_nowait",
        ),
    ],
)

v8_builder(
    name = "v8_linux64_compile_rel",
    bucket = "try",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    use_goma = GOMA.DEFAULT,
    executable = "recipe:v8/compilator",
    in_list = "tryserver",
)
