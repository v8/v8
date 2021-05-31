# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//definitions.star", "beta_re", "stable_re", "extended_re")

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
    refs = ["refs/branch-heads/" + beta_re],
)

luci.gitiles_poller(
    name = "v8-trigger-br-stable",
    bucket = "ci.br.stable",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/" + stable_re],
)

luci.gitiles_poller(
    name = "v8-trigger-br-extended",
    bucket = "ci.br.extended",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/" + extended_re],
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
