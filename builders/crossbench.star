# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/service-accounts.star", "V8_TRY_ACCOUNT")

def crossbench_cbb_builder(builder_name, recipe_path, os):
    """
    Adds a new crossbench-cbb builder

    Args:
      builder_name: Name of the builder
      recipe_path: Path to the LUCI recipe the builder should run.
      os: The os for the builder.

    """
    dims = {
        "pool": "luci.flex.try",
        "os": os,
    }

    luci.builder(
        name = builder_name,
        bucket = "try",
        dimensions = dims,
        executable = luci.recipe(
            name = recipe_path,
            cipd_package = "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build",
        ),
        service_account = V8_TRY_ACCOUNT,
    )

    # Add the builder to the "crossbench" list in milo
    luci.list_view_entry(
        list_view = "crossbench",
        builder = builder_name,
    )

crossbench_cbb_builder("Crossbench CBB Mac Try", "perf/crossbench", "Mac")

luci.cq_group(
    name = "crossbench-main-cq",
    watch = cq.refset(
        repo = "https://chromium.googlesource.com/crossbench",
        refs = ["refs/heads/main"],
    ),
    acls = [
        acl.entry(
            [acl.CQ_COMMITTER],
            groups = [
                "project-v8-committers",
            ],
        ),
    ],
    verifiers = [
        luci.cq_tryjob_verifier(
            builder = "crossbench_presubmit",
            disable_reuse = True,
        ),
        luci.cq_tryjob_verifier(
            builder = "Crossbench CBB Mac Try",
        ),
    ],
)
