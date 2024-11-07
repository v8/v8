# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def siso_properties(project):
    return {
        "$build/siso": {
            "configs": [
                "builder",
            ],
            "enable_cloud_profiler": True,
            "enable_cloud_trace": True,
            "experiments": [],
            "project": project,
            "remote_jobs": 250,
        },
    }

SISO = struct(
    CHROMIUM_TRUSTED = siso_properties("rbe-chromium-trusted"),
    CHROMIUM_UNTRUSTED = siso_properties("rbe-chromium-untrusted"),
    NONE = {},
)
