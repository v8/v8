# Note: The buildbots evaluate this file with CWD set to the parent
# directory and assume that the root of the checkout is in ./v8/, so
# all paths in here must match this assumption.

vars = {
  "chromium_trunk": "https://src.chromium.org/svn/trunk",

  "buildtools_revision": "5d89977ce55240995d1596fe420b818468f5ec37",
}

deps = {
  # Remember to keep the revision in sync with the Makefile.
  "v8/build/gyp":
    "http://gyp.googlecode.com/svn/trunk@1831",

  "v8/third_party/icu":
    Var("chromium_trunk") + "/deps/third_party/icu46@258359",

  "v8/buildtools":
    "https://chromium.googlesource.com/chromium/buildtools.git@" +
    Var("buildtools_revision"),
}

deps_os = {
  "win": {
    "v8/third_party/cygwin":
      Var("chromium_trunk") + "/deps/third_party/cygwin@66844",

    "v8/third_party/python_26":
      Var("chromium_trunk") + "/tools/third_party/python_26@89111",
  }
}

include_rules = [
  # Everybody can use some things.
  "+include",
  "+unicode",
]

# checkdeps.py shouldn't check for includes in these directories:
skip_child_includes = [
  "build",
  "third_party",
]

hooks = [
  {
    # A change to a .gyp, .gypi, or to GYP itself should run the generator.
    "pattern": ".",
    "action": ["python", "v8/build/gyp_v8"],
  },
]
