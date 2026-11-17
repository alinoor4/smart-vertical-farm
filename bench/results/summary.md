# Benchmark summary

Written by `farm_cli bench` from the CSV files beside it. Do not edit by hand.

- Machine: AMD Ryzen 5 7430U with Radeon Graphics, 6 cores / 12 threads, 15.4 GB RAM, Microsoft Windows 11 Pro Education 10.0.26300, Windows power plan Balanced, on AC power
- Logical processors: 12
- Compiler: GCC 16.2.0
- Build flags: RELEASE: -O3 -DNDEBUG
- Seeds 1 to 5 per configuration (a quick run: the two smallest sizes, seed 1). Each run is timed three times and the fastest kept; a run over a second is timed once.
- Branch and bound stops after 2 seconds per instance and may report that it did not prove its answer. That is a result, not a failure.
- Tables show medians over the seeds. The measured exponent of a series is the least-squares slope of log(time) against log(n) over the points whose median time is at least 0.05 ms.

## knap_1d

One resource, integer weights 1 to 30, capacity half the total weight; groups of 10 copies whose values fall 5% per copy.

n counts copies. quality is value as % of the DP optimum.

| algorithm | n | median time (ms) | median quality |
|---|---|---|---|
| greedy | 50 | 0.0017 | 99.63 |
| greedy | 100 | 0.0030 | 100.00 |
| greedy | 200 | 0.0058 | 100.00 |
| greedy | 400 | 0.0150 | 100.00 |
| greedy | 800 | 0.0428 | 100.00 |
| greedy | 1600 | 0.0905 | 100.00 |
| dp | 50 | 0.0340 | 100.00 |
| dp | 100 | 0.1631 | 100.00 |
| dp | 200 | 0.8496 | 100.00 |
| dp | 400 | 3.5157 | 100.00 |
| dp | 800 | 14.5852 | 100.00 |
| dp | 1600 | 57.6425 | 100.00 |
| bnb | 50 | 0.0184 | 100.00 |
| bnb | 100 | 0.0803 | 100.00 |
| bnb | 200 | 0.2272 | 100.00 |
| bnb | 400 | 2.4970 | 100.00 |
| bnb | 800 | 5.0019 | 100.00 |
| bnb | 1600 | 12.8648 | 100.00 |

Measured exponents:

- greedy: none (fewer than two points with a time of at least 0.05 ms)
- dp: 2.10 (from 5 points)
- bnb: 1.91 (from 5 points)

## knap_dims

8 groups of 5 copies, integer weights 0 to 6, every capacity 24; the number of resources grows.

n counts copies. param is resources. quality is value as % of the optimum.

| algorithm | param | n | median time (ms) | median quality |
|---|---|---|---|---|
| greedy | 1 | 40 | 0.0020 | 97.29 |
| greedy | 2 | 40 | 0.0018 | 95.68 |
| greedy | 3 | 40 | 0.0020 | 90.70 |
| greedy | 4 | 40 | 0.0021 | 90.03 |
| dp | 1 | 40 | 0.0046 | 100.00 |
| dp | 2 | 40 | 0.0870 | 100.00 |
| dp | 3 | 40 | 2.1712 | 100.00 |
| dp | 4 | 40 | 58.2359 | 100.00 |
| bnb | 1 | 40 | 0.0087 | 100.00 |
| bnb | 2 | 40 | 0.0195 | 100.00 |
| bnb | 3 | 40 | 0.0712 | 100.00 |
| bnb | 4 | 40 | 0.1326 | 100.00 |

Every row has the same n, so there is no exponent against n for this suite.

## knap_farm

The knapsack problem of a generated farm with racks = crops / 3.

n counts crops. quality is value as % of the root bound.

| algorithm | n | median time (ms) | median quality |
|---|---|---|---|
| greedy | 6 | 0.0045 | 99.72 |
| greedy | 12 | 0.0064 | 98.83 |
| greedy | 18 | 0.0110 | 99.03 |
| greedy | 24 | 0.0133 | 99.55 |
| bnb | 6 | 0.0557 | 99.77 |
| bnb | 12 | 1.9576 | 99.93 |
| bnb | 18 | 9.6310 | 99.96 |
| bnb | 24 | 19.2967 | 99.96 |

Measured exponents:

- greedy: none (fewer than two points with a time of at least 0.05 ms)
- bnb: 4.30 (from 4 points)

## pack_tight

10 tray types, shelves of five sizes, conflict probability 0.15, compatibility probability 0.8; the trays fill 90% of the shelf area.

n counts trays. param is tray area as % of shelf area. quality is placed area as % of tray area.

| algorithm | param | n | median time (ms) | median quality |
|---|---|---|---|---|
| ffd | 90 | 50 | 0.0063 | 90.03 |
| ffd | 90 | 100 | 0.0169 | 94.10 |
| ffd | 90 | 200 | 0.0507 | 94.43 |
| ffd | 90 | 400 | 0.1648 | 93.25 |
| ffd | 90 | 800 | 0.6007 | 96.68 |
| bfd | 90 | 50 | 0.0078 | 88.90 |
| bfd | 90 | 100 | 0.0253 | 94.91 |
| bfd | 90 | 200 | 0.0868 | 93.74 |
| bfd | 90 | 400 | 0.2975 | 95.88 |
| bfd | 90 | 800 | 1.1375 | 94.31 |
| ga | 90 | 50 | 36.9935 | 100.00 |
| ga | 90 | 100 | 131.7896 | 100.00 |
| ga | 90 | 200 | 456.9992 | 100.00 |
| ga | 90 | 400 | 1783.1137 | 100.00 |
| ga | 90 | 800 | 6537.8554 | 100.00 |

Measured exponents:

- ffd: 1.78 (from 3 points)
- bfd: 1.86 (from 3 points)
- ga: 1.87 (from 5 points)

## pack_loose

The same instances with the trays filling 60% of the shelf area.

n counts trays. param is tray area as % of shelf area. quality is placed area as % of tray area.

| algorithm | param | n | median time (ms) | median quality |
|---|---|---|---|---|
| ffd | 60 | 50 | 0.0073 | 100.00 |
| ffd | 60 | 100 | 0.0172 | 100.00 |
| ffd | 60 | 200 | 0.0504 | 100.00 |
| ffd | 60 | 400 | 0.1667 | 100.00 |
| ffd | 60 | 800 | 0.6017 | 100.00 |
| bfd | 60 | 50 | 0.0106 | 100.00 |
| bfd | 60 | 100 | 0.0314 | 100.00 |
| bfd | 60 | 200 | 0.1219 | 100.00 |
| bfd | 60 | 400 | 0.4203 | 100.00 |
| bfd | 60 | 800 | 1.5245 | 100.00 |
| ga | 60 | 50 | 19.6288 | 100.00 |
| ga | 60 | 100 | 86.2109 | 100.00 |
| ga | 60 | 200 | 289.4140 | 100.00 |
| ga | 60 | 400 | 985.6962 | 100.00 |
| ga | 60 | 800 | 3797.0764 | 100.00 |

Measured exponents:

- ffd: 1.79 (from 3 points)
- bfd: 1.82 (from 3 points)
- ga: 1.87 (from 5 points)

## sched

One crop, cycle 5 weeks, no early harvest, 26 weeks; every week the demand is 1.3 times what the slots can deliver, 40% of it under contract. Nothing in it is random, so the five seeds repeat one instance and only the timings differ.

n counts slots. quality is hard lots on time, %.

| algorithm | n | median time (ms) | median quality |
|---|---|---|---|
| edf | 5 | 0.0022 | 92.31 |
| edf | 10 | 0.0042 | 80.77 |
| edf | 20 | 0.0121 | 80.77 |
| edf | 40 | 0.0406 | 80.77 |
| edf | 80 | 0.1552 | 80.77 |
| lookahead | 5 | 0.0039 | 100.00 |
| lookahead | 10 | 0.0127 | 100.00 |
| lookahead | 20 | 0.0459 | 100.00 |
| lookahead | 40 | 0.2026 | 100.00 |
| lookahead | 80 | 0.8004 | 100.00 |

Measured exponents:

- edf: none (fewer than two points with a time of at least 0.05 ms)
- lookahead: 1.98 (from 2 points)

## sched_window

The same with 20 slots, for lookahead windows of different lengths (and no randomness: the seeds repeat one instance).

n counts slots. param is lookahead weeks. quality is hard lots on time, %.

| algorithm | param | n | median time (ms) | median quality |
|---|---|---|---|---|
| lookahead | 0 | 20 | 0.0316 | 80.77 |
| lookahead | 1 | 20 | 0.0375 | 87.18 |
| lookahead | 2 | 20 | 0.0398 | 100.00 |
| lookahead | 3 | 20 | 0.0764 | 100.00 |
| lookahead | 5 | 20 | 0.0806 | 100.00 |
| lookahead | 10 | 20 | 0.1024 | 100.00 |
| lookahead | 26 | 20 | 0.1442 | 100.00 |

Every row has the same n, so there is no exponent against n for this suite.

## sched_exact

Cycle 2 weeks, 2 slots, 8 weeks, a given number of lots, 40% of the weekly demand under contract (and no randomness: the seeds repeat one instance).

n counts lots. quality is value as % of the exact optimum (-1: the exact search stopped early).

| algorithm | n | median time (ms) | median quality |
|---|---|---|---|
| edf | 8 | 0.0006 | 100.00 |
| edf | 12 | 0.0006 | -16.67 |
| edf | 16 | 0.0006 | -37.50 |
| edf | 20 | 0.0009 | -50.00 |
| lookahead | 8 | 0.0006 | 100.00 |
| lookahead | 12 | 0.0008 | 100.00 |
| lookahead | 16 | 0.0008 | 100.00 |
| lookahead | 20 | 0.0011 | 100.00 |
| exact | 8 | 0.0016 | 100.00 |
| exact | 12 | 0.0058 | 100.00 |
| exact | 16 | 0.0153 | 100.00 |
| exact | 20 | 0.0379 | 100.00 |

Measured exponents:

- edf: none (fewer than two points with a time of at least 0.05 ms)
- lookahead: none (fewer than two points with a time of at least 0.05 ms)
- exact: none (fewer than two points with a time of at least 0.05 ms)

## forecast

24 generated crops with 156 weeks of history, the last 12 weeks held out; the relative noise of the history varies.

n counts crops. param is noise, %. quality is MAE as % of the mean demand.

| algorithm | param | n | median time (ms) | median quality |
|---|---|---|---|---|
| naive | 5 | 24 | 0.0055 | 8.97 |
| naive | 15 | 24 | 0.0054 | 19.10 |
| naive | 30 | 24 | 0.0052 | 36.74 |
| ma | 5 | 24 | 0.0143 | 14.00 |
| ma | 15 | 24 | 0.0143 | 20.20 |
| ma | 30 | 24 | 0.0136 | 31.37 |
| ses | 5 | 24 | 0.0687 | 13.24 |
| ses | 15 | 24 | 0.0661 | 19.97 |
| ses | 30 | 24 | 0.0616 | 31.22 |
| hw | 5 | 24 | 1.7000 | 6.01 |
| hw | 15 | 24 | 1.6494 | 16.57 |
| hw | 30 | 24 | 1.5954 | 33.03 |

Every row has the same n, so there is no exponent against n for this suite.

## colouring

Random graphs: every pair of vertices is joined with probability param / 100.

n counts vertices. param is edge probability, %. quality is colours used.

| algorithm | param | n | median time (ms) | median quality |
|---|---|---|---|---|
| wp | 10 | 10 | 0.0010 | 2.00 |
| wp | 10 | 15 | 0.0008 | 2.00 |
| wp | 10 | 20 | 0.0011 | 3.00 |
| wp | 10 | 50 | 0.0028 | 4.00 |
| wp | 10 | 100 | 0.0112 | 7.00 |
| wp | 10 | 200 | 0.0164 | 10.00 |
| wp | 10 | 400 | 0.0658 | 15.00 |
| dsatur | 10 | 10 | 0.0011 | 2.00 |
| dsatur | 10 | 15 | 0.0010 | 2.00 |
| dsatur | 10 | 20 | 0.0017 | 3.00 |
| dsatur | 10 | 50 | 0.0100 | 4.00 |
| dsatur | 10 | 100 | 0.1107 | 6.00 |
| dsatur | 10 | 200 | 0.5366 | 9.00 |
| dsatur | 10 | 400 | 3.8898 | 13.00 |
| exact | 10 | 10 | 0.0014 | 2.00 |
| exact | 10 | 15 | 0.0013 | 2.00 |
| exact | 10 | 20 | 0.0019 | 3.00 |
| wp | 30 | 10 | 0.0006 | 3.00 |
| wp | 30 | 15 | 0.0008 | 4.00 |
| wp | 30 | 20 | 0.0011 | 4.00 |
| wp | 30 | 50 | 0.0052 | 8.00 |
| wp | 30 | 100 | 0.0091 | 13.00 |
| wp | 30 | 200 | 0.0370 | 22.00 |
| wp | 30 | 400 | 0.1478 | 36.00 |
| dsatur | 30 | 10 | 0.0008 | 3.00 |
| dsatur | 30 | 15 | 0.0013 | 3.00 |
| dsatur | 30 | 20 | 0.0023 | 4.00 |
| dsatur | 30 | 50 | 0.0368 | 7.00 |
| dsatur | 30 | 100 | 0.2402 | 12.00 |
| dsatur | 30 | 200 | 1.7830 | 19.00 |
| dsatur | 30 | 400 | 21.9342 | 33.00 |
| exact | 30 | 10 | 0.0010 | 3.00 |
| exact | 30 | 15 | 0.0019 | 3.00 |
| exact | 30 | 20 | 0.0052 | 4.00 |
| wp | 50 | 10 | 0.0006 | 4.00 |
| wp | 50 | 15 | 0.0009 | 5.00 |
| wp | 50 | 20 | 0.0012 | 6.00 |
| wp | 50 | 50 | 0.0065 | 12.00 |
| wp | 50 | 100 | 0.0128 | 20.00 |
| wp | 50 | 200 | 0.0605 | 34.00 |
| wp | 50 | 400 | 0.2328 | 58.00 |
| dsatur | 50 | 10 | 0.0007 | 4.00 |
| dsatur | 50 | 15 | 0.0017 | 5.00 |
| dsatur | 50 | 20 | 0.0032 | 6.00 |
| dsatur | 50 | 50 | 0.0730 | 11.00 |
| dsatur | 50 | 100 | 0.4377 | 19.00 |
| dsatur | 50 | 200 | 3.4651 | 31.00 |
| dsatur | 50 | 400 | 43.5720 | 54.00 |
| exact | 50 | 10 | 0.0014 | 4.00 |
| exact | 50 | 15 | 0.0031 | 5.00 |
| exact | 50 | 20 | 0.0096 | 6.00 |

Measured exponents:

- wp 10: none (fewer than two points with a time of at least 0.05 ms)
- dsatur 10: 2.57 (from 3 points)
- exact 10: none (fewer than two points with a time of at least 0.05 ms)
- wp 30: none (fewer than two points with a time of at least 0.05 ms)
- dsatur 30: 3.26 (from 3 points)
- exact 30: none (fewer than two points with a time of at least 0.05 ms)
- wp 50: 1.94 (from 2 points)
- dsatur 50: 3.06 (from 4 points)
- exact 50: none (fewer than two points with a time of at least 0.05 ms)
