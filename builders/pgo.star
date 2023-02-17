# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GOMA", "RECLIENT", "v8_builder")
load("//lib/service-accounts.star", "V8_PGO_ACCOUNT")

v8_builder(
    name = "PGO Builder",
    bucket = "ci-hp",
    service_account = V8_PGO_ACCOUNT,
    executable = "recipe:v8/pgo_builder",
    schedule = "*/10 * * * *",
    in_list = "pgo",
    execution_timeout = 3600,
)

v8_builder(
    name = "V8 Linux PGO instrumentation - builder",
    bucket = "ci",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    executable = "recipe:v8/compilator",
    properties = {
        "default_targets": ["d8_pgo"],
        "builder_group": "client.v8",
    },
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT,
    in_list = "pgo",
)

v8_builder(
    name = "V8 Linux64 PGO instrumentation - builder",
    bucket = "ci",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    executable = "recipe:v8/compilator",
    properties = {
        "default_targets": ["d8_pgo"],
        "builder_group": "client.v8",
    },
    use_goma = GOMA.NO,
    use_remoteexec = RECLIENT.DEFAULT,
    in_list = "pgo",
)
