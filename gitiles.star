# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

luci.gitiles_poller(
    name = "chromium-trigger",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/chromium/src",
    refs = ["refs/heads/master"],
)

luci.gitiles_poller(
    name = "v8-trigger",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/heads/master"],
)

luci.gitiles_poller(
    name = "v8-trigger-br-beta",
    bucket = "ci.br.beta",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/8\\.6"],
)

luci.gitiles_poller(
    name = "v8-trigger-br-stable",
    bucket = "ci.br.stable",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/8\\.5"],
)

luci.gitiles_poller(
    name = "v8-trigger-official",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = [
        "refs/branch-heads/\\d+\\.\\d+",
        "refs/heads/\\d+\\.\\d+\\.\\d+",
    ],
)

luci.gitiles_poller(
    name = "v8-trigger-branches-auto-tag",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/\\d+\\.\\d+"],
)
