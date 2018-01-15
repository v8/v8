# Heap Stats

Heap stats is a HTML-based tool for visualizing V8-internal object statistics.
For example, the tool can be used to visualize how much heap memory is used for
maintaining internal state versus actually allocated by the user.

The tool consumes log files produced by d8 (or Chromium) by passing
`--trace-gc-object-stats`.

Hosting requires a web server, e.g.:

    cd tools/heap/stats
    python -m SimpleHTTPServer 8000
