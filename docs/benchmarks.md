---
title: 'Running benchmarks locally'
description: 'This document explains how to run classic benchmark suites in d8.'
---
We have a simple workflow for running the “classic” benchmarks of SunSpider, Kraken and Octane. You can run with different binaries and flag combinations, and results are averaged over multiple runs.

## CPU

Build the `d8` shell following the instructions at [Building with GN](/docs/build-gn).

Before you run benchmarks, make sure you set your CPU frequency scaling governor to performance.

```bash
sudo tools/cpu.sh fast
```

The commands `cpu.sh` understands are

- `fast`, performance (alias for `fast`)
- `slow`, powersave (alias for `slow`)
- `default`, ondemand (alias for `default`)
- `dualcore` (disables all but two cores), dual (alias for `dualcore`)
- `allcores` (re-enables all available cores), all (alias for `allcores`).

## CSuite

`CSuite` is our simple benchmark runner:

```bash
test/benchmarks/csuite/csuite.py
    (sunspider | kraken | octane)
    (baseline | compare)
    <path to d8 binary>
    [-x "<optional extra d8 command-line flags>"]
```

First run in `baseline` mode to create the baselines, then in `compare` mode to get results. `CSuite` defaults to doing 10 runs for Octane, 100 for SunSpider, and 80 for Kraken, but you can override these for quicker results with the `-r` option.

`CSuite` creates two subdirectories in the directory where you run from:

1. `./_benchmark_runner_data` — this is cached output from the N runs.
1. `./_results` — it writes the results into file master here. You could save these
  files with different names, and they’ll show up in compare mode.

In compare mode, you’ll naturally use a different binary or at least different flags.

## Example usage

Say you’ve built two versions of `d8`, and want to see what happens to SunSpider. First, create baselines:

```bash
$ test/benchmarks/csuite/csuite.py sunspider baseline out.gn/master/d8
Wrote ./_results/master.
Run sunspider again with compare mode to see results.
```

As suggested, run again but this time in `compare` mode with a different binary:

```
$ test/benchmarks/csuite/csuite.py sunspider compare out.gn/x64.release/d8

                               benchmark:    score |   master |      % |
===================================================+==========+========+
                       3d-cube-sunspider:     13.9 S     13.4 S   -3.6 |
                      3d-morph-sunspider:      8.6 S      8.4 S   -2.3 |
                   3d-raytrace-sunspider:     15.1 S     14.9 S   -1.3 |
           access-binary-trees-sunspider:      3.7 S      3.9 S    5.4 |
               access-fannkuch-sunspider:     11.9 S     11.8 S   -0.8 |
                  access-nbody-sunspider:      4.6 S      4.8 S    4.3 |
                 access-nsieve-sunspider:      8.4 S      8.1 S   -3.6 |
      bitops-3bit-bits-in-byte-sunspider:      2.0 |      2.0 |        |
           bitops-bits-in-byte-sunspider:      3.7 S      3.9 S    5.4 |
            bitops-bitwise-and-sunspider:      2.7 S      2.9 S    7.4 |
            bitops-nsieve-bits-sunspider:      5.3 S      5.6 S    5.7 |
         controlflow-recursive-sunspider:      3.8 S      3.6 S   -5.3 |
                    crypto-aes-sunspider:     10.9 S      9.8 S  -10.1 |
                    crypto-md5-sunspider:      7.0 |      7.4 S    5.7 |
                   crypto-sha1-sunspider:      9.2 S      9.0 S   -2.2 |
             date-format-tofte-sunspider:      9.8 S      9.9 S    1.0 |
             date-format-xparb-sunspider:     10.3 S     10.3 S        |
                   math-cordic-sunspider:      6.1 S      6.2 S    1.6 |
             math-partial-sums-sunspider:     20.2 S     20.1 S   -0.5 |
            math-spectral-norm-sunspider:      3.2 S      3.0 S   -6.2 |
                    regexp-dna-sunspider:      7.6 S      7.8 S    2.6 |
                 string-base64-sunspider:     14.2 S     14.0 |   -1.4 |
                  string-fasta-sunspider:     12.8 S     12.6 S   -1.6 |
               string-tagcloud-sunspider:     18.2 S     18.2 S        |
            string-unpack-code-sunspider:     20.0 |     20.1 S    0.5 |
         string-validate-input-sunspider:      9.4 S      9.4 S        |
                               SunSpider:    242.6 S    241.1 S   -0.6 |
---------------------------------------------------+----------+--------+
```

The output of the previous run is cached in a subdirectory created in the current directory (`_benchmark_runner_data`). The aggregate results are also cached, in directory `_results`. These directories can be deleted after you’ve run the compare step.

Another situation is when you have the same binary, but want to see the results of different flags. Feeling rather droll, you’d like to see how Octane performs without an optimizing compiler. First the baseline:

```bash
$ test/benchmarks/csuite/csuite.py -r 1 octane baseline out.gn/x64.release/d8

Normally, octane requires 10 runs to get stable results.
Wrote /usr/local/google/home/mvstanton/src/v8/_results/master.
Run octane again with compare mode to see results.
```

Note the warning that one run usually isn’t enough to be sure of many performance optimizations, however, our “change” should have a reproducible effect with only one run! Now let’s compare, passing the `--noopt` flag to turn off [TurboFan](/docs/turbofan):

```bash
$ test/benchmarks/csuite/csuite.py -r 1 octane compare out.gn/x64.release/d8 \
  -x "--noopt"

Normally, octane requires 10 runs to get stable results.
                               benchmark:    score |   master |      % |
===================================================+==========+========+
                                Richards:    973.0 |  26770.0 |  -96.4 |
                               DeltaBlue:   1070.0 |  57245.0 |  -98.1 |
                                  Crypto:    923.0 |  32550.0 |  -97.2 |
                                RayTrace:   2896.0 |  75035.0 |  -96.1 |
                             EarleyBoyer:   4363.0 |  42779.0 |  -89.8 |
                                  RegExp:   2881.0 |   6611.0 |  -56.4 |
                                   Splay:   4241.0 |  19489.0 |  -78.2 |
                            SplayLatency:  14094.0 |  57192.0 |  -75.4 |
                            NavierStokes:   1308.0 |  39208.0 |  -96.7 |
                                   PdfJS:   6385.0 |  26645.0 |  -76.0 |
                                Mandreel:    709.0 |  33166.0 |  -97.9 |
                         MandreelLatency:   5407.0 |  97749.0 |  -94.5 |
                                 Gameboy:   5440.0 |  54336.0 |  -90.0 |
                                CodeLoad:  25631.0 |  25282.0 |    1.4 |
                                   Box2D:   3288.0 |  67572.0 |  -95.1 |
                                    zlib:  59154.0 |  58775.0 |    0.6 |
                              Typescript:  12700.0 |  23310.0 |  -45.5 |
                                  Octane:   4070.0 |  37234.0 |  -89.1 |
---------------------------------------------------+----------+--------+
```

Neat to see that `CodeLoad` and `zlib` were relatively unharmed.

## Under the hood

`CSuite` is based on two scripts in the same directory, `benchmark.py` and `compare-baseline.py`. There are more options in those scripts. For example, you can record multiple baselines and do 3-, 4-, or 5-way comparisons. `CSuite` is optimized for quick use, and sacrifices some flexibility.
