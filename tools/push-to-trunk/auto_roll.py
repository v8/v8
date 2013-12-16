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


class AutoRollOptions(CommonOptions):
  def __init__(self, options):
    super(AutoRollOptions, self).__init__(options)
    self.requires_editor = False


class Preparation(Step):
  MESSAGE = "Preparation."

  def RunStep(self):
    self.InitialEnvironmentChecks()
    self.CommonPrepare()


class FetchLatestRevision(Step):
  MESSAGE = "Fetching latest V8 revision."

  def RunStep(self):
    log = self.Git("svn log -1 --oneline").strip()
    match = re.match(r"^r(\d+) ", log)
    if not match:
      self.Die("Could not extract current svn revision from log.")
    self.Persist("latest", match.group(1))


class CheckLastPush(Step):
  MESSAGE = "Checking last V8 push to trunk."

  def RunStep(self):
    self.RestoreIfUnset("latest")
    log = self.Git("svn log -1 --oneline ChangeLog").strip()
    match = re.match(r"^r(\d+) \| Prepare push to trunk", log)
    if match:
      latest = int(self._state["latest"])
      last_push = int(match.group(1))
      # TODO(machebach): This metric counts all revisions. It could be
      # improved by counting only the revisions on bleeding_edge.
      if latest - last_push < 10:
        # This makes sure the script doesn't push twice in a row when the cron
        # job retries several times.
        self.Die("Last push too recently: %d" % last_push)


class FetchLKGR(Step):
  MESSAGE = "Fetching V8 LKGR."

  def RunStep(self):
    lkgr_url = "https://v8-status.appspot.com/lkgr"
    # Retry several times since app engine might have issues.
    self.Persist("lkgr", self.ReadURL(lkgr_url, wait_plan=[5, 20, 300, 300]))


class PushToTrunk(Step):
  MESSAGE = "Pushing to trunk if possible."

  def RunStep(self):
    self.RestoreIfUnset("latest")
    self.RestoreIfUnset("lkgr")
    latest = int(self._state["latest"])
    lkgr = int(self._state["lkgr"])
    if latest == lkgr:
      print "ToT (r%d) is clean. Pushing to trunk." % latest
      # TODO(machenbach): Call push to trunk script.
      # TODO(machenbach): Update the script before calling it.
      # self._side_effect_handler.Command(
      #     "tools/push-to-trunk/push-to-trunk.py",
      #     "-f -c %s -r %s" % (self._options.c, self._options.r))
    else:
      print("ToT (r%d) is ahead of the LKGR (r%d). Skipping push to trunk."
            % (latest, lkgr))


def RunAutoRoll(config,
                options,
                side_effect_handler=DEFAULT_SIDE_EFFECT_HANDLER):
  step_classes = [
    Preparation,
    FetchLatestRevision,
    CheckLastPush,
    FetchLKGR,
    PushToTrunk,
  ]
  RunScript(step_classes, config, options, side_effect_handler)


def BuildOptions():
  result = optparse.OptionParser()
  result.add_option("-c", "--chromium", dest="c",
                    help=("Specify the path to your Chromium src/ "
                          "directory to automate the V8 roll."))
  result.add_option("-r", "--reviewer", dest="r",
                    help=("Specify the account name to be used for reviews."))
  result.add_option("-s", "--step", dest="s",
                    help="Specify the step where to start work. Default: 0.",
                    default=0, type="int")
  return result


def Main():
  parser = BuildOptions()
  (options, args) = parser.parse_args()
  if not options.c or not options.r:
    print "You need to specify the chromium src location and a reviewer."
    parser.print_help()
    return 1
  RunAutoRoll(CONFIG, AutoRollOptions(options))

if __name__ == "__main__":
  sys.exit(Main())
