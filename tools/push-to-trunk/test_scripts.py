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
import tempfile
import unittest

import common_includes
from common_includes import *
import push_to_trunk
from push_to_trunk import *


TEST_CONFIG = {
  BRANCHNAME: "test-prepare-push",
  TRUNKBRANCH: "test-trunk-push",
  PERSISTFILE_BASENAME: "/tmp/test-v8-push-to-trunk-tempfile",
  TEMP_BRANCH: "test-prepare-push-temporary-branch-created-by-script",
  DOT_GIT_LOCATION: None,
  VERSION_FILE: None,
  CHANGELOG_FILE: None,
  CHANGELOG_ENTRY_FILE: "/tmp/test-v8-push-to-trunk-tempfile-changelog-entry",
  PATCH_FILE: "/tmp/test-v8-push-to-trunk-tempfile-patch",
  COMMITMSG_FILE: "/tmp/test-v8-push-to-trunk-tempfile-commitmsg",
  CHROMIUM: "/tmp/test-v8-push-to-trunk-tempfile-chromium",
  DEPS_FILE: "/tmp/test-v8-push-to-trunk-tempfile-chromium/DEPS",
}


class ToplevelTest(unittest.TestCase):
  def testMakeChangeLogBodySimple(self):
    commits = lambda: [
          ["        Title text 1",
           "Title text 1\n\nBUG=\n",
           "        author1@chromium.org"],
          ["        Title text 2",
           "Title text 2\n\nBUG=1234\n",
           "        author2@chromium.org"],
        ]
    self.assertEquals("        Title text 1\n"
                      "        author1@chromium.org\n\n"
                      "        Title text 2\n"
                      "        (Chromium issue 1234)\n"
                      "        author2@chromium.org\n\n",
                      MakeChangeLogBody(commits))

  def testMakeChangeLogBodyEmpty(self):
    commits = lambda: []
    self.assertEquals("", MakeChangeLogBody(commits))

  def testMakeChangeLogBugReferenceEmpty(self):
    self.assertEquals("", MakeChangeLogBugReference(""))
    self.assertEquals("", MakeChangeLogBugReference("LOG="))
    self.assertEquals("", MakeChangeLogBugReference(" BUG ="))
    self.assertEquals("", MakeChangeLogBugReference("BUG=none\t"))

  def testMakeChangeLogBugReferenceSimple(self):
    self.assertEquals("        (issue 987654)\n",
                      MakeChangeLogBugReference("BUG = v8:987654"))
    self.assertEquals("        (Chromium issue 987654)\n",
                      MakeChangeLogBugReference("BUG=987654 "))

  def testMakeChangeLogBugReferenceFromBody(self):
    self.assertEquals("        (Chromium issue 1234567)\n",
                      MakeChangeLogBugReference("Title\n\nTBR=\nBUG=\n"
                                                " BUG=\tchromium:1234567\t\n"
                                                "R=somebody\n"))

  def testMakeChangeLogBugReferenceMultiple(self):
    # All issues should be sorted and grouped. Multiple references to the same
    # issue should be filtered.
    self.assertEquals("        (issues 123, 234, Chromium issue 345)\n",
                      MakeChangeLogBugReference("Title\n\n"
                                                "BUG=v8:234\n"
                                                "  BUG\t= 345, \tv8:234,\n"
                                                "BUG=v8:123\n"
                                                "R=somebody\n"))
    self.assertEquals("        (Chromium issues 123, 234)\n",
                      MakeChangeLogBugReference("Title\n\n"
                                                "BUG=234,,chromium:123 \n"
                                                "R=somebody\n"))
    self.assertEquals("        (Chromium issues 123, 234)\n",
                      MakeChangeLogBugReference("Title\n\n"
                                                "BUG=chromium:234, , 123\n"
                                                "R=somebody\n"))
    self.assertEquals("        (issues 345, 456)\n",
                      MakeChangeLogBugReference("Title\n\n"
                                                "\t\tBUG=v8:345,v8:456\n"
                                                "R=somebody\n"))
    self.assertEquals("        (issue 123, Chromium issues 345, 456)\n",
                      MakeChangeLogBugReference("Title\n\n"
                                                "BUG=chromium:456\n"
                                                "BUG = none\n"
                                                "R=somebody\n"
                                                "BUG=456,v8:123, 345"))

  def testMakeChangeLogBugReferenceLong(self):
    # -----------------00--------10--------20--------30--------
    self.assertEquals("        (issues 234, 1234567890, 1234567"
                      "8901234567890, Chromium issues 12345678,\n"
                      "        123456789)\n",
                      MakeChangeLogBugReference("BUG=v8:234\n"
                                                "BUG=v8:1234567890\n"
                                                "BUG=v8:12345678901234567890\n"
                                                "BUG=123456789\n"
                                                "BUG=12345678\n"))
    # -----------------00--------10--------20--------30--------
    self.assertEquals("        (issues 234, 1234567890, 1234567"
                      "8901234567890, Chromium issues\n"
                      "        123456789, 1234567890)\n",
                      MakeChangeLogBugReference("BUG=v8:234\n"
                                                "BUG=v8:12345678901234567890\n"
                                                "BUG=v8:1234567890\n"
                                                "BUG=123456789\n"
                                                "BUG=1234567890\n"))
    # -----------------00--------10--------20--------30--------
    self.assertEquals("        (Chromium issues 234, 1234567890"
                      ", 12345678901234567,\n"
                      "        1234567890123456789)\n",
                      MakeChangeLogBugReference("BUG=234\n"
                                                "BUG=12345678901234567\n"
                                                "BUG=1234567890123456789\n"
                                                "BUG=1234567890\n"))

class ScriptTest(unittest.TestCase):
  def MakeEmptyTempFile(self):
    handle, name = tempfile.mkstemp()
    os.close(handle)
    self._tmp_files.append(name)
    return name

  def MakeTempVersionFile(self):
    name = self.MakeEmptyTempFile()
    with open(name, "w") as f:
      f.write("  // Some line...\n")
      f.write("\n")
      f.write("#define MAJOR_VERSION    3\n")
      f.write("#define MINOR_VERSION    22\n")
      f.write("#define BUILD_NUMBER     5\n")
      f.write("#define PATCH_LEVEL      0\n")
      f.write("  // Some line...\n")
      f.write("#define IS_CANDIDATE_VERSION 0\n")
    return name

  def MakeStep(self, step_class=Step, state=None):
    state = state or {}
    step = step_class()
    step.SetConfig(TEST_CONFIG)
    step.SetState(state)
    step.SetNumber(0)
    step.SetSideEffectHandler(self)
    return step

  def GitMock(self, cmd, args="", pipe=True):
    self._git_index += 1
    try:
      git_invocation = self._git_recipe[self._git_index]
    except IndexError:
      raise Exception("Calling git %s" % args)
    if git_invocation[0] != args:
      raise Exception("Expected: %s - Actual: %s" % (git_invocation[0], args))
    if len(git_invocation) == 3:
      # Run optional function checking the context during this git command.
      git_invocation[2]()
    return git_invocation[1]

  def LogMock(self, cmd, args=""):
    print "Log: %s %s" % (cmd, args)

  MOCKS = {
    "git": GitMock,
    "vi": LogMock,
  }

  def Command(self, cmd, args="", prefix="", pipe=True):
    return ScriptTest.MOCKS[cmd](self, cmd, args)

  def ReadLine(self):
    self._rl_index += 1
    try:
      return self._rl_recipe[self._rl_index]
    except IndexError:
      raise Exception("Calling readline too often")

  def setUp(self):
    self._git_recipe = []
    self._git_index = -1
    self._rl_recipe = []
    self._rl_index = -1
    self._tmp_files = []

  def tearDown(self):
    Command("rm", "-rf %s*" % TEST_CONFIG[PERSISTFILE_BASENAME])

    # Clean up temps. Doesn't work automatically.
    for name in self._tmp_files:
      if os.path.exists(name):
        os.remove(name)

    if self._git_index < len(self._git_recipe) -1:
      raise Exception("Called git too seldom: %d vs. %d" %
                      (self._git_index, len(self._git_recipe)))
    if self._rl_index < len(self._rl_recipe) -1:
      raise Exception("Too little input: %d vs. %d" %
                      (self._rl_index, len(self._rl_recipe)))

  def testPersistRestore(self):
    self.MakeStep().Persist("test1", "")
    self.assertEquals("", self.MakeStep().Restore("test1"))
    self.MakeStep().Persist("test2", "AB123")
    self.assertEquals("AB123", self.MakeStep().Restore("test2"))

  def testGitOrig(self):
    self.assertTrue(Command("git", "--version").startswith("git version"))

  def testGitMock(self):
    self._git_recipe = [["--version", "git version 1.2.3"], ["dummy", ""]]
    self.assertEquals("git version 1.2.3", self.MakeStep().Git("--version"))
    self.assertEquals("", self.MakeStep().Git("dummy"))

  def testCommonPrepareDefault(self):
    self._git_recipe = [
      ["status -s -uno", ""],
      ["status -s -b -uno", "## some_branch"],
      ["svn fetch", ""],
      ["branch", "  branch1\n* %s" % TEST_CONFIG[TEMP_BRANCH]],
      ["branch -D %s" % TEST_CONFIG[TEMP_BRANCH], ""],
      ["checkout -b %s" % TEST_CONFIG[TEMP_BRANCH], ""],
      ["branch", ""],
    ]
    self._rl_recipe = ["Y"]
    self.MakeStep().CommonPrepare()
    self.MakeStep().PrepareBranch()
    self.assertEquals("some_branch", self.MakeStep().Restore("current_branch"))

  def testCommonPrepareNoConfirm(self):
    self._git_recipe = [
      ["status -s -uno", ""],
      ["status -s -b -uno", "## some_branch"],
      ["svn fetch", ""],
      ["branch", "  branch1\n* %s" % TEST_CONFIG[TEMP_BRANCH]],
    ]
    self._rl_recipe = ["n"]
    self.MakeStep().CommonPrepare()
    self.assertRaises(Exception, self.MakeStep().PrepareBranch)
    self.assertEquals("some_branch", self.MakeStep().Restore("current_branch"))

  def testCommonPrepareDeleteBranchFailure(self):
    self._git_recipe = [
      ["status -s -uno", ""],
      ["status -s -b -uno", "## some_branch"],
      ["svn fetch", ""],
      ["branch", "  branch1\n* %s" % TEST_CONFIG[TEMP_BRANCH]],
      ["branch -D %s" % TEST_CONFIG[TEMP_BRANCH], None],
    ]
    self._rl_recipe = ["Y"]
    self.MakeStep().CommonPrepare()
    self.assertRaises(Exception, self.MakeStep().PrepareBranch)
    self.assertEquals("some_branch", self.MakeStep().Restore("current_branch"))

  def testInitialEnvironmentChecks(self):
    TEST_CONFIG[DOT_GIT_LOCATION] = self.MakeEmptyTempFile()
    os.environ["EDITOR"] = "vi"
    self.MakeStep().InitialEnvironmentChecks()

  def testReadAndPersistVersion(self):
    TEST_CONFIG[VERSION_FILE] = self.MakeTempVersionFile()
    step = self.MakeStep()
    step.ReadAndPersistVersion()
    self.assertEquals("3", self.MakeStep().Restore("major"))
    self.assertEquals("22", self.MakeStep().Restore("minor"))
    self.assertEquals("5", self.MakeStep().Restore("build"))
    self.assertEquals("0", self.MakeStep().Restore("patch"))
    self.assertEquals("3", step._state["major"])
    self.assertEquals("22", step._state["minor"])
    self.assertEquals("5", step._state["build"])
    self.assertEquals("0", step._state["patch"])

  def testRegex(self):
    self.assertEqual("(issue 321)",
                     re.sub(r"BUG=v8:(.*)$", r"(issue \1)", "BUG=v8:321"))
    self.assertEqual("(Chromium issue 321)",
                     re.sub(r"BUG=(.*)$", r"(Chromium issue \1)", "BUG=321"))

    cl = "  too little\n\ttab\ttab\n         too much\n        trailing  "
    cl = MSub(r"\t", r"        ", cl)
    cl = MSub(r"^ {1,7}([^ ])", r"        \1", cl)
    cl = MSub(r"^ {9,80}([^ ])", r"        \1", cl)
    cl = MSub(r" +$", r"", cl)
    self.assertEqual("        too little\n"
                     "        tab        tab\n"
                     "        too much\n"
                     "        trailing", cl)

    self.assertEqual("//\n#define BUILD_NUMBER  3\n",
                     MSub(r"(?<=#define BUILD_NUMBER)(?P<space>\s+)\d*$",
                          r"\g<space>3",
                          "//\n#define BUILD_NUMBER  321\n"))

  def testPrepareChangeLog(self):
    TEST_CONFIG[VERSION_FILE] = self.MakeTempVersionFile()
    TEST_CONFIG[CHANGELOG_ENTRY_FILE] = self.MakeEmptyTempFile()

    self._git_recipe = [
      ["log 1234..HEAD --format=%H", "rev1\nrev2"],
      ["log -1 rev1 --format=\"%w(80,8,8)%s\"", "        Title text 1"],
      ["log -1 rev1 --format=\"%B\"", "Title\n\nBUG=\n"],
      ["log -1 rev1 --format=\"%w(80,8,8)(%an)\"",
       "        author1@chromium.org"],
      ["log -1 rev2 --format=\"%w(80,8,8)%s\"", "        Title text 2"],
      ["log -1 rev2 --format=\"%B\"", "Title\n\nBUG=321\n"],
      ["log -1 rev2 --format=\"%w(80,8,8)(%an)\"",
       "        author2@chromium.org"],
    ]

    self.MakeStep().Persist("last_push", "1234")
    self.MakeStep(PrepareChangeLog).Run()

    cl = FileToText(TEST_CONFIG[CHANGELOG_ENTRY_FILE])
    self.assertTrue(re.search(r"\d+\-\d+\-\d+: Version 3\.22\.5", cl))
    self.assertTrue(re.search(r"        Title text 1", cl))
    self.assertTrue(re.search(r"        Title text 2", cl))
    self.assertTrue(re.search(r"        author1@chromium.org", cl))
    self.assertTrue(re.search(r"        author2@chromium.org", cl))
    self.assertTrue(re.search(r"        \(Chromium issue 321\)", cl))
    self.assertFalse(re.search(r"BUG=", cl))
    self.assertEquals("3", self.MakeStep().Restore("major"))
    self.assertEquals("22", self.MakeStep().Restore("minor"))
    self.assertEquals("5", self.MakeStep().Restore("build"))
    self.assertEquals("0", self.MakeStep().Restore("patch"))

  def testEditChangeLog(self):
    TEST_CONFIG[CHANGELOG_ENTRY_FILE] = self.MakeEmptyTempFile()
    TEST_CONFIG[CHANGELOG_FILE] = self.MakeEmptyTempFile()
    TextToFile("        Original CL", TEST_CONFIG[CHANGELOG_FILE])
    TextToFile("  New  \n\tLines  \n", TEST_CONFIG[CHANGELOG_ENTRY_FILE])
    os.environ["EDITOR"] = "vi"

    self._rl_recipe = [
      "",  # Open editor.
    ]

    self.MakeStep(EditChangeLog).Run()

    self.assertEquals("        New\n        Lines\n\n\n        Original CL",
                      FileToText(TEST_CONFIG[CHANGELOG_FILE]))

  def testIncrementVersion(self):
    TEST_CONFIG[VERSION_FILE] = self.MakeTempVersionFile()
    self.MakeStep().Persist("build", "5")

    self._rl_recipe = [
      "Y",  # Increment build number.
    ]

    self.MakeStep(IncrementVersion).Run()

    self.assertEquals("3", self.MakeStep().Restore("new_major"))
    self.assertEquals("22", self.MakeStep().Restore("new_minor"))
    self.assertEquals("6", self.MakeStep().Restore("new_build"))
    self.assertEquals("0", self.MakeStep().Restore("new_patch"))

  def testLastChangeLogEntries(self):
    TEST_CONFIG[CHANGELOG_FILE] = self.MakeEmptyTempFile()
    l = """
        Fixed something.
        (issue 1234)\n"""
    for _ in xrange(10): l = l + l

    cl_chunk = """2013-11-12: Version 3.23.2\n%s
        Performance and stability improvements on all platforms.\n\n\n""" % l

    cl_chunk_full = cl_chunk + cl_chunk + cl_chunk
    TextToFile(cl_chunk_full, TEST_CONFIG[CHANGELOG_FILE])

    cl = GetLastChangeLogEntries(TEST_CONFIG[CHANGELOG_FILE])
    self.assertEquals(cl_chunk, cl)

  def testSquashCommits(self):
    TEST_CONFIG[CHANGELOG_ENTRY_FILE] = self.MakeEmptyTempFile()
    with open(TEST_CONFIG[CHANGELOG_ENTRY_FILE], "w") as f:
      f.write("1999-11-11: Version 3.22.5\n")
      f.write("\n")
      f.write("        Log text 1.\n")
      f.write("        Chromium issue 12345\n")
      f.write("\n")
      f.write("        Performance and stability improvements on all "
              "platforms.\n")

    self._git_recipe = [
      ["diff svn/trunk hash1", "patch content"],
    ]

    self.MakeStep().Persist("prepare_commit_hash", "hash1")
    self.MakeStep().Persist("date", "1999-11-11")

    self.MakeStep(SquashCommits).Run()

    msg = FileToText(TEST_CONFIG[COMMITMSG_FILE])
    self.assertTrue(re.search(r"Version 3\.22\.5", msg))
    self.assertTrue(re.search(r"Performance and stability", msg))
    self.assertTrue(re.search(r"Log text 1\. Chromium issue 12345", msg))
    self.assertFalse(re.search(r"\d+\-\d+\-\d+", msg))

    patch = FileToText(TEST_CONFIG[ PATCH_FILE])
    self.assertTrue(re.search(r"patch content", patch))

  def _PushToTrunk(self, force=False):
    TEST_CONFIG[DOT_GIT_LOCATION] = self.MakeEmptyTempFile()
    TEST_CONFIG[VERSION_FILE] = self.MakeTempVersionFile()
    TEST_CONFIG[CHANGELOG_ENTRY_FILE] = self.MakeEmptyTempFile()
    TEST_CONFIG[CHANGELOG_FILE] = self.MakeEmptyTempFile()
    if not os.path.exists(TEST_CONFIG[CHROMIUM]):
      os.makedirs(TEST_CONFIG[CHROMIUM])
    TextToFile("1999-04-05: Version 3.22.4", TEST_CONFIG[CHANGELOG_FILE])
    TextToFile("Some line\n   \"v8_revision\": \"123444\",\n  some line",
                 TEST_CONFIG[DEPS_FILE])
    os.environ["EDITOR"] = "vi"

    def CheckPreparePush():
      cl = FileToText(TEST_CONFIG[CHANGELOG_FILE])
      self.assertTrue(re.search(r"Version 3.22.5", cl))
      self.assertTrue(re.search(r"        Log text 1", cl))
      self.assertTrue(re.search(r"        \(issue 321\)", cl))
      version = FileToText(TEST_CONFIG[VERSION_FILE])
      self.assertTrue(re.search(r"#define BUILD_NUMBER\s+6", version))

    def CheckSVNCommit():
      commit = FileToText(TEST_CONFIG[COMMITMSG_FILE])
      self.assertTrue(re.search(r"Version 3.22.5", commit))
      self.assertTrue(re.search(r"Log text 1. \(issue 321\)", commit))
      version = FileToText(TEST_CONFIG[VERSION_FILE])
      self.assertTrue(re.search(r"#define MINOR_VERSION\s+22", version))
      self.assertTrue(re.search(r"#define BUILD_NUMBER\s+5", version))
      self.assertFalse(re.search(r"#define BUILD_NUMBER\s+6", version))
      self.assertTrue(re.search(r"#define PATCH_LEVEL\s+0", version))
      self.assertTrue(re.search(r"#define IS_CANDIDATE_VERSION\s+0", version))

    force_flag = " -f" if force else ""
    self._git_recipe = [
      ["status -s -uno", ""],
      ["status -s -b -uno", "## some_branch\n"],
      ["svn fetch", ""],
      ["branch", "  branch1\n* branch2\n"],
      ["checkout -b %s" % TEST_CONFIG[TEMP_BRANCH], ""],
      ["branch", "  branch1\n* branch2\n"],
      ["branch", "  branch1\n* branch2\n"],
      ["checkout -b %s svn/bleeding_edge" % TEST_CONFIG[BRANCHNAME], ""],
      ["log -1 --format=%H ChangeLog", "1234\n"],
      ["log -1 1234", "Last push ouput\n"],
      ["log 1234..HEAD --format=%H", "rev1\n"],
      ["log -1 rev1 --format=\"%w(80,8,8)%s\"", "        Log text 1.\n"],
      ["log -1 rev1 --format=\"%B\"", "Text\nBUG=v8:321\nText\n"],
      ["log -1 rev1 --format=\"%w(80,8,8)(%an)\"",
       "        author1@chromium.org\n"],
      [("commit -a -m \"Prepare push to trunk.  "
        "Now working on version 3.22.6.\""),
       " 2 files changed\n",
        CheckPreparePush],
      ["cl upload -r \"reviewer@chromium.org\" --send-mail%s" % force_flag,
       "done\n"],
      ["cl dcommit -f", "Closing issue\n"],
      ["svn fetch", "fetch result\n"],
      ["checkout svn/bleeding_edge", ""],
      [("log -1 --format=%H --grep=\"Prepare push to trunk.  "
        "Now working on version 3.22.6.\""),
       "hash1\n"],
      ["diff svn/trunk hash1", "patch content\n"],
      ["checkout -b %s svn/trunk" % TEST_CONFIG[TRUNKBRANCH], ""],
      ["apply --index --reject  \"%s\"" % TEST_CONFIG[PATCH_FILE], ""],
      ["add \"%s\"" % TEST_CONFIG[VERSION_FILE], ""],
      ["commit -F \"%s\"" % TEST_CONFIG[COMMITMSG_FILE], "", CheckSVNCommit],
      ["svn dcommit 2>&1", "Some output\nCommitted r123456\nSome output\n"],
      ["svn tag 3.22.5 -m \"Tagging version 3.22.5\"", ""],
      ["status -s -uno", ""],
      ["checkout master", ""],
      ["pull", ""],
      ["checkout -b v8-roll-123456", ""],
      [("commit -am \"Update V8 to version 3.22.5.\n\n"
        "TBR=reviewer@chromium.org\""),
       ""],
      ["cl upload --send-mail%s" % force_flag, ""],
      ["checkout -f some_branch", ""],
      ["branch -D %s" % TEST_CONFIG[TEMP_BRANCH], ""],
      ["branch -D %s" % TEST_CONFIG[BRANCHNAME], ""],
      ["branch -D %s" % TEST_CONFIG[TRUNKBRANCH], ""],
    ]
    self._rl_recipe = [
      "Y",  # Confirm last push.
      "",  # Open editor.
      "Y",  # Increment build number.
      "reviewer@chromium.org",  # V8 reviewer.
      "LGTX",  # Enter LGTM for V8 CL (wrong).
      "LGTM",  # Enter LGTM for V8 CL.
      "Y",  # Sanity check.
      "reviewer@chromium.org",  # Chromium reviewer.
    ]
    if force:
      # TODO(machenbach): The lgtm for the prepare push is just temporary.
      # There should be no user input in "force" mode.
      self._rl_recipe = [
        "LGTM",  # Enter LGTM for V8 CL.
      ]

    class Options( object ):
      pass

    options = Options()
    options.s = 0
    options.l = None
    options.f = force
    options.r = "reviewer@chromium.org" if force else None
    options.c = TEST_CONFIG[CHROMIUM]
    RunPushToTrunk(TEST_CONFIG, options, self)

    deps = FileToText(TEST_CONFIG[DEPS_FILE])
    self.assertTrue(re.search("\"v8_revision\": \"123456\"", deps))

    cl = FileToText(TEST_CONFIG[CHANGELOG_FILE])
    self.assertTrue(re.search(r"\d\d\d\d\-\d+\-\d+: Version 3\.22\.5", cl))
    self.assertTrue(re.search(r"        Log text 1", cl))
    self.assertTrue(re.search(r"        \(issue 321\)", cl))
    self.assertTrue(re.search(r"1999\-04\-05: Version 3\.22\.4", cl))

    # Note: The version file is on build number 5 again in the end of this test
    # since the git command that merges to the bleeding edge branch is mocked
    # out.

  def testPushToTrunk(self):
    self._PushToTrunk()

  def testPushToTrunkForced(self):
    self._PushToTrunk(force=True)
