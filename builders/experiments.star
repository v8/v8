# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "ci_pair_factory", "in_console", "v8_builder", "v8_failure_notifier")

RECLIENT = struct(
    DEFAULT = {
        "instance": "rbe-chromium-trusted",
        "metrics_project": "chromium-reclient-metrics",
    },
    CACHE_SILO = {
        "instance": "rbe-chromium-trusted",
        "metrics_project": "chromium-reclient-metrics",
        "cache_silo": True,
    },
    COMPARE = {
        "instance": "rbe-chromium-trusted",
        "metrics_project": "chromium-reclient-metrics",
        "compare": True,
    },
)

def experiment_builder(**kwargs):
    notify_owners = kwargs.pop("notify_owners", None)
    if notify_owners:
        builder_name = kwargs["name"]
        v8_failure_notifier(
            name = "notification for %s" % builder_name,
            notify_emails = notify_owners,
            notified_by = [builder_name],
        )
    bucket = kwargs.pop("bucket", "ci")
    return v8_builder(
        bucket = bucket,
        **kwargs
    )

in_category = in_console("experiments")
experiment_builder_pair = ci_pair_factory(experiment_builder)

in_category(
    "Features",
    experiment_builder_pair(
        name = "V8 Linux64 - cppgc-non-default - debug",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["mlippautz@chromium.org"],
        notifies = ["blamelist"],
    ),
    experiment_builder_pair(
        name = "V8 Linux64 - debug - perfetto",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["skyostil@google.com"],
        notifies = ["blamelist"],
    ),
    experiment_builder_pair(
        name = "V8 Linux64 - debug - single generation",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["dinfuehr@chromium.org"],
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
    experiment_builder_pair(
        name = "V8 Linux64 - disable runtime call stats",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["cbruni@chromium.org"],
        notifies = ["blamelist"],
    ),
    experiment_builder_pair(
        name = "V8 Linux64 - external code space - debug",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["ishell@chromium.org"],
        notifies = ["blamelist"],
    ),
    experiment_builder(
        name = "V8 Linux64 - Fuzzilli - builder",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["saelo@google.com", "msarm@google.com"],
        notifies = ["blamelist"],
    ),
)

in_category(
    "Fuchsia",
    experiment_builder(
        name = "V8 Fuchsia",
        parent_builder = "V8 Fuchsia - builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        notifies = ["v8-infra-cc"],
    ),
)

in_category(
    "FYI",
    experiment_builder(
        name = "V8 Linux64 - debug - fyi",
        parent_builder = "V8 Linux64 - debug builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        notify_owners = ["jgruber@chromium.org"],
        notifies = ["blamelist"],
    ),
    experiment_builder(
        name = "V8 Linux64 - fyi",
        parent_builder = "V8 Linux64 - builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        notify_owners = ["jgruber@chromium.org"],
        notifies = ["blamelist"],
    ),
    experiment_builder_pair(
        name = "V8 Linux64 gcc",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-20.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
    experiment_builder(
        name = "V8 Linux64 gcc - debug builder",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-20.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
    experiment_builder(
        name = "V8 Linux64 - gcov coverage",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"enable_swarming": False, "builder_group": "client.v8", "clobber": True, "coverage": "gcov"},
        use_goma = GOMA.NO,
        execution_timeout = 10800,
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
    experiment_builder(
        name = "V8 Linux64 - coverage",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"enable_swarming": False, "builder_group": "client.v8", "gclient_vars": {"checkout_clang_coverage_tools": "True"}},
        use_goma = GOMA.DEFAULT,
        execution_timeout = 7200,
        notify_owners = ["machenbach@chromium.org"],
    ),
    experiment_builder(
        name = "V8 Linux64 - minor mc - debug",
        parent_builder = "V8 Linux64 - debug builder",
        dimensions = {"host_class": "multibot"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["omerkatz@chromium.org"],
    ),
    experiment_builder_pair(
        name = "V8 Linux - predictable",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
)

in_category(
    "Heap Sandbox",
    experiment_builder_pair(
        name = "V8 Linux64 - arm64 - sim - heap sandbox - debug",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notify_owners = ["saelo@chromium.org"],
        notifies = ["sheriffs on new failure", "blamelist"],
        description = {
            "purpose": "Arm64 simulator heap sandbox",
            "request": "https://crbug.com/v8/12257",
            "cq_base": "v8_linux_arm64_sim_heap_sandbox_dbg",
        },
    ),
)

in_category(
    "Linux64 no sandbox",
    experiment_builder_pair(
        name = "V8 Linux64 - no sandbox",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
        close_tree = False,
        triggered_by = ["v8-trigger"],
    ),
    experiment_builder_pair(
        name = "V8 Linux64 - no sandbox - debug",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        use_goma = GOMA.DEFAULT,
        close_tree = False,
        triggered_by = ["v8-trigger"],
    ),
)

in_category(
    "Mac",
    experiment_builder(
        name = "V8 iOS - sim - builder",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"$depot_tools/osx_sdk": {"sdk_version": "12d4e"}, "target_platform": "ios", "builder_group": "client.v8"},
        caches = [
            swarming.cache(
                path = "osx_sdk",
                name = "osx_sdk",
            ),
        ],
        use_goma = GOMA.DEFAULT,
        notifies = ["v8-infra-cc"],
    ),
    experiment_builder(
        name = "V8 Mac - arm64 - sim - debug",
        parent_builder = "V8 Mac - arm64 - sim - debug builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
    experiment_builder(
        name = "V8 Mac - arm64 - sim - release",
        parent_builder = "V8 Mac - arm64 - sim - release builder",
        dimensions = {"host_class": "multibot"},
        execution_timeout = 19800,
        properties = {"builder_group": "client.v8"},
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
    experiment_builder(
        name = "V8 Mac64 - full debug builder",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.DEFAULT,
        notifies = ["sheriffs on new failure", "blamelist"],
    ),
)

in_category(
    "Reclient",
    experiment_builder(
        name = "V8 Linux64 - builder (goma cache silo)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Linux64 - builder (reclient)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Linux64 - builder (reclient compare)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.COMPARE,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Linux64 - node.js integration ng (goma cache silo)",
        triggered_by = ["v8-trigger"],
        executable = "recipe:v8/node_integration_ng",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"v8_tot": True, "builder_group": "client.v8"},
        use_goma = GOMA.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Linux64 - node.js integration ng (reclient)",
        triggered_by = ["v8-trigger"],
        executable = "recipe:v8/node_integration_ng",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"v8_tot": True, "builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Linux64 - node.js integration ng (reclient compare)",
        triggered_by = ["v8-trigger"],
        executable = "recipe:v8/node_integration_ng",
        dimensions = {"os": "Ubuntu-18.04", "cpu": "x86-64"},
        properties = {"v8_tot": True, "builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.COMPARE,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Win32 - builder (goma cache silo)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Win32 - builder (reclient)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Win32 - builder (reclient compare)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        execution_timeout = 9000,
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.COMPARE,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Win64 - builder (reclient)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Win64 - builder (reclient compare)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Windows-10", "cpu": "x86-64"},
        execution_timeout = 9000,
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.COMPARE,
        notify_owners = ["richardwa@google.com"],
    ),
    experiment_builder(
        name = "V8 Mac64 - builder (reclient)",
        triggered_by = ["v8-trigger"],
        dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
        properties = {"builder_group": "client.v8"},
        use_goma = GOMA.NO,
        use_remoteexec = RECLIENT.CACHE_SILO,
        notify_owners = ["richardwa@google.com"],
    ),
)
