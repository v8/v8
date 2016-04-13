#! /usr/bin/python
#
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

import argparse
import heapq
import json
from matplotlib import colors
from matplotlib import pyplot
import numpy
import struct


__DESCRIPTION = """
Process v8.ignition_dispatches_counters.json and list top counters,
or plot a dispatch heatmap.
"""


__HELP_EPILOGUE = """
examples:
  # Print the top 10 counters, reading from default filename
  # v8.ignition_dispatches_counters.json (default mode)
  $ tools/ignition/bytecode_dispatches_report.py

  # Print the top 15 counters reading from data.json
  $ tools/ignition/bytecode_dispatches_report.py -t 15 data.json

  # Save heatmap to default filename v8.ignition_dispatches_counters.svg
  $ tools/ignition/bytecode_dispatches_report.py -p

  # Save heatmap to filename data.svg
  $ tools/ignition/bytecode_dispatches_report.py -p -o data.svg

  # Open the heatmap in an interactive viewer
  $ tools/ignition/bytecode_dispatches_report.py -p -i
"""

__COUNTER_BITS = struct.calcsize("P") * 8  # Size in bits of a pointer
__COUNTER_MAX = 2**__COUNTER_BITS - 1


def warn_if_counter_may_have_saturated(dispatches_table):
  for source, counters_from_source in dispatches_table.items():
    for destination, counter in counters_from_source.items():
      if counter == __COUNTER_MAX:
        print "WARNING: {} -> {} may have saturated.".format(source,
                                                             destination)


def find_top_counters(dispatches_table, top_count):
  def flattened_counters_generator():
    for source, counters_from_source in dispatches_table.items():
      for destination, counter in counters_from_source.items():
        yield source, destination, counter

  return heapq.nlargest(top_count, flattened_counters_generator(),
                        key=lambda x: x[2])


def print_top_counters(dispatches_table, top_count):
  top_counters = find_top_counters(dispatches_table, top_count)
  print "Top {} dispatch counters:".format(top_count)
  for source, destination, counter in top_counters:
    print "{:>12d}\t{} -> {}".format(counter, source, destination)


def build_counters_matrix(dispatches_table):
  labels = sorted(dispatches_table.keys())

  counters_matrix = numpy.empty([len(labels), len(labels)], dtype=int)
  for from_index, from_name in enumerate(labels):
    current_row = dispatches_table[from_name];
    for to_index, to_name in enumerate(labels):
      counters_matrix[from_index, to_index] = current_row.get(to_name, 0)

  # Reverse y axis for a nicer appearance
  xlabels = labels
  ylabels = list(reversed(xlabels))
  counters_matrix = numpy.flipud(counters_matrix)

  return counters_matrix, xlabels, ylabels


def plot_dispatches_table(dispatches_table, figure, axis):
  counters_matrix, xlabels, ylabels = build_counters_matrix(dispatches_table)

  image = axis.pcolor(
    counters_matrix,
    cmap='jet',
    norm=colors.LogNorm(),
    edgecolor='grey',
    linestyle='dotted',
    linewidth=0.5
  )

  axis.xaxis.set(
    ticks=numpy.arange(0.5, len(xlabels)),
    label="From bytecode handler"
  )
  axis.xaxis.tick_top()
  axis.set_xlim(0, len(xlabels))
  axis.set_xticklabels(xlabels, rotation='vertical')

  axis.yaxis.set(
    ticks=numpy.arange(0.5, len(ylabels)),
    label="To bytecode handler",
    ticklabels=ylabels
  )
  axis.set_ylim(0, len(ylabels))

  figure.colorbar(
    image,
    ax=axis,
    fraction=0.01,
    pad=0.01
  )


def parse_command_line():
  command_line_parser = argparse.ArgumentParser(
    formatter_class=argparse.RawDescriptionHelpFormatter,
    description=__DESCRIPTION,
    epilog=__HELP_EPILOGUE
  )
  command_line_parser.add_argument(
    "--plot_size", "-s",
    metavar="N",
    default=30,
    help="shorter side, in inches, of the output plot (default 30)"
  )
  command_line_parser.add_argument(
    "--plot", "-p",
    action="store_true",
    help="plot dispatches table heatmap"
  )
  command_line_parser.add_argument(
    "--interactive", "-i",
    action="store_true",
    help="open an interactive viewer, rather than writing to file"
  )
  command_line_parser.add_argument(
    "--top_count", "-t",
    metavar="N",
    type=int,
    default=10,
    help="print the top N counters (default 10)"
  )
  command_line_parser.add_argument(
    "--output_filename", "-o",
    metavar="<output filename>",
    default="v8.ignition_dispatches_table.svg",
    help=("file to save the plot file to. File type is deduced from the "
          "extension. PDF, SVG, PNG supported")
  )
  command_line_parser.add_argument(
    "input_filename",
    metavar="<input filename>",
    default="v8.ignition_dispatches_table.json",
    nargs='?',
    help="Ignition counters JSON file"
  )

  return command_line_parser.parse_args()


def main():
  program_options = parse_command_line()

  with open(program_options.input_filename) as stream:
    dispatches_table = json.load(stream)

  warn_if_counter_may_have_saturated(dispatches_table)

  if program_options.plot:
    figure, axis = pyplot.subplots()
    plot_dispatches_table(dispatches_table, figure, axis)

    if program_options.interactive:
      pyplot.show()
    else:
      figure.set_size_inches(program_options.plot_size,
                             program_options.plot_size)
      pyplot.savefig(program_options.output_filename)
  else:
    print_top_counters(dispatches_table, program_options.top_count)


if __name__ == "__main__":
  main()
