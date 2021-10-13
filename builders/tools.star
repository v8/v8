# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "defaults_ci", "v8_builder")

v8_builder(
    defaults_ci,
    name = "Infra Expriments",
    bucket = "ci",
    dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
    service_account = "v8-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
    executable = "recipe:v8/spike",
    schedule = "* * * * * 1970",
    in_list = "tools",
)
