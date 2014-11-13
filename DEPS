# Note: The buildbots evaluate this file with CWD set to the parent
# directory and assume that the root of the checkout is in ./v8/, so
# all paths in here must match this assumption.

vars = {
  "git_url": "https://chromium.googlesource.com",
}

deps = {
  # Remember to keep the revision in sync with the Makefile.
  "v8/build/gyp":
    Var("git_url") + "/external/gyp.git@a3e2a5caf24a1e0a45401e09ad131210bf16b852",
  "v8/third_party/icu":
    Var("git_url") + "/chromium/deps/icu52.git@26d8859357ac0bfb86b939bf21c087b8eae22494",
  "v8/buildtools":
    Var("git_url") + "/chromium/buildtools.git@fb782d4369d5ae04f17a2fceef7de5a63e50f07b",
  "v8/testing/gtest":
    Var("git_url") + "/external/googletest.git@4650552ff637bb44ecf7784060091cbed3252211",
  "v8/testing/gmock":
    Var("git_url") + "/external/googlemock.git@896ba0e03f520fb9b6ed582bde2bd00847e3c3f2",
}

deps_os = {
  "android": {
    "v8/third_party/android_tools":
      Var("git_url") + "/android_tools.git@31869996507de16812bb53a3d0aaa15cd6194c16",
  },
  "win": {
    "v8/third_party/cygwin":
      Var("git_url") + "/chromium/deps/cygwin.git@06a117a90c15174436bfa20ceebbfdf43b7eb820",
    "v8/third_party/python_26":
      Var("git_url") + "/chromium/deps/python_26.git@67d19f904470effe3122d27101cc5a8195abd157",
  }
}

include_rules = [
  # Everybody can use some things.
  "+include",
  "+unicode",
  "+third_party/fdlibm",
]

# checkdeps.py shouldn't check for includes in these directories:
skip_child_includes = [
  "build",
  "third_party",
]

hooks = [
  # Pull clang-format binaries using checked-in hashes.
  {
    "name": "clang_format_win",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=win32",
                "--no_auth",
                "--bucket", "chromium-clang-format",
                "-s", "v8/buildtools/win/clang-format.exe.sha1",
    ],
  },
  {
    "name": "clang_format_mac",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=darwin",
                "--no_auth",
                "--bucket", "chromium-clang-format",
                "-s", "v8/buildtools/mac/clang-format.sha1",
    ],
  },
  {
    "name": "clang_format_linux",
    "pattern": ".",
    "action": [ "download_from_google_storage",
                "--no_resume",
                "--platform=linux*",
                "--no_auth",
                "--bucket", "chromium-clang-format",
                "-s", "v8/buildtools/linux64/clang-format.sha1",
    ],
  },
  {
    # A change to a .gyp, .gypi, or to GYP itself should run the generator.
    "pattern": ".",
    "action": ["python", "v8/build/gyp_v8"],
  },
]
