load("//lib.star", "v8_auto", "v8_builder")

v8_auto(
    name = "Auto-tag",
    recipe = "v8/auto_tag",
    execution_timeout = 21600,
    properties = {"mastername": "client.v8.branches"},
    schedule = "triggered",
    triggering_policy = scheduler.policy(
        kind = scheduler.GREEDY_BATCHING_KIND,
        max_batch_size = 1,
    ),
    triggered_by = ["v8-trigger-branches-auto-tag"],
)
v8_auto(
    name = "V8 lkgr finder",
    recipe = "lkgr_finder",
    cipd_package = "infra/recipe_bundles/chromium.googlesource.com/infra/infra",
    cipd_version = "refs/heads/master",
    properties = {
        "lkgr_project": "v8",
        "allowed_lag": 4,
    },
    schedule = "8,23,38,53 * * * *",
)
v8_auto(
    name = "Auto-roll - push",
    recipe = "v8/auto_roll_push",
    schedule = "12,27,42,57 * * * *",
)
v8_auto(
    name = "Auto-roll - deps",
    recipe = "v8/auto_roll_deps",
    schedule = "0,15,30,45 * * * *",
)
v8_auto(
    name = "Auto-roll - v8 deps",
    recipe = "v8/auto_roll_v8_deps",
    schedule = "0 3 * * *",
)
v8_auto(
    name = "Auto-roll - test262",
    recipe = "v8/auto_roll_v8_deps",
    schedule = "0 14 * * *",
)
v8_auto(
    name = "Auto-roll - wasm-spec",
    recipe = "v8/auto_roll_v8_deps",
    schedule = "0 4 * * *",
)
v8_auto(
    name = "Auto-roll - release process",
    recipe = "v8/auto_roll_release_process",
    schedule = "10,25,40,55 * * * *",
)

v8_builder(
    name = "v8_verify_flakes",
    bucket = "try.triggered",
    executable = {"name": "v8/verify_flakes"},
    execution_timeout = 16200,
    schedule = "with 3h interval",
)
