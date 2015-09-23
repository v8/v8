#!/usr/bin/env python
#
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is used to analyze GCTracer's NVP output."""


from argparse import ArgumentParser
from copy import deepcopy
from gc_nvp_common import split_nvp
from sys import stdin


class Histogram:
  def __init__(self, granularity, fill_empty):
    self.granularity = granularity
    self.histogram = {}
    self.fill_empty = fill_empty

  def add(self, key):
    index = int(key / self.granularity)
    if index not in self.histogram:
      self.histogram[index] = 0
    self.histogram[index] += 1

  def __str__(self):
    ret = []
    keys = self.histogram.keys()
    keys.sort()
    last = -self.granularity
    for key in keys:
      min_value = key * self.granularity
      max_value = min_value + self.granularity

      if self.fill_empty:
        while (last + self.granularity) != min_value:
          last += self.granularity
          ret.append("  [{0},{1}[: {2}".format(
            str(last), str(last + self.granularity), 0))

      ret.append("  [{0},{1}[: {2}".format(
        str(min_value), str(max_value), self.histogram[key]))
      last = min_value
    return "\n".join(ret)


class Category:
  def __init__(self, key, histogram):
    self.key = key
    self.values = []
    self.histogram = histogram

  def process_entry(self, entry):
    if self.key in entry:
      self.values.append(float(entry[self.key]))
      if self.histogram:
        self.histogram.add(float(entry[self.key]))

  def __str__(self):
    ret = [self.key]
    ret.append("  len: {0}".format(len(self.values)))
    if len(self.values) > 0:
      ret.append("  min: {0}".format(min(self.values)))
      ret.append("  max: {0}".format(max(self.values)))
      ret.append("  avg: {0}".format(sum(self.values) / len(self.values)))
      if self.histogram:
        ret.append(str(self.histogram))
    return "\n".join(ret)


def main():
  parser = ArgumentParser(description="Process GCTracer's NVP output")
  parser.add_argument('keys', metavar='KEY', type=str, nargs='+',
                      help='the keys (names) to process')
  parser.add_argument('--histogram-granularity', metavar='GRANULARITY',
                      type=int, nargs='?', default=5,
                      help='histogram granularity (default: 5)')
  parser.add_argument('--no-histogram-print-empty', dest='histogram_print_empty',
                      action='store_false',
                      help='print empty histogram buckets')
  feature_parser = parser.add_mutually_exclusive_group(required=False)
  feature_parser.add_argument('--histogram', dest='histogram',
                              action='store_true',
                              help='print histogram')
  feature_parser.add_argument('--no-histogram', dest='histogram',
                              action='store_false',
                              help='do not print histogram')
  parser.set_defaults(histogram=True)
  parser.set_defaults(histogram_print_empty=True)
  args = parser.parse_args()

  histogram = None
  if args.histogram:
    histogram = Histogram(args.histogram_granularity, args.histogram_print_empty)

  categories = [ Category(key, deepcopy(histogram))
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
