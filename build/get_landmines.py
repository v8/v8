#!/usr/bin/env python
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This file emits the list of reasons why a particular build needs to be clobbered
(or a list of 'landmines').
"""

import sys


def main():
  """
  ALL LANDMINES ARE EMITTED FROM HERE.
  """
  # TODO(machenbach): Uncomment to test if landmines work.
  # print 'Need to clobber after ICU52 roll.'
  return 0


if __name__ == '__main__':
  sys.exit(main())
