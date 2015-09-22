#!/usr/bin/env python
#
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is used to analyze GCTracer's NVP output."""

from argparse import ArgumentParser
from gc_nvp_common import split_nvp
from sys import stdin

class Histogram:
  def __init__(self, values, granularity):
    self.values = list(values)  # copy over values
    self.granularity = granularity

  def __str__(self):
    ret = []
    values = list(self.values)  # copy over values
    min_value = 0
    while len(values) > 0:
      max_value = min_value +  self.granularity
      sub = [x for x in self.values if x >= min_value and x < max_value]
      ret.append("  [{0},{1}[: {2}".format(
        str(min_value), str(max_value),len(sub)))
      min_value += self.granularity
      values = [x for x in values if x not in sub]
    return "\n".join(ret)


class Category:
  def __init__(self, key, histogram, granularity):
    self.key = key
    self.values = []
    self.histogram = histogram
    self.granularity = granularity

  def process_entry(self, entry):
    if self.key in entry:
      self.values.append(float(entry[self.key]))

  def __str__(self):
    ret = [self.key]
    ret.append("  len: {0}".format(len(self.values)))
    if len(self.values) > 0:
      ret.append("  min: {0}".format(min(self.values)))
      ret.append("  max: {0}".format(max(self.values)))
      ret.append("  avg: {0}".format(sum(self.values) / len(self.values)))
      if self.histogram:
        ret.append(str(Histogram(self.values, self.granularity)))
    return "\n".join(ret)


def main():
  parser = ArgumentParser(description="Process GCTracer's NVP output")
  parser.add_argument('keys', metavar='KEY', type=str, nargs='+',
                      help='the keys (names) to process')
  parser.add_argument('--histogram-granularity', metavar='GRANULARITY',
                      type=int, nargs='?', default=5,
                      help='histogram granularity (default: 5)')
  feature_parser = parser.add_mutually_exclusive_group(required=False)
  feature_parser.add_argument('--histogram', dest='histogram',
                              action='store_true',
                              help='print histogram')
  feature_parser.add_argument('--no-histogram', dest='histogram',
                              action='store_false',
                              help='do not print histogram')
  parser.set_defaults(histogram=True)
  args = parser.parse_args()

  categories = [ Category(key, args.histogram, args.histogram_granularity)
                 for key in args.keys ]

  while True:
    line = stdin.readline()
    if not line:
      break
    obj = split_nvp(line)
    for category in categories:
      category.process_entry(obj)

  for category in categories:
    print(category)


if __name__ == '__main__':
  main()
