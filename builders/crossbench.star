# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "CQ", "v8_builder")

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
            builder = "v8_presubmit",
            disable_reuse = True,
        ),
    ],
)
