ordered_consoles = {
    "br": {
        "Linux": [
            "V8 Linux - builder",
            "V8 Linux - debug builder",
            "V8 Linux",
            "V8 Linux - debug",
            "V8 Linux - full debug",
            "V8 Linux - shared",
            "V8 Linux - noi18n - debug",
            "V8 Linux - verify csa",
            "V8 Linux - vtunejit",
        ],
        "Linux64": [
            "V8 Linux64 - builder",
            "V8 Linux64 - debug builder",
            "V8 Linux64 - custom snapshot - debug builder",
            "V8 Linux64",
            "V8 Linux64 - internal snapshot",
            "V8 Linux64 - debug",
            "V8 Linux64 - custom snapshot - debug",
            "V8 Linux64 - debug - header includes",
            "V8 Linux64 - shared",
            "V8 Linux64 - verify csa",
            "V8 Linux64 - pointer compression",
        ],
        "Fuchsia": [
            "V8 Fuchsia - builder",
            "V8 Fuchsia - debug builder",
        ],
        "Windows": [
            "V8 Win32 - builder",
            "V8 Win32 - debug builder",
            "V8 Win32",
            "V8 Win32 - debug",
            "V8 Win64",
            "V8 Win64 - debug",
            "V8 Win64 - msvc",
        ],
        "Mac": [
            "V8 Mac64",
            "V8 Mac64 - debug",
        ],
        "GCStress": [
            "V8 Linux - gc stress",
            "V8 Linux64 GC Stress - custom snapshot",
            "V8 Mac64 GC Stress",
        ],
        "Sanitizers": [
            "V8 Linux64 ASAN",
            "V8 Linux64 - cfi",
            "V8 Linux64 TSAN - builder",
            "V8 Linux64 TSAN",
            "V8 Linux64 TSAN - concurrent marking",
            "V8 Linux64 TSAN - isolates",
            "V8 Linux - arm64 - sim - CFI",
            "V8 Linux - arm64 - sim - MSAN",
            "V8 Linux64 UBSan",
            "V8 Mac64 ASAN",
            "V8 Win64 ASAN",
        ],
        "Misc": [
            "V8 Presubmit",
            "V8 Fuzzer",
            "V8 Linux gcc",
            "V8 Linux64 gcc - debug",
        ],
    },
    "br.ports": {
        "Arm": [
            "V8 Arm - builder",
            "V8 Arm - debug builder",
            "V8 Android Arm - builder",
            "V8 Linux - arm - sim",
            "V8 Linux - arm - sim - debug",
            "V8 Linux - arm - sim - lite",
            "V8 Linux - arm - sim - lite - debug",
            "V8 Arm",
            "V8 Arm - debug",
            "V8 Arm GC Stress",
        ],
        "Arm64": [
            "V8 Arm64 - builder",
            "V8 Android Arm64 - builder",
            "V8 Android Arm64 - debug builder",
            "V8 Android Arm64 - N5X",
            "V8 Linux - arm64 - sim",
            "V8 Linux - arm64 - sim - debug",
            "V8 Linux - arm64 - sim - gc stress",
            "V8 Linux64 - arm64 - sim - pointer compression - builder",
            "V8 Linux64 - arm64 - sim - pointer compression",
        ],
        "Mips": [
            "V8 Linux - mipsel - sim - builder",
            "V8 Linux - mips64el - sim - builder",
            "V8 Linux - mipsel - sim",
            "V8 Linux - mips64el - sim",
        ],
        "IBM": [
            "V8 Linux - ppc64 - sim",
            "V8 Linux - s390x - sim",
        ],
    },
    "chromium": {
        "Future": [
            "Linux - Future",
            "Linux - Future (dbg)",
            "Linux V8 API Stability",
        ],
    },
    "clusterfuzz": {
        "Windows": [
            "V8 Clusterfuzz Win64 ASAN - release builder",
            "V8 Clusterfuzz Win64 ASAN - debug builder",
        ],
        "Mac": [
            "V8 Clusterfuzz Mac64 ASAN - release builder",
            "V8 Clusterfuzz Mac64 ASAN - debug builder",
        ],
        "Linux": [
            "V8 Clusterfuzz Linux64 - release builder",
            "V8 Clusterfuzz Linux64 - debug builder",
            "V8 Clusterfuzz Linux64 ASAN no inline - release builder",
            "V8 Clusterfuzz Linux64 ASAN - debug builder",
            "V8 Clusterfuzz Linux64 ASAN arm64 - debug builder",
            "V8 Clusterfuzz Linux ASAN arm - debug builder",
            "V8 Clusterfuzz Linux MSAN no origins",
            "V8 Clusterfuzz Linux MSAN chained origins",
            "V8 Clusterfuzz Linux64 CFI - release builder",
            "V8 Clusterfuzz Linux64 TSAN - release builder",
            "V8 Clusterfuzz Linux64 UBSan - release builder",
        ],
        "Fuzzers": [
            "V8 NumFuzz",
            "V8 NumFuzz - debug",
            "V8 NumFuzz - TSAN",
        ],
    },
    "experiments": {
        "V8": [
            "V8 iOS - sim",
            "V8 Linux64 - debug - perfetto - builder",
            "V8 Linux64 - debug - perfetto",
            "V8 Linux64 - Fuzzilli",
            "V8 Linux64 - fyi",
            "V8 Linux64 - debug - fyi",
            "V8 Linux64 - gcov coverage",
            "V8 Linux - predictable",
            "V8 Linux64 - reverse jsargs",
            "V8 Fuchsia",
            "V8 Mac64 - full debug",
        ],
    },
    "infra": [
        "V8 lkgr finder",
        "Auto-roll - push",
        "Auto-roll - deps",
        "Auto-roll - v8 deps",
        "Auto-roll - test262",
        "Auto-roll - wasm-spec",
    ],
    "integration": {
        "Layout": [
            "V8 Blink Win",
            "V8 Blink Mac",
            "V8 Blink Linux",
            "V8 Blink Linux Debug",
            "V8 Blink Linux Future",
        ],
        "Nonlayout": [
            "Linux Debug Builder",
            "V8 Linux GN",
            "V8 Android GN (dbg)",
            "Linux ASAN Builder",
        ],
        "GPU": [
            "Win V8 FYI Release (NVIDIA)",
            "Mac V8 FYI Release (Intel)",
            "Linux V8 FYI Release (NVIDIA)",
            "Linux V8 FYI Release - pointer compression (NVIDIA)",
            "Android V8 FYI Release (Nexus 5X)",
        ],
        "Node.js": [
            "V8 Linux64 - node.js integration ng",
        ],
    },
    "main": {
        "Linux": [
            "V8 Linux - builder",
            "V8 Linux - debug builder",
            "V8 Linux",
            "V8 Linux - debug",
            "V8 Linux - full debug",
            "V8 Linux - shared",
            "V8 Linux - noi18n - debug",
            "V8 Linux - verify csa",
            "V8 Linux - vtunejit",
        ],
        "Linux64": [
            "V8 Linux64 - builder",
            "V8 Linux64 - debug builder",
            "V8 Linux64 - custom snapshot - debug builder",
            "V8 Linux64",
            "V8 Linux64 - internal snapshot",
            "V8 Linux64 - debug",
            "V8 Linux64 - custom snapshot - debug",
            "V8 Linux64 - debug - header includes",
            "V8 Linux64 - shared",
            "V8 Linux64 - verify csa",
            "V8 Linux64 - pointer compression",
        ],
        "Fuchsia": [
            "V8 Fuchsia - builder",
            "V8 Fuchsia - debug builder",
        ],
        "Windows": [
            "V8 Win32 - builder",
            "V8 Win32 - debug builder",
            "V8 Win32",
            "V8 Win32 - debug",
            "V8 Win64",
            "V8 Win64 - debug",
            "V8 Win64 - msvc",
        ],
        "Mac": [
            "V8 Mac64",
            "V8 Mac64 - debug",
        ],
        "GCStress": [
            "V8 Linux - gc stress",
            "V8 Linux64 GC Stress - custom snapshot",
            "V8 Mac64 GC Stress",
        ],
        "Sanitizers": [
            "V8 Linux64 ASAN",
            "V8 Linux64 - cfi",
            "V8 Linux64 TSAN - builder",
            "V8 Linux64 TSAN",
            "V8 Linux64 TSAN - concurrent marking",
            "V8 Linux64 TSAN - isolates",
            "V8 Linux - arm64 - sim - CFI",
            "V8 Linux - arm64 - sim - MSAN",
            "V8 Linux64 UBSan",
            "V8 Mac64 ASAN",
            "V8 Win64 ASAN",
        ],
        "Misc": [
            "V8 Presubmit",
            "V8 Fuzzer",
            "V8 Linux gcc",
            "V8 Linux64 gcc - debug",
        ],
    },
    "official": {
        "Linux": [
            "V8 Official Arm32",
            "V8 Official Arm64",
            "V8 Official Android Arm32",
            "V8 Official Android Arm64",
            "V8 Official Linux32",
            "V8 Official Linux32 Debug",
            "V8 Official Linux64",
            "V8 Official Linux64 Debug",
        ],
        "Windows": [
            "V8 Official Win32",
            "V8 Official Win32 Debug",
            "V8 Official Win64",
            "V8 Official Win64 Debug",
        ],
        "Mac": [
            "V8 Official Mac64",
            "V8 Official Mac64 Debug",
        ],
    },
    "perf": {
        "Arm": [
            "V8 Arm - builder - perf",
            "V8 Android Arm - builder - perf",
        ],
        "Arm64": [
            "V8 Arm64 - builder - perf",
            "V8 Android Arm64 - builder - perf",
        ],
        "Linux": [
            "V8 Linux - builder - perf",
        ],
        "Linux64": [
            "V8 Linux64 - builder - perf",
        ],
    },
    "ports": {
        "Arm": [
            "V8 Arm - builder",
            "V8 Arm - debug builder",
            "V8 Android Arm - builder",
            "V8 Linux - arm - sim",
            "V8 Linux - arm - sim - debug",
            "V8 Linux - arm - sim - lite",
            "V8 Linux - arm - sim - lite - debug",
            "V8 Arm",
            "V8 Arm - debug",
            "V8 Arm GC Stress",
        ],
        "Arm64": [
            "V8 Arm64 - builder",
            "V8 Android Arm64 - builder",
            "V8 Android Arm64 - debug builder",
            "V8 Android Arm64 - N5X",
            "V8 Linux - arm64 - sim",
            "V8 Linux - arm64 - sim - debug",
            "V8 Linux - arm64 - sim - gc stress",
            "V8 Linux64 - arm64 - sim - pointer compression - builder",
            "V8 Linux64 - arm64 - sim - pointer compression",
        ],
        "Mips": [
            "V8 Linux - mipsel - sim - builder",
            "V8 Linux - mips64el - sim - builder",
            "V8 Linux - mipsel - sim",
            "V8 Linux - mips64el - sim",
        ],
        "IBM": [
            "V8 Linux - ppc64 - sim",
            "V8 Linux - s390x - sim",
        ],
    },
    "tryserver": [
        "v8_android_arm_compile_rel",
        "v8_android_arm64_compile_dbg",
        "v8_android_arm64_n5x_rel_ng",
        "v8_android_arm64_n5x_rel_ng_triggered",
        "v8_fuchsia_compile_rel",
        "v8_fuchsia_rel_ng",
        "v8_fuchsia_rel_ng_triggered",
        "v8_full_presubmit",
        "v8_ios_simulator",
        "v8_linux64_arm64_pointer_compression_rel_ng",
        "v8_linux64_arm64_pointer_compression_rel_ng_triggered",
        "v8_linux64_asan_rel_ng",
        "v8_linux64_asan_rel_ng_triggered",
        "v8_linux64_cfi_rel_ng",
        "v8_linux64_cfi_rel_ng_triggered",
        "v8_linux64_dbg_ng",
        "v8_linux64_dbg_ng_triggered",
        "v8_linux64_gc_stress_custom_snapshot_dbg_ng",
        "v8_linux64_gc_stress_custom_snapshot_dbg_ng_triggered",
        "v8_linux64_fuzzilli_ng",
        "v8_linux64_fuzzilli_ng_triggered",
        "v8_linux64_fyi_rel_ng",
        "v8_linux64_fyi_rel_ng_triggered",
        "v8_linux64_gcc_compile_dbg",
        "v8_linux64_header_includes_dbg",
        "v8_linux64_msan_rel_ng",
        "v8_linux64_msan_rel_ng_triggered",
        "v8_linux64_nodcheck_rel_ng",
        "v8_linux64_nodcheck_rel_ng_triggered",
        "v8_linux64_perfetto_dbg_ng",
        "v8_linux64_perfetto_dbg_ng_triggered",
        "v8_linux64_pointer_compression_rel_ng",
        "v8_linux64_pointer_compression_rel_ng_triggered",
        "v8_linux64_rel_ng",
        "v8_linux64_rel_ng_triggered",
        "v8_linux64_reverse_jsargs_dbg_ng",
        "v8_linux64_reverse_jsargs_dbg_ng_triggered",
        "v8_linux64_tsan_rel_ng",
        "v8_linux64_tsan_rel_ng_triggered",
        "v8_linux64_tsan_isolates_rel_ng",
        "v8_linux64_tsan_isolates_rel_ng_triggered",
        "v8_linux64_ubsan_rel_ng",
        "v8_linux64_ubsan_rel_ng_triggered",
        "v8_linux64_shared_compile_rel",
        "v8_linux64_verify_csa_rel_ng",
        "v8_linux64_verify_csa_rel_ng_triggered",
        "v8_linux_arm64_dbg_ng",
        "v8_linux_arm64_dbg_ng_triggered",
        "v8_linux_arm64_gc_stress_dbg_ng",
        "v8_linux_arm64_gc_stress_dbg_ng_triggered",
        "v8_linux_arm64_rel_ng",
        "v8_linux_arm64_rel_ng_triggered",
        "v8_linux_arm64_cfi_rel_ng",
        "v8_linux_arm64_cfi_rel_ng_triggered",
        "v8_linux_arm_dbg_ng",
        "v8_linux_arm_dbg_ng_triggered",
        "v8_linux_arm_lite_rel_ng",
        "v8_linux_arm_lite_rel_ng_triggered",
        "v8_linux_arm_rel_ng",
        "v8_linux_arm_rel_ng_triggered",
        "v8_linux_blink_rel",
        "v8_linux_chromium_gn_rel",
        "v8_linux_dbg_ng",
        "v8_linux_dbg_ng_triggered",
        "v8_linux_gc_stress_dbg_ng",
        "v8_linux_gc_stress_dbg_ng_triggered",
        "v8_linux_gcc_compile_rel",
        "v8_linux_gcc_rel_ng",
        "v8_linux_gcc_rel_ng_triggered",
        "v8_linux_mips64el_compile_rel",
        "v8_linux_mipsel_compile_rel",
        "v8_linux_nodcheck_rel_ng",
        "v8_linux_nodcheck_rel_ng_triggered",
        "v8_linux_noi18n_compile_dbg",
        "v8_linux_noi18n_rel_ng",
        "v8_linux_rel_ng",
        "v8_linux_rel_ng_triggered",
        "v8_linux_optional_rel_ng",
        "v8_linux_optional_rel_ng_triggered",
        "v8_linux_shared_compile_rel",
        "v8_linux_verify_csa_rel_ng",
        "v8_linux_verify_csa_rel_ng_triggered",
        "v8_mac64_dbg_ng",
        "v8_mac64_dbg_ng_triggered",
        "v8_mac64_asan_rel_ng",
        "v8_mac64_asan_rel_ng_triggered",
        "v8_mac64_compile_full_dbg",
        "v8_mac64_gc_stress_dbg_ng",
        "v8_mac64_gc_stress_dbg_ng_triggered",
        "v8_mac64_rel_ng",
        "v8_mac64_rel_ng_triggered",
        "v8_mac_arm64_rel_ng",
        "v8_mac_arm64_rel_ng_triggered",
        "v8_mac_arm64_dbg_ng",
        "v8_mac_arm64_dbg_ng_triggered",
        "v8_mac_arm64_full_dbg_ng",
        "v8_mac_arm64_full_dbg_ng_triggered",
        "v8_odroid_arm_rel_ng",
        "v8_odroid_arm_rel_ng_triggered",
        "v8_linux_torque_compare",
        "v8_presubmit",
        "v8_win64_dbg_ng",
        "v8_win64_dbg_ng_triggered",
        "v8_win64_msvc_compile_rel",
        "v8_win64_msvc_rel_ng",
        "v8_win64_msvc_rel_ng_triggered",
        "v8_win64_rel_ng",
        "v8_win64_rel_ng_triggered",
        "v8_win_compile_dbg",
        "v8_win_dbg_ng",
        "v8_win_dbg_ng_triggered",
        "v8_win_rel_ng",
        "v8_win_rel_ng_triggered",
    ],
}

def ordered_console_builders(console, bucket = "ci"):
    entries = []
    for category, builders in ordered_consoles[console].items():
        for name in builders:
            entry = luci.console_view_entry(
                builder = bucket + "/" + name,
                category = category,
            )
            entries.append(entry)
    return entries

def ordered_list_builders(console):
    entries = []
    for name in ordered_consoles[console]:
        if name.endswith("_ng_triggered"):
            name = "try.triggered/" + name
        elif name.find("_") != -1:
            name = "try/" + name
        else:
            name = "ci/" + name
        entry = luci.list_view_entry(name)
        entries.append(entry)
    return entries

def console_view(name, title, repo, refs, exclude_ref = None, header = "//consoles/header_main.textpb", entries = None):
    luci.console_view(
        name = name,
        title = title,
        repo = repo,
        refs = refs,
        exclude_ref = exclude_ref,
        favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
        header = header,
        entries = entries or ordered_console_builders(name),
    )

def master_console_view(name, title, repo = "https://chromium.googlesource.com/v8/v8"):
    console_view(
        name = name,
        title = title,
        repo = repo,
        refs = ["refs/heads/master"],
    )

def branch_console_view(name, title, version):
    if name.endswith("ports"):
        console_name = "br.ports"
        bucket_name = "ci." + name[:-6]
    else:
        console_name = "br"
        bucket_name = "ci." + name
    console_view(
        name = name,
        title = title,
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = ["refs/branch-heads/" + version],
        header = "//consoles/header_branch.textpb",
        entries = ordered_console_builders(console_name, bucket_name),
    )

def list_view(name, title):
    luci.list_view(
        name = name,
        title = title,
        favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
        entries = ordered_list_builders(name),
    )

luci.milo(
    logo = "https://storage.googleapis.com/chrome-infra-public/logo/v8.svg",
)
master_console_view("main", "Main")
master_console_view("ports", "Ports")
master_console_view("experiments", "Experiments")
master_console_view("integration", "Integration")
master_console_view("clusterfuzz", "ClusterFuzz")
master_console_view("perf", "Perf")
master_console_view("chromium", "Chromium", "https://chromium.googlesource.com/chromium/src")
console_view(
    name = "official",
    title = "Official",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/\\d+\\.\\d+", "refs/heads/\\d+\\.\\d+\\.\\d+"],
    exclude_ref = "refs/heads/master",
)
branch_console_view("br.stable", "Stable Main", "8\\.4")
branch_console_view("br.stable.ports", "Stable Ports", "8\\.4")
branch_console_view("br.beta", "Beta Main", "8\\.5")
branch_console_view("br.beta.ports", "Beta Ports", "8\\.5")
list_view("infra", "Infra")
list_view("tryserver", "Tryserver")

luci.console_view_entry(builder = "ci/Auto-tag", category = "Tag", console_view = "br.stable")
luci.console_view_entry(builder = "ci/Auto-tag", category = "Tag", console_view = "br.beta")
luci.list_view_entry(builder = "try.triggered/v8_flako", list_view = "tryserver")
luci.list_view_entry(builder = "try.triggered/v8_verify_flakes", list_view = "tryserver")
