# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/service-accounts.star", "V8_TRY_ACCOUNT")
load("//lib/builders.star", "presubmit_builder")

def crossbench_cbb_builder(builder_name, recipe_path, os, cpu, caches = None, properties = None):
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
        "cpu": cpu,
    }

    luci.builder(
        name = builder_name,
        bucket = "crossbench.try",
        dimensions = dims,
        executable = luci.recipe(
            name = recipe_path,
            cipd_package = "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build",
        ),
        service_account = V8_TRY_ACCOUNT,
        caches = caches or [],
        properties = properties,
    )

    # Add the builder to the "crossbench" list in milo
    luci.list_view_entry(
        list_view = "crossbench",
        builder = builder_name,
    )

presubmit_builder("Crossbench Presubmit", "crossbench.try", timeout = 900, console = "crossbench")
crossbench_cbb_builder("Crossbench End2End Mac arm64 Try", "perf/crossbench", "Mac", "arm64")
crossbench_cbb_builder("Crossbench End2End Linux x64 Try", "perf/crossbench", "Ubuntu-20", "x86-64")
crossbench_cbb_builder("Crossbench End2End Windows x64 Try", "perf/crossbench", "Windows-10", "x86-64")
crossbench_cbb_builder(
    "Crossbench End2End Android x64 Try",
    "perf/crossbench_android",
    "Ubuntu-22.04",
    "x86-64",
    properties = {"android_sdk": 33},
)
crossbench_cbb_builder(
    "Crossbench Pytype Try",
    "perf/pytype",
    "Ubuntu-20",
    "x86-64",
    caches = [
        swarming.cache(
            name = "crossbench_pytype_cache",
            path = "pytype",
        ),
    ],
    properties = {"timeout": 1200},
)

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
                "project-v8-submit-access",
            ],
        ),
    ],
    verifiers = [
        luci.cq_tryjob_verifier(
            builder = "Crossbench Presubmit",
            disable_reuse = True,
        ),
        luci.cq_tryjob_verifier(
            builder = "Crossbench End2End Mac arm64 Try",
        ),
        luci.cq_tryjob_verifier(
            builder = "Crossbench End2End Linux x64 Try",
        ),
        luci.cq_tryjob_verifier(
            builder = "Crossbench End2End Windows x64 Try",
            experiment_percentage = 50,
        ),
        luci.cq_tryjob_verifier(
            builder = "Crossbench End2End Android x64 Try",
        ),
        luci.cq_tryjob_verifier(
            builder = "Crossbench Pytype Try",
        ),
    ],
)
