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