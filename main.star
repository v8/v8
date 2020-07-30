load(
    "//lib.star",
    "tryserver_acls",
    "waterfall_acls",
)

lucicfg.config(
    config_dir = "generated",
    tracked_files = [
        "cr-buildbucket.cfg",
        "project.cfg",
    ],
    fail_on_warnings = True,
)

luci.project(
    name = "V8",
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
                "project-v8-admins",
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

exec("//auto.star")
exec("//branch_coverage.star")
exec("//try_ng.star")
exec("//others.star")
