#!/usr/bin/env python
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Script to set v8's version file to the version given by the latest tag.

The script can be run in two modes:
1) As a gclient hook with the option --hook.
   The script will write a temporary version file into the checkout.
2) During compilation as an action.
   The script will write version.cc in the output folder based on the
   tag info. In case of a failure it will fall back to the temporary file
   from the hook call.

In most cases, 2) will succeed and the temporary information from 1) won't
be used. In case the checkout is copied somewhere and the git context is
lost (e.g. on the android_aosp builder), the temporary file from 1) is
required.
"""


import optparse
import os
import re
import subprocess
import sys


CWD = os.path.abspath(
    os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
VERSION_CC = os.path.join(CWD, "src", "version.cc")
TMP_VERSION_CC = os.path.join(CWD, ".version.cc")


def generate_version_file():
  tag = subprocess.check_output(
      "git describe --tags",
      shell=True,
      cwd=CWD,
  ).strip()
  assert tag

  # Check for commits not exactly matching a tag. Those are candidate builds
  # for the next version. The output has the form
  # <tag name>-<n commits>-<hash>.
  if "-" in tag:
    version = tag.split("-")[0]
    candidate = "1"
  else:
    version = tag
    candidate = "0"
  version_levels = version.split(".")

  # Set default patch level if none is given.
  if len(version_levels) == 3:
    version_levels.append("0")
  assert len(version_levels) == 4

  major, minor, build, patch = version_levels

  # Increment build level for candidate builds.
  if candidate == "1":
    build = str(int(build) + 1)
    patch = "0"

  # Modify version.cc with the new values.
  output = []
  with open(VERSION_CC, "r") as f:
    for line in f:
      for definition, substitute in (
          ("MAJOR_VERSION", major),
          ("MINOR_VERSION", minor),
          ("BUILD_NUMBER", build),
          ("PATCH_LEVEL", patch),
          ("IS_CANDIDATE_VERSION", candidate)):
        if line.startswith("#define %s" % definition):
          line =  re.sub("\d+$", substitute, line)
      output.append(line)
  # Log what was calculated.
  candidate_txt = " (candidate)" if candidate == "1" else ""
  patch_txt = ".%s" % patch if patch != "0" else ""
  version_txt = ("%s.%s.%s%s%s" %
                 (major, minor, build, patch_txt, candidate_txt))
  print "Modifying version.cc. Set V8 version to %s" %  version_txt
  return "".join(output)


def delete_tmp_version_file():
  # Make sure a subsequent call to this script doesn't use an outdated
  # version file.
  if os.path.exists(TMP_VERSION_CC):
    os.remove(TMP_VERSION_CC)


def main():
  parser = optparse.OptionParser()
  parser.add_option("--hook",
                    help="Run as a gclient hook",
                    default=False, action="store_true")
  (options, args) = parser.parse_args()

  if options.hook:
    version_out = TMP_VERSION_CC
  else:
    if len(args) != 1:
      print "Error: Specify the output file path for version.cc"
      return 1
    version_out = args[0]

  assert os.path.exists(os.path.dirname(version_out))

  try:
    version_file_content = generate_version_file()
  except Exception as e:
    # Allow exceptions when run during compilation. E.g. there might be no git
    # context availabe. When run as a gclient hook, generation must succeed.
    if options.hook:
      delete_tmp_version_file()
      raise e
    # Assume the script already ran as a hook.
    print "No git context available. Using V8 version from hook."
    assert os.path.exists(TMP_VERSION_CC)
    with open(TMP_VERSION_CC, "r") as f:
      version_file_content = f.read()

  delete_tmp_version_file()
  with open(version_out, "w") as f:
    f.write(version_file_content)
  return 0

if __name__ == "__main__":
  sys.exit(main())
