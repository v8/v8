# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

RECLIENT = struct(
    DEFAULT = {
        "instance": "rbe-chromium-trusted",
        "metrics_project": "chromium-reclient-metrics",
    },
    DEFAULT_UNTRUSTED = {
        "instance": "rbe-chromium-untrusted",
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
    NO = {"use_remoteexec": False},
    NONE = {},
)

RECLIENT_JOBS = struct(
    J500 = 500,
)

def reclient_properties(use_remoteexec, reclient_jobs, name, scandeps_server):
    if use_remoteexec == None:
        return {}

    if use_remoteexec == RECLIENT.NONE or use_remoteexec == RECLIENT.NO:
        return {
            "$build/v8": {"use_remoteexec": False},
        }

    reclient = dict(use_remoteexec)
    rewrapper_env = {}
    if reclient.get("cache_silo"):
        reclient.pop("cache_silo")
        rewrapper_env.update({
            "RBE_cache_silo": name,
        })

    if reclient.get("compare"):
        reclient.pop("compare")
        rewrapper_env.update({
            "RBE_compare": "true",
        })
        reclient["ensure_verified"] = True

    if rewrapper_env:
        reclient["rewrapper_env"] = rewrapper_env

    if reclient_jobs:
        reclient["jobs"] = reclient_jobs

    if scandeps_server:
        reclient["scandeps_server"] = True

    return {
        "$build/reclient": reclient,
        "$build/v8": {"use_remoteexec": True},
    }
