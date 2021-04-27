#!/usr/bin/env lucicfg
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "//lib/lib.star",
    "waterfall_acls",
)

lucicfg.config(
    config_dir = "generated",
    tracked_files = [
        "cr-buildbucket-dev.cfg",
        "luci-milo-dev.cfg",
        "luci-logdog-dev.cfg",
        "luci-scheduler-dev.cfg",
    ],
    fail_on_warnings = True,
)

luci.project(
    name = "v8",
    buildbucket = "cr-buildbucket-dev.appspot.com",
    logdog = "luci-logdog-dev.appspot.com",
    milo = "luci-milo-dev.appspot.com",
    scheduler = "luci-scheduler-dev.appspot.com",
    swarming = "chromium-swarm-dev.appspot.com",
    acls = [
        acl.entry(
            [
                acl.BUILDBUCKET_READER,
                acl.LOGDOG_READER,
                acl.PROJECT_CONFIGS_READER,
                acl.SCHEDULER_READER,
            ],
            groups = ["all"],
        ),
        acl.entry(
            [acl.SCHEDULER_OWNER],
            groups = [
                "project-v8-sheriffs",
            ],
        ),
        acl.entry(
            [acl.LOGDOG_WRITER],
            groups = ["luci-logdog-chromium-dev-writers"],
        ),
    ],
)

luci.logdog(gs_bucket = "chromium-luci-logdog")

luci.bucket(name = "ci", acls = waterfall_acls)

luci.gitiles_poller(
    name = "v8-trigger",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/heads/master"],
)

exec("//lib/recipes.star")

luci.builder(
    bucket = "ci",
    name = "V8 Win64 - dev image",
    swarming_host = "chromium-swarm-dev.appspot.com",
    swarming_tags = ["vpython:native-python-wrapper"],
    dimensions = {"cpu": "x86-64", "os": "Windows-10", "pool": "luci.chromium.ci"},
    executable = "recipe:v8",
    properties = {
        "$build/goma": {
            "enable_ats": True,
            "rpc_extra_params": "?prod",
            "server_host": "goma.chromium.org",
        },
        "$recipe_engine/isolated": {
            "server": "https://isolateserver-dev.appspot.com/",
        },
        "builder_group": "client.v8",
        "recipe": "v8",
    },
    execution_timeout = 7200 * time.second,
    build_numbers = True,
    service_account = "v8-ci-builder-dev@chops-service-accounts.iam.gserviceaccount.com",
    triggered_by = ["v8-trigger"],
)

luci.console_view(
    name = "dev_image",
    title = "Dev Image",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/heads/master"],
    favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
    header = "//consoles/header_dev.textpb",
)

luci.console_view_entry(
    console_view = "dev_image",
    builder = "V8 Win64 - dev image",
)
