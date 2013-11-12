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

import os
import re
import subprocess
import sys

PERSISTFILE_BASENAME = "PERSISTFILE_BASENAME"
TEMP_BRANCH = "TEMP_BRANCH"
BRANCHNAME = "BRANCHNAME"
DOT_GIT_LOCATION = "DOT_GIT_LOCATION"
VERSION_FILE = "VERSION_FILE"
CHANGELOG_FILE = "CHANGELOG_FILE"
CHANGELOG_ENTRY_FILE = "CHANGELOG_ENTRY_FILE"
COMMITMSG_FILE = "COMMITMSG_FILE"
PATCH_FILE = "PATCH_FILE"


def TextToFile(text, file_name):
  with open(file_name, "w") as f:
    f.write(text)


def AppendToFile(text, file_name):
  with open(file_name, "a") as f:
    f.write(text)


def LinesInFile(file_name):
  with open(file_name) as f:
    for line in f:
      yield line


def FileToText(file_name):
  with open(file_name) as f:
    return f.read()


def MSub(rexp, replacement, text):
  return re.sub(rexp, replacement, text, flags=re.MULTILINE)


# Some commands don't like the pipe, e.g. calling vi from within the script or
# from subscripts like git cl upload.
def Command(cmd, args="", prefix="", pipe=True):
  cmd_line = "%s %s %s" % (prefix, cmd, args)
  print "Command: %s" % cmd_line
  try:
    if pipe:
      return subprocess.check_output(cmd_line, shell=True)
    else:
      return subprocess.check_call(cmd_line, shell=True)
  except subprocess.CalledProcessError:
    return None


# Wrapper for side effects.
class SideEffectHandler(object):
  def Command(self, cmd, args="", prefix="", pipe=True):
    return Command(cmd, args, prefix, pipe)

  def ReadLine(self):
    return sys.stdin.readline().strip()

DEFAULT_SIDE_EFFECT_HANDLER = SideEffectHandler()


class Step(object):
  def __init__(self, text="", requires=None):
    self._text = text
    self._number = -1
    self._requires = requires
    self._side_effect_handler = DEFAULT_SIDE_EFFECT_HANDLER

  def SetNumber(self, number):
    self._number = number

  def SetConfig(self, config):
    self._config = config

  def SetState(self, state):
    self._state = state

  def SetOptions(self, options):
    self._options = options

  def SetSideEffectHandler(self, handler):
    self._side_effect_handler = handler

  def Config(self, key):
    return self._config[key]

  def Run(self):
    assert self._number >= 0
    assert self._config is not None
    assert self._state is not None
    assert self._side_effect_handler is not None
    if self._requires:
      self.RestoreIfUnset(self._requires)
      if not self._state[self._requires]:
        return
    print ">>> Step %d: %s" % (self._number, self._text)
    self.RunStep()

  def RunStep(self):
    raise NotImplementedError

  def ReadLine(self):
    return self._side_effect_handler.ReadLine()

  def Git(self, args="", prefix="", pipe=True):
    return self._side_effect_handler.Command("git", args, prefix, pipe)

  def Editor(self, args):
    return self._side_effect_handler.Command(os.environ["EDITOR"], args,
                                             pipe=False)

  def Die(self, msg=""):
    if msg != "":
      print "Error: %s" % msg
    print "Exiting"
    raise Exception(msg)

  def Confirm(self, msg):
    print "%s [Y/n] " % msg,
    answer = self.ReadLine()
    return answer == "" or answer == "Y" or answer == "y"

  def DeleteBranch(self, name):
    git_result = self.Git("branch").strip()
    for line in git_result.splitlines():
      if re.match(r".*\s+%s$" % name, line):
        msg = "Branch %s exists, do you want to delete it?" % name
        if self.Confirm(msg):
          if self.Git("branch -D %s" % name) is None:
            self.Die("Deleting branch '%s' failed." % name)
          print "Branch %s deleted." % name
        else:
          msg = "Can't continue. Please delete branch %s and try again." % name
          self.Die(msg)

  def Persist(self, var, value):
    value = value or "__EMPTY__"
    TextToFile(value, "%s-%s" % (self._config[PERSISTFILE_BASENAME], var))

  def Restore(self, var):
    value = FileToText("%s-%s" % (self._config[PERSISTFILE_BASENAME], var))
    value = value or self.Die("Variable '%s' could not be restored." % var)
    return "" if value == "__EMPTY__" else value

  def RestoreIfUnset(self, var_name):
    if self._state.get(var_name) is None:
      self._state[var_name] = self.Restore(var_name)

  def InitialEnvironmentChecks(self):
    # Cancel if this is not a git checkout.
    if not os.path.exists(self._config[DOT_GIT_LOCATION]):
      self.Die("This is not a git checkout, this script won't work for you.")

    # Cancel if EDITOR is unset or not executable.
    if (not os.environ.get("EDITOR") or
        Command("which", os.environ["EDITOR"]) is None):
      self.Die("Please set your EDITOR environment variable, you'll need it.")

  def CommonPrepare(self):
    # Check for a clean workdir.
    if self.Git("status -s -uno").strip() != "":
      self.Die("Workspace is not clean. Please commit or undo your changes.")

    # Persist current branch.
    current_branch = ""
    git_result = self.Git("status -s -b -uno").strip()
    for line in git_result.splitlines():
      match = re.match(r"^## (.+)", line)
      if match:
        current_branch = match.group(1)
        break
    self.Persist("current_branch", current_branch)

    # Fetch unfetched revisions.
    if self.Git("svn fetch") is None:
      self.Die("'git svn fetch' failed.")

    # Get ahold of a safe temporary branch and check it out.
    if current_branch != self._config[TEMP_BRANCH]:
      self.DeleteBranch(self._config[TEMP_BRANCH])
      self.Git("checkout -b %s" % self._config[TEMP_BRANCH])

    # Delete the branch that will be created later if it exists already.
    self.DeleteBranch(self._config[BRANCHNAME])

  def CommonCleanup(self):
    self.RestoreIfUnset("current_branch")
    self.Git("checkout -f %s" % self._state["current_branch"])
    if self._config[TEMP_BRANCH] != self._state["current_branch"]:
      self.Git("branch -D %s" % self._config[TEMP_BRANCH])
    if self._config[BRANCHNAME] != self._state["current_branch"]:
      self.Git("branch -D %s" % self._config[BRANCHNAME])

    # Clean up all temporary files.
    Command("rm", "-f %s*" % self._config[PERSISTFILE_BASENAME])

  def ReadAndPersistVersion(self, prefix=""):
    def ReadAndPersist(var_name, def_name):
      match = re.match(r"^#define %s\s+(\d*)" % def_name, line)
      if match:
        value = match.group(1)
        self.Persist("%s%s" % (prefix, var_name), value)
        self._state["%s%s" % (prefix, var_name)] = value
    for line in LinesInFile(self._config[VERSION_FILE]):
      for (var_name, def_name) in [("major", "MAJOR_VERSION"),
                                   ("minor", "MINOR_VERSION"),
                                   ("build", "BUILD_NUMBER"),
                                   ("patch", "PATCH_LEVEL")]:
        ReadAndPersist(var_name, def_name)

  def RestoreVersionIfUnset(self, prefix=""):
    for v in ["major", "minor", "build", "patch"]:
      self.RestoreIfUnset("%s%s" % (prefix, v))

  def WaitForLGTM(self):
    print ("Please wait for an LGTM, then type \"LGTM<Return>\" to commit "
           "your change. (If you need to iterate on the patch or double check "
           "that it's sane, do so in another shell, but remember to not "
           "change the headline of the uploaded CL.")
    answer = ""
    while answer != "LGTM":
      print "> ",
      answer = self.ReadLine()
      if answer != "LGTM":
        print "That was not 'LGTM'."

  def WaitForResolvingConflicts(self, patch_file):
    print("Applying the patch \"%s\" failed. Either type \"ABORT<Return>\", "
          "or resolve the conflicts, stage *all* touched files with "
          "'git add', and type \"RESOLVED<Return>\"")
    answer = ""
    while answer != "RESOLVED":
      if answer == "ABORT":
        self.Die("Applying the patch failed.")
      if answer != "":
        print "That was not 'RESOLVED' or 'ABORT'."
      print "> ",
      answer = self.ReadLine()

  # Takes a file containing the patch to apply as first argument.
  def ApplyPatch(self, patch_file, reverse_patch=""):
    args = "apply --index --reject %s \"%s\"" % (reverse_patch, patch_file)
    if self.Git(args) is None:
      self.WaitForResolvingConflicts(patch_file)


class UploadStep(Step):
  def __init__(self):
    Step.__init__(self, "Upload for code review.")

  def RunStep(self):
    print "Please enter the email address of a V8 reviewer for your patch: ",
    reviewer = self.ReadLine()
    args = "cl upload -r \"%s\" --send-mail" % reviewer
    if self.Git(args,pipe=False) is None:
      self.Die("'git cl upload' failed, please try again.")
