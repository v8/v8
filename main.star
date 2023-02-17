#!/usr/bin/env -S bash -xc 'lucicfg format && lucicfg "$0"'
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

lucicfg.check_version("1.33.7", "Please update depot_tools")

load(
    "//lib/lib.star",
    "tryserver_acls",
    "waterfall_acls",
)
load(
    "//lib/service-accounts.star",
    "V8_CI_ACCOUNT",
    "V8_PGO_ACCOUNT",
    "V8_SERVICE_ACCOUNTS",
    "V8_TRY_ACCOUNT",
)

# Use LUCI Scheduler BBv2 names and add Scheduler realms configs.
lucicfg.enable_experiment("crbug.com/1182002")

luci.builder.defaults.experiments.set(
    {
        # Omit python2 for all builds.
        "luci.buildbucket.omit_python2": 100,
    },
)

lucicfg.config(
    config_dir = "generated",
    tracked_files = [
        "commit-queue.cfg",
        "cr-buildbucket.cfg",
        "luci-milo.cfg",
        "luci-logdog.cfg",
        "luci-scheduler.cfg",
        "luci-notify.cfg",
        "project.cfg",
        "realms.cfg",
    ],
    fail_on_warnings = True,
    lint_checks = ["none", "+formatting"],
)

luci.project(
    name = "v8",
    buildbucket = "cr-buildbucket.appspot.com",
    logdog = "luci-logdog",
    milo = "luci-milo",
    notify = "luci-notify.appspot.com",
    scheduler = "luci-scheduler",
    swarming = "chromium-swarm.appspot.com",
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
            groups = ["luci-logdog-chromium-writers"],
        ),
        acl.entry(
            [acl.CQ_COMMITTER],
            groups = [
                "project-v8-committers",
            ],
        ),
        acl.entry(
            [acl.CQ_DRY_RUNNER],
            groups = [
                "project-v8-tryjob-access",
            ],
        ),
    ],
    bindings = [
        luci.binding(
            roles = "role/configs.validator",
            users = [V8_TRY_ACCOUNT],
        ),
        luci.binding(
            roles = "role/swarming.poolOwner",
            groups = "mdb/v8-infra",
        ),
        luci.binding(
            roles = "role/swarming.poolViewer",
            groups = "all",
        ),
        # Allow any V8 build to trigger a test ran under chromium's task
        # service accounts.
        luci.binding(
            roles = "role/swarming.taskServiceAccount",
            users = [
                "chromium-tester@chops-service-accounts.iam.gserviceaccount.com",
                "chrome-gpu-gold@chops-service-accounts.iam.gserviceaccount.com",
                "chrome-gold@chops-service-accounts.iam.gserviceaccount.com",
            ],
        ),
    ],
)

## Swarming permissions

# Allow admins to use LED and "Debug" button on every V8 builder and bot.
# TODO(alexschulze): Exempt highly-privileged pool from LED and "Debug"
luci.binding(
    realm = "@root",
    roles = "role/swarming.poolUser",
    groups = "mdb/v8-infra",
)
luci.binding(
    realm = "@root",
    roles = "role/swarming.taskTriggerer",
    groups = "mdb/v8-infra",
)

# Allow cria/project-v8-led-users to use LED and "Debug" button on
# try and ci builders
def led_users(*, pool_realm, builder_realms, groups):
    luci.realm(
        name = pool_realm,
        bindings = [luci.binding(
            roles = "role/swarming.poolUser",
            groups = groups,
            users = V8_SERVICE_ACCOUNTS,
        )],
    )
    for br in builder_realms:
        luci.binding(
            realm = br,
            roles = "role/swarming.taskTriggerer",
            groups = groups,
            users = V8_SERVICE_ACCOUNTS,
        )

led_users(
    pool_realm = "pools/ci",
    builder_realms = [
        "ci",
        "ci.br.beta",
        "ci.br.stable",
        "ci.br.extended",
        "ci.br.extwin",
    ],
    groups = "project-v8-led-users",
)

led_users(
    pool_realm = "pools/try",
    builder_realms = ["try", "try.triggered"],
    groups = "project-v8-led-users",
)

luci.realm(
    name = "pools/highly-privileged",
    bindings = [
        luci.binding(
            roles = "role/swarming.poolUser",
            projects = "devtools-frontend",
        ),
    ],
)

def grantInvocationCreator(realms, users):
    for realm in realms:
        luci.realm(name = realm, bindings = [
            # Allow try builders to create invocations in their own builds.
            luci.binding(
                roles = "role/resultdb.invocationCreator",
                users = users,
            ),
        ])

grantInvocationCreator(["try", "try.triggered"], [V8_TRY_ACCOUNT])
grantInvocationCreator(["ci"], [V8_CI_ACCOUNT, V8_PGO_ACCOUNT])

luci.logdog(gs_bucket = "chromium-luci-logdog")

luci.bucket(name = "ci", acls = waterfall_acls)
luci.bucket(name = "try", acls = tryserver_acls)
luci.bucket(name = "try.triggered", acls = tryserver_acls)
luci.bucket(name = "ci.br.beta", acls = waterfall_acls)
luci.bucket(name = "ci.br.stable", acls = waterfall_acls)
luci.bucket(name = "ci.br.extended", acls = waterfall_acls)
luci.bucket(name = "ci.br.extwin", acls = waterfall_acls)

exec("//lib/recipes.star")

exec("//builders/all.star")

exec("//cq.star")
exec("//gitiles.star")
exec("//milo.star")
exec("//notify.star")
exec("//generators.star")
