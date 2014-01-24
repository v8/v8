// Copyright 2013 the V8 project authors. All rights reserved.
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

var worker_scripts = [
  "../csvparser.js",
  "../splaytree.js",
  "../codemap.js",
  "../profile.js",
  "../profile_view.js",
  "../logreader.js",
  "../tickprocessor.js",
  "composer.js",
  "gnuplot-4.6.3-emscripten.js"
];


function plotWorker() {
  var worker = null;

  var delegateList = {
    "log"     : log,
    "error"   : logError,
    "display" : display,
    "range"   : setRange,
    "script"  : scriptLoaded
  }

  function initialize() {
    ui.freeze();
    worker = new Worker("worker.js");
    running = false;

    worker.postMessage({ "call" : "load scripts",
                         "args" : worker_scripts });

    worker.addEventListener("message", function(event) {
      var call = delegateList[event.data["call"]];
      call(event.data["args"]);
    });
  }

  function scriptLoaded() {
    ui.thaw();
  }

  // Public methods.
  this.run = function(filename,
                      resx, resy,
                      distortion,
                      range_start, range_end) {
    var args = {
      'file'        : filename,
      'resx'        : resx,
      'resy'        : resy,
      'distortion'  : distortion,
      'range_start' : range_start,
      'range_end'   : range_end
    }
    worker.postMessage({ 'call' : 'run', 'args' : args });
  }

  this.reset = function() {
    if (worker) worker.terminate();
    initialize();
  }
}


function UIWrapper() {
  var input_elements = ["range_start",
                        "range_end",
                        "distortion",
                        "start",
                        "file"];

  this.log = document.getElementById("log");
  this.plot = document.getElementById('plot');
  for (var i in input_elements) {
    var id = input_elements[i];
    this[id] = document.getElementById(id);
  }

  this.freeze = function() {
    this.plot.style.webkitFilter = "grayscale(1)";
    for (var i in input_elements) {
      this[input_elements[i]].disabled = true;
    }
  }

  this.thaw = function() {
    this.plot.style.webkitFilter = "";
    for (var i in input_elements) {
      this[input_elements[i]].disabled = false;
    }
  }

  this.reset = function() {
    this.thaw();
    this.log.value = "";
    this.range_start.value = "automatic";
    this.range_end.value = "automatic";
  }
}


function log(text) {
  ui.log.value += text;
  ui.log.scrollTop = ui.log.scrollHeight;
}


function logError(text) {
  if (ui.log.value.length > 0 &&
      ui.log.value[ui.log.value.length-1] != "\n") {
    ui.log.value += "\n";
  }
  ui.log.value += "ERROR: " + text + "\n";
  ui.log.scrollTop = ui.log.scrollHeight;
  error_logged = true;
}


function display(args) {
  if (error_logged) {
    log("Plot failed.\n\n");
  } else {
    log("Displaying plot. Total time: " +
        (Date.now() - timer) / 1000 + "ms.\n\n");
    var blob = new Blob([new Uint8Array(args.contents).buffer],
                        { "type" : "image\/svg+xml" });
    window.URL = window.URL || window.webkitURL;
    ui.plot.src = window.URL.createObjectURL(blob);
  }

  ui.thaw();
}


function start(event) {
  error_logged = false;
  ui.freeze();

  try {
    var file = getSelectedFile();
    var distortion = getDistortion();
    var range = getRange();
  } catch (e) {
    logError(e.message);
    display();
    return;
  }

  timer = Date.now();
  worker.run(file, kResX, kResY, distortion, range[0], range[1]);
}


function getSelectedFile() {
  var file = ui.file.files[0];
  if (!file) throw Error("No valid file selected.");
  if (!file.type.toString().match(/text/)) {
    throw Error("'" + escape(file.name) + "' is not a text file.");
  }
  return file;
}


function getDistortion() {
  var input_distortion =
      parseInt(ui.distortion.value, 10);
  if (isNaN(input_distortion)) {
    input_distortion = ui.distortion.value = 4500;
  }
  return input_distortion / 1000000;
}


function getRange() {
  var input_start =
      parseInt(ui.range_start.value, 10);
  if (isNaN(input_start)) input_start = undefined;
  var input_end =
      parseInt(ui.range_end.value, 10);
  if (isNaN(input_end)) input_end = undefined;
  return [input_start, input_end];
}


function setRange(args) {
  ui.range_start.value = args.start.toFixed(1);
  ui.range_end.value = args.end.toFixed(1);
}


function onload() {
  kResX = 1600;
  kResY = 900;
  error_logged = false;
  ui = new UIWrapper();
  ui.reset();
  worker = new plotWorker();
  worker.reset();
}


var kResX;
var kResY;
var error_logged;
var ui;
var worker;
var timer;
