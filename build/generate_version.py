#!/usr/bin/env python
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Script to set v8's version file to the version given by the latest tag.

The script is intended to be run as a gclient hook. The script will write a
generated version file into the checkout.

On build systems where no git is available and where the version information
is not necessary, the version generation can be bypassed with the option
--skip.
"""


import optparse
import os
import re
import subprocess
import sys


CWD = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
VERSION_CC = os.path.join(CWD, "src", "version.cc")
VERSION_GEN_CC = os.path.join(CWD, "src", "version_gen.cc")


def generate_version_file():
  # Make sure the tags are fetched.
  subprocess.check_output(
      "git fetch origin +refs/tags/*:refs/tags/*",
      shell=True,
      cwd=CWD)
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

  # Modify version_gen.cc with the new values.
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

  # Prepare log message.
  candidate_txt = " (candidate)" if candidate == "1" else ""
  patch_txt = ".%s" % patch if patch != "0" else ""
  version_txt = ("%s.%s.%s%s%s" %
                 (major, minor, build, patch_txt, candidate_txt))
  log_message = "Modifying version_gen.cc. Set V8 version to %s" %  version_txt
  return "".join(output), log_message


def bypass_version_file():
  with open(VERSION_CC, "r") as f:
    return f.read(), "Bypassing V8 version creation."


def main():
  parser = optparse.OptionParser()
  parser.add_option("--skip",
                    help="Use raw version.cc file (disables version "
                         "generation and uses a dummy version).",
                    default=False, action="store_true")
  (options, args) = parser.parse_args()

  if options.skip:
    version_file_content, log_message = bypass_version_file()
  else:
    version_file_content, log_message = generate_version_file()

  old_content = ""
  if os.path.exists(VERSION_GEN_CC):
    with open(VERSION_GEN_CC, "r") as f:
      old_content = f.read()

  # Only generate version file if content has changed.
  if old_content != version_file_content:
    with open(VERSION_GEN_CC, "w") as f:
      f.write(version_file_content)
    # Log what was calculated.
    print log_message

  return 0


if __name__ == "__main__":
  sys.exit(main())
