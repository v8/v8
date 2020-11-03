# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "GCLIENT_VARS", "GOMA", "v8_builder", "v8_notifier")

def experiment_builder(**kwargs):
    to_notify = kwargs.pop("to_notify", None)
    if to_notify:
        builder_name = kwargs["name"]
        v8_notifier(
            name = "notification for %s" % builder_name,
            notify_emails = to_notify,
            notified_by = [builder_name],
        )

    v8_builder(in_console = "experiments/V8", **kwargs)

experiment_builder(
    name = "V8 iOS - sim",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    properties = {"$depot_tools/osx_sdk": {"sdk_version": "12a7209"}, "target_platform": "ios", "builder_group": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Linux64 - debug - perfetto - builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8", "triggers": ["V8 Linux64 - debug - perfetto"]},
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Linux64 - debug - perfetto",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"builder_group": "client.v8"},
)

experiment_builder(
    name = "V8 Linux64 gcc - debug",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8"},
    gclient_vars = [GCLIENT_VARS.V8_HEADER_INCLUDES],
    use_goma = GOMA.NO,
    to_notify = ["v8-waterfall-sheriff@grotations.appspotmail.com"],
)

experiment_builder(
    name = "V8 Linux64 - Fuzzilli",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8"},
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Linux64 - fyi",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"builder_group": "client.v8"},
    to_notify = ["jgruber@chromium.org"],
)

experiment_builder(
    name = "V8 Linux64 - debug - fyi",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"builder_group": "client.v8"},
    to_notify = ["jgruber@chromium.org"],
)

experiment_builder(
    name = "V8 Linux64 - gcov coverage",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"enable_swarming": False, "builder_group": "client.v8", "clobber": True, "coverage": "gcov"},
    use_goma = GOMA.NO,
)

experiment_builder(
    name = "V8 Linux64 TSAN - no-concurrent-marking - builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8", "triggers": ["V8 Linux64 TSAN - no-concurrent-marking"]},
    use_goma = GOMA.DEFAULT,
    to_notify = ["v8-waterfall-sheriff@grotations.appspotmail.com"],
)

experiment_builder(
    name = "V8 Linux64 TSAN - no-concurrent-marking",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    properties = {"builder_group": "client.v8"},
    to_notify = ["v8-waterfall-sheriff@grotations.appspotmail.com"],
)

experiment_builder(
    name = "V8 Linux - predictable",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8"},
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Fuchsia",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    properties = {"builder_group": "client.v8"},
)

experiment_builder(
    name = "V8 Mac64 - full debug",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8"},
    caches = [
        swarming.cache(
            path = "osx_sdk",
            name = "osx_sdk",
        ),
    ],
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Mac - arm64 - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8", "triggers": ["V8 Mac - arm64 - release"]},
    gclient_vars = [GCLIENT_VARS.XCODE12_BETA],
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Mac - arm64 - release",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"builder_group": "client.v8"},
)

experiment_builder(
    name = "V8 Mac - arm64 - sim - release builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8", "triggers": ["V8 Mac - arm64 - sim - release"]},
    gclient_vars = [GCLIENT_VARS.XCODE12_BETA],
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Mac - arm64 - sim - release",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"builder_group": "client.v8"},
)

experiment_builder(
    name = "V8 Mac - arm64 - sim - debug builder",
    bucket = "ci",
    triggered_by = ["v8-trigger"],
    dimensions = {"os": "Mac-10.15", "cpu": "x86-64"},
    properties = {"builder_group": "client.v8", "triggers": ["V8 Mac - arm64 - sim - debug"]},
    gclient_vars = [GCLIENT_VARS.XCODE12_BETA],
    use_goma = GOMA.DEFAULT,
)

experiment_builder(
    name = "V8 Mac - arm64 - sim - debug",
    bucket = "ci",
    dimensions = {"host_class": "multibot"},
    execution_timeout = 19800,
    properties = {"builder_group": "client.v8"},
)
