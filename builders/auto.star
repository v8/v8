# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "defaults_ci", "GOMA", "v8_builder")

def auto_builder(name, recipe, cipd_package = None, cipd_version = None, execution_timeout = None, properties = None, **kwargs):
    properties = dict((properties or {}))
    executable = dict()
    executable["name"] = recipe
    if cipd_package:
        executable["cipd_package"] = cipd_package
    if cipd_version:
        executable["cipd_version"] = cipd_version
    v8_builder(
        defaults_ci,
        name = name,
        bucket = "ci",
        executable = executable,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        service_account = "v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com",
        execution_timeout = execution_timeout,
        properties = properties,
        use_goma = GOMA.DEFAULT,
        **kwargs
    )

auto_builder(
    name = "V8 lkgr finder",
    recipe = "lkgr_finder",
    cipd_package = "infra/recipe_bundles/chromium.googlesource.com/infra/infra",
    cipd_version = "refs/heads/master",
    properties = {
        "lkgr_project": "v8",
        "allowed_lag": 4,
    },
    schedule = "8,23,38,53 * * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - push",
    recipe = "v8/auto_roll_push",
    schedule = "12,27,42,57 * * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - deps",
    recipe = "v8/auto_roll_deps",
    schedule = "0,15,30,45 * * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - v8 deps",
    recipe = "v8/auto_roll_v8_deps",
    schedule = "0 3 * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - test262",
    recipe = "v8/auto_roll_v8_deps",
    schedule = "0 14 * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - wasm-spec",
    recipe = "v8/auto_roll_v8_deps",
    schedule = "0 4 * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-tag",
    recipe = "v8/auto_tag",
    execution_timeout = 21600,
    properties = {"builder_group": "client.v8.branches"},
    schedule = "triggered",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by = ["v8-trigger-branches-auto-tag"],
)

auto_builder(
    name = "Auto-roll - release process",
    recipe = "v8/auto_roll_release_process",
    schedule = "10,25,40,55 * * * *",
)
