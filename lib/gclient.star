# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# These settings enable overwriting variables in V8's DEPS file.
GCLIENT_VARS = struct(
    CENTIPEDE = {"checkout_centipede_deps": "True"},
    INSTRUMENTED_LIBRARIES = {"checkout_instrumented_libraries": "True"},
    ITTAPI = {"checkout_ittapi": "True"},
    V8_HEADER_INCLUDES = {"check_v8_header_includes": "True"},
    GCMOLE = {"download_gcmole": "True"},
    JSFUNFUZZ = {"download_jsfunfuzz": "True"},
)

def gclient_vars_properties(props):
    gclient_vars = {}
    for prop in props:
        gclient_vars.update(prop)
    if gclient_vars:
        return {"gclient_vars": gclient_vars}
    else:
        return {}
