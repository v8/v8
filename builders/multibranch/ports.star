# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/builders.star", "multibranch_builder")
load("//lib/gclient.star", "GCLIENT_VARS")
load("//lib/lib.star", "BARRIER", "ci_pair_factory", "greedy_batching_of_1", "in_branch_console")
load("//lib/reclient.star", "RECLIENT")

def port_builder(*args, **kwargs):
    experiments = kwargs.pop("experiments", {})
    experiments["v8.resultdb"] = 100
    kwargs["experiments"] = experiments
    return multibranch_builder(*args, **kwargs)

in_category = in_branch_console("ports")
multibranch_builder_pair = ci_pair_factory(port_builder)

in_category(
    "Arm",
    multibranch_builder(
        name = "V8 Arm - builder",
        triggering_policy = greedy_batching_of_1,
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports", "target_arch": "arm", "binary_size_tracking": {"category": "linux_arm32", "binary": "d8"}},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder(
        name = "V8 Arm - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"target_arch": "arm", "builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder(
        name = "V8 Android Arm - builder",
        triggering_policy = greedy_batching_of_1,
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports", "target_arch": "arm", "target_platform": "android", "binary_size_tracking": {"category": "android_arm32", "binary": "d8"}},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder(
        name = "V8 Android Arm - verify deterministic - debug",
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports", "target_arch": "arm", "target_platform": "android", "default_targets": ["verify_deterministic_mksnapshot"]},
        use_remoteexec = RECLIENT.DEFAULT,
        first_branch_version = "12.8",
        barrier = BARRIER.NONE,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm - sim",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        gclient_vars = [GCLIENT_VARS.GCMOLE],
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm - sim - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm - sim - lite",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.TREE_CLOSER,
        tester_barrier = BARRIER.NONE,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm - sim - lite - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.TREE_CLOSER,
        tester_barrier = BARRIER.NONE,
    ),
)

in_category(
    "Arm64",
    multibranch_builder(
        name = "V8 Arm64 - builder",
        triggering_policy = greedy_batching_of_1,
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"target_arch": "arm", "target_bits": 64, "builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder(
        name = "V8 Android Arm64 - builder",
        triggering_policy = greedy_batching_of_1,
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports", "target_arch": "arm", "target_platform": "android", "binary_size_tracking": {"category": "android_arm64", "binary": "d8"}},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder(
        name = "V8 Android Arm64 - debug builder",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"target_platform": "android", "target_arch": "arm", "builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
    ),
    multibranch_builder(
        name = "V8 Android Arm64 - N5X",
        parent_builder = "V8 Android Arm64 - builder",
        properties = {"builder_group": "client.v8.ports"},
        barrier = BARRIER.NONE,
        notifies = ["V8 Flake Sheriff"],
        disable_resultdb_exports = True,
    ),
    multibranch_builder(
        name = "V8 Linux64 - arm64 - no wasm - debug builder",
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {
            "builder_group": "client.v8.ports",
            "target_arch": "arm",
            "target_bits": 64,
        },
        use_remoteexec = RECLIENT.DEFAULT,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm64 - sim",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        gclient_vars = [GCLIENT_VARS.GCMOLE],
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm64 - sim - debug",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.LKGR_TREE_CLOSER,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - arm64 - sim - gc stress",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        execution_timeout = 23400,
        properties = {"builder_group": "client.v8.ports"},
        tester_notifies = ["V8 Flake Sheriff"],
        use_remoteexec = RECLIENT.DEFAULT,
    ),
    multibranch_builder_pair(
        name = "V8 Linux64 - arm64 - sim - no pointer compression",
        triggered_by_gitiles = True,
        tester_execution_timeout = 19800,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        tester_notifies = ["V8 Flake Sheriff"],
        first_branch_version = "10.6",
        use_remoteexec = RECLIENT.DEFAULT,
        disable_resultdb_exports = True,
    ),
)

in_category(
    "Mips",
    multibranch_builder_pair(
        name = "V8 Linux - mips64el - sim",
        triggered_by_gitiles = True,
        tester_execution_timeout = 19800,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.NONE,
        disable_resultdb_exports = True,
    ),
)

in_category(
    "IBM",
    multibranch_builder_pair(
        name = "V8 Linux - ppc64 - sim",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.NONE,
        disable_resultdb_exports = True,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - s390x - sim",
        triggered_by_gitiles = True,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.NONE,
        disable_resultdb_exports = True,
    ),
)

in_category(
    "RISC-V",
    multibranch_builder_pair(
        name = "V8 Linux - riscv32 - sim",
        tester_execution_timeout = 19800,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        barrier = BARRIER.NONE,
        first_branch_version = "10.5",
        disable_resultdb_exports = True,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - riscv64 - sim",
        tester_execution_timeout = 19800,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        barrier = BARRIER.NONE,
        disable_resultdb_exports = True,
    ),
    multibranch_builder_pair(
        name = "V8 Linux - riscv64 - sim - pointer compression",
        tester_execution_timeout = 19800,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        barrier = BARRIER.NONE,
        first_branch_version = "11.8",
        disable_resultdb_exports = True,
    ),
)

in_category(
    "Loongson",
    multibranch_builder_pair(
        name = "V8 Linux - loong64 - sim",
        triggered_by_gitiles = True,
        tester_execution_timeout = 19800,
        dimensions = {"os": "Ubuntu-22.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8.ports"},
        use_remoteexec = RECLIENT.DEFAULT,
        barrier = BARRIER.NONE,
        disable_resultdb_exports = True,
    ),
)
