# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from testrunner.local import utils

# Increase the timeout for these:
SLOW_ARCHS = [
    "arm", "arm64", "mips64", "mips64el", "s390", "s390x", "riscv32", "riscv64",
    "loong64"
]


class BuildConfig(object):

  def __init__(self, build_config, options):
    self.options = options

    self.asan = build_config['asan']
    self.cfi = build_config['cfi']
    self.code_comments = build_config['code_comments']
    self.component_build = build_config['component_build']
    self.concurrent_marking = build_config['concurrent_marking']
    self.conservative_stack_scanning = build_config[
        'conservative_stack_scanning']
    self.current_cpu = build_config['current_cpu']
    self.v8_cfi = build_config['v8_cfi']
    self.dcheck_always_on = build_config['dcheck_always_on']
    self.debug_code = build_config['debug_code']
    self.dict_property_const_tracking = build_config[
        'dict_property_const_tracking']
    self.direct_local = build_config['direct_local']
    self.disassembler = build_config['disassembler']
    self.gdbjit = build_config['gdbjit']
    self.is_android = build_config['is_android']
    self.clang = build_config['clang']
    self.clang_coverage = build_config['clang_coverage']
    self.debugging_features = build_config['debugging_features']
    self.DEBUG_defined = build_config['DEBUG_defined']
    self.full_debug = build_config['full_debug']
    self.is_ios = build_config['is_ios']
    self.official_build = build_config['official_build']
    self.lite_mode = build_config['lite_mode']
    self.has_maglev = build_config['has_maglev']
    self.msan = build_config['msan']
    self.i18n = build_config['i18n']
    self.pointer_compression = build_config['pointer_compression']
    self.pointer_compression_shared_cage = build_config[
        'pointer_compression_shared_cage']
    self.verify_predictable = build_config['verify_predictable']
    self.sandbox = build_config['sandbox']
    self.shared_ro_heap = build_config['shared_ro_heap']
    self.single_generation = build_config['single_generation']
    self.slow_dchecks = build_config['slow_dchecks']
    self.third_party_heap = build_config['third_party_heap']
    self.tsan = build_config['tsan']
    self.has_turbofan = build_config['has_turbofan']
    self.ubsan = build_config['ubsan']
    self.verify_csa = build_config['verify_csa']
    self.verify_heap = build_config['verify_heap']
    self.has_webassembly = build_config['has_webassembly']
    self.write_barriers = build_config['write_barriers']
    self.has_jitless = build_config['has_jitless']
    self.target_cpu = build_config['target_cpu']
    self.v8_current_cpu = build_config['v8_current_cpu']
    self.v8_target_cpu = build_config['v8_target_cpu']
    self.mips_arch_variant = build_config['mips_arch_variant']
    self.mips_use_msa = build_config['mips_use_msa']

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

  @property
  def no_simd_hardware(self):
    # TODO(liviurau): Add some tests and refactor the logic here.
    # We try to find all the reasons why we have no_simd.
    no_simd_hardware = any(i in self.options.extra_flags for i in [
        '--noenable-sse3', '--no-enable-sse3', '--noenable-ssse3',
        '--no-enable-ssse3', '--noenable-sse4-1', '--no-enable-sse4_1'
    ])

    # Set no_simd_hardware on architectures without Simd enabled.
    if self.arch == 'mips64el':
      no_simd_hardware = not self.simd_mips

    if self.arch == 'loong64'  or \
       self.arch == 'riscv32':
      no_simd_hardware = True

    # S390 hosts without VEF1 do not support Simd.
    if self.arch == 's390x' and \
       not self.simulator_run and \
       not utils.IsS390SimdSupported():
      no_simd_hardware = True

    # Ppc64 processors earlier than POWER9 do not support Simd instructions
    if self.arch == 'ppc64' and \
       not self.simulator_run and \
       utils.GuessPowerProcessorVersion() < 9:
      no_simd_hardware = True

    return no_simd_hardware

  def timeout_scalefactor(self, initial_factor):
    """Increases timeout for slow build configurations."""
    factors = dict(
        lite_mode=2,
        verify_predictable=4,
        tsan=2,
        use_sanitizer=1.5,
        full_debug=4,
    )
    result = initial_factor
    for k, v in factors.items():
      if getattr(self, k, False):
        result *= v
    if self.arch in SLOW_ARCHS:
      result *= 4.5
    return result

  def __str__(self):
    attrs = [
        'asan',
        'cfi',
        'code_comments',
        'v8_cfi',
        'dcheck_always_on',
        'debug_code',
        'dict_property_const_tracking',
        'disassembler',
        'gdbjit',
        'debugging_features',
        'DEBUG_defined',
        'has_jitless',
        'lite_mode',
        'has_maglev',
        'msan',
        'i18n',
        'pointer_compression',
        'pointer_compression_shared_cage',
        'verify_predictable',
        'sandbox',
        'slow_dchecks',
        'third_party_heap',
        'tsan',
        'has_turbofan',
        'ubsan',
        'verify_csa',
        'verify_heap',
        'has_webassembly',
    ]
    detected_options = [attr for attr in attrs if getattr(self, attr, False)]
    return ', '.join(detected_options)
