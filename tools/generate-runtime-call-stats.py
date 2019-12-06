#!/usr/bin/python3
# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs chromium/src/run_benchmark for a given story and extracts the generated
# runtime call stats.

import argparse
import csv
import json
import os
import pathlib
import re
import tabulate
import shutil
import statistics
import subprocess
import sys
import tempfile
import gzip

def parse_args():
  parser = argparse.ArgumentParser(
      description="Run story and collect runtime call stats.")
  parser.add_argument("story", metavar="story", nargs=1, help="story to run")
  parser.add_argument(
      "-r",
      "--repeats",
      dest="repeats",
      metavar="N",
      action="store",
      type=int,
      default=1,
      help="number of times to run the story")
  parser.add_argument(
      "-v",
      "--verbose",
      dest="verbose",
      action="store_true",
      help="output benchmark runs to stdout")
  parser.add_argument(
      "--device",
      dest="device",
      action="store",
      help="device to run the test on. Passed directly to run_benchmark")
  parser.add_argument(
      "-d",
      "--dir",
      dest="dir",
      action="store",
      help=("directory to look for already generated output in. This must "
            "already exists and it won't re-run the benchmark"))
  parser.add_argument(
      "-f",
      "--format",
      dest="format",
      action="store",
      choices=["csv", "table"],
      help=("output as CSV"))
  parser.add_argument(
      "-o",
      "--output",
      metavar="FILE",
      dest="out_file",
      action="store",
      help=("write table to FILE rather stdout"))
  parser.add_argument(
      "-e",
      "--executable",
      dest="executable",
      metavar="EXECUTABLE",
      action="store",
      help=("path to executable to run. If not given it will pass '--browser "
            "release' to run_benchmark"))
  parser.add_argument(
      "--chromium-dir",
      dest="chromium_dir",
      metavar="DIR",
      action="store",
      default=".",
      help=("path to chromium directory. If not given, the script must be run "
            "inside the chromium/src directory"))
  parser.add_argument(
      "--js-flags",
      dest="js_flags",
      action="store",
      help="flags to pass to v8")
  parser.add_argument(
      "--extra-browser-args",
      dest="browser_args",
      action="store",
      help="flags to pass to chrome")
  parser.add_argument(
      "--benchmark",
      dest="benchmark",
      action="store",
      default="v8.browsing_desktop",
      help="benchmark to run")
  parser.add_argument(
      "--stdev",
      dest="stdev",
      action="store_true",
      help="adds columns for the standard deviation")

  return parser.parse_args()


def process_trace(trace_file):
  text_string = pathlib.Path(trace_file).read_text()
  result = json.loads(text_string)

  output = {}
  result = result["traceEvents"]
  for o in result:
    o = o["args"]
    if "runtime-call-stats" in o:
      r = o["runtime-call-stats"]
      for name in r:
        count = r[name][0]
        duration = r[name][1]
        if name in output:
          output[name]["count"] += count
          output[name]["duration"] += duration
        else:
          output[name] = {"count": count, "duration": duration}

  return output


def run_benchmark(story, repeats=1, output_dir=".", verbose=False, js_flags=None,
                  browser_args=None, chromium_dir=".", executable=None,
                  benchmark="v8.browsing_desktop", device=None):

  orig_chromium_dir = chromium_dir
  xvfb = os.path.join(chromium_dir, "testing", "xvfb.py")
  if not os.path.isfile(xvfb):
    chromium_dir = os.path(chromium_dir, "src")
    xvfb = os.path.join(chromium_dir, "testing", "xvfb.py")
    if not os.path.isfile(xvfb):
      print(("chromium_dir does not point to a valid chromium checkout: " +
             orig_chromium_dir))
      sys.exit(1)

  command = [
      xvfb,
      os.path.join(chromium_dir, "tools", "perf", "run_benchmark"),
      "run",
      "--story", story,
      "--pageset-repeat", str(repeats),
      "--intermediate-dir", output_dir,
      benchmark,
  ]

  if executable:
    command += ["--browser-executable", executable]
  else:
    command += ["--browser", "release"]

  if device:
    command += ["--device", device]
  if browser_args:
    command += ["--extra-browser-args", browser_args]
  if js_flags:
    command += ["--js-flags", js_flags]

  if not benchmark.startswith("v8."):
    # Most benchmarks by default don't collect runtime call stats so enable them
    # manually.
    categories = [
        "v8",
        "disabled-by-default-v8.runtime_stats",
    ]

    command += ["--extra-chrome-categories", ",".join(categories)]

  print("Output directory: %s" % output_dir)
  stdout = ""
  print(f"Running: {' '.join(command)}\n")
  proc = subprocess.Popen(
      command,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      universal_newlines=True)
  proc.stderr.close()
  status_matcher = re.compile("\[ +(\w+) +\]")
  for line in iter(proc.stdout.readline, ""):
    stdout += line
    match = status_matcher.match(line)
    if verbose or match:
      print(line, end="")

  proc.stdout.close()

  if proc.wait() != 0:
    print("\nrun_benchmark failed:")
    # If verbose then everything has already been printed.
    if not verbose:
      print(stdout)
    sys.exit(1)

  print("\nrun_benchmark completed")


def write_output(f, table, headers, format="table"):
  if format == "csv":
    writer = csv.writer(f)
    writer.writerow(headers)
    writer.writerows(table)
  else:
    f.write(tabulate.tabulate(table, headers=headers, floatfmt=".2f"))
    f.write("\n")


def main():
  args = parse_args()
  story = args.story[0]

  if args.dir is not None:
    output_dir = args.dir
    if not os.path.isdir(output_dir):
      print("Specified output directory does not exist: " % output_dir)
      sys.exit(1)
  else:
    output_dir = tempfile.mkdtemp(prefix="runtime_call_stats_")
    run_benchmark(story,
                  repeats=args.repeats,
                  output_dir=output_dir,
                  verbose=args.verbose,
                  js_flags=args.js_flags,
                  browser_args=args.browser_args,
                  chromium_dir=args.chromium_dir,
                  benchmark=args.benchmark,
                  executable=args.executable,
                  device=args.device)

  outputs = {}
  combined_output = {}
  for i in range(0, args.repeats):
    story_dir = f"{story.replace(':', '_')}_{i + 1}"
    trace_dir = os.path.join(output_dir, story_dir, "trace", "traceEvents")
    trace_file = os.path.join(trace_dir, "results.json")

    # this script always unzips the json file and stores the output in
    # results.json so just re-use that if it already exists, otherwise unzip the
    # one file found in the traceEvents directory.
    if not os.path.isfile(trace_file):
      trace_files = os.listdir(trace_dir)
      if len(trace_files) != 1:
        print("Expecting just one file but got: %s" % trace_files)
        sys.exit(1)

      gz_trace_file = os.path.join(trace_dir, trace_files[0])
      trace_file = os.path.join(trace_dir, "results.json")

      with gzip.open(gz_trace_file, "rb") as f_in:
        with open(trace_file, "wb") as f_out:
          shutil.copyfileobj(f_in, f_out)

    output = process_trace(trace_file)
    outputs[i] = output

    for name in output:
      value = output[name]
      if name not in combined_output:
        combined_output[name] = {
            "duration": [0.0] * args.repeats,
            "count": [0] * args.repeats
        }

      combined_output[name]["count"][i] = value["count"]
      combined_output[name]["duration"][i] = value["duration"] / 1000.0

  table = []
  for name in combined_output:
    value = combined_output[name]
    row = [name]
    total_count = 0
    total_duration = 0
    for i in range(0, args.repeats):

      count = value["count"][i]
      duration = value["duration"][i]
      total_count += count
      total_duration += duration
      row += [count, duration]

    if args.repeats > 1:
      totals = [total_count / args.repeats]
      if args.stdev:
        totals += [statistics.stdev(row[1:-1:2])]
      totals += [total_duration / args.repeats]
      if args.stdev:
        totals += [statistics.stdev(row[2:-1:2])]
      row += totals

    table += [row]

  def sort_duration(value):
    return value[-1]

  table.sort(key=sort_duration)
  headers = [""] + ["Count", "Duration (ms)"] * args.repeats
  if args.repeats > 1:
    if args.stdev:
      headers += ["Count Mean", "Count Stdev",
                  "Duration Mean (ms)", "Duration Stdev"]
    else:
      headers += ["Count Mean", "Duration Mean (ms)"]

  if args.out_file:
    with open(args.out_file, "w", newline="") as f:
      write_output(f, table, headers, args.format)
  else:
    write_output(sys.stdout, table, headers, args.format)

if __name__ == '__main__':
  sys.exit(main())
