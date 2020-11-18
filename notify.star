# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "FAILED_STEPS_EXCLUDE")

luci.notifier(
    name = "beta/stable notifier",
    on_occurrence = ["FAILURE"],
    failed_step_regexp_exclude = FAILED_STEPS_EXCLUDE,
    notify_emails = [
        "v8-waterfall-sheriff@grotations.appspotmail.com",
    ],
)

# Enable luci.tree_closer.
lucicfg.enable_experiment("crbug.com/1054172")

luci.notify(tree_closing_enabled = True)

luci.tree_closer(
    name = "v8 tree closer",
    tree_status_host = "v8-status.appspot.com",
    failed_step_regexp_exclude = [
            "bot_update",
            "isolate tests",
            "package build",
            "extract build",
            "cleanup_temp",
            "gsutil upload",
            "taskkill",
            "Failure reason",
            "steps",
            ".* \\(flakes\\)",
            ".* \\(retry shards with patch\\)",
            ".* \\(with patch\\)",
            ".* \\(without patch\\)",
    ],
)

luci.notifier(
    name = "api stability notifier",
    on_occurrence = ["FAILURE"],
    failed_step_regexp = [
        "compile",
    ],
    notify_emails = [
        "machenbach@chromium.org",
        "hablich@chromium.org",
    ],
    notified_by = [
        "Linux V8 API Stability",
    ],
)

luci.notifier(
    name = "vtunejit notifier",
    on_occurrence = ["FAILURE"],
    failed_step_regexp = [
        "gclient runhooks",
        "compile",
    ],
    notify_emails = ["chunyang.dai@intel.com"],
    notified_by = ["ci/V8 Linux - vtunejit"],
)
