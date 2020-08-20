load(
    "//lib.star",
    "tryserver_acls",
    "waterfall_acls",
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
)

luci.logdog(gs_bucket = "chromium-luci-logdog")

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
    refs = ["refs/branch-heads/8\\.6"],
)
luci.gitiles_poller(
    name = "v8-trigger-br-stable",
    bucket = "ci.br.stable",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/8\\.5"],
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

luci.cq(
    submit_max_burst = 1,
    submit_burst_delay = 60 * time.second,
    status_host = "chromium-cq-status.appspot.com",
)

luci.cq_group(
    name = "infra-cq",
    watch = cq.refset(
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = ["refs/heads/infra/config"],
    ),
    retry_config = cq.retry_config(
        single_quota = 2,
        global_quota = 4,
        failure_weight = 2,
        transient_failure_weight = 1,
        timeout_weight = 4,
    ),
    verifiers = [luci.cq_tryjob_verifier(
        "try/v8_presubmit",
        disable_reuse = True,
        cancel_stale = False,
    )],
)
luci.cq_group(
    name = "v8-cq",
    watch = cq.refset(
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = [
            "refs/heads/master",
            "refs/branch-heads/.+",
        ],
    ),
    tree_status_host = "v8-status.appspot.com",
    retry_config = cq.retry_config(
        single_quota = 2,
        global_quota = 4,
        failure_weight = 2,
        transient_failure_weight = 1,
        timeout_weight = 4,
    ),
    verifiers = [
        luci.cq_tryjob_verifier(
            "chromium:try/cast_shell_android",
            experiment_percentage = 20,
        ),
        luci.cq_tryjob_verifier(
            "chromium:try/cast_shell_linux",
            experiment_percentage = 20,
        ),
        luci.cq_tryjob_verifier(
            "chromium:try/linux-blink-rel",
            location_regexp = [".+/[+]/src/inspector/.+", ".+/[+]/test/inspector/.+"],
        ),
        luci.cq_tryjob_verifier(
            "chromium:try/linux-rel",
            location_regexp = [
                ".+/[+]/include/.+\\.h",
                ".+/[+]/src/api\\.cc",
                ".+/[+]/src/inspector/.+",
                ".+/[+]/src/message-template\\.h",
                ".+/[+]/test/inspector/.+",
            ],
        ),
        luci.cq_tryjob_verifier(
            "node-ci:try/node_ci_linux64_rel",
            cancel_stale = False,
        ),
    ],
)

exec("//auto.star")
exec("//branch_coverage.star")
exec("//perf.star")
exec("//others.star")
exec("//try.star")
exec("//try_ng.star")
exec("//milo.star")
exec("//notify.star")
