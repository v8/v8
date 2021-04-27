# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

_RECIPE_NAME_PREFIX = "recipe:"

def _recipe_for_package(cipd_package):
    def recipe(*, name, cipd_version = None, recipe = None):
        # Force the caller to put the recipe prefix rather than adding it
        # programatically to make the string greppable
        if not name.startswith(_RECIPE_NAME_PREFIX):
            fail("Recipe name {!r} does not start with {!r}"
                .format(name, _RECIPE_NAME_PREFIX))
        if recipe == None:
            recipe = name[len(_RECIPE_NAME_PREFIX):]
        return luci.recipe(
            name = name,
            cipd_package = cipd_package,
            cipd_version = cipd_version,
            recipe = recipe,
            use_bbagent = True,
        )

    return recipe

build_recipe = _recipe_for_package(
    "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build",
)

build_recipe(
    name = "recipe:chromium",
)

build_recipe(
    name = "recipe:chromium_integration",
)

build_recipe(
    name = "recipe:chromium_trybot",
)

build_recipe(
    name = "recipe:run_presubmit",
)

build_recipe(
    name = "recipe:v8",
)

build_recipe(
    name = "recipe:v8/archive",
)

build_recipe(
    name = "recipe:v8/auto_roll_deps",
)

build_recipe(
    name = "recipe:v8/auto_roll_push",
)

build_recipe(
    name = "recipe:v8/auto_roll_release_process",
)

build_recipe(
    name = "recipe:v8/auto_roll_v8_deps",
)

build_recipe(
    name = "recipe:v8/auto_tag",
)

build_recipe(
    name = "recipe:v8/flako",
)

build_recipe(
    name = "recipe:v8/node_integration_ng",
)

build_recipe(
    name = "recipe:v8/verify_flakes",
)

build_recipe(
    name = "recipe:v8/presubmit",
)

build_recipe(
    name = "recipe:lkgr_finder",
)
