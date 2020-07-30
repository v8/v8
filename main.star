load(
    "//lib.star",
    "tryserver_acls",
    "waterfall_acls",
)

lucicfg.config(
    config_dir = "generated",
    tracked_files = [
        "cr-buildbucket.cfg",
        "luci-scheduler.cfg",
        "project.cfg",
    ],
    fail_on_warnings = True,
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
    ],
)

luci.bucket(name = "ci", acls = waterfall_acls)
luci.bucket(name = "try", acls = tryserver_acls)
luci.bucket(name = "try.triggered", acls = tryserver_acls)
luci.bucket(name = "ci.br.beta", acls = waterfall_acls)
luci.bucket(name = "ci.br.stable", acls = waterfall_acls)

luci.gitiles_poller(
    name = "chromium-trigger",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/chromium/src",
    refs = ["refs/heads/master"],
)
luci.gitiles_poller(
    name = "v8-trigger",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/heads/master"],
)
luci.gitiles_poller(
    name = "v8-trigger-br-beta",
    bucket = "ci.br.beta",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/8\\.5"],
)
luci.gitiles_poller(
    name = "v8-trigger-br-stable",
    bucket = "ci.br.stable",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/8\\.4"],
)
luci.gitiles_poller(
    name = "v8-trigger-official",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = [
        "refs/branch-heads/\\d+\\.\\d+",
        "refs/heads/\\d+\\.\\d+\\.\\d+",
    ],
)
luci.gitiles_poller(
    name = "v8-trigger-branches-auto-tag",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/\\d+\\.\\d+"],
)
"""
luci.gitiles_poller(
    name = "v8-try-triggered",
    bucket = "try.triggered",
    repo = "https://chromium.googlesource.com/v8/v8",
)
"""

exec("//auto.star")
exec("//branch_coverage.star")
exec("//try_ng.star")
exec("//others.star")
