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

import argparse
import json
import os
import re
import sys
import urllib

from common_includes import *
import push_to_trunk
from push_to_trunk import PushToTrunkOptions
from push_to_trunk import RunPushToTrunk

SETTINGS_LOCATION = "SETTINGS_LOCATION"

CONFIG = {
  PERSISTFILE_BASENAME: "/tmp/v8-auto-roll-tempfile",
  DOT_GIT_LOCATION: ".git",
  SETTINGS_LOCATION: "~/.auto-roll",
}


class AutoRollOptions(CommonOptions):
  def __init__(self, options):
    super(AutoRollOptions, self).__init__(options)
    self.requires_editor = False
    self.status_password = options.status_password
    self.c = options.c
    self.push = getattr(options, 'push', False)
    self.author = getattr(options, 'a', None)


class Preparation(Step):
  MESSAGE = "Preparation."

  def RunStep(self):
    self.InitialEnvironmentChecks()
    self.CommonPrepare()


class CheckAutoRollSettings(Step):
  MESSAGE = "Checking settings file."

  def RunStep(self):
    settings_file = os.path.realpath(self.Config(SETTINGS_LOCATION))
    if os.path.exists(settings_file):
      settings_dict = json.loads(FileToText(settings_file))
      if settings_dict.get("enable_auto_roll") is False:
        self.Die("Push to trunk disabled by auto-roll settings file: %s"
                 % settings_file)


class CheckTreeStatus(Step):
  MESSAGE = "Checking v8 tree status message."

  def RunStep(self):
    status_url = "https://v8-status.appspot.com/current?format=json"
    status_json = self.ReadURL(status_url, wait_plan=[5, 20, 300, 300])
    self["tree_message"] = json.loads(status_json)["message"]
    if re.search(r"nopush|no push", self["tree_message"], flags=re.I):
      self.Die("Push to trunk disabled by tree state: %s"
               % self["tree_message"])


class FetchLatestRevision(Step):
  MESSAGE = "Fetching latest V8 revision."

  def RunStep(self):
    match = re.match(r"^r(\d+) ", self.GitSVNLog())
    if not match:
      self.Die("Could not extract current svn revision from log.")
    self["latest"] = match.group(1)


class CheckLastPush(Step):
  MESSAGE = "Checking last V8 push to trunk."

  def RunStep(self):
    last_push_hash = self.FindLastTrunkPush()
    last_push = int(self.GitSVNFindSVNRev(last_push_hash))

    # TODO(machenbach): This metric counts all revisions. It could be
    # improved by counting only the revisions on bleeding_edge.
    if int(self["latest"]) - last_push < 10:
      # This makes sure the script doesn't push twice in a row when the cron
      # job retries several times.
      self.Die("Last push too recently: %d" % last_push)


class FetchLKGR(Step):
  MESSAGE = "Fetching V8 LKGR."

  def RunStep(self):
    lkgr_url = "https://v8-status.appspot.com/lkgr"
    # Retry several times since app engine might have issues.
    self["lkgr"] = self.ReadURL(lkgr_url, wait_plan=[5, 20, 300, 300])


class PushToTrunk(Step):
  MESSAGE = "Pushing to trunk if possible."

  def PushTreeStatus(self, message):
    if not self._options.status_password:
      print "Skipping tree status update without password file."
      return
    params = {
      "message": message,
      "username": "v8-auto-roll@chromium.org",
      "password": FileToText(self._options.status_password).strip(),
    }
    params = urllib.urlencode(params)
    print "Pushing tree status: '%s'" % message
    self.ReadURL("https://v8-status.appspot.com/status", params,
                 wait_plan=[5, 20])

  def RunStep(self):
    latest = int(self["latest"])
    lkgr = int(self["lkgr"])
    if latest == lkgr:
      print "ToT (r%d) is clean. Pushing to trunk." % latest
      self.PushTreeStatus("Tree is closed (preparing to push)")

      # TODO(machenbach): Update the script before calling it.
      try:
        if self._options.push:
          self._side_effect_handler.Call(
              RunPushToTrunk,
              push_to_trunk.CONFIG,
              PushToTrunkOptions.MakeForcedOptions(self._options.author,
                                                   self._options.reviewer,
                                                   self._options.c),
              self._side_effect_handler)
      finally:
        self.PushTreeStatus(self["tree_message"])
    else:
      print("ToT (r%d) is ahead of the LKGR (r%d). Skipping push to trunk."
            % (latest, lkgr))


def RunAutoRoll(config,
                options,
                side_effect_handler=DEFAULT_SIDE_EFFECT_HANDLER):
  step_classes = [
    Preparation,
    CheckAutoRollSettings,
    CheckTreeStatus,
    FetchLatestRevision,
    CheckLastPush,
    FetchLKGR,
    PushToTrunk,
  ]
  RunScript(step_classes, config, options, side_effect_handler)


def BuildOptions():
  parser = argparse.ArgumentParser()
  parser.add_argument("-a", "--author", dest="a",
                      help="The author email used for rietveld.")
  parser.add_argument("-c", "--chromium", dest="c",
                      help=("The path to your Chromium src/ directory to "
                            "automate the V8 roll."))
  parser.add_argument("-p", "--push",
                      help="Push to trunk if possible. Dry run if unspecified.",
                      default=False, action="store_true")
  parser.add_argument("-r", "--reviewer",
                      help="The account name to be used for reviews.")
  parser.add_argument("-s", "--step", dest="s",
                      help="Specify the step where to start work. Default: 0.",
                      default=0, type=int)
  parser.add_argument("--status-password",
                      help="A file with the password to the status app.")
  return parser


def Main():
  parser = BuildOptions()
  options = parser.parse_args()
  if not options.a or not options.c or not options.reviewer:
    print "You need to specify author, chromium src location and reviewer."
    parser.print_help()
    return 1
  RunAutoRoll(CONFIG, AutoRollOptions(options))

if __name__ == "__main__":
  sys.exit(Main())
