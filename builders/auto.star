# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "defaults_ci", "v8_builder")

def auto_builder(name, execution_timeout = None, properties = None, **kwargs):
    properties = dict((properties or {}))
    v8_builder(
        defaults_ci,
        name = name,
        bucket = "ci",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        service_account = "v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com",
        execution_timeout = execution_timeout,
        properties = properties,
        **kwargs
    )

autoroller_target_config = {
    "solution_name": "v8",
    "project_name": "v8/v8",
    "account": "v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com",
    "log_template": "Rolling v8/%s: %s/+log/%s..%s",
    "cipd_log_template": "Rolling v8/%s: %s..%s",
}

auto_builder(
    name = "V8 lkgr finder",
    executable = "recipe:lkgr_finder",
    properties = {
        "lkgr_project": "v8",
        "allowed_lag": 4,
    },
    schedule = "8,23,38,53 * * * *",
    in_list = "infra",
    notifies = ["infra"],
)

auto_builder(
    name = "Auto-roll - push",
    executable = "recipe:v8/auto_roll_push",
    schedule = "12,27,42,57 * * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - deps",
    executable = "recipe:v8/auto_roll_deps",
    schedule = "0,15,30,45 * * * *",
    in_list = "infra",
)

auto_builder(
    name = "Auto-roll - v8 deps",
    executable = "recipe:v8/auto_roll_v8_deps",
    schedule = "0 3 * * *",
    in_list = "infra",
    properties = {
        "autoroller_config": {
            "target_config": autoroller_target_config,
            "subject": "Update V8 DEPS.",
            "excludes": [
                # https://crrev.com/c/1547863
                "third_party/perfetto",
                "third_party/protobuf",
                # Skip these dependencies (list without solution name prefix).
                "third_party/google_benchmark/src",
                "test/mozilla/data",
                "test/simdjs/data",
                "test/test262/data",
                "test/wasm-js/data",
                "testing/gtest",
                "third_party/WebKit/Source/platform/inspector_protocol",
                "third_party/blink/renderer/platform/inspector_protocol",
            ],
            "reviewers": [
                "v8-waterfall-sheriff@grotations.appspotmail.com",
            ],
            "show_commit_log": False,
        },
    },
)

auto_builder(
    name = "Auto-roll - test262",
    executable = "recipe:v8/auto_roll_v8_deps",
    schedule = "0 14 * * *",
    in_list = "infra",
    properties = {
        "autoroller_config": {
            "target_config": autoroller_target_config,
            "subject": "Update Test262.",
            "includes": [
                # Only roll these dependencies (list without solution name prefix).
                "test/test262/data",
            ],
            "reviewers": [
                "adamk@chromium.org",
                "gsathya@chromium.org",
            ],
            "show_commit_log": True,
        },
    },
)

auto_builder(
    name = "Auto-roll - wasm-spec",
    executable = "recipe:v8/auto_roll_v8_deps",
    schedule = "0 4 * * *",
    in_list = "infra",
    properties = {
        "autoroller_config": {
            "target_config": autoroller_target_config,
            "subject": "Update wasm-spec.",
            "includes": [
                # Only roll these dependencies (list without solution name prefix).
                "test/wasm-js/data",
            ],
            "reviewers": [
                "ahaas@chromium.org",
                "clemensb@chromium.org",
            ],
            "show_commit_log": True,
        },
    },
)

auto_builder(
    name = "Auto-roll - google_benchmark",
    executable = "recipe:v8/auto_roll_v8_deps",
    schedule = "0 5 * * *",
    in_list = "infra",
    properties = {
        "autoroller_config": {
            "target_config": autoroller_target_config,
            "subject": "Update google_benchmark",
            "includes": [
                # Only roll these dependencies (list without solution name prefix).
                "third_party/google_benchmark/src",
            ],
            "reviewers": [
                "v8-waterfall-sheriff@grotations.appspotmail.com",
                "mlippautz@chromium.org",
            ],
            "show_commit_log": True,
        },
    },
)

auto_builder(
    name = "Auto-tag",
    executable = "recipe:v8/auto_tag",
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
    executable = "recipe:v8/auto_roll_release_process",
    schedule = "10,25,40,55 * * * *",
)

auto_builder(
    name = "Infra Expriments",
    executable = "recipe:v8/spike",
    schedule = "* * * * * 1970",
    in_list = "infra",
)
