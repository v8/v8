load("//lib.star", "GOMA", "v8_perf_builder")

v8_perf_builder(
    name = "V8 Arm - builder - perf",
    properties = {"target_arch": "arm"},
)
v8_perf_builder(
    name = "V8 Arm64 - builder - perf",
    properties = {"target_arch": "arm", "target_bits": 64},
)
v8_perf_builder(
    name = "V8 Android Arm - builder - perf",
    properties = {"target_arch": "arm", "target_platform": "android"},
)
v8_perf_builder(
    name = "V8 Android Arm64 - builder - perf",
    properties = {"target_arch": "arm", "target_platform": "android"},
)
v8_perf_builder(
    name = "V8 Linux - builder - perf",
)
v8_perf_builder(
    name = "V8 Linux64 - builder - perf",
)
