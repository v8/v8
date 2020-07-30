load("//lib.star", "v8_auto")

v8_auto(
    name = "Auto-tag",
    recipe = "v8/auto_tag",
    mastername = "client.v8.branches",
    execution_timeout = 21600,
)
v8_auto(
    name = "V8 lkgr finder",
    recipe = "lkgr_finder",
    cipd_package = "infra/recipe_bundles/chromium.googlesource.com/infra/infra",
    cipd_version = "refs/heads/master",
    lkgr_project = "v8",
    allowed_lag = 4,
)

v8_auto(
    name = "Auto-roll - push",
    recipe = "v8/auto_roll_push",
)
v8_auto(
    name = "Auto-roll - deps",
    recipe = "v8/auto_roll_deps",
)
v8_auto(
    name = "Auto-roll - v8 deps",
    recipe = "v8/auto_roll_v8_deps",
)
v8_auto(
    name = "Auto-roll - test262",
    recipe = "v8/auto_roll_v8_deps",
)
v8_auto(
    name = "Auto-roll - wasm-spec",
    recipe = "v8/auto_roll_v8_deps",
)
v8_auto(
    name = "Auto-roll - release process",
    recipe = "v8/auto_roll_release_process",
)
