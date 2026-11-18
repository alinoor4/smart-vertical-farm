# Complexity analysis

The bounds of every algorithm in `src/core`, as built, with the measured growth beside them. Sections 3 to 7 derive each row; section 9 lists where the code differs from the plan's text.

## 1. Summary of the bounds

| Component | Algorithm | Paradigm | Time | Space | Optimal? |
|---|---|---|---|---|---|
| Constraints | Compatibility and conflict graph | Graph construction | `O(C²·S + K)` | `O(C·S + C²)` | n/a |
| Constraints | Welsh-Powell | Greedy | `O(C log C + E)` | `O(C + E)` | No; at most Δ+1 colours |
| Constraints | DSATUR | Greedy | `O(C·(C + E))` | `O(C + E)` | No; exact on bipartite graphs |
| Constraints | Exact colouring | Backtracking | at most `Σ B_i` nodes (Bell numbers), `O(C)` each | `O(C + E + C·k)` | Yes when it finishes |
| Forecast | Seasonal naive | Recurrence | `O(H)` | `O(H)` | n/a |
| Forecast | Moving average (k = 4, 8, 12) | Recurrence, small search | `O(Y + H)`, 3 passes | `O(H)` | n/a; best k on the grid |
| Forecast | Simple exponential smoothing | Recurrence, small search | `O(Y + H)`, 9 passes | `O(H)` | n/a; best α on the grid |
| Forecast | Holt-Winters with grid search | Recurrence, exhaustive search | `O(80·(Y + H))` | `O(L + H)` | Least one-step SSE on the grid |
| Selection | Greedy | Greedy | `O(N log N + N·m)` | `O(N)` | No |
| Selection | Dynamic programming | DP | `O(N·states·m)`, `states = Π (W_r + 1)` | `O(states)` doubles + `N·states` bits | Yes on the integer grid; not once the grid is coarsened |
| Selection | Branch and bound | Branch and bound | exponential worst case; `O(m·N)` per node, `O(m·N log N)` setup | `O(m·N)` | Yes when it finishes |
| Packing | First-Fit and Best-Fit Decreasing | Greedy | `O(n log n + n·S·(C + R))` | `O(n + S·C)` | No |
| Packing | Genetic algorithm | Metaheuristic | `O(G·P·n·S·(C + R))` | `O(P·n + S·C)` | No |
| Scheduling | Interval depth | Sweep line | `O(J log J)` | `O(J)` | Yes |
| Scheduling | EDF | Greedy | `O(J log J + T·k·J)` | `O(J + k)` | No |
| Scheduling | EDF with lookahead | Greedy with a feasibility test | EDF plus `O(T·k·(k + H_w log(H_w + k)))` | `O(J + k)` | No |
| Scheduling | Exact | Backtracking | `O((k + 1)^J · (k² + J))` worst case | `O(J + k)` | Yes when it finishes, given the structure of the lots (7.4) |

The measured growth, beside the predicted one. "Points" is the number of sizes with a median time of at least 0.05 ms, which is all the fit uses; a fit on two or three points is weak evidence and is labelled so below.

| Algorithm | Suite, and what grows | Predicted growth | Measured exponent (points) |
|---|---|---|---|
| Welsh-Powell | `colouring`, vertices; edge probability 10 / 30 / 50% | about n² (E grows as n²) | none / none / 1.94 (2) |
| DSATUR | `colouring`, vertices; 10 / 30 / 50% | about n³ | 2.57 (3) / 3.26 (3) / 3.06 (4) |
| Exact colouring | `colouring`, vertices (at most 20) | exponential | none: every run is under 0.05 ms |
| Greedy knapsack | `knap_1d` (copies), `knap_farm` (crops) | N log N | none, none: every run is under 0.1 ms |
| DP | `knap_1d`, copies (one resource) | about N² (the table grows with N) | 2.10 (5) |
| DP | `knap_dims`, resources (n fixed) | times 25 per added resource | no exponent; time ratios 18.9, 25.0, 26.8 |
| Branch and bound | `knap_1d`, copies | exponential worst case | 1.91 (5) |
| Branch and bound | `knap_farm`, crops | exponential worst case | 4.30 (4) |
| First-Fit Decreasing | `pack_tight` / `pack_loose`, trays | about n² (shelves grow with n) | 1.78 (3) / 1.79 (3) |
| Best-Fit Decreasing | `pack_tight` / `pack_loose`, trays | about n² | 1.86 (3) / 1.82 (3) |
| Genetic algorithm | `pack_tight` / `pack_loose`, trays | about n² | 1.87 (5) / 1.87 (5) |
| EDF | `sched`, slots | about k² (lots grow with k) | none: one point |
| EDF with lookahead | `sched`, slots | about k² | 1.98 (2) |
| Exact scheduling | `sched_exact`, lots | exponential | none: every run is under 0.05 ms |
| Forecast methods | `forecast` (n fixed) | linear in Y | no exponent; times in 4.1 and 4.2 |

---

## 2. Notation and how the measurements were made

This document analyses the algorithms in `src/core` **as they are built**. Every bound below was derived from the code, not copied from the plan; where the code differs from the plan's text, section 9 says so. Every measured number comes from `bench/results/summary.md` and the CSV files beside it (written by `farm_cli bench`; the machine, compiler and flags are recorded at the top of `summary.md`), or from the output of `farm_cli run --scenario data/default_scenario.json` and `farm_cli compare --scenario data/flat_scenario.json`; the text says which.

Each algorithm gets the same seven parts: **1** the problem it solves, **2** pseudo-code, **3** time, **4** space, **5** correctness or optimality, **6** the measured exponent beside the theoretical one, **7** where it sits in the pipeline and what it is compared against.

**Contents.**

1. [Summary of the bounds](#1-summary-of-the-bounds)
2. [Notation and how the measurements were made](#2-notation-and-how-the-measurements-were-made)
3. [Constraints](#3-constraints) (graph construction, Welsh-Powell, DSATUR, exact colouring)
4. [Forecasting](#4-forecasting) (the three simple methods, Holt-Winters, the selection rule)
5. [Crop selection](#5-crop-selection) (greedy, dynamic programming, branch and bound)
6. [Packing](#6-packing) (First-Fit and Best-Fit Decreasing, genetic algorithm)
7. [Scheduling](#7-scheduling) (slot arithmetic and interval depth, EDF, lookahead, exact)
8. [Hardness](#8-hardness)
9. [Where this document differs from the plan's text](#9-where-this-document-differs-from-the-plans-text)
10. [References](#10-references)

| Symbol | Meaning |
|---|---|
| `C`, `S` | crops, shelves |
| `K`, `E` | explicit conflict rules; edges of the conflict graph (the adjacency lists hold `2E` entries) |
| `Δ`, `k` | largest degree of the conflict graph; colours (in section 3) or slots (in section 7), the context says which |
| `B_i` | the i-th Bell number, the number of ways to split i things into groups (`B_10` = 115,975; `B_12` = 4,213,597; `B_20` = 51,724,158,235,372) |
| `Y`, `H`, `L` | weeks of history, forecast horizon in weeks, season length (52) |
| `N`, `m` | copies the knapsack may choose from (the sum of the group sizes), active budgets (at most 5) |
| `W_r`, `states` | grid steps of resource r (capacity divided by its unit); `Π (W_r + 1)`, the table size of the DP |
| `n`, `R` | trays to pack; most rows on any one shelf (set by the shelf depth and the smallest tray depth, so a constant for a given farm) |
| `G`, `P` | genetic algorithm: generations run, population size |
| `J`, `J_h`, `H_w` | lots of one crop, hard (contract) lots, hard lots whose latest start lies in the lookahead window |
| `T`, `p`, `f` | horizon in weeks, crop cycle in weeks, harvest window in weeks |

Time and space are worst-case upper bounds read off the code. Space counts what the algorithm allocates; the input itself is not counted unless said.

**Measured exponents.** `farm_cli bench` runs ten suites (plan section 15) and writes one CSV each plus `summary.md`. For every size the time of a run is the fastest of up to three; a size's time is the median over five seeds. The *measured exponent* of a series is the least-squares slope of `ln(median time)` against `ln(n)` over the sizes whose median is at least 0.05 ms (smaller times are mostly timer and call overhead). "None" means fewer than two such sizes. A slope can only be compared with a polynomial bound: for an exponential bound the text reports what was measured instead (node counts, growth per step). Some suites keep `n` fixed and vary another parameter; they have no exponent against `n`.

---

## 3. Constraints

Code: `src/core/constraints.cpp`. Conflict graph: vertices are crops, an edge means "these two crops may not share a shelf".

### 3.1 Compatibility and conflict graph (`buildConstraints`)

**1 Problem.** For every crop and shelf decide whether the crop may stand there (clearance, light, humidity and tray footprint, a tray may be turned a quarter when the crop allows it); for every pair of crops decide whether they may share a shelf (an explicit rule, a height ratio at which the taller crop shades the shorter, or no shelf that suits both); and bound the trays of a crop by shelf area.

**2 Pseudo-code.**

```text
for each crop c, shelf s:
    bad = clearance(c,s) | light(c,s) | humidity(c,s) | footprint(c,s)     # four O(1) tests
    compat[c][s] = (bad == 0)
    if compat[c][s]: maxTrays[c] += floor(area(s) / trayArea(c))
listed[a][b] = listed[b][a] = true   for every explicit rule (a, b)
for each pair of crops a < b:
    if listed[a][b]:                                    reason = Explicit
    elif max(height a, b) >= ratio * min(height a, b):  reason = Height
    elif no shelf s with compat[a][s] and compat[b][s]: reason = NoCommonShelf   # adds no restriction
    else:                                               reason = None
    conflict[a][b] = conflict[b][a] = reason
adjacency[a] = every b with conflict[a][b] != None, ascending
```

**3 Time.** The first loop has `C·S` iterations of constant work: `O(C·S)`. Reading the rules is `O(K)`. The pair loop has `C(C-1)/2` iterations; the first two tests are constant and the third scans the shelves, so at worst `O(S)` each: `O(C²·S)`. Building the adjacency lists is `O(C²)`. Total `O(C²·S + K)`.

**4 Space.** `compat` and `whyNot` hold `C·S` entries each, `conflict` and `listed` hold `C²`: `O(C·S + C²)`.

**5 Correctness.** Not an optimisation: the result is a definition. Each test is the rule of plan section 7.1, and `whyNot` keeps one bit per failed rule so that the app can explain a refusal.

**6 Measured.** No suite times it on its own. On the 12-crop sample farm the command line reports its stage time as 0.000 s.

**7 Pipeline.** Step 3. Its `compat` and `conflict` matrices are what `placeOne` obeys (section 6), `maxTraysByArea` caps the copies per crop in the knapsack (section 5), and the adjacency lists are the graph that section 3.2 to 3.4 colour.

### 3.2 Welsh-Powell

**1 Problem.** Colour the conflict graph so that conflicting crops get different colours; one colour class is a set of crops that may share a shelf, so the number of colours is an upper bound on how many shelf groups the crops need.

**2 Pseudo-code.**

```text
order = vertices sorted by (degree descending, index ascending)
colour[v] = -1 for every vertex                          # -1: not coloured yet
for v in order:
    used = empty set
    for u in adj[v]:                                     # the colours the neighbours already have
        if colour[u] >= 0: add colour[u] to used
    c = 0
    while c in used: c += 1                              # the smallest colour no neighbour uses
    colour[v] = c
return colour
```

**3 Time.** The sort is `O(C log C)` (the comparator is constant time). For a vertex `v`, building `used` (at most `deg(v) + 1` entries) and scanning the neighbours is `O(deg(v) + 1)`; the degrees sum to `2E`. Total `O(C log C + C + E) = O(C log C + E)`.

**4 Space.** `O(C)` beyond the graph (the order, the colours, one `used` array of at most `Δ + 1` entries); `O(C + E)` with the graph.

**5 Correctness.** A heuristic. The colouring is always proper: a vertex takes a colour that none of its already coloured neighbours has, and neighbours coloured later avoid its colour in turn. It uses at most `Δ + 1` colours: `v` has at most `deg(v)` coloured neighbours, so one of the colours `0 .. deg(v)` is free. It is not optimal: on the crown graph of Appendix A (G1), which is bipartite and so needs 2 colours, it uses 3. The method is from [Welsh-Powell 1967].

**6 Measured.** `colouring` suite: 1.94 at 50% edge probability (2 points); none at 10% and 30%, where only the largest size (400 vertices, 0.07 ms and 0.15 ms) reaches 0.05 ms. At a fixed edge probability `q`, `E` is about `q·C²/2`, so the bound grows as `C²`; the one fit is consistent with that and too thin to say more.

**7 Pipeline.** `colourAll` in `pipeline.cpp`, for the colour-count warning and the Constraints tab. Compared with DSATUR and the exact search (`farm_cli compare`, Colouring table, and the `colouring` suite). On the sample farm all three use 5 colours. At 400 vertices and 50% edge probability Welsh-Powell takes 0.23 ms and uses 58 colours; DSATUR takes 43.57 ms and uses 54.

### 3.3 DSATUR

**1 Problem.** The same as 3.2, with a rule that colours the most constrained vertex first.

**2 Pseudo-code.**

```text
colour[v] = -1 for every vertex
repeat C times:
    best = none
    for each uncoloured vertex v, in index order:
        saturation = number of distinct colours among v's coloured neighbours
        if best is none or saturation > saturation(best)
           or (saturation == saturation(best) and degree(v) > degree(best)):
            best = v                       # on a full tie the lower index stays
    c = 0
    while a coloured neighbour of best has colour c: c += 1      # the smallest free colour
    colour[best] = c
return colour
```

**3 Time.** Each of the `C` rounds recomputes the saturation of every uncoloured vertex by walking its adjacency list (a stamp array counts a colour once): `O(C + 2E)` per round, so `O(C·(C + E))`. Choosing the colour adds `O(deg)` per round, `O(C + E)` in all. The implementation recomputes from scratch every round, as the plan specified; it does not keep saturations up to date.

**4 Space.** `O(C)` beyond the graph (the colours and the stamp array); `O(C + E)` with it.

**5 Correctness.** A heuristic. Proper by the same argument as 3.2, and at most `Δ + 1` colours for the same reason. It is exact on bipartite graphs [Brélaz 1979, Theorem 1], so it finds 2 colours on the G1 crown graph where Welsh-Powell finds 3. For other graphs it has no guarantee.

**6 Measured.** 2.57 (3 points) at 10% edge probability, 3.26 (3 points) at 30%, 3.06 (4 points) at 50%, against a predicted 3: at a fixed edge probability `E` grows as `C²`, so `C·(C + E)` grows as `C³`. The spread (about ±0.4) is what three-point fits give; the successive doublings of the median time at 30% edge probability are 7.4, 12.3 times (from 100 to 200 and 200 to 400 vertices), steeper at the end than cubic growth's 8, and I did not look for the cause.

**7 Pipeline.** `colourAll`, as 3.2. It also supplies the first incumbent of the exact search (3.4).

### 3.4 Exact colouring (`colourExact`)

**1 Problem.** The fewest colours any proper colouring needs (the chromatic number), that is, the fewest shelf groups the crops can form.

**2 Pseudo-code.**

```text
order = Welsh-Powell order;  best = number of colours of the DSATUR colouring      # first incumbent
extend(i, used):                  # colour order[i], order[i+1], ...; `used` colours so far
    nodes += 1;  if nodes > nodeLimit: stopped = true
    if stopped or used >= best: return                       # cannot improve
    if i == C: best = used; return                           # a colouring with fewer colours
    v = order[i];  blocked = colours of v's coloured neighbours
    for k in 0 .. used:                                      # an existing colour, or one new colour
        if k not in blocked:
            colour[v] = k;  extend(i + 1, max(used, k + 1))
    colour[v] = -1
extend(0, 0);   proven = not stopped
```

**3 Time.** A call at depth `i` is a way of colouring the first `i` vertices in which every vertex takes an existing colour or the next new one. Such a colouring is the same thing as a split of `i` vertices into groups, so there are at most `B_i` calls at depth `i` and at most `Σ B_i ≤ (C + 1)·B_C` in all. Each call costs `O(used + deg) = O(C)`. The bound is exponential in `C` (`B_C` grows faster than any single exponential, `B_20` is about 5·10¹³), which is why the search takes a node limit (2,000,000 by default) and the benchmark runs it only up to 20 vertices. Pruning on `used >= best` cuts most of the tree on the graphs measured: the 12-crop flat sample farm needs 11 nodes, where `B_12` is 4,213,597.

**4 Space.** `order` and `colour` are `O(C)`; the recursion is at most `C` deep and each level holds a flag array of at most `best` entries, where `best` is at most the DSATUR count `k` (at most `Δ + 1`): `O(C·k)`. With the graph, `O(C + E + C·k)`.

**5 Correctness.** Exact when it finishes. (a) Nothing is lost by letting a vertex take only an existing colour or one new colour: any proper colouring becomes one of those by renaming its colours in order of first use along `order`. (b) The prune is safe: a partial colouring that already uses `used >= best` colours cannot be completed with fewer. (c) The incumbent starts as a real colouring (DSATUR's), so the answer is always achievable. If the node limit stops the search, `proven` is false and the count is an upper bound only. The tests check the search against an independent subset-DP oracle.

**6 Measured.** No exponent: no run reaches 0.05 ms, and the search ran only up to 20 vertices. At 50% edge probability the median time is 0.0014, 0.0031 and 0.0096 ms for 10, 15 and 20 vertices, growing 2.2 and then 3.1 times per five vertices. Three points cannot show exponential growth, but they do not contradict it. Every exact run in the suite was proven.

**7 Pipeline.** `colourAll`. On the 12-crop flat sample farm it proves 5 colours in 11 nodes (`farm_cli compare`). Compared with the two heuristics, as 3.2.

---

## 4. Forecasting

Code: `src/core/forecast.cpp`. Input: one crop's weekly order history `y` of `Y` weeks. Output: weekly spot demand in kg for `H` weeks ahead, never below zero.

### 4.1 Seasonal naive, moving average, simple exponential smoothing

**1 Problem.** Estimate next weeks' spot demand with the simplest recurrences: repeat last season; average the last `k` weeks; keep a smoothed level.

**2 Pseudo-code.**

```text
seasonal_naive(y, L, H):
    if Y < L: return [y[Y-1]] * H                       # less than one season: the last value
    return [ y[Y - L + (h mod L)] for h in 0 .. H-1 ]

moving_average(y, H):
    for k in (4, 8, 12) with k < Y:                      # one pass each
        window = y[0] + .. + y[k-1];  sse = 0
        for t in k .. Y-1:
            sse += (y[t] - window / k)^2
            window += y[t] - y[t-k]                      # slide the window
    k* = the k with the smallest sse (the first wins a tie);  k* = Y if no k qualified
    return [ mean(y[Y-k*] .. y[Y-1]) ] * H

ses(y, H):
    for alpha in 0.1, 0.2, .., 0.9:                      # one pass each
        level = y[0];  sse = 0
        for t in 1 .. Y-1:
            sse += (y[t] - level)^2;  level = alpha*y[t] + (1-alpha)*level
    return [ level of the alpha with the smallest sse ] * H
```

**3 Time.** Seasonal naive writes `H` values: `O(H)`. Moving average makes 3 passes of `O(Y)` plus a mean of `k*` values and `H` writes: `O(Y + H)`. Simple exponential smoothing makes 9 passes: `O(9·Y + H)`.

**4 Space.** `O(H)` for the output; a constant number of scalars otherwise.

**5 Correctness.** These are estimators, not solutions of an optimisation problem, so there is no optimality to claim. What is exact: `k` and `α` are the grid values with the least one-step in-sample squared error. Whether a method is good is empirical, and the hold-out of 4.3 measures it.

**6 Measured.** The `forecast` suite keeps `n` fixed (24 crops, 144 training weeks each), so there is no exponent. Median time for the 24 crops at 5% noise: 0.0055 ms (seasonal naive), 0.0143 ms (moving average), 0.0687 ms (simple exponential smoothing). The order follows the pass counts 0, 3 and 9. Hold-out error as % of mean demand at 5 / 15 / 30% noise: seasonal naive 8.97 / 19.10 / 36.74, moving average 14.00 / 20.20 / 31.37, simple exponential smoothing 13.24 / 19.97 / 31.22.

**7 Pipeline.** Step 2: every crop is forecast over `H = T - 1 + (longest cycle)` weeks. They are three of the four candidates of the selection rule (4.3); the `compare` Forecast table and the `forecast` suite compare all four on the same hold-out.

### 4.2 Additive Holt-Winters with a grid search

**1 Problem.** Forecast a series that has a trend and a seasonal pattern (additive Holt-Winters, [Holt 2004; Winters 1960]), choosing the three smoothing constants by exhaustive search over a grid: `α` in {0.1, 0.3, 0.5, 0.7, 0.9}, `β` in {0.01, 0.05, 0.1, 0.3}, `γ` in {0.05, 0.1, 0.3, 0.5}, which is 80 settings.

**2 Pseudo-code.**

```text
fit(y, L, alpha, beta, gamma, H):                        # needs Y >= 2L
    first = mean(y[0..L-1]);  second = mean(y[L..2L-1])
    trend = (second - first) / L;  level = first + trend * (L-1) / 2
    season[i] = y[i] - (first + trend * (i - (L-1)/2))   for i in 0 .. L-1
    for t in L .. Y-1:
        old = season[t mod L]
        sse += (y[t] - (level + trend + old))^2          # one-step prediction error
        newLevel = alpha * (y[t] - old) + (1 - alpha) * (level + trend)
        trend = beta * (newLevel - level) + (1 - beta) * trend
        season[t mod L] = gamma * (y[t] - newLevel) + (1 - gamma) * old
        level = newLevel
    return ( [level + (h+1)*trend + season[(Y+h) mod L] for h in 0..H-1],  sse )
holt_winters(y, L, H):  the fit with the smallest sse over the 80 settings   # alpha outer .. gamma inner, first wins a tie
```

**3 Time.** One fit costs `O(L)` to initialise, `Y - L` constant-time updates and `H` writes: `O(Y + H)` since `L ≤ Y/2`. Eighty fits: `O(80·(Y + H))`.

**4 Space.** `O(L)` for the season array and `O(H)` for each forecast: `O(L + H)`.

**5 Correctness.** The grid search is exhaustive, so the returned fit has the least one-step in-sample squared error of the 80 settings (an earlier setting is replaced only by one better by more than 1e-9). That is not the least error over all real-valued constants, and the model is additive and assumes the pattern repeats every `L` weeks.

**6 Measured.** No exponent (`n` fixed in the suite). Median time for 24 crops: 1.70, 1.65 and 1.60 ms at 5, 15 and 30% noise, about 25 times simple exponential smoothing's 0.0687 ms. Eighty fits against nine explains a factor of about 9; the rest is presumably the heavier update per week (three smoothed quantities instead of one), which I did not profile. Hold-out error at 5 / 15 / 30% noise: 6.01 / 16.57 / 33.03 %, the best of the four at 5% and 15% and behind simple exponential smoothing and the moving average at 30%.

**7 Pipeline.** Step 2, when it wins the hold-out. On the sample farm it is chosen for 8 of the 12 crops, seasonal naive for 3 and the moving average for 1 (output of `farm_cli run --scenario data/default_scenario.json`).

### 4.3 Selection by hold-out (`forecastSeries`)

`forecastSeries` with the default `Auto` holds out the last `min(12, Y/4)` weeks, runs the four methods on the weeks before them, scores each on the held-out weeks (MAE, RMSE, sMAPE) and keeps the lowest MAE; a later method replaces the current choice only if it is better by more than 1e-9, in the order seasonal naive, moving average, simple exponential smoothing, Holt-Winters. It then refits the chosen method on the whole history. With fewer than four weeks of history nothing is held out and seasonal naive is used.

**Time.** The four methods run once each on the training part and the chosen one runs once more on the whole history, plus `O(Y)` to copy the two parts: at most `(3 + 9 + 80) + 80 = 172` passes over the series, each `O(Y + H)`, so `O(Y + H)` with a constant of at most 172. **Space.** `O(Y + L + H)`. The selection is a model-choice heuristic, not an optimal rule.

---

## 5. Crop selection

Code: `src/core/knapsack.cpp`. The model (plan section 9): decide how many trays of each crop to plant. A crop is a *group* of copies with the same weight (what one tray uses of labour, water, nutrient, energy and floor area) and non-increasing values, because the k-th tray of a crop sells less than the one before. A solution takes the first `count` copies of each group. Maximise total value subject to the `m` budgets. `N` is the total number of copies, at most 400 per crop.

### 5.1 Greedy

**1 Problem.** Pick trays quickly, by value for the share of the budgets they use.

**2 Pseudo-code.**

```text
for each copy (g, k):
    density = value / sum over budgeted resources r of (weight[g][r] / budget[r])    # infinity if the sum is 0
sort the copies by (density descending, value descending, crop id, k)
remaining = budget;  count[g] = 0 for every group g
for copy (g, k) in sorted order:
    if k != count[g] + 1: continue                       # only a crop's next tray
    if weight[g][r] > remaining[r] + 1e-9 for some r: continue          # does not fit
    remaining -= weight[g]
    count[g] += 1
return count
```

**3 Time.** Computing the densities is `O(N·m)`, the sort `O(N log N)`, the scan `O(N·m)`: `O(N log N + N·m)`.

**4 Space.** `O(N)` for the sorted list and `O(C)` for the counts.

**5 Correctness.** A heuristic: the result always fits, and nothing more is claimed. Counterexample (Appendix A, G3): one budget of 10, group X with weight 6 and value 7, group Y with weight 5 and two copies of value 5. X has the better density (7/0.6 against 5/0.5), so greedy takes it and has no room left for Y: value 7 against the optimum 10 (two Y).

**6 Measured.** None: no run reaches 0.1 ms (the largest, 1,600 copies, takes 0.0905 ms). On `knap_farm` it reaches 98.8 to 99.7% of the branch-and-bound root bound.

**7 Pipeline.** Step 5 with `--knapsack greedy`. Its ranking (`rankCopies`) also drives the refill step (pipeline step 7) and its solution is the first incumbent of branch and bound (5.3). On the flat sample farm it finds 38,941.5 PKR/week, 98.62% of the optimum (`farm_cli compare`).

### 5.2 Dynamic programming

**1 Problem.** The same problem, solved exactly by filling a table over all combinations of the `m` budgets on a grid.

**2 Pseudo-code.**

```text
steps[r] = floor(budget[r] / unit[r]);   states = product of (steps[r] + 1)
if states > maxStates (2,000,000):        # coarsen: every resource gets at most G steps, G the largest that fits
    G = largest integer >= 1 with product of (min(steps[r], G) + 1) <= maxStates;  steps[r] = min(steps[r], G)
need[g][r] = ceil(weight[g][r] / unit[r])          # weights rounded up
items = every copy whose need fits the grid
best[0 .. states-1] = 0
for item i in items:
    for s = states-1 down to 0:                    # descending, so each item is used at most once
        if every coordinate of s >= need[i] and best[s - offset(i)] + value(i) > best[s]:
            best[s] = best[s - offset(i)] + value(i);  took[i][s] = true
s = states - 1                                     # walk back through the items
for i = last item down to first:  if took[i][s]: count[group(i)] += 1;  s -= offset(i)
```

(`offset(i)` is the position of the vector `need[i]` in the flattened table; a state is a vector of allowances, one per resource.)

**3 Time.** Each of the `N` items visits every one of the `states` cells, and a visit does `O(m)` work in the worst case (the coordinate test and the step to the next cell): `O(N·states·m)`. Without coarsening, `states = Π (W_r + 1)`: polynomial in each budget, but a **product** over the budgets, so exponential in `m`. With the farm's units (labour 0.01 h, water 1 L, nutrient 0.01 L, energy 0.1 kWh, area 10 cm²) the sample farm's full table would have about 1.2·10¹⁷ cells, which is why the table is coarsened to at most 2,000,000 cells.

**4 Space.** `best` holds `states` doubles and `took` holds `N·states` bits: `O(states)` doubles plus `N·states` bits. On the flat sample farm (`N` = 181, Appendix A G6, 1,889,568 cells) the bit table alone is 181 × 1,889,568 bits, about 43 MB.

**5 Correctness.** Exact on the integer grid, by the 0/1 knapsack recurrence `best[s] = max(best[s], best[s - w] + v)`. **Why the states are visited in descending order:** `s - offset` is smaller than `s` (or equal for a weightless item), so when the loop reaches `s` all smaller states have not yet been touched by this item and still hold the value from before it; each copy is therefore used at most once. Visiting upwards would let a copy be reused, which is the unbounded knapsack. **Why copies may be treated as independent items:** the copies of a group have equal weight and non-increasing value, so any choice of `j` copies of a group can be exchanged for the first `j` without losing value; the walk back counts chosen copies per group and the answer takes that many first copies. Every table solution is feasible for the real problem because weights are rounded up. The result is flagged `provenOptimal` only if the grid was not coarsened and every weight and budget is a whole number of units; once coarsened it is an approximation. On the flat sample farm with all five budgets the grid is 17 steps per resource (`18⁵` = 1,889,568 cells; `19⁵` would be 2,476,099, over the limit) and the DP finds 13,915.5 PKR/week, 35.24% of the optimum 39,486.5: the demonstration of why it cannot be the main solver.

**6 Measured.** `knap_1d` (one budget, equal to half the total weight, so `states` grows in proportion to `N`: median 281 cells at 50 copies, 13,121 at 1,600): `N·states` grows as `N²`; measured exponent **2.10** (5 points). `knap_dims` (40 copies, every budget 24, so 25 values per resource): the table has 25, 625, 15,625 and 390,625 cells for 1 to 4 budgets, and the median time is 0.0046, 0.0870, 2.1712 and 58.2359 ms, so each added budget multiplies it by 18.9, 25.0 and 26.8 against 25 predicted by the table size. On the flat sample farm it takes about 3 s (one run on the benchmark machine).

**7 Pipeline.** Step 5 with `--knapsack dp`; the app runs it on a worker thread. Compared with greedy and branch and bound in `compare`, `knap_1d` and `knap_dims`; with one active budget it matches branch and bound exactly (switch the others off in the app).

### 5.3 Branch and bound

**1 Problem.** The same problem, solved exactly by depth-first search over the tray count of one crop at a time, cutting branches whose upper bound cannot beat the best solution found.

**2 Pseudo-code.**

```text
order groups by the density of their first copy, descending
lists[r] (for each budgeted resource r) and lists[S] (the surrogate weight ws = sum_r weight[r]/budget[r]):
    every copy, sorted by value / weight descending (weight 0 first)
bound(depth, rem) = min( fractional(lists[S], depth, sum_r rem[r]/budget[r]),
                         min over r of fractional(lists[r], depth, rem[r]) )
fractional(list, depth, cap):  skip copies of groups before depth; take whole copies while they fit,
                               then the fitting fraction of the first that does not; stop
best = greedy's value                                  # first incumbent
search(depth, rem, current):
    nodes += 1;  stop at the node or the time limit (the clock is read every 4096 nodes)
    if depth == number of groups: best = max(best, current); return
    if current + bound(depth, rem) <= best + 1e-9: return                          # prune
    kmax = min(group size, floor((rem[r] + 1e-9) / weight[r]) over r with weight[r] > 0)
    for k = kmax down to 0:  search(depth + 1, rem - k*weight, current + value of the first k copies)
```

**3 Time.** Setup builds and sorts `m + 1` lists of `N` copies: `O(m·N log N)`. Each node evaluates `m + 1` fractional bounds, each a scan of a list of at most `N` copies: `O(m·N)`, plus `O(m)` per child. The number of nodes is at most the number of ways to choose a tray count for every group, `Π (u_g + 1)` for group sizes `u_g`: exponential in the number of crops in the worst case. The search stops after 5,000,000 nodes or 5 s (2 s in the benchmarks) and then reports `provenOptimal = false` with the best solution found and the root bound.

**4 Space.** The lists hold `(m + 1)·N` entries; the recursion is at most one frame per group: `O(m·N)`.

**5 Correctness.** Exact when it finishes: the search enumerates every feasible count for every group (`kmax` is the most that fits) except where a bound proves a branch useless, so what must be shown is that **each bound is an upper bound** on the value any completion of the node can reach.
- *One resource.* Any completion takes a set of the remaining copies whose weights in resource `r` fit in `rem[r]`. Dropping the other budgets and the rule that a group's copies are taken first-to-last only enlarges the set of allowed choices, so the best value can only grow. Allowing fractions enlarges it again. The best fractional choice is found by taking copies in decreasing value-per-weight order, which is the fractional knapsack greedy [CLRS]. So `fractional(lists[r], ...)` is a relaxation's optimum, hence an upper bound.
- *The surrogate.* Every feasible completion satisfies `sum_i weight[i][r]·x_i <= rem[r]` for each `r`. Dividing by `budget[r] > 0` and summing gives one constraint, `sum_i (sum_r weight[i][r]/budget[r])·x_i <= sum_r rem[r]/budget[r]`, which every feasible completion also satisfies. The same relaxation argument applies. It is the term that cuts the search when several budgets bind at once.
- *The minimum* of upper bounds is an upper bound, and a node is cut only when `current + bound <= best + 1e-9`, so nothing that could beat the incumbent by more than the tolerance is lost. Because the search starts from greedy's solution, the answer is never worse than greedy's. If a limit stops it, the root bound still says how far the answer can be from the optimum.

**6 Measured.** The worst case is exponential and the measurements do not show it on these families, so the exponents describe a local trend, not the bound. `knap_1d`: exponent **1.91** (5 points); the node count (median) is 363, 1,095, 761, 7,367, 7,551 and 10,265 for 50 to 1,600 copies: with one budget the fractional bound is tight and prunes almost everything. `knap_dims` (nodes, median): 61, 211, 618 and 920 for 1 to 4 budgets, where the DP's table grows from 25 to 390,625 cells. `knap_farm` (the problem of a generated farm): exponent **4.30** (4 points, 6 to 24 crops), with 96, 2,966, 11,458 and 17,254 nodes; four sizes say nothing about larger farms. Every `knap_1d` and `knap_farm` instance was proven optimal within the limits (the `aux2` column is 1 in every row). On the flat sample farm it proves its optimum, 39,486.5 PKR/week, in 2,269 nodes.

**7 Pipeline.** Step 5, the default. With a proven result its value is an upper bound on every plan, and `planValue / relaxedOptimum` is the headline "plan share". Compared with greedy and the DP in `compare` and in the three `knap_*` suites.

---

## 6. Packing

Code: `src/core/packing.cpp`. Each shelf is filled in rows along its width, stacked along its depth. One function, `placeOne`, places one tray and never breaks a rule; the three packers differ only in the order in which they offer trays and in the fit policy they pass to it.

### 6.1 FFD and BFD (First-Fit and Best-Fit Decreasing)

**1 Problem.** Place the trays the knapsack chose on the shelves: inside the shelf, not overlapping, on a compatible shelf, never with a conflicting crop on the same shelf. Maximise the value of the trays placed.

**2 Pseudo-code.**

```text
order = trays sorted by (longer side descending, shorter side descending, crop id, tray id)
for tray in order:
    if not placeOne(tray, policy): unplaced.append(tray)

placeOne(tray, policy):
    a shelf is a candidate if the crop is compatible with it and no conflicting crop stands on it   # O(C) per shelf
    FirstFit: the first candidate shelf, in id order, where an existing row has room
              (width left >= tray width, row depth >= tray depth) or a new row fits; rows before a new row
    BestFit:  among the existing rows of all candidates, the one that leaves the least width
              (then the best depth match, then the lower shelf and row);
              only if no row fits: a new row on a shelf already in use with the least depth left, else an empty shelf
    a tray may be turned a quarter when the crop allows it: in an existing row take the narrower orientation that fits;
        for a new row take the one that lets more trays into the space left
    put the tray at (row.usedWidth, row.y); a new row takes the depth of its first tray
```

**3 Time.** Sorting is `O(n log n)`. One `placeOne` visits the `S` shelves; at each, the candidate test is `O(C)` (it compares the crop with every crop already on the shelf) and the row scan is `O(R)`: `O(S·(C + R))`. Over `n` trays: `O(n log n + n·S·(C + R))`. `C` and `R` are fixed for a farm, so for growing instances this is `O(n log n + n·S)`; if the shelves grow with the trays, as they do in the benchmark instances, it is quadratic.

**4 Space.** Placements `O(n)`, one `has-crop` flag array per shelf `O(S·C)`, rows at most `n`: `O(n + S·C)`.

**5 Correctness.** A heuristic. Every layout it returns is valid: `placeOne` enforces the rules and `validatePacking` re-checks them; a violation is reported as a bug. It is not optimal and neither policy dominates: on the Appendix A G4 instance First-Fit places all 4 trays and Best-Fit places 3. The problem contains bin packing and graph colouring and is NP-hard (section 8). For one-dimensional bin packing, First-Fit Decreasing never uses more than 11/9·OPT + 6/9 bins [Dósa 2007]; that guarantee does not carry over to rows with conflicts, and none is claimed here. The row ("level") layout belongs to the family analysed in [Coffman et al. 1980].

**6 Measured.** `pack_tight` (trays fill 90% of the shelf area) FFD **1.78**, BFD **1.86**; `pack_loose` (60%) FFD **1.79**, BFD **1.82**; 3 points each (200, 400 and 800 trays). The benchmark adds shelves in proportion to the trays, so `S` grows with `n` and the bound is quadratic; BFD scans every shelf and row for every tray, FFD stops at the first shelf with room, and both measured slopes sit just under 2 (the `n log n` term and fixed costs weigh on small sizes). Quality (placed area as % of tray area): FFD 90.03 to 96.68% and BFD 88.90 to 95.88% on `pack_tight`, 100% for both on `pack_loose`.

**7 Pipeline.** Step 6 (FFD is the default packer) and, with the packer's own policy, the refill step 7, which spends what is left of the budgets on trays that verifiably fit. Compared in `compare` and the `pack_*` suites. On the flat sample farm: FFD places 94 of 102 trays (36,858.5 PKR/week), BFD 95 (37,203.0).

### 6.2 Genetic algorithm

**1 Problem.** The same, searched over the order in which trays are offered and over the fit policy, because the order decides what fits.

**2 Pseudo-code.**

```text
decode(order, policy): empty layout; for tray in order: placeOne(tray, policy)
fitness: more placed value, then fewer shelves in use, then a larger sum of (placed area / shelf area)^2
population (P = 40): [decreasing order + First-Fit, decreasing order + Best-Fit,
                      order by first-copy value per cm^2 + First-Fit]
                     + P-3 random shuffles of the decreasing order, each with a random policy
repeat until 150 generations, or 40 generations in a row without improvement:
    next = the two fittest individuals, unchanged
    while |next| < P:
        parent1, parent2 = winners of two three-way tournaments
        child.order  = order crossover of the parents (probability 0.9), else a copy of parent1
        child.policy = the policy of one parent
        with probability 0.3: swap two positions of child.order
        with probability 0.05: flip child.policy
        evaluate(child) by decode
    population = next
return decode of the fittest
```

**3 Time.** One evaluation is a decode, `n` calls of `placeOne`, `O(n·S·(C + R))`, plus `O(n + S + C)` for the fitness and `O(n)` for the crossover. The first population needs `P` evaluations and each generation `P - 2`: `O(G·P·n·S·(C + R))` for `G` generations run (at most 150; the benchmark does not record how many each run used). Sorting the starting orders is `O(n log n)` once.

**4 Space.** `P` permutations of `n` trays, `O(P·n)`, plus the layout being decoded, `O(n + S·C)`.

**5 Correctness.** A heuristic with no convergence guarantee. Every individual decodes through `placeOne`, so every layout is valid. It is never worse than FFD or BFD in its own fitness order: the first population contains both decreasing orders with their policies, evaluated exactly as those packers would, and the two fittest individuals survive each generation unchanged, so the best fitness never falls. The crossover is the order crossover, usually credited to [Davis 1985]: the child keeps a slice of one parent's order and takes the remaining trays in the other parent's order, so it is always a permutation. [Falkenauer 1996] designed a genetic algorithm for bin packing that evolves groups of items rather than orders; this one is the simpler order-based kind. The draws come from a seeded SplitMix64 generator in a documented order, so the same seed gives the same result on every machine.

**6 Measured.** Exponent **1.87** on both suites (5 points each); with `P` and the generation cap fixed and `S` proportional to `n`, the bound is quadratic. At 800 trays one run takes 6.54 s on `pack_tight` and 3.80 s on `pack_loose` (median of five seeds). It placed 100% of the tray area at every size on both suites, which FFD and BFD did not on `pack_tight` (6.1).

**7 Pipeline.** Step 6 with `--packer ga`; the refill after it uses First-Fit. Compared with FFD and BFD in `compare` and the `pack_*` suites. On the flat sample farm it places 97 of 102 trays (38,404.0 PKR/week) in about 0.2 s.

---

## 7. Scheduling

Code: `src/core/scheduling.cpp`, one crop at a time. A **lot** is one tray's worth of one delivery: it may be planted in any week `s` with `release <= s <= latestStart` and is harvested `p` weeks later, so a batch occupies a slot for `p` weeks. A **slot** is one tray position, and the slots of a crop are identical machines. A *hard* lot is a contract delivery (missing it costs its penalty); a *soft* lot is forecast spot demand. For one crop, `release = max(0, latestStart - f)` with the same `f` for every lot, so a lot with a later `latestStart` never has an earlier release.

### 7.1 Slot arithmetic and interval depth

Three small routines. `buildLots` creates the lots for the delivery weeks `p .. T-1+p` with a cumulative rounding of the spot kilograms: `O(T + J)`. `hardMissed` and `minSlotsForHardLots` give the number of *contract slots* the knapsack uses. `intervalDepth` is a separate sweep.

**1 Problem.** `intervalDepth`: the largest number of half-open intervals `[start, end)` open at the same moment. If every lot had one fixed start week, this is the fewest slots that can run all the intervals without overlap (*interval partitioning*). `minSlotsForHardLots`: the fewest slots, all free at week 0, that meet every hard lot's deadline when each lot has a start window.

**2 Pseudo-code.**

```text
intervalDepth(intervals):
    events = [(start, +1), (end, -1) for each interval];  sort events by (week, delta)   # an end sorts before a start
    open = 0;  deepest = 0
    for (week, delta) in events:  open += delta;  deepest = max(deepest, open)
    return deepest

hardMissed(freeAt, hardLots sorted by (latestStart, id), p):
    heap = min-heap of freeAt;  missed = 0
    for lot in hardLots:
        if heap is empty: missed += 1; continue
        a = heap.pop_min();  start = max(a, lot.release)
        if start <= lot.latestStart: heap.push(start + p)       # the slot is busy until start + p
        else: heap.push(a); missed += 1                         # the lot is lost, the slot is untouched
    return missed

minSlotsForHardLots(lots, p):  for m = 0, 1, 2, ...: if hardMissed(m slots free at week 0, the hard lots) == 0: return m
```

**3 Time.** `intervalDepth`: sorting `2J` events `O(J log J)`, one sweep `O(J)`. `hardMissed`: building the heap `O(k)`, then `O(log k)` per lot: `O(k + J_h log k)`. `minSlotsForHardLots` sorts the hard lots (`O(J log J)`) and tries `m = 0 .. k` where `k` is the answer, `k <= J_h`: `O(k·(k + J_h log k))`, at worst `O(J_h² log J_h)`.

**4 Space.** `O(J)` for `intervalDepth` and for the sorted hard lots; `O(k)` for the heap.

**5 Correctness.** *Interval depth* is exact, and so is the claim that it equals the fewest slots for fixed intervals (interval partitioning [Kleinberg-Tardos, 4.1]): at the busiest moment every open interval needs its own slot, so at least `depth` slots are needed; conversely, taking the intervals in order of start and giving each a free slot never needs more than `depth`, because at the moment an interval starts, the slots in use hold intervals that are still open then, and together with the new one there are at most `depth` of them. Sorting ends before starts makes back-to-back intervals share a slot. *The slot search*: the schedule `hardMissed` simulates is a valid schedule, so a count it accepts is **sufficient**. With one fixed start per lot (`f = 0`) it equals the interval depth, which a test asserts on 300 seeded random instances. For `f > 0` I have not proved that it is the **minimum**; the pipeline uses it only to decide how many trays carry the contract bonus, so an overestimate would change a value, never a result's validity.

**6 Measured.** No suite times either routine alone, and `intervalDepth` is not called by the pipeline at all; the tests use it as the oracle for the slot search.

**7 Pipeline.** `minSlotsForHardLots` is step 4: its result is each crop's `contractSlots`, which spreads the crop's penalty over the first trays in the knapsack's tray values. `hardMissed` is also the test inside the lookahead scheduler (7.3).

### 7.2 EDF (earliest deadline first)

**1 Problem.** Decide, week by week, which lot each free slot starts, so as to deliver as much as possible on time.

**2 Pseudo-code.**

```text
pending = lots sorted by (dueWeek, hard before soft, value descending, id)
freeAt[k] = 0 for every slot k
for t = 0 .. T-1:                                                       # planting weeks
    for k = 0 .. slots-1:
        if freeAt[k] > t: continue                                      # the slot is busy
        pick = first lot in pending that is unscheduled
               and has release <= t <= latestStart
        if there is none: continue                                      # the slot is idle this week
        schedule pick on slot k from t to t + p
        freeAt[k] = t + p
```

**3 Time.** Sorting is `O(J log J)`. There are `T·k` (week, slot) pairs, and the search for the first eligible lot walks `pending`, `O(J)` in the worst case: `O(J log J + T·k·J)`.

**4 Space.** `pending`, the scheduled flags and the batches `O(J)`, `freeAt` `O(k)`: `O(J + k)`.

**5 Correctness.** A heuristic. Every schedule is valid (a slot is only started when it is free, each lot at most once, every start inside its window and before `T`). It is not optimal because it never keeps a slot empty for a lot that is not yet released: it fills slots with soft lots that fall due soon, and a contract lot released a week later finds every slot busy for `p` weeks. Appendix A, G5: EDF delivers 2 of 5 contract lots (value 45), while lookahead delivers 5 of 5 (value 60), which is also the exact optimum. (The classical result that earliest-deadline-first is optimal for minimising maximum lateness assumes every job is available at once [Kleinberg-Tardos, 4.2]; release times are exactly what breaks it here.)

**6 Measured.** `sched` suite (slots 5 to 80, `J` grows in proportion): exponent none, because only the largest size reaches 0.05 ms. The medians are 0.0022, 0.0042, 0.0121, 0.0406 and 0.1552 ms, so successive doublings multiply the time by 1.9, 2.9, 3.4 and 3.8, approaching the 4 that `T·k·J` growing as `k²` predicts. On this suite EDF serves 80.77% of the hard lots from 10 slots up (92.31% at 5).

**7 Pipeline.** Step 10 with `--scheduler edf`. Compared with lookahead and the exact search in `compare` and the three `sched*` suites. On the flat sample farm it delivers 67 of 84 contract lots on time, with penalties of 39,210 and a net of 651,150 PKR over the plan.

### 7.3 EDF with lookahead (the default)

**1 Problem.** The same, without EDF's failure: never take a spot lot that costs a contract delivery that was still achievable.

**2 Pseudo-code.**

```text
the loop of 7.2, but for a free slot k in week t:
    firstReleased = first eligible lot in pending order;  firstHard = first eligible hard lot
    if firstHard exists: pick it
    elif firstReleased exists and admit(firstReleased, k, t): pick it                 # a soft lot
    else: leave the slot idle this week

admit(soft lot, slot k, week t):                      # L = lookahead window in weeks, by default the crop's cycle p
    future  = unscheduled hard lots with t <= latestStart <= t + L, sorted by (latestStart, id)
    without = hardMissed(freeAt, future, p)
    with    = hardMissed(freeAt with freeAt[k] = t + p, future, p)
    return with <= without
```

**3 Time.** The loop is EDF's, `O(J log J + T·k·J)`; building `future` is one more scan of the `J` lots, already counted. Each admission test (at most one per (week, slot) pair, so at most `T·k`) also sorts `future` (`O(H_w log H_w)`), copies `freeAt` (`O(k)`) and calls `hardMissed` twice (`O(k + H_w log k)` each). Extra: `O(T·k·(k + H_w log(H_w + k)))`.

**4 Space.** `O(J + k)`, with `future` of at most `J`.

**5 Correctness.** A heuristic, always valid, with no dominance guarantee: it protects only the hard lots due in the next `L` weeks, may leave a slot idle for a soft lot that would have been harmless, and the plan's validation found random instances on which it did worse than plain EDF, so the tests do not assert otherwise. In the `sched_window` suite (20 slots) the share of hard lots on time rises with the window: 80.77% at `L = 0`, 87.18% at `L = 1`, 100% from `L = 2` on; the default `L = p` is 5 there.

**6 Measured.** `sched`: exponent **1.98** from 2 points (40 and 80 slots), predicted about 2; the doublings of the median time (0.0039 ms at 5 slots to 0.8004 ms at 80) are 3.3, 3.6, 4.4 and 4.0. At 80 slots it takes 5.2 times as long as EDF (0.8004 against 0.1552 ms). `sched_window`: the median time grows with the window as more hard lots fall inside it: 0.0316, 0.0375, 0.0398, 0.0764, 0.0806, 0.1024 and 0.1442 ms for `L` = 0, 1, 2, 3, 5, 10 and 26. On `sched` it serves 100% of the hard lots at every size.

**7 Pipeline.** Step 10, the default. Compared with EDF and the exact search. On the flat sample farm: 84 of 84 contract lots on time, no penalties, net 678,480 PKR, at the price of 140 idle slot-weeks against EDF's 102.

### 7.4 Exact search

**1 Problem.** For one crop with few lots, the schedule of greatest value minus the penalties of missed hard lots.

**2 Pseudo-code.**

```text
order lots by (latestStart, id);  gain[i] = value + penalty of lot i;  suffix[i] = gain[i] + gain[i+1] + ..
search(i, current):
    nodes += 1;  stop at the node limit
    if current + suffix[i] <= best + 1e-9: return          # even taking every remaining lot cannot beat the best
    if i == J: best = current; remember the choices; return
    for each slot k with a distinct freeAt value:           # slots that free at the same week are alike
        start = max(freeAt[k], release[i])
        if start <= latestStart[i] and start < T:
            freeAt[k] = start + p;  search(i + 1, current + gain[i]);  restore freeAt[k]
    search(i + 1, current)                                  # skip lot i
answer: best - total penalty of all hard lots
```

Used only for at most 22 lots and 2,000,000 nodes; beyond either, the lookahead schedule is shown and the result says so.

**3 Time.** Each lot has at most `k + 1` choices (one of `k` slots, or skip), so there are at most `(k + 1)^J` leaves and about as many nodes. A node loops over the slots with a linear search among the free times already tried, `O(k²)`; improving `best` copies two vectors of `J` entries. In all `O((k + 1)^J·(k² + J))` in the worst case, with `J <= 22` and the node limit as the guard. The `suffix` bound cuts most branches.

**4 Space.** The recursion is `J` deep and the arrays hold `J` or `k` entries: `O(J + k)`.

**5 Correctness.** Optimal when it finishes. The search enumerates "skip, or start on a slot at its earliest week", in `latestStart` order, with one slot tried per distinct free time. Slots with equal free times are interchangeable, so that last restriction loses nothing. The other two restrictions lose nothing because of the structure of the lots (a later `latestStart` never has an earlier release):
- *Order on a slot.* Suppose two neighbouring batches on one slot are in the wrong order: lot `a` starts at `s_a`, lot `b` at `s_b >= s_a + p`, and `latestStart(a) > latestStart(b)`, so `release(a) >= release(b)`. Swap them. Lot `b` starts at `s_a`: that is no earlier than `release(a) >= release(b)`, and no later than `s_b <= latestStart(b)`. Lot `a` starts at `s_b >= s_a >= release(a)` and `s_b <= latestStart(b) < latestStart(a)`. The two time intervals on the slot are unchanged. Repeating the swap puts every slot's lots in `latestStart` order.
- *Earliest start.* Starting a batch earlier keeps it inside its window and never delays the next batch on that slot.

The bound is valid because every lot's `gain` is at least 0, so no completion of a node can exceed `current + suffix[i]`. The code does not check the structure of the lots; the argument holds for lots built by `buildLots`, which are the only ones the pipeline passes, and for any hand-made lots with the same property. The tests compare the search with an independent brute force over every slot and every start week on 300 random small instances (they agree on all of them).

**6 Measured.** No exponent: every run is under 0.05 ms. `sched_exact` (2 slots, 8 weeks, cycle 2): 0.0016, 0.0058, 0.0153 and 0.0379 ms for 8, 12, 16 and 20 lots, growing 3.6, 2.6 and 2.5 times per four added lots, against the worst case of `3⁴ = 81` times for two slots: the bound and the narrow windows prune almost everything here. Quality as % of the exact optimum: lookahead 100% at all four sizes; EDF 100, -16.67, -37.50 and -50.00% (negative: the penalties of the contracts it missed exceed its revenue).

**7 Pipeline.** Step 10 with `--scheduler exact`. Because of the 22-lot limit it is a demonstration: on the flat sample farm 10 crops have more lots and show the lookahead schedule instead (`farm_cli compare`). Compared with EDF and lookahead in `compare` and `sched_exact`.

---

## 8. Hardness

Known results, stated as sources give them. Section 10 says how far each was checked.

- **Knapsack.** The 0/1 knapsack problem is NP-hard [Karp 1972] but only in the weak sense: the table DP of 5.2 with one budget costs `O(N·W)`, pseudo-polynomial. With several budgets (the *multidimensional* knapsack) the problem is NP-hard in the strong sense when the number of budgets is part of the input, so no algorithm polynomial in the budgets' values is expected [Bertsimas-Demir 2002, citing earlier work]. The standard text on this problem is [Kellerer et al. 2004, chapter 9]. For a **fixed** number `m` of budgets the table DP is pseudo-polynomial, `O(N·Π (W_r + 1))`, but the table is a product over the budgets and grows exponentially with `m`: measured 25 times per added budget in 5.2. The problem solved here contains the multidimensional knapsack (a group with one copy is an ordinary item), so it is at least as hard.
- **Packing.** Bin packing is NP-hard in the strong sense [Garey-Johnson 1979]. The packing problem here contains it: if every tray has the depth of a shelf, each shelf holds exactly one row, and placing trays is one-dimensional bin packing along the width, with the shelves as bins. It also contains graph colouring: with trays so small that capacity never binds, finding the fewest shelves in use under the conflicts is colouring the conflict graph. So no polynomial algorithm is expected to pack optimally, and FFD, BFD and the genetic algorithm are heuristics.
- **Colouring.** The chromatic number is NP-hard [Karp 1972], which is why Welsh-Powell and DSATUR are heuristics and the exact search is exponential and limited to small graphs.
- **Scheduling.** Single-machine non-preemptive scheduling with release times and deadlines (equivalently, minimising maximum lateness with release times) is NP-hard in the strong sense (the lecture notes *Elements of Scheduling*, Theorem 3.11; the classification of machine scheduling problems is in [Lenstra et al. 1977]). The lots of one crop here are a special case, with equal lengths `p` and windows of one width `f`; **I do not claim that this special case is NP-hard**, and nothing in this document depends on it. The exact search is exponential because it is a plain search, and it is limited to 22 lots for that reason.

---

## 9. Where this document differs from the plan's text

The plan (sections 7.4, 8.4, 10, 11.8 and 17) states bounds and says "if your implementation differs, document what you built". These are the differences, all from reading the code.

| Where | The plan says | The code does |
|---|---|---|
| Forecast selection (4.3) | each method runs twice | all four methods run once on the training part, and only the chosen (or forced) method runs again on the whole history |
| Packing (6.1, 6.2) | `O(n·(S + R))` per tray | `O(S·(C + R))`: the candidate test compares the crop with every crop on the shelf, `O(C)` per shelf |
| Lookahead (7.3) | EDF plus `O(T·k·H_w log k)` | the admission test also sorts the window's lots and copies the free times: `O(T·k·(k + H_w log(H_w + k)))` |
| Exact colouring (3.4) | space `O(C + E)` | the recursion keeps a flag array per level: `O(C + E + C·k)` |
| Exact scheduling (7.4) | `O((k + 1)^J)` | each node costs `O(k²)` and each improvement `O(J)`: `O((k + 1)^J·(k² + J))` |
| Welsh-Powell (3.2) | `O(C log C + C + E)` | the same, written `O(C log C + E)` |
| Pipeline step 10 | schedule every crop that has lots | a crop without lots also gets an (empty) schedule, so its trays show as idle |
| `intervalDepth` (7.1) | part of the scheduling component | exists and is tested, but the pipeline does not call it; `minSlotsForHardLots` does the work |
| `minSlotsForHardLots` (7.1) | "interval partitioning generalised to flexible windows" | sufficient by construction and equal to the depth for fixed starts; minimality for flexible windows is not proved |

---

## 10. References

How each was checked is given in brackets. "Record" means the bibliographic record (authors, title, venue, year) was confirmed; "statement" means the statement used here was found in a source that states it; "secondary" means I could not read the work itself and the statement comes from a source that cites it.

- R. M. Karp, *Reducibility among combinatorial problems*, in *Complexity of Computer Computations*, Plenum Press, 1972, pp. 85-103. [Record; statement: its list of 21 problems includes knapsack and chromatic number (secondary).]
- M. R. Garey and D. S. Johnson, *Computers and Intractability: A Guide to the Theory of NP-Completeness*, W. H. Freeman, 1979. [Statement: bin packing is strongly NP-complete (secondary).]
- D. Bertsimas and R. Demir, *An approximate dynamic programming approach to multidimensional knapsack problems*, Management Science 48(4), 2002, pp. 550-565. [Record; statement: the multidimensional knapsack problem is NP-hard in the strong sense (read in the paper's own text as found by search).]
- J. K. Lenstra, A. H. G. Rinnooy Kan and P. Brucker, *Complexity of machine scheduling problems*, Annals of Discrete Mathematics 1, 1977, pp. 343-362. [Record; its abstract says it establishes NP-completeness for many machine scheduling problems.]
- *Elements of Scheduling*, lecture notes, arXiv:2001.06005, Theorem 3.11. [Statement: 1|r_j|L_max is strongly NP-hard.]
- D. J. A. Welsh and M. B. Powell, *An upper bound for the chromatic number of a graph and its application to timetabling problems*, The Computer Journal 10(1), 1967, pp. 85-86. [Record only; the paper itself was not read.]
- D. Brélaz, *New methods to color the vertices of a graph*, Communications of the ACM 22(4), 1979, pp. 251-256. [Record; statement: Theorem 1, DSATUR is exact on bipartite graphs.]
- C. C. Holt, *Forecasting seasonals and trends by exponentially weighted moving averages*, International Journal of Forecasting 20(1), 2004, pp. 5-10 (first issued as a 1957 report of the Carnegie Institute of Technology). [Record.]
- P. R. Winters, *Forecasting sales by exponentially weighted moving averages*, Management Science 6(3), 1960, pp. 324-342. [Record.]
- H. Kellerer, U. Pferschy and D. Pisinger, *Knapsack Problems*, Springer, 2004, chapter 9 (multidimensional knapsack problems; relaxations, and exact algorithms including branch and bound and dynamic programming). [Record and chapter list from the publisher's listing; the text itself was not read.]
- J. Kleinberg and E. Tardos, *Algorithm Design*, Addison-Wesley, 2005, chapter 4 (4.1 interval scheduling and partitioning, 4.2 scheduling to minimise lateness by an exchange argument). [Record; section content from lecture material built on the book (secondary).]
- T. H. Cormen, C. E. Leiserson, R. L. Rivest and C. Stein, *Introduction to Algorithms* (the course text), the greedy-algorithms chapter, for the fractional knapsack problem [CLRS]. [Not looked up: cited from the course text by topic only.]
- E. G. Coffman Jr., M. R. Garey, D. S. Johnson and R. E. Tarjan, *Performance bounds for level-oriented two-dimensional packing algorithms*, SIAM Journal on Computing 9(4), 1980, pp. 808-826. [Record.]
- G. Dósa, *The tight bound of First Fit Decreasing bin-packing algorithm is FFD(I) ≤ 11/9 OPT(I) + 6/9*, ESCAPE 2007, Lecture Notes in Computer Science 4614, pp. 1-11. [Record and statement.]
- E. Falkenauer, *A hybrid grouping genetic algorithm for bin packing*, Journal of Heuristics 2(1), 1996, pp. 5-30. [Record; cited as the genetic algorithm designed for bin packing, which this project's order-based algorithm is not.]
- L. Davis, *Applying adaptive algorithms to epistatic domains*, Proceedings of the 9th International Joint Conference on Artificial Intelligence (IJCAI-85), 1985. [Record; the genetic-algorithm literature credits this paper with the order crossover used in 6.2, but I could not confirm that from the paper itself (secondary).]
