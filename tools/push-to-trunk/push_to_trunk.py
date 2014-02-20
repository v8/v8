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
import sys
import tempfile
import urllib2

from common_includes import *

TRUNKBRANCH = "TRUNKBRANCH"
CHROMIUM = "CHROMIUM"
DEPS_FILE = "DEPS_FILE"

CONFIG = {
  BRANCHNAME: "prepare-push",
  TRUNKBRANCH: "trunk-push",
  PERSISTFILE_BASENAME: "/tmp/v8-push-to-trunk-tempfile",
  TEMP_BRANCH: "prepare-push-temporary-branch-created-by-script",
  DOT_GIT_LOCATION: ".git",
  VERSION_FILE: "src/version.cc",
  CHANGELOG_FILE: "ChangeLog",
  CHANGELOG_ENTRY_FILE: "/tmp/v8-push-to-trunk-tempfile-changelog-entry",
  PATCH_FILE: "/tmp/v8-push-to-trunk-tempfile-patch-file",
  COMMITMSG_FILE: "/tmp/v8-push-to-trunk-tempfile-commitmsg",
  DEPS_FILE: "DEPS",
}

PUSH_MESSAGE_SUFFIX = " (based on bleeding_edge revision r%d)"
PUSH_MESSAGE_RE = re.compile(r".* \(based on bleeding_edge revision r(\d+)\)$")


class PushToTrunkOptions(CommonOptions):
  @staticmethod
  def MakeForcedOptions(author, reviewer, chrome_path):
    """Convenience wrapper."""
    class Options(object):
      pass
    options = Options()
    options.s = 0
    options.l = None
    options.b = None
    options.f = True
    options.m = False
    options.c = chrome_path
    options.reviewer = reviewer
    options.a = author
    return PushToTrunkOptions(options)

  def __init__(self, options):
    super(PushToTrunkOptions, self).__init__(options, options.m)
    self.requires_editor = not options.f
    self.wait_for_lgtm = not options.f
    self.tbr_commit = not options.m
    self.l = options.l
    self.reviewer = options.reviewer
    self.c = options.c
    self.b = getattr(options, 'b', None)
    self.author = getattr(options, 'a', None)


class Preparation(Step):
  MESSAGE = "Preparation."

  def RunStep(self):
    self.InitialEnvironmentChecks()
    self.CommonPrepare()
    self.PrepareBranch()
    self.DeleteBranch(self.Config(TRUNKBRANCH))


class FreshBranch(Step):
  MESSAGE = "Create a fresh branch."

  def RunStep(self):
    self.GitCreateBranch(self.Config(BRANCHNAME), "svn/bleeding_edge")


class DetectLastPush(Step):
  MESSAGE = "Detect commit ID of last push to trunk."

  def RunStep(self):
    last_push = self._options.l or self.FindLastTrunkPush()
    while True:
      # Print assumed commit, circumventing git's pager.
      print self.GitLog(n=1, git_hash=last_push)
      if self.Confirm("Is the commit printed above the last push to trunk?"):
        break
      last_push = self.FindLastTrunkPush(parent_hash=last_push)

    if self._options.b:
      # Read the bleeding edge revision of the last push from a command-line
      # option.
      last_push_bleeding_edge = self._options.b
    else:
      # Retrieve the bleeding edge revision of the last push from the text in
      # the push commit message.
      last_push_title = self.GitLog(n=1, format="%s", git_hash=last_push)
      last_push_be_svn = PUSH_MESSAGE_RE.match(last_push_title).group(1)
      if not last_push_be_svn:
        self.Die("Could not retrieve bleeding edge revision for trunk push %s"
                 % last_push)
      last_push_bleeding_edge = self.GitSVNFindGitHash(last_push_be_svn)
      if not last_push_bleeding_edge:
        self.Die("Could not retrieve bleeding edge git hash for trunk push %s"
                 % last_push)

    # TODO(machenbach): last_push_trunk points to the svn revision on trunk.
    # It is not used yet but we'll need it for retrieving the current version.
    self["last_push_trunk"] = last_push
    # TODO(machenbach): This currently points to the prepare push revision that
    # will be deprecated soon. After the deprecation it will point to the last
    # bleeding_edge revision that went into the last push.
    self["last_push_bleeding_edge"] = last_push_bleeding_edge


class PrepareChangeLog(Step):
  MESSAGE = "Prepare raw ChangeLog entry."

  def Reload(self, body):
    """Attempts to reload the commit message from rietveld in order to allow
    late changes to the LOG flag. Note: This is brittle to future changes of
    the web page name or structure.
    """
    match = re.search(r"^Review URL: https://codereview\.chromium\.org/(\d+)$",
                      body, flags=re.M)
    if match:
      cl_url = ("https://codereview.chromium.org/%s/description"
                % match.group(1))
      try:
        # Fetch from Rietveld but only retry once with one second delay since
        # there might be many revisions.
        body = self.ReadURL(cl_url, wait_plan=[1])
      except urllib2.URLError:
        pass
    return body

  def RunStep(self):
    # These version numbers are used again later for the trunk commit.
    self.ReadAndPersistVersion()
    self["date"] = self.GetDate()
    self["version"] = "%s.%s.%s" % (self["major"],
                                    self["minor"],
                                    self["build"])
    output = "%s: Version %s\n\n" % (self["date"],
                                     self["version"])
    TextToFile(output, self.Config(CHANGELOG_ENTRY_FILE))
    commits = self.GitLog(format="%H",
        git_hash="%s..HEAD" % self["last_push_bleeding_edge"])

    # Cache raw commit messages.
    commit_messages = [
      [
        self.GitLog(n=1, format="%s", git_hash=commit),
        self.Reload(self.GitLog(n=1, format="%B", git_hash=commit)),
        self.GitLog(n=1, format="%an", git_hash=commit),
      ] for commit in commits.splitlines()
    ]

    # Auto-format commit messages.
    body = MakeChangeLogBody(commit_messages, auto_format=True)
    AppendToFile(body, self.Config(CHANGELOG_ENTRY_FILE))

    msg = ("        Performance and stability improvements on all platforms."
           "\n#\n# The change log above is auto-generated. Please review if "
           "all relevant\n# commit messages from the list below are included."
           "\n# All lines starting with # will be stripped.\n#\n")
    AppendToFile(msg, self.Config(CHANGELOG_ENTRY_FILE))

    # Include unformatted commit messages as a reference in a comment.
    comment_body = MakeComment(MakeChangeLogBody(commit_messages))
    AppendToFile(comment_body, self.Config(CHANGELOG_ENTRY_FILE))


class EditChangeLog(Step):
  MESSAGE = "Edit ChangeLog entry."

  def RunStep(self):
    print ("Please press <Return> to have your EDITOR open the ChangeLog "
           "entry, then edit its contents to your liking. When you're done, "
           "save the file and exit your EDITOR. ")
    self.ReadLine(default="")
    self.Editor(self.Config(CHANGELOG_ENTRY_FILE))
    handle, new_changelog = tempfile.mkstemp()
    os.close(handle)

    # Strip comments and reformat with correct indentation.
    changelog_entry = FileToText(self.Config(CHANGELOG_ENTRY_FILE)).rstrip()
    changelog_entry = StripComments(changelog_entry)
    changelog_entry = "\n".join(map(Fill80, changelog_entry.splitlines()))
    changelog_entry = changelog_entry.lstrip()

    if changelog_entry == "":
      self.Die("Empty ChangeLog entry.")

    with open(new_changelog, "w") as f:
      f.write(changelog_entry)
      f.write("\n\n\n")  # Explicitly insert two empty lines.

    AppendToFile(FileToText(self.Config(CHANGELOG_FILE)), new_changelog)
    TextToFile(FileToText(new_changelog), self.Config(CHANGELOG_FILE))
    os.remove(new_changelog)


class IncrementVersion(Step):
  MESSAGE = "Increment version number."

  def RunStep(self):
    new_build = str(int(self["build"]) + 1)

    if self.Confirm(("Automatically increment BUILD_NUMBER? (Saying 'n' will "
                     "fire up your EDITOR on %s so you can make arbitrary "
                     "changes. When you're done, save the file and exit your "
                     "EDITOR.)" % self.Config(VERSION_FILE))):
      text = FileToText(self.Config(VERSION_FILE))
      text = MSub(r"(?<=#define BUILD_NUMBER)(?P<space>\s+)\d*$",
                  r"\g<space>%s" % new_build,
                  text)
      TextToFile(text, self.Config(VERSION_FILE))
    else:
      self.Editor(self.Config(VERSION_FILE))

    self.ReadAndPersistVersion("new_")


class CommitLocal(Step):
  MESSAGE = "Commit to local branch."

  def RunStep(self):
    self["prep_commit_msg"] = ("Prepare push to trunk.  "
        "Now working on version %s.%s.%s." % (self["new_major"],
                                              self["new_minor"],
                                              self["new_build"]))

    # Include optional TBR only in the git command. The persisted commit
    # message is used for finding the commit again later.
    if self._options.tbr_commit:
      message = "%s\n\nTBR=%s" % (self["prep_commit_msg"],
                                  self._options.reviewer)
    else:
      message = "%s" % self["prep_commit_msg"]
    self.GitCommit(message)


class CommitRepository(Step):
  MESSAGE = "Commit to the repository."

  def RunStep(self):
    self.WaitForLGTM()
    # Re-read the ChangeLog entry (to pick up possible changes).
    # FIXME(machenbach): This was hanging once with a broken pipe.
    TextToFile(GetLastChangeLogEntries(self.Config(CHANGELOG_FILE)),
               self.Config(CHANGELOG_ENTRY_FILE))

    self.GitPresubmit()
    self.GitDCommit()


class StragglerCommits(Step):
  MESSAGE = ("Fetch straggler commits that sneaked in since this script was "
             "started.")

  def RunStep(self):
    self.GitSVNFetch()
    self.GitCheckout("svn/bleeding_edge")
    self["prepare_commit_hash"] = self.GitLog(n=1, format="%H",
                                              grep=self["prep_commit_msg"])


class SquashCommits(Step):
  MESSAGE = "Squash commits into one."

  def RunStep(self):
    # Instead of relying on "git rebase -i", we'll just create a diff, because
    # that's easier to automate.
    TextToFile(self.GitDiff("svn/trunk", self["prepare_commit_hash"]),
               self.Config(PATCH_FILE))

    # Convert the ChangeLog entry to commit message format.
    text = FileToText(self.Config(CHANGELOG_ENTRY_FILE))

    # Remove date and trailing white space.
    text = re.sub(r"^%s: " % self["date"], "", text.rstrip())

    # Retrieve svn revision for showing the used bleeding edge revision in the
    # commit message.
    self["svn_revision"] = self.GitSVNFindSVNRev(self["prepare_commit_hash"])
    suffix = PUSH_MESSAGE_SUFFIX % int(self["svn_revision"])
    text = MSub(r"^(Version \d+\.\d+\.\d+)$", "\\1%s" % suffix, text)

    # Remove indentation and merge paragraphs into single long lines, keeping
    # empty lines between them.
    def SplitMapJoin(split_text, fun, join_text):
      return lambda text: join_text.join(map(fun, text.split(split_text)))
    strip = lambda line: line.strip()
    text = SplitMapJoin("\n\n", SplitMapJoin("\n", strip, " "), "\n\n")(text)

    if not text:
      self.Die("Commit message editing failed.")
    TextToFile(text, self.Config(COMMITMSG_FILE))
    os.remove(self.Config(CHANGELOG_ENTRY_FILE))


class NewBranch(Step):
  MESSAGE = "Create a new branch from trunk."

  def RunStep(self):
    self.GitCreateBranch(self.Config(TRUNKBRANCH), "svn/trunk")


class ApplyChanges(Step):
  MESSAGE = "Apply squashed changes."

  def RunStep(self):
    self.ApplyPatch(self.Config(PATCH_FILE))
    Command("rm", "-f %s*" % self.Config(PATCH_FILE))


class SetVersion(Step):
  MESSAGE = "Set correct version for trunk."

  def RunStep(self):
    output = ""
    for line in FileToText(self.Config(VERSION_FILE)).splitlines():
      if line.startswith("#define MAJOR_VERSION"):
        line = re.sub("\d+$", self["major"], line)
      elif line.startswith("#define MINOR_VERSION"):
        line = re.sub("\d+$", self["minor"], line)
      elif line.startswith("#define BUILD_NUMBER"):
        line = re.sub("\d+$", self["build"], line)
      elif line.startswith("#define PATCH_LEVEL"):
        line = re.sub("\d+$", "0", line)
      elif line.startswith("#define IS_CANDIDATE_VERSION"):
        line = re.sub("\d+$", "0", line)
      output += "%s\n" % line
    TextToFile(output, self.Config(VERSION_FILE))


class CommitTrunk(Step):
  MESSAGE = "Commit to local trunk branch."

  def RunStep(self):
    self.GitAdd(self.Config(VERSION_FILE))
    self.GitCommit(file_name = self.Config(COMMITMSG_FILE))
    Command("rm", "-f %s*" % self.Config(COMMITMSG_FILE))


class SanityCheck(Step):
  MESSAGE = "Sanity check."

  def RunStep(self):
    if not self.Confirm("Please check if your local checkout is sane: Inspect "
        "%s, compile, run tests. Do you want to commit this new trunk "
        "revision to the repository?" % self.Config(VERSION_FILE)):
      self.Die("Execution canceled.")


class CommitSVN(Step):
  MESSAGE = "Commit to SVN."

  def RunStep(self):
    result = self.GitSVNDCommit()
    if not result:
      self.Die("'git svn dcommit' failed.")
    result = filter(lambda x: re.search(r"^Committed r[0-9]+", x),
                    result.splitlines())
    if len(result) > 0:
      self["trunk_revision"] = re.sub(r"^Committed r([0-9]+)", r"\1",result[0])

    # Sometimes grepping for the revision fails. No idea why. If you figure
    # out why it is flaky, please do fix it properly.
    if not self["trunk_revision"]:
      print("Sorry, grepping for the SVN revision failed. Please look for it "
            "in the last command's output above and provide it manually (just "
            "the number, without the leading \"r\").")
      self.DieNoManualMode("Can't prompt in forced mode.")
      while not self["trunk_revision"]:
        print "> ",
        self["trunk_revision"] = self.ReadLine()


class TagRevision(Step):
  MESSAGE = "Tag the new revision."

  def RunStep(self):
    self.GitSVNTag(self["version"])


class CheckChromium(Step):
  MESSAGE = "Ask for chromium checkout."

  def Run(self):
    self["chrome_path"] = self._options.c
    if not self["chrome_path"]:
      self.DieNoManualMode("Please specify the path to a Chromium checkout in "
                          "forced mode.")
      print ("Do you have a \"NewGit\" Chromium checkout and want "
          "this script to automate creation of the roll CL? If yes, enter the "
          "path to (and including) the \"src\" directory here, otherwise just "
          "press <Return>: "),
      self["chrome_path"] = self.ReadLine()


class SwitchChromium(Step):
  MESSAGE = "Switch to Chromium checkout."
  REQUIRES = "chrome_path"

  def RunStep(self):
    self["v8_path"] = os.getcwd()
    os.chdir(self["chrome_path"])
    self.InitialEnvironmentChecks()
    # Check for a clean workdir.
    if not self.GitIsWorkdirClean():
      self.Die("Workspace is not clean. Please commit or undo your changes.")
    # Assert that the DEPS file is there.
    if not os.path.exists(self.Config(DEPS_FILE)):
      self.Die("DEPS file not present.")


class UpdateChromiumCheckout(Step):
  MESSAGE = "Update the checkout and create a new branch."
  REQUIRES = "chrome_path"

  def RunStep(self):
    os.chdir(self["chrome_path"])
    self.GitCheckout("master")
    self.GitPull()
    self.GitCreateBranch("v8-roll-%s" % self["trunk_revision"])


class UploadCL(Step):
  MESSAGE = "Create and upload CL."
  REQUIRES = "chrome_path"

  def RunStep(self):
    os.chdir(self["chrome_path"])

    # Patch DEPS file.
    deps = FileToText(self.Config(DEPS_FILE))
    deps = re.sub("(?<=\"v8_revision\": \")([0-9]+)(?=\")",
                  self["trunk_revision"],
                  deps)
    TextToFile(deps, self.Config(DEPS_FILE))

    if self._options.reviewer:
      print "Using account %s for review." % self._options.reviewer
      rev = self._options.reviewer
    else:
      print "Please enter the email address of a reviewer for the roll CL: ",
      self.DieNoManualMode("A reviewer must be specified in forced mode.")
      rev = self.ReadLine()
    suffix = PUSH_MESSAGE_SUFFIX % int(self["svn_revision"])
    self.GitCommit("Update V8 to version %s%s.\n\nTBR=%s"
                   % (self["version"], suffix, rev))
    self.GitUpload(author=self._options.author,
                   force=self._options.force_upload)
    print "CL uploaded."


class SwitchV8(Step):
  MESSAGE = "Returning to V8 checkout."
  REQUIRES = "chrome_path"

  def RunStep(self):
    os.chdir(self["v8_path"])


class CleanUp(Step):
  MESSAGE = "Done!"

  def RunStep(self):
    if self["chrome_path"]:
      print("Congratulations, you have successfully created the trunk "
            "revision %s and rolled it into Chromium. Please don't forget to "
            "update the v8rel spreadsheet:" % self["version"])
    else:
      print("Congratulations, you have successfully created the trunk "
            "revision %s. Please don't forget to roll this new version into "
            "Chromium, and to update the v8rel spreadsheet:"
            % self["version"])
    print "%s\ttrunk\t%s" % (self["version"],
                             self["trunk_revision"])

    self.CommonCleanup()
    if self.Config(TRUNKBRANCH) != self["current_branch"]:
      self.GitDeleteBranch(self.Config(TRUNKBRANCH))


def RunPushToTrunk(config,
                   options,
                   side_effect_handler=DEFAULT_SIDE_EFFECT_HANDLER):
  step_classes = [
    Preparation,
    FreshBranch,
    DetectLastPush,
    PrepareChangeLog,
    EditChangeLog,
    IncrementVersion,
    CommitLocal,
    UploadStep,
    CommitRepository,
    StragglerCommits,
    SquashCommits,
    NewBranch,
    ApplyChanges,
    SetVersion,
    CommitTrunk,
    SanityCheck,
    CommitSVN,
    TagRevision,
    CheckChromium,
    SwitchChromium,
    UpdateChromiumCheckout,
    UploadCL,
    SwitchV8,
    CleanUp,
  ]

  RunScript(step_classes, config, options, side_effect_handler)


def BuildOptions():
  result = optparse.OptionParser()
  result.add_option("-a", "--author", dest="a",
                    help=("Specify the author email used for rietveld."))
  result.add_option("-b", "--last-bleeding-edge", dest="b",
                    help=("Manually specify the git commit ID of the last "
                          "bleeding edge revision that was pushed to trunk. "
                          "This is used for the auto-generated ChangeLog "
                          "entry."))
  result.add_option("-c", "--chromium", dest="c",
                    help=("Specify the path to your Chromium src/ "
                          "directory to automate the V8 roll."))
  result.add_option("-f", "--force", dest="f",
                    help="Don't prompt the user.",
                    default=False, action="store_true")
  result.add_option("-l", "--last-push", dest="l",
                    help=("Manually specify the git commit ID "
                          "of the last push to trunk."))
  result.add_option("-m", "--manual", dest="m",
                    help="Prompt the user at every important step.",
                    default=False, action="store_true")
  result.add_option("-r", "--reviewer",
                    help=("Specify the account name to be used for reviews."))
  result.add_option("-s", "--step", dest="s",
                    help="Specify the step where to start work. Default: 0.",
                    default=0, type="int")
  return result


def ProcessOptions(options):
  if options.s < 0:
    print "Bad step number %d" % options.s
    return False
  if not options.m and not options.reviewer:
    print "A reviewer (-r) is required in (semi-)automatic mode."
    return False
  if options.f and options.m:
    print "Manual and forced mode cannot be combined."
    return False
  if not options.m and not options.c:
    print "A chromium checkout (-c) is required in (semi-)automatic mode."
    return False
  if not options.m and not options.a:
    print "Specify your chromium.org email with -a in (semi-)automatic mode."
    return False
  return True


def Main():
  parser = BuildOptions()
  (options, args) = parser.parse_args()
  if not ProcessOptions(options):
    parser.print_help()
    return 1
  RunPushToTrunk(CONFIG, PushToTrunkOptions(options))

if __name__ == "__main__":
  sys.exit(Main())
