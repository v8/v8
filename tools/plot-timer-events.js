// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

var kExecutionName = 'V8.Execute';

var TimerEvents = {
  'V8.Execute':
       { ranges: [], color: "#444444", pause: false, index:  1 },
  'V8.CompileFullCode':
       { ranges: [], color: "#CC0000", pause:  true, index:  2 },
  'V8.RecompileSynchronous':
       { ranges: [], color: "#CC0044", pause:  true, index:  3 },
  'V8.RecompileParallel':
       { ranges: [], color: "#CC4499", pause: false, index:  4 },
  'V8.CompileEval':
       { ranges: [], color: "#CC4400", pause:  true, index:  5 },
  'V8.Parse':
       { ranges: [], color: "#00CC00", pause:  true, index:  6 },
  'V8.PreParse':
       { ranges: [], color: "#44CC00", pause:  true, index:  7 },
  'V8.ParseLazy':
       { ranges: [], color: "#00CC44", pause:  true, index:  8 },
  'V8.GCScavenger':
       { ranges: [], color: "#0044CC", pause:  true, index:  9 },
  'V8.GCCompactor':
       { ranges: [], color: "#4444CC", pause:  true, index: 10 },
  'V8.GCContext':
       { ranges: [], color: "#4400CC", pause:  true, index: 11 },
}

var kNumRows = 11;
var kBarWidth = 0.33;
var kPauseTolerance = 0.05;  // Milliseconds.
var kY1Offset = 3;
var kY2Factor = 5;
var kResX = 1600;
var kResY = 400;
var kLabelPadding = 5;
var kNumPauseLabels = 7;

var kOverrideRangeStart = undefined;
var kOverrideRangeEnd = undefined;

var xrange_start = Infinity;
var xrange_end = 0;
var obj_index = 0;
var execution_pauses = [];


function Range(start, end) {
  // Everthing from here are in milliseconds.
  this.start = start;
  this.end = end;
}


Range.prototype.duration = function() { return this.end - this.start; }


function ProcessTimerEvent(name, start, length) {
  var event = TimerEvents[name];
  if (event === undefined) return;
  start /= 1000;  // Convert to milliseconds.
  length /= 1000;
  var end = start + length;
  event.ranges.push(new Range(start, end));
  if (name == kExecutionName) {
    if (start < xrange_start) xrange_start = start;
    if (end > xrange_end) xrange_end = end;
  }
}


function CollectData() {
  // Collect data from log.
  var logreader = new LogReader(
    { 'timer-event' : { parsers: [null, parseInt, parseInt],
                        processor: ProcessTimerEvent
                      } });

  var line;
  while (line = readline()) {
    logreader.processLogLine(line);
  }

  // Collect execution pauses.
  for (name in TimerEvents) {
    var event = TimerEvents[name];
    if (!event.pause) continue;
    var ranges = event.ranges;
    // Add ranges of this event to the pause list.
    for (var j = 0; j < ranges.length; j++) {
      execution_pauses.push(ranges[j]);
    }
  }
}


function drawBar(row, color, start, end) {
  obj_index++;
  command = "set object " + obj_index + " rect";
  command += " from " + start + ", " + (row - kBarWidth + kY1Offset);
  command += " to " + end + ", " + (row + kBarWidth + kY1Offset);
  command += " fc rgb \"" + color + "\"";
  print(command);
}


function MergeRanges(ranges, merge_tolerance) {
  ranges.sort(function(a, b) { return a.start - b.start; });
  var result = [];
  var j = 0;
  for (var i = 0; i < ranges.length; i = j) {
    var merge_start = ranges[i].start;
    if (merge_start > xrange_end) break;  // Out of plot range.
    var merge_end = ranges[i].end;
    for (j = i + 1; j < ranges.length; j++) {
      var next_range = ranges[j];
      // Don't merge ranges if there is no overlap (including merge tolerance).
      if (next_range.start >= merge_end + kPauseTolerance) break;
      // Merge ranges.
      if (next_range.end > merge_end) {  // Extend range end.
        merge_end = next_range.end;
      }
    }
    if (merge_end < xrange_start) continue;  // Out of plot range.
    result.push(new Range(merge_start, merge_end));
  }
  return result;
}


function ExcludeRanges(include, exclude) {
  // We assume that both input lists are sorted and merged with MergeRanges.
  var result = [];
  var exclude_index = 0;
  var include_index = 0;
  var include_start, include_end, exclude_start, exclude_end;

  function NextInclude() {
    if (include_index >= include.length) return false;
    include_start = include[include_index].start;
    include_end = include[include_index].end;
    include_index++;
    return true;
  }

  function NextExclude() {
    if (exclude_index >= exclude.length) {
      // No more exclude, finish by repeating case (2).
      exclude_start = Infinity;
      exclude_end = Infinity;
      return false;
    }
    exclude_start = exclude[exclude_index].start;
    exclude_end = exclude[exclude_index].end;
    exclude_index++;
    return true;
  }

  if (!NextInclude() || !NextExclude()) return include;

  while (true) {
    if (exclude_end <= include_start) {
      // (1) Exclude and include do not overlap.
      // Include       #####
      // Exclude   ##
      NextExclude();
    } else if (include_end <= exclude_start) {
      // (2) Exclude and include do not overlap.
      // Include   #####
      // Exclude         ###
      result.push(new Range(include_start, include_end));
      if (!NextInclude()) break;
    } else if (exclude_start <= include_start &&
               exclude_end < include_end &&
               include_start < exclude_end) {
      // (3) Exclude overlaps with begin of include.
      // Include    #######
      // Exclude  #####
      // Result        ####
      include_start = exclude_end;
      NextExclude();
    } else if (include_start < exclude_start &&
               include_end <= exclude_end &&
               exclude_start < include_end) {
      // (4) Exclude overlaps with end of include.
      // Include    #######
      // Exclude        #####
      // Result     ####
      result.push(new Range(include_start, exclude_start));
      if (!NextInclude()) break;
    } else if (exclude_start > include_start && exclude_end < include_end) {
      // (5) Exclude splits include into two parts.
      // Include    #######
      // Exclude      ##
      // Result     ##  ###
      result.push(new Range(include_start, exclude_start));
      include_start = exclude_end;
      NextExclude();
    } else if (exclude_start <= include_start && exclude_end >= include_end) {
      // (6) Exclude entirely covers include.
      // Include    ######
      // Exclude   #########
      if (!NextInclude()) break;
    } else {
      throw new Error("this should not happen!");
    }
  }

  return result;
}


function GnuplotOutput() {
  xrange_start = kOverrideRangeStart ? kOverrideRangeStart : xrange_start;
  xrange_end = kOverrideRangeEnd ? kOverrideRangeEnd : xrange_end;

  print("set terminal pngcairo size " + kResX + "," + kResY +
        " enhanced font 'Helvetica,10'");
  print("set yrange [0:" + (kNumRows + kY1Offset + 1) + "]");
  print("set xlabel \"execution time in ms\"");
  print("set xrange [" + xrange_start + ":" + xrange_end + "]");
  print("set style fill pattern 2 bo 1");
  print("set style rect fs solid 1 noborder");
  print("set style line 1 lt 1 lw 1 lc rgb \"#000000\"");
  print("set xtics out nomirror");
  print("unset key");

  // Name Y-axis.
  var ytics = [];
  for (name in TimerEvents) {
    var index = TimerEvents[name].index;
    ytics.push('"' + name + '"' + ' ' + (index + kY1Offset));
  }
  print("set ytics out nomirror (" + ytics.join(', ') + ")");

  // Smallest visible gap given our resolution.
  // We remove superfluous objects to go easy on Gnuplot.
  var tolerance = (xrange_end - xrange_start) / kResX / 2;

  // Sort, merge and remove invisible gaps for each time row.
  for (var name in TimerEvents) {
    var event = TimerEvents[name];
    event.ranges = MergeRanges(event.ranges, tolerance);
  }

  // Knock out execution pauses.
  var execution_event = TimerEvents[kExecutionName];
  var exclude_ranges = MergeRanges(execution_pauses, tolerance);
  execution_event.ranges = ExcludeRanges(execution_event.ranges,
                                         exclude_ranges);
  execution_event.ranges = MergeRanges(execution_event.ranges, tolerance);

  // Plot timeline.
  for (var name in TimerEvents) {
    var event = TimerEvents[name];
    var ranges = event.ranges;
    for (var i = 0; i < ranges.length; i++) {
      drawBar(event.index, event.color, ranges[i].start, ranges[i].end);
    }
  }

  if (execution_pauses.length == 0) {
    // Force plot and return without plotting execution pause impulses.
    print("plot 1/0");
    return;
  }

  // Plot execution pauses as impulses.  This may be better resolved
  // due to possibly smaller merge tolerance.
  if (tolerance > kPauseTolerance) {
    execution_pauses = MergeRanges(execution_pauses, kPauseTolerance);
  } else {
    execution_pauses = exclude_ranges;
  }

  // Label the longest pauses.
  execution_pauses.sort(
      function(a, b) { return b.duration() - a.duration(); });

  var max_pause_time = execution_pauses[0].duration();
  var padding = kLabelPadding * (xrange_end - xrange_start) / kResX;
  var y_scale = kY1Offset / max_pause_time;
  for (var i = 0; i < execution_pauses.length && i < kNumPauseLabels; i++) {
    var pause = execution_pauses[i];
    var label_content = (pause.duration() | 0) + " ms";
    var label_x = pause.end + padding;
    var label_y = Math.max(1, (pause.duration() * y_scale));
    print("set label \"" + label_content + "\" at " +
          label_x + "," + label_y + " font \"Helvetica,7'\"");
  }

  // Scale second Y-axis appropriately.
  print("set y2range [0:" + (max_pause_time * kY2Factor) + "]");

  // Plot graph with impulses as data set.
  print("plot '-' using 1:2 axes x1y2 with impulses ls 1");
  for (var i = 0; i < execution_pauses.length; i++) {
    var pause = execution_pauses[i];
    print(pause.end + " " + pause.duration());
  }
  print("e");
}


CollectData();
GnuplotOutput();
