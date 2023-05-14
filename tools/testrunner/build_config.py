# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Increase the timeout for these:
SLOW_ARCHS = [
    "arm", "arm64", "mips64", "mips64el", "s390", "s390x", "riscv32", "riscv64",
    "loong64"
]

# Timeout scale factor per build flag.
SCALE_FACTOR = dict(
    full_debug=4,
    lite_mode=2,
    tsan=2,
    use_sanitizer=1.5,
    verify_predictable=4,
)

INITIALIZATION_ERROR = f"""
Error initializing property '%s'. It depends on a build flag of V8's
build config. If you see this error in testing, you might need to add
the dependencies to tools/testrunner/testdata/v8_build_config.json. If
you see this error in production, ensure to add the dependences to the
v8_dump_build_config action in V8's top-level BUILD.gn file.
"""

class _BuildConfigInternal(object):
  """Placeholder for all attributes and properties of the build config.

  It's initialized with all attributes of the v8_dump_build_config action
  in V8's top-level BUILD.gn file. Additionally, this defines read-only
  properties using other attributes for convenience.
  """

  def __init__(self, build_config):
    for key, value in build_config.items():
      setattr(self, key, value)

  @property
  def arch(self):
    # In V8 land, GN's x86 is called ia32.
    return 'ia32' if self.v8_target_cpu == 'x86' else self.v8_target_cpu

  @property
  def simulator_run(self):
    return self.target_cpu != self.v8_target_cpu

  @property
  def use_sanitizer(self):
    return self.asan or self.cfi or self.msan or self.tsan or self.ubsan

  @property
  def no_js_shared_memory(self):
    return (
        not self.shared_ro_heap
        or self.pointer_compression and not self.pointer_compression_shared_cage
        or not self.write_barriers)

  @property
  def mips_arch(self):
    return self.arch in ['mips64', 'mips64el']

  @property
  def simd_mips(self):
    return (self.mips_arch and self.mips_arch_variant == "r6" and
            self.mips_use_msa)


class BuildConfig(object):
  """Enables accessing all build-time flags as set in V8's BUILD.gn file.

  All flags are auto-generated based on the output of V8's
  v8_dump_build_config action.
  """

  def __init__(self, build_config):
    self.internal = _BuildConfigInternal(build_config)

    for key in self.keys():
      try:
        setattr(self, key, getattr(self.internal, key))
      except AttributeError as e:
        raise Exception(INITIALIZATION_ERROR % key)

    bool_options = [key for key, value in self.items() if value is True]
    string_options = [
      f'{key}="{value}"'
      for key, value in self.items() if value and isinstance(value, str)]
    self._str_rep = ', '.join(sorted(bool_options + string_options))

  def keys(self):
    for key in dir(self.internal):
      if not key.startswith('_'):
        yield key

  def items(self):
    for key in self.keys():
      yield key, getattr(self, key)

  def timeout_scalefactor(self, initial_factor):
    """Increases timeout for slow build configurations."""
    result = initial_factor
    for key, value in SCALE_FACTOR.items():
      try:
        if getattr(self, key):
          result *= value
      except AttributeError:
        raise Exception(INITIALIZATION_ERROR % k)
    if self.arch in SLOW_ARCHS:
      result *= 4.5
    return result

  def __str__(self):
    return self._str_rep
