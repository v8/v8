# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import v8_suppressions

class FuzzerTest(unittest.TestCase):
  def testDiff(self):
    # TODO(machenbach): Mock out suppression configuration.
    suppress = v8_suppressions.get_suppression(
        'x64', 'fullcode', 'x64', 'default')
    one = ''
    two = ''
    diff = None
    self.assertEquals(diff, suppress.diff(one, two))

    one = 'a \n  b\nc();'
    two = 'a \n  b\nc();'
    diff = None
    self.assertEquals(diff, suppress.diff(one, two))

    # Ignore line before caret, caret position, stack trace char numbers
    # error message and validator output.
    one = """
undefined
weird stuff
      ^
Validation of asm.js module failed: foo bar
somefile.js: TypeError: undefined is not a function
stack line :15: foo
  undefined
"""
    two = """
undefined
other weird stuff
            ^
somefile.js: TypeError: baz is not a function
stack line :2: foo
Validation of asm.js module failed: baz
  undefined
"""
    diff = None
    self.assertEquals(diff, suppress.diff(one, two))

    one = """
Still equal
Extra line
"""
    two = """
Still equal
"""
    diff = '- Extra line'
    self.assertEquals(diff, suppress.diff(one, two))

    one = """
Still equal
"""
    two = """
Still equal
Extra line
"""
    diff = '+ Extra line'
    self.assertEquals(diff, suppress.diff(one, two))

    one = """
undefined
somefile.js: TypeError: undefined is not a constructor
"""
    two = """
undefined
otherfile.js: TypeError: undefined is not a constructor
"""
    diff = """- somefile.js: TypeError: undefined is not a constructor
+ otherfile.js: TypeError: undefined is not a constructor"""
    self.assertEquals(diff, suppress.diff(one, two))
