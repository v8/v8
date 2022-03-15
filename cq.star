# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

luci.cq(
    submit_max_burst = 1,
    submit_burst_delay = 60 * time.second,
    status_host = "chromium-cq-status.appspot.com",
)

luci.cq_group(
    name = "infra-cq",
    watch = cq.refset(
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = ["refs/heads/infra/config"],
    ),
    retry_config = cq.retry_config(
        single_quota = 2,
        global_quota = 4,
        failure_weight = 2,
        transient_failure_weight = 1,
        timeout_weight = 4,
    ),
    verifiers = [luci.cq_tryjob_verifier(
        "try/v8_presubmit",
        disable_reuse = True,
        cancel_stale = False,
    )],
)

luci.cq_group(
    name = "v8-cq",
    watch = cq.refset(
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = [
            "refs/heads/main",
        ],
    ),
    tree_status_host = "v8-status.appspot.com",
    retry_config = cq.retry_config(
        single_quota = 2,
        global_quota = 4,
        failure_weight = 2,
        transient_failure_weight = 1,
        timeout_weight = 4,
    ),
    verifiers = [
        # TODO machenbach: maybe remove these experimental chromium builders
        # It was a trial, never brought anything. Just wastes resources.
        luci.cq_tryjob_verifier(
            "chromium:try/cast_shell_android",
            experiment_percentage = 20,
        ),
        luci.cq_tryjob_verifier(
            "chromium:try/cast_shell_linux",
            experiment_percentage = 20,
        ),
        luci.cq_tryjob_verifier(
            "chromium:try/linux-blink-rel",
            location_regexp = [
                ".+/[+]/include/cppgc/.+",
                ".+/[+]/src/inspector/.+",
                ".+/[+]/src/wasm/wasm-feature-flags\\.h",
                ".+/[+]/src/wasm/wasm-js\\.{h,cc}",
                ".+/[+]/test/inspector/.+",
            ],
        ),
        luci.cq_tryjob_verifier(
            "chromium:try/linux-rel",
            location_regexp = [
                ".+/[+]/include/.+\\.h",
                ".+/[+]/src/api\\.cc",
                ".+/[+]/src/inspector/.+",
                ".+/[+]/src/message-template\\.h",
                ".+/[+]/test/inspector/.+",
            ],
        ),
        luci.cq_tryjob_verifier(
            "node-ci:try/node_ci_linux64_rel",
            cancel_stale = False,
        ),
    ],
)

luci.cq_group(
    name = "v8-branch-cq",
    watch = cq.refset(
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = [
            "refs/branch-heads/.+",
        ],
    ),
    tree_status_host = "v8-status.appspot.com",
    retry_config = cq.retry_config(
        single_quota = 2,
        global_quota = 4,
        failure_weight = 2,
        transient_failure_weight = 1,
        timeout_weight = 4,
    ),
)
