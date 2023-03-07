#!/usr/bin/env -S bash -xc 'lucicfg format && lucicfg "$0"'
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

lucicfg.check_version("1.30.9", "Please update depot_tools")

load(
    "//lib/lib.star",
    "RECLIENT",
    "waterfall_acls",
)

# Use LUCI Scheduler BBv2 names and add Scheduler realms configs.
lucicfg.enable_experiment("crbug.com/1182002")

lucicfg.config(
    config_dir = "generated",
    tracked_files = [
        "cr-buildbucket-dev.cfg",
        "luci-logdog-dev.cfg",
        "luci-milo-dev.cfg",
        "luci-scheduler-dev.cfg",
        "realms-dev.cfg",
    ],
    fail_on_warnings = True,
    lint_checks = ["none", "+formatting"],
)

luci.project(
    name = "v8",
    dev = True,
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
    refs = ["refs/heads/main"],
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
        "$build/reclient": RECLIENT.DEFAULT,
        "$build/v8": {"use_remoteexec": True},
        "use_goma": False,
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
    refs = ["refs/heads/main"],
    favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
    header = "//consoles/header_dev.textpb",
)

luci.console_view_entry(
    console_view = "dev_image",
    builder = "V8 Win64 - dev image",
)
