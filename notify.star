# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "FAILED_STEPS_EXCLUDE", "v8_notifier")

v8_notifier(
    name = "sheriffs",
    on_occurrence = ["FAILURE"],
    failed_step_regexp_exclude = FAILED_STEPS_EXCLUDE,
    notify_emails = [
        "v8-waterfall-sheriff@grotations.appspotmail.com",
        "mtv-sf-v8-sheriff@grotations.appspotmail.com",
        "v8-infra-alerts-cc@google.com",
    ],
)

v8_notifier(
    name = "sheriffs on new failure",
    on_new_status = ["FAILURE", "INFRA_FAILURE"],
    notify_emails = [
        "v8-waterfall-sheriff@grotations.appspotmail.com",
        "mtv-sf-v8-sheriff@grotations.appspotmail.com",
        "v8-infra-alerts-cc@google.com",
    ],
)

v8_notifier(
    name = "blamelist",
    on_new_status = ["FAILURE"],
    notify_blamelist = True,
)

v8_notifier(
    name = "v8-infra-cc",
    on_new_status = ["FAILURE"],
    notify_emails = ["v8-infra-alerts-cc@google.com"],
)

v8_notifier(
    name = "infra",
    on_occurrence = ["FAILURE"],
    failed_step_regexp_exclude = FAILED_STEPS_EXCLUDE,
    notify_emails = [
        "v8-infra@google.com",
    ],
)

v8_notifier(
    name = "infra-failure",
    on_occurrence = ["INFRA_FAILURE"],
    notify_emails = [
        "v8-infra@google.com",
    ],
)

v8_notifier(
    name = "memory sheriffs",
    on_occurrence = ["FAILURE"],
    failed_step_regexp_exclude = FAILED_STEPS_EXCLUDE,
    notify_emails = [
        "v8-memory-sheriffs@google.com",
        "v8-infra-alerts-cc@google.com",
    ],
)

v8_notifier(
    name = "NumFuzz maintainer",
    on_occurrence = ["FAILURE"],
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
        ".* \\(retry shards with patch\\)",
        ".* \\(with patch\\)",
        ".* \\(without patch\\)",
    ],
    notify_emails = [
        "almuthanna@chromium.org",
        "v8-infra-alerts-cc@google.com",
    ],
)

v8_notifier(
    name = "jsvu/esvu maintainer",
    on_occurrence = ["FAILURE"],
    failed_step_regexp_exclude = FAILED_STEPS_EXCLUDE,
    notify_emails = [
        "mathiasb@google.com",
        "v8-infra-alerts-cc@google.com",
    ],
)

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

v8_notifier(
    name = "api stability notifier",
    on_new_status = ["FAILURE"],
    notify_emails = [
        "machenbach@chromium.org",
        "hablich@chromium.org",
        "v8-infra-alerts-cc@google.com",
    ],
    notified_by = [
        "Linux V8 API Stability",
    ],
)

v8_notifier(
    name = "vtunejit notifier",
    on_occurrence = ["FAILURE"],
    failed_step_regexp = [
        "gclient runhooks",
        "compile",
    ],
    notify_emails = [
        "chunyang.dai@intel.com",
        "v8-infra-alerts-cc@google.com",
    ],
    notified_by = ["ci/V8 Linux - vtunejit"],
)
