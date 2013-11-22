#!/usr/bin/env python
# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import optparse
import re
import sys

from common_includes import *

CONFIG = {
  PERSISTFILE_BASENAME: "/tmp/v8-auto-roll-tempfile",
  DOT_GIT_LOCATION: ".git",
}


class Preparation(Step):
  def __init__(self):
    Step.__init__(self, "Preparation.")

  def RunStep(self):
    self.InitialEnvironmentChecks()
    self.CommonPrepare()


class FetchLatestRevision(Step):
  def __init__(self):
    Step.__init__(self, "Fetching latest V8 revision.")

  def RunStep(self):
    log = self.Git("svn log -1 --oneline").strip()
    match = re.match(r"^r(\d+) ", log)
    if not match:
      self.Die("Could not extract current svn revision from log.")
    self.Persist("latest", match.group(1))


class FetchLKGR(Step):
  def __init__(self):
    Step.__init__(self, "Fetching V8 LKGR.")

  def RunStep(self):
    lkgr_url = "https://v8-status.appspot.com/lkgr"
    self.Persist("lkgr", self.ReadURL(lkgr_url))


class PushToTrunk(Step):
  def __init__(self):
    Step.__init__(self, "Pushing to trunk if possible.")

  def RunStep(self):
    self.RestoreIfUnset("latest")
    self.RestoreIfUnset("lkgr")
    latest = int(self._state["latest"])
    lkgr = int(self._state["lkgr"])
    if latest == lkgr:
      print "ToT (r%d) is clean. Pushing to trunk." % latest
      # TODO(machenbach): Call push to trunk script.
    else:
      print("ToT (r%d) is ahead of the LKGR (r%d). Skipping push to trunk."
            % (latest, lkgr))


def RunAutoRoll(config,
                options,
                side_effect_handler=DEFAULT_SIDE_EFFECT_HANDLER):
  step_classes = [
    Preparation,
    FetchLatestRevision,
    FetchLKGR,
    PushToTrunk,
  ]
  RunScript(step_classes, config, options, side_effect_handler)


def BuildOptions():
  result = optparse.OptionParser()
  result.add_option("-s", "--step", dest="s",
                    help="Specify the step where to start work. Default: 0.",
                    default=0, type="int")
  return result


def Main():
  parser = BuildOptions()
  (options, args) = parser.parse_args()
  RunAutoRoll(CONFIG, options)

if __name__ == "__main__":
  sys.exit(Main())
