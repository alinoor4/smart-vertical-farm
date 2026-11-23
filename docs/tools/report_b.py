"""Report, part B: algorithms (from docs/complexity-analysis.md) and implementation."""
import re

import complexity_md
from farm_data import n0, n1, n2
from facts import PACKERS


def write(r, D, F):
    algorithms(r, D, F, complexity_md.sections())
    implementation(r, D, F)


def ex(exponent):
    """'1.94 (2)' for an exponent and its number of points, 'none' when the fit had fewer than two points."""
    value, points = exponent
    return "none" if value is None else "%s (%d)" % (n2(value), points)


def algo(r, md, number, key, title=None, skip_problem=False):
    """One algorithm subsection from the complexity document: problem, pseudo-code, time, space, correctness."""
    s = md[number]

    def fill(text):             # {ref:key} -> the section number the report gave to that key
        return re.sub(r"\{ref:(\w+)\}", lambda m: r.ref(m.group(1)), text)

    r.h(3, key, title or s["title"])
    for paragraph in s["intro"]:
        r.p(fill(paragraph))
    if not skip_problem:
        for paragraph in s["1"]:
            r.p(fill(paragraph))
    if s["code"]:
        r.p("**Pseudo-code.**", keep_next=True, after=2)
        r.code(fill(s["code"]))
        for paragraph in s["2"]:
            r.p(fill(paragraph), size=10)
    for label, part in (("Time.", "3"), ("Space.", "4"), ("Correctness and optimality.", "5")):
        lines = s[part]
        if not lines:
            continue
        first = True
        for line in lines:
            if line.startswith("- "):
                r.p(fill(line[2:]), style="List Bullet", size=10.5)
            else:
                r.p(("**%s** " % label if first else "") + fill(line))
            first = False


# ------------------------------------------------------------------ 7 algorithms

def algorithms(r, D, F, md):
    c = F.cmp
    r.h(1, "algorithms", "Algorithms")
    r.p("This section describes the algorithms of the five components: the idea, pseudo-code, the time and space bounds "
        "with their derivation, and an argument for correctness or optimality. The bounds were derived from the code as "
        "built. The complete treatment, with the measured exponents, is `docs/complexity-analysis.md`; here it is "
        "reproduced as far as the argument needs. “The plan” is the implementation plan the program was built from "
        "(`plan/IMPLEMENTATION_PLAN.md`); G1 to G6 are the golden cases of its Appendix A, small instances and the flat "
        "sample farm whose numbers were computed independently and are asserted by the tests (%s)." % r.sec("impl"))
    r.table("tab:symbols", "Symbols used in the complexity bounds.", ["Symbol", "Meaning"],
            [["C, S, E, Δ", "crops, shelves, edges of the conflict graph, its largest degree"],
             ["Y, H, L", "weeks of history, forecast horizon in weeks, season length"],
             ["N, m", "copies the knapsack may choose from, active budgets (at most 5)"],
             ["W_{r}, states", "grid steps of resource r; Π (W_{r} + 1), the size of the dynamic-programming table"],
             ["n, R", "trays to pack; most rows on any one shelf (set by the shelf and tray depths, a constant for a given farm)"],
             ["G, P", "generations run and population size of the genetic algorithm"],
             ["J, J_{h}, H_{w}", "lots of one crop, hard (contract) lots, hard lots whose latest start is inside the lookahead window"],
             ["T, p, f, k", "horizon in weeks, crop cycle, harvest window, slots (or colours, as the context says)"]],
            [3.6, 12.8], size=9)

    # ---- 7.1 constraint layer
    r.h(2, "alg_cons", "Constraint layer")
    r.p("The constraint layer turns the farm’s rules into a *compatibility matrix* (which crop may stand on which "
        "shelf) and a *conflict graph* (which crops may not share a shelf). This is the pruning before optimisation that "
        "the proposal asks for: the knapsack may not choose more trays of a crop than fit by area on its compatible "
        "shelves, and the packers only try compatible, conflict-free shelves, so no invalid configuration is generated "
        "and none has to be repaired. On the default farm %d of %d crop-shelf pairs are compatible, so %s%% of the "
        "assignments are eliminated before any optimiser runs, and the conflict graph has %d edges (%d from explicit "
        "rules, %d from crop heights). Colouring the graph gives a necessary condition for packing: the crops on one "
        "shelf must be pairwise non-conflicting, so if the selected crops need more colours than there are shelves, no "
        "packing exists. Three algorithms colour the graph; on the sample farm Welsh-Powell, DSATUR and the exact search "
        "all use %d colours."
        % (F.pairs_ok, F.pairs_total, n1(F.pruned), F.edges, F.edges_explicit, F.edges_height, F.colours[0]))
    algo(r, md, "3.1", "a31")
    algo(r, md, "3.2", "a32")
    algo(r, md, "3.3", "a33")
    algo(r, md, "3.4", "a34")

    # ---- 7.2 forecasting
    chosen = {name: int(c("Forecast", name, "chosen for crops")) for name in
              ("seasonal naive", "moving average", "simple exponential smoothing", "Holt-Winters")}
    r.h(2, "alg_fore", "Demand forecasting")
    r.p("The forecast supplies the demand d_{c} that limits how many trays of a crop are worth growing. Four methods are "
        "implemented and an automatic rule chooses between them by hold-out error: the last weeks of the history are held "
        "out, each method forecasts them from the weeks before, and the method with the lowest mean absolute error (MAE) "
        "is refitted on the whole history. Contracts are known commitments and are added later; the forecast covers spot "
        "demand only. On the default farm the rule chooses Holt-Winters for %d crops, seasonal naive for %d, moving "
        "average for %d and simple exponential smoothing for %d."
        % (chosen["Holt-Winters"], chosen["seasonal naive"], chosen["moving average"], chosen["simple exponential smoothing"]))
    algo(r, md, "4.1", "a41")
    algo(r, md, "4.2", "a42")
    algo(r, md, "4.3", "a43", skip_problem=True)

    # ---- 7.3 crop selection
    r.h(2, "alg_sel", "Crop selection")
    r.p("Crop selection is model M1 (%s). Three algorithms solve it. Greedy is fast and has no guarantee. Dynamic "
        "programming is exact, but its table grows exponentially with the number of budgets. Branch and bound is exact "
        "when it finishes and is the default; it starts from the greedy solution and cuts a branch when a relaxation "
        "bound shows that it cannot beat the best solution found." % r.sec("m1"))
    algo(r, md, "5.1", "a51")
    algo(r, md, "5.2", "a52")
    algo(r, md, "5.3", "a53")

    # ---- 7.4 packing
    r.h(2, "alg_pack", "Grow-space packing")
    r.p("Packing is model M2 (%s). All three packers are built on one placement primitive, `placeOne`, which puts a "
        "single tray on the first (First-Fit) or the tightest (Best-Fit) place in the row layout where it fits and "
        "never produces an invalid layout. First-Fit and Best-Fit Decreasing call it on the trays sorted by size. The "
        "genetic algorithm searches over the order of the trays and over the policy, with `placeOne` as its decoder, so "
        "every individual is a valid packing." % r.sec("m2"))
    algo(r, md, "6.1", "a61")
    algo(r, md, "6.2", "a62")

    # ---- 7.5 scheduling
    r.h(2, "sched", "Harvest and replant scheduling")
    r.p("Scheduling is model M3 (%s), one crop at a time. The slot arithmetic comes first, because the knapsack values "
        "need the number of slots that the contracts require; three schedulers then place the batches. Earliest "
        "deadline first is simple and fast, but it never keeps a slot empty for a contract lot that is not yet "
        "released; the lookahead variant does, when a spot batch would cost a contract delivery that was otherwise still "
        "achievable." % r.sec("m3"))
    algo(r, md, "7.1", "a71")
    algo(r, md, "7.2", "a72")
    algo(r, md, "7.3", "a73")
    algo(r, md, "7.4", "a74")

    # ---- 7.6 summary and hardness
    r.h(2, "hard", "Complexity summary and hardness")
    rows = [[row[1], row[2], row[3], row[4], row[5]] for row in complexity_md.summary_table()]
    r.table("tab:bounds", "Time and space bounds of every algorithm, as built (derived from the code in docs/complexity-analysis.md).",
            ["Algorithm", "Paradigm", "Time", "Space", "Optimal?"], rows, [3.3, 2.4, 4.6, 2.9, 3.2], size=8,
            source="docs/complexity-analysis.md, section 1.")
    r.p("**Hardness.** The results below are known; each is stated as its source gives it.", keep_next=True)
    dp_ratio = _dp_ratio(D)
    r.bullets([
        "**Knapsack.** The 0/1 knapsack problem is NP-hard [Karp 1972], but only in the weak sense: with one budget the "
        "table of dynamic programming costs O(N·W), pseudo-polynomial. With several budgets (the multi-dimensional "
        "knapsack) the problem is strongly NP-hard when the number of budgets is part of the input [Bertsimas-Demir 2002]; "
        "the standard text is [Kellerer et al. 2004, chapter 9]. For a fixed number m of budgets the table is "
        "pseudo-polynomial, O(N·Π (W_{r} + 1)), but it is a product over the budgets and grows exponentially "
        "with m: the benchmark measures a factor of about %s per added budget (%s). M1 contains the multi-dimensional "
        "knapsack, so it is at least as hard." % (n0(dp_ratio), r.sec("bench")),
        "**Packing.** Bin packing is strongly NP-hard [Garey-Johnson 1979]. M2 contains it: if every tray is as deep as a "
        "shelf, each shelf holds one row and placing trays is one-dimensional bin packing along the width. It also contains "
        "graph colouring: with trays so small that capacity never binds, finding the fewest shelves in use under the "
        "conflicts is colouring the conflict graph. No polynomial algorithm is expected to pack optimally, so First-Fit "
        "Decreasing, Best-Fit Decreasing and the genetic algorithm are heuristics.",
        "**Colouring.** The chromatic number is NP-hard [Karp 1972]. That is why Welsh-Powell and DSATUR are heuristics "
        "and the exact search is exponential and limited to small graphs.",
        "**Scheduling.** Single-machine non-preemptive scheduling with release times and deadlines (equivalently, "
        "minimising maximum lateness with release times) is strongly NP-hard (*Elements of Scheduling*, Theorem 3.11; "
        "the classification of machine scheduling problems is [Lenstra et al. 1977]). The lots of one crop are a special "
        "case, with equal lengths p and windows of one width f; **we do not claim that this special case is NP-hard**, "
        "and nothing in this report depends on it. The exact search is exponential because it is a plain search, and it "
        "is limited to 22 lots for that reason."])


def _dp_ratio(D):
    """Average factor by which the DP's time grows per added budget (suite knap_dims)."""
    times = [t for _, t, _ in D.bench.suites["knap_dims"].series("dp", x="param")]
    ratios = [b / a for a, b in zip(times, times[1:])]
    return sum(ratios) / len(ratios)


# ------------------------------------------------------------------ 8 implementation

def implementation(r, D, F):
    r.h(1, "impl", "Implementation")
    r.h(2, "lang", "Language and structure")
    r.p("The program is written in C++17 and builds with CMake and Ninja; the toolchain used here is GCC %s (MinGW-w64) "
        "on Windows. It is split into a library, `farm_core`, that holds everything algorithmic, and three programs "
        "built on it: `farm_cli`, a command-line tool (generate and validate scenarios, run the pipeline, compare every "
        "variant of every stage, run the benchmarks); `farm_app`, the desktop application; and `farm_tests`. The "
        "library compiles without any GUI header, and the command-line tool and the tests build on a machine with no "
        "OpenGL." % D.bench.header["Compiler"].replace("GCC ", ""))
    r.p("The code follows a deliberately plain style so that every line can be explained: structs and free functions, "
        "`std::vector`, no inheritance hierarchies and no template metaprogramming. Each algorithm is a function of its "
        "own with a header comment that states what it computes and its time complexity.")
    r.p("**Every algorithm is written by hand.** The library `farm_core` uses only the C++ standard library; the one "
        "exception is the JSON header inside `json_io.cpp`. There is no solver, optimisation, graph, statistics or "
        "machine-learning library anywhere. Random numbers come from a hand-written SplitMix64 generator, because the "
        "standard distributions are implementation-defined.")

    r.h(2, "libs", "Third-party libraries")
    r.p("Five libraries are linked into the executables. None of them is algorithmic: they provide the window, the "
        "charts, JSON and PNG output. CMake downloads them at pinned versions; none is copied into the repository.")
    r.table("tab:libs", "Third-party libraries.", ["Library", "Version", "Used for", "Licence"],
            D.third_party, [4.0, 2.2, 7.4, 2.8], size=9, source="README.md.")
    r.p("The project’s own code is released under the MIT licence.")

    r.h(2, "testing", "Testing")
    r.p("`farm_tests` runs **%d tests with %d failed checks** (a CTest run adds two more entries: the command line on the "
        "flat sample farm must print the reference numbers, and a quick benchmark run must finish). The tests are of four "
        "kinds." % (D.tests, D.failed_checks))
    r.bullets([
        "**Golden cases.** The plan’s Appendix A lists six cases, G1 to G6: small instances for each algorithm, with "
        "the exact expected answers (for example the crown graph, on which Welsh-Powell needs three colours and DSATUR "
        "two), and the flat sample farm end to end. Their numbers were computed by `plan/reference_model.py`, an "
        "executable specification written in Python before the C++ code; the tests assert them.",
        "**Property tests.** On seeded random instances the algorithms are compared with brute-force oracles: every "
        "colouring is proper and the exact count is at most both heuristics; dynamic programming and branch and bound "
        "equal a brute force over all subsets; every layout is accepted by `validatePacking` and the genetic algorithm is "
        "never worse than First-Fit or Best-Fit Decreasing; every schedule is valid and the exact search equals a "
        "brute force over all start times.",
        "**Edge cases and input checks.** Empty inputs, a tray larger than every shelf, a crop compatible with no shelf, "
        "zero budgets, and a scenario file with missing or impossible values, which must be refused with a readable message.",
        "**Pipeline and files.** The stages together, the refill step, the warnings, the JSON round trip, and the "
        "benchmark harness."])
    r.p("Results on the flat farm match the reference model: the knapsack optimum is %s PKR per week, proven in %s nodes, and "
        "First-Fit Decreasing places %d of %d trays (%s)." % (n1(F.flat_relaxed), n0(F.flat_nodes), F.flat_ffd_placed,
                                                              F.flat_trays, r.ref("tab:flat")))

    r.h(2, "repro", "Determinism and reproducibility")
    same = D.identical_runs
    r.p("Results are deterministic: the same input and seed give the same output on every run and every machine. The "
        "program uses no `std::rand`, no time-based seed and no implementation-defined distribution, and every sort uses a "
        "comparator that is a total order. %s The program that produced the numbers of this report was built from "
        "repository commit %s." % (
            "When this report was generated, two runs of `farm_cli run` on the default scenario gave identical result files "
            "apart from the timing fields." if same else
            "(The determinism check of the generator was not run.)", D.commit))

    r.h(2, "app", "The desktop application")
    r.p("`farm_app` is a Dear ImGui application. A sidebar holds the scenario, the five weekly budgets (each can be "
        "switched off), the horizon and the algorithm of every stage; seven tabs show the results: Overview (headline "
        "numbers, the selection table, trays per crop), Layout (every shelf drawn to scale with its trays), Schedule (a "
        "timeline with one row per slot), Forecast (history, hold-out predictions and the chosen forecast), Constraints "
        "(the conflict graph and the compatibility table), Compare (one table per stage and the benchmark charts) and "
        "Data (crops, shelves, contracts). The pipeline runs on a worker thread, so the window stays responsive. Two "
        "special modes serve the documentation: `--screenshot` writes every figure of this report from the running "
        "application, and `--selftest` drives it through a short script and exits with code 0 when every step works. "
        "%s shows the screens." % r.ref("app:shots"))
