# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "RECLIENT", "defaults_ci", "v8_builder")
load(
    "//lib/service-accounts.star",
    "V8_CI_ACCOUNT",
    "V8_TEST262_EXPORT_ACCOUNT",
    "V8_TEST262_IMPORT_ACCOUNT",
)

v8_builder(
    defaults_ci,
    name = "Infra Expriments",
    bucket = "ci",
    dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
    service_account = V8_CI_ACCOUNT,
    executable = "recipe:v8/spike",
    schedule = "* * * * * 1970",
    in_list = "tools",
)

v8_builder(
    defaults_ci,
    name = "Branch Monitor",
    bucket = "ci",
    dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
    service_account = V8_CI_ACCOUNT,
    executable = "recipe:v8/branch_monitor",
    properties = {"max_gap_seconds": 43200},
    schedule = "49 * * * *",
    in_list = "tools",
    notifies = ["branch monitor", "infra"],
)

v8_builder(
    name = "Test262 exporter",
    bucket = "ci-hp",
    dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
    service_account = V8_TEST262_EXPORT_ACCOUNT,
    executable = "recipe:v8/test262_export",
    in_list = "tools",
    execution_timeout = 3600,
    notifies = ["test262 impex", "infra"],
)

v8_builder(
    name = "Test262 PR approver",
    bucket = "ci-hp",
    dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
    service_account = V8_TEST262_EXPORT_ACCOUNT,
    executable = "recipe:v8/test262_export",
    schedule = "0 2 * * 0",
    in_list = "tools",
    execution_timeout = 3600,
    notifies = ["test262 impex", "infra"],
    properties = {"approver": True},
)

v8_builder(
    name = "Test262 importer",
    bucket = "ci-hp",
    dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
    service_account = V8_TEST262_IMPORT_ACCOUNT,
    executable = "recipe:v8/test262_import",
    schedule = "0 2 * * 0",
    in_list = "tools",
    execution_timeout = 3600,
    notifies = ["test262 impex", "infra"],
)

v8_builder(
    name = "Test262 import watcher",
    bucket = "ci-hp",
    dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
    service_account = V8_TEST262_IMPORT_ACCOUNT,
    executable = "recipe:v8/test262_watch",
    schedule = "*/30 * * * *",
    in_list = "tools",
    execution_timeout = 3600,
    notifies = ["test262 impex", "infra"],
)

luci.gitiles_poller(
    name = "test262-trigger",
    bucket = "ci-hp",
    repo = "https://chromium.googlesource.com/v8/v8",
    triggers = ["Test262 exporter"],
    path_regexps = [
        "test/test262/local-tests/test/staging/.+",
    ],
)
