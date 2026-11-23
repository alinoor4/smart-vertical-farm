"""Report, part D: discussion, conclusion, references and the appendices."""
import complexity_md
from docx.shared import Cm
from farm_data import n0, n1, n2
from facts import PACKERS
from report_c import claim, ms

# first words of each entry of docs/complexity-analysis.md section 10 -> the key used in the text
REFERENCE_KEYS = [
    ("R. M. Karp", "Karp 1972"), ("M. R. Garey", "Garey-Johnson 1979"), ("D. Bertsimas", "Bertsimas-Demir 2002"),
    ("J. K. Lenstra", "Lenstra et al. 1977"), ("*Elements of Scheduling*", "Elements of Scheduling"),
    ("D. J. A. Welsh", "Welsh-Powell 1967"), ("D. Br", "Brélaz 1979"), ("C. C. Holt", "Holt 2004"),
    ("P. R. Winters", "Winters 1960"), ("H. Kellerer", "Kellerer et al. 2004"), ("J. Kleinberg", "Kleinberg-Tardos"),
    ("T. H. Cormen", "CLRS"), ("E. G. Coffman", "Coffman et al. 1980"), ("G. D", "Dósa 2007"),
    ("E. Falkenauer", "Falkenauer 1996"), ("L. Davis", "Davis 1985")]


def write(r, D, F):
    discussion(r, D, F)
    conclusion(r, D, F)
    references(r, D, F)
    appendices(r, D, F)


# ------------------------------------------------------------------ discussion

def discussion(r, D, F):
    S = D.bench.suites
    c = F.cmp
    r.h(1, "discussion", "Discussion")
    r.h(2, "wins", "When each algorithm wins")
    wp = S["colouring"].series("wp", param=50)[-1]
    ds = S["colouring"].series("dsatur", param=50)[-1]
    f = S["forecast"]
    hw5 = [v for p, _, v in f.series("hw", x="param")][0]
    nv5 = [v for p, _, v in f.series("naive", x="param")][0]
    ses30 = [v for p, _, v in f.series("ses", x="param")][-1]
    hw30 = [v for p, _, v in f.series("hw", x="param")][-1]
    ga_ms, ffd_ms = c("Packing", PACKERS[2], "ms"), c("Packing", PACKERS[0], "ms")
    chosen = {m: int(c("Forecast", m, "chosen for crops")) for m in ("seasonal naive", "moving average", "simple exponential smoothing", "Holt-Winters")}
    claim(wp[1] < ds[1] and ds[2] < wp[2] and hw5 < nv5 and ses30 < hw30, "the winners named in the discussion table")
    r.table("tab:wins", "Which algorithm to choose, and the measured evidence.",
            ["Stage", "Choice", "Evidence"],
            [["Colouring",
              ["**Welsh-Powell** when speed matters; **DSATUR** when fewer colours matter; the **exact search** for at most 20 vertices."],
              ["At 400 vertices and 50%% edge probability Welsh-Powell needs %s colours in %s ms, DSATUR %s colours in %s ms. On the sample farm all three find %d colours and the exact search proves it in %d nodes." %
               (n0(wp[2]), ms(wp[1]), n0(ds[2]), ms(ds[1]), F.colours[0], F.exact_nodes)]],
             ["Forecasting",
              ["**Holt-Winters** for a clear season with little noise; a **moving average** or **exponential smoothing** for noisy demand; choose per crop by hold-out error."],
              ["At 5%% noise Holt-Winters has an error of %s%% against %s%% for seasonal naive; at 30%% noise smoothing (%s%%) beats Holt-Winters (%s%%). On the default farm the rule picks Holt-Winters for %d crops, seasonal naive for %d, a moving average for %d." %
               (n2(hw5), n2(nv5), n2(ses30), n2(hw30), chosen["Holt-Winters"], chosen["seasonal naive"], chosen["moving average"])]],
             ["Crop selection",
              ["**Branch and bound** by default; **greedy** when a quick answer is enough; **dynamic programming** only for one or two budgets."],
              ["Branch and bound proves the optimum in %s nodes and %s ms; greedy reaches %s%% in %s ms; dynamic programming grows by a factor of 25 per budget and reaches %s%% when its grid is coarsened." %
               (n0(F.bnb_nodes), ms(c("Crop selection", "branch and bound", "ms")), n2(F.greedy_pct), ms(c("Crop selection", "greedy", "ms")), n1(F.dp_pct))]],
             ["Packing",
              ["The **genetic algorithm** when space is tight and the value of every tray matters; **First-Fit Decreasing** for interactive use and loose farms."],
              ["Of %d selected trays the genetic algorithm places %d, First-Fit %d, Best-Fit %d. The genetic algorithm takes about %s times as long as First-Fit. With trays filling only 60%% of the shelves every packer places everything. Best-Fit never won on the sample data; it stays because neither policy dominates the other in general." %
               (int(F.selected_by_bnb), F.placed[PACKERS[2]], F.placed[PACKERS[0]], F.placed[PACKERS[1]], n0(ga_ms / ffd_ms))]],
             ["Scheduling",
              ["**Lookahead** when contracts carry penalties; plain **EDF** only when spot sales are all that matters; the **exact search** to validate on small instances."],
              ["Lookahead delivers %d of %d contract lots on time against %d for EDF, at the cost of %d spot lots, and earns %s PKR more over the plan. EDF serves %d spot lots against %d." %
               (F.look["hard"], F.look["hard_total"], F.edf["hard"], F.spot_given_up, n0(F.net_gain), F.edf["soft"], F.look["soft"])]]],
            [2.6, 5.6, 8.2], size=8.5, first_col_bold=True, source="farm_cli compare, bench/results/summary.md.")

    r.h(2, "tradeoffs", "Trade-offs")
    shares = F.by_packer
    claim(shares[PACKERS[2]]["share"] > shares[PACKERS[0]]["share"] > shares[PACKERS[1]]["share"], "plan share: genetic > First-Fit > Best-Fit")
    packer_range = max(v["net"] for v in shares.values()) - min(v["net"] for v in shares.values())
    claim(packer_range > F.net_gain, "the packer moves the net revenue more than the scheduler does")
    r.bullets([
        "**Exactness against time.** Dynamic programming is exact but exponential in the number of budgets; branch and bound is exact "
        "and, on these instances, fast, but its worst case is exponential and it can stop at its limit; the genetic algorithm "
        "gives the best layouts and costs about %s times the time of First-Fit Decreasing." % n0(ga_ms / ffd_ms),
        "**Contracts against spot sales.** The lookahead scheduler gives up %d spot lots to protect %d contract lots. Whether "
        "that pays depends on the penalty rate: with the sample penalties it earns %s PKR over the plan." % (F.spot_given_up, F.edf_missed, n0(F.net_gain)),
        "**Packing matters most.** The final plan reaches %s%% of the relaxed optimum with the genetic packer, %s%% with First-Fit "
        "Decreasing and %s%% with Best-Fit Decreasing. The net revenue over the plan differs by %s PKR between the best and "
        "the worst packer, against %s PKR between the two schedulers on one plan."
        % (n2(shares[PACKERS[2]]["share"]), n2(shares[PACKERS[0]]["share"]), n2(shares[PACKERS[1]]["share"]), n0(packer_range), n0(F.net_gain)),
        "**Fidelity against tractability.** A row layout, a static set of slots and one crop per slot make the model small enough "
        "to solve exactly in parts and to explain; each of them excludes something a real farm does (below)."])

    r.h(2, "limits", "Limitations")
    r.p("The following limits apply to the results of this report.", keep_next=True)
    r.bullets([
        "**The data are synthetic.** No real farm data exists. The sample farm was tuned so that several budgets nearly bind, "
        "space is tight and plain EDF misses contracts. Conclusions about which algorithm wins are conclusions about this "
        "model and these generated instances.",
        "**Scope.** The following were considered and left out on purpose: growing a different crop in a slot after a harvest "
        "(crop rotation), or filling an idle slot with another crop, because it would make the conflict constraints depend on "
        "time; conflicts between shelves above or below each other (a conflict means “may not share a shelf”); true 3D "
        "packing, because trays are never stacked and the vertical dimension is handled by the clearance constraint; free-form "
        "rectangle packing (packing is row based); labour peaks at planting or harvest, crops harvested several times per "
        "cycle, and batches already growing at time zero; editing crops, shelves and contracts inside the application (they "
        "are edited in the JSON file and reloaded); native file dialogs, installers, databases, user accounts, networking, "
        "sensors and localisation.",
        "**Measurement.** All times come from one machine. Several exponents rest on two to five points, and the exponent of "
        "branch and bound describes a local trend, not its exponential worst case. The three scheduling suites contain no "
        "randomness (the five seeds repeat one instance), and the exact search was measured only up to 20 lots.",
        "**Theory.** The minimality of the slot count `minSlotsForHardLots` is not proved for flexible harvest windows (it "
        "is sufficient by construction and equals the interval depth for fixed starts). The optimality of the exact "
        "scheduler rests on an exchange argument that uses the structure of the lots; no NP-hardness is claimed for the "
        "scheduling special case. The poor result of dynamic programming on the farm is by design.",
        "**Application.** The desktop application was checked through screenshots of every tab, an automatic self-test "
        "and by hand on one Windows PC; no automated test drives the mouse, and display scaling above 100% was not tried.",
        "**Differences from the plan.** `docs/complexity-analysis.md` lists nine places where the code differs from the "
        "bounds or steps stated in the implementation plan; none of them changes a plan."])


# ------------------------------------------------------------------ conclusion

def conclusion(r, D, F):
    r.h(1, "concl", "Conclusion and future work")
    r.p("The project joins five algorithmic components (a constraint graph, demand forecasting, a multi-dimensional "
        "knapsack, row-based packing and harvest scheduling) into one pipeline that decides what to plant, where to put it and "
        "when to harvest. All %d algorithms are written by hand in C++17, analysed, tested (%d tests) and measured. On the "
        "sample farm the final plan is worth %s%% of a proven upper bound and delivers all %d contract lots on time. Three "
        "findings stand out. First, the packer matters most for the value of the plan, and the genetic "
        "algorithm earns its extra time only when space is tight. Second, lookahead scheduling trades %d spot lots for %d "
        "contract lots and a gain of %s PKR; plain earliest deadline first cannot see that a contract lot is about to be "
        "released. Third, dynamic programming is exact but its table grows by a factor of 25 per budget, which makes it "
        "unusable for the farm problem, while branch and bound solves it in milliseconds."
        % (F.algorithms, D.tests, n2(F.plan_share), F.hard_total, F.spot_given_up, F.edf_missed, n0(F.net_gain)))
    r.p("**Future work.**", keep_next=True, after=2)
    r.bullets([
        "Lift the scope limits one at a time: crop rotation and refilling idle slots with another crop (which makes the "
        "conflict constraints depend on time), labour peaks, and several harvests per cycle.",
        "Replace generated data by data from a real farm, and calibrate the tray values, the contract penalties and the "
        "labour model against it.",
        "Prove the minimality of the slot count for flexible windows, or replace it by an exact method; compare the "
        "heuristics with an integer-programming bound on small instances.",
        "Speed up the genetic algorithm: the decodings of one generation are independent and can run in parallel.",
        "Re-plan on a rolling horizon as new orders arrive, instead of one plan for a fixed horizon."])


# ------------------------------------------------------------------ references

def references(r, D, F):
    r.h(1, "refs", "References")
    entries = complexity_md.references()
    keyed = []
    for entry in entries:
        for prefix, key in REFERENCE_KEYS:
            if entry.startswith(prefix):
                keyed.append((key, entry))
                break
        else:
            raise SystemExit("complexity-analysis.md has a reference that docs/tools/report_d.py does not know: " + entry[:60])
    keyed.sort(key=lambda item: item[0].lower().replace("é", "e").replace("ó", "o"))
    r.p("References are cited in the text by author and year in square brackets.", size=10, after=4)
    seoul = ("**[Seoul Accord]** Seoul Accord, *Section D - Graduate Attributes*, table D.4.1 “Range of Problem Solving”. "
             "www.seoulaccord.org. The supplied pages carry no date; they were supplied to the team on 13 November 2026.")
    paragraph = r.p(seoul, after=3, indent_cm=0.8)
    paragraph.paragraph_format.first_line_indent = Cm(-0.8)
    for key, entry in keyed:
        paragraph = r.p("**[%s]** %s" % (key, entry), after=3, indent_cm=0.8)
        paragraph.paragraph_format.first_line_indent = Cm(-0.8)
    r.p("What was checked for each reference (the record, the statement used, or only a secondary source) is recorded in "
        "section 10 of `docs/complexity-analysis.md`; a reference marked there as secondary was not read in full.", size=10, before=6)


# ------------------------------------------------------------------ appendices

def appendices(r, D, F):
    r.appendix("app:build", "A", "How to build and run")
    r.p("The project builds on 64-bit Windows 10 or 11 with MinGW-w64 g++, CMake and Ninja (one winget package provides all "
        "three) and Git for Windows, and needs internet access for the first build, which downloads the third-party sources. "
        "The desktop application needs a graphics driver with OpenGL 3.0; the library, the command-line tool and the tests "
        "need no graphics. The complete instructions are in `README.md`.")
    r.code("git clone <repository-url>\n"
           "cd <the cloned folder>\n"
           ".\\build.ps1 -Test                     # configure, build everything, run the tests\n"
           "\n"
           ".\\build\\farm_app.exe                  # the desktop application\n"
           ".\\build\\farm_cli.exe run --scenario data\\default_scenario.json --packer ga\n"
           ".\\build\\farm_cli.exe compare --scenario data\\default_scenario.json\n"
           ".\\build\\farm_cli.exe bench --quick   # the benchmark suites (without --quick: about 1.5 minutes)")
    r.p("**Reproducing this report.** The report and the slides are generated by scripts in `docs/tools/` (documentation "
        "tooling in Python; the product is C++ only). The scripts run the program, read the files it writes and insert every "
        "number from them. After a change to the program or the benchmarks, rebuild and run again:")
    r.code("uv venv .venv-docs\n"
           "uv pip install --python .venv-docs python-docx python-pptx\n"
           ".venv-docs\\Scripts\\python docs\\tools\\build_report.py\n"
           ".venv-docs\\Scripts\\python docs\\tools\\build_slides.py")
    r.p("The numbers of this report were produced by repository commit %s." % D.commit, size=10)

    r.appendix("app:shots", "B", "Screenshots of the application")
    r.p("The seven tabs of the desktop application on the default scenario, in the light theme. Each was written by "
        "`farm_app --screenshot`.", keep_next=True)
    shots = [("ui_overview", "the headline numbers, the table of selected trays and the relaxed selection against the final plan"),
             ("ui_layout", "every shelf to scale with its trays, and the packer comparison"),
             ("ui_schedule", "the timeline of batches, the delivery table and the weekly resource use"),
             ("ui_forecast", "history, hold-out predictions, the chosen forecast and the error table"),
             ("ui_constraints", "the conflict graph, the pruning numbers and the compatibility table"),
             ("ui_compare", "one table per stage, from the function compareAll"),
             ("ui_data", "the crops, shelves and contracts of the scenario")]
    for name, text in shots:
        r.figure("fig:" + name, D.fig(name), "The %s tab: %s." % (name[3:].capitalize(), text), source="farm_app --screenshot.")

    r.appendix("app:contrib", "C", "Individual contributions")
    r.callout(["**To be completed by the team.** This table is left empty on purpose: the authors of this report did not guess "
               "who did what. Each member should describe their own contribution."], fill="FFF4CE", edge="E0A800")
    r.table("tab:contrib", "Individual contributions.", ["Team member", "Roll number", "Contribution"],
            [["", "", ""], ["", "", ""], ["", "", ""], ["", "", ""]], [4.6, 3.0, 8.8], size=10, zebra=False, min_row_cm=1.4)
