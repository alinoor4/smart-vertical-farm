"""Builds docs/slides/Smart_Vertical_Farm_Slides.pptx. Documentation tooling only: the product is C++.

    .venv-docs\\Scripts\\python docs\\tools\\build_slides.py [--no-refresh] [--build-dir build] [--out FILE]

Like build_report.py, the script reads the files that the program wrote and types no measured value
by hand. Every slide has speaker notes. It stops with a message when a quoted Seoul Accord sentence
is missing, a slide has no notes, or a statement no longer matches the data.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ccp
import complexity_md
import facts
import farm_data
from farm_data import n0, n1, n2, n3
from facts import PACKERS, RESOURCES
from pptx.enum.shapes import MSO_SHAPE
from pptx.util import Inches
from pptx_kit import ACCENT, BLUE, BROWN, DARK, GREEN2, GREY, LIGHTGREY, PALE, RED, TEXT, WHITE, Deck, rgb, slide_text

ROOT = farm_data.ROOT
QUOTED = (1, 2, 3, 8)               # the characteristics quoted on slide 3
FOOTER = "Smart Vertical Farm  ·  Design and Analysis of Algorithms"


def claim(condition, text):
    if not condition:
        raise SystemExit("A statement of the slides no longer matches the data: %s\nRevise it in build_slides.py." % text)


def build(D, F, team):
    deck = Deck(FOOTER)
    S = D.bench.suites
    e = F.exp
    sc = D.scenario
    cmp = F.cmp
    cycles = [c["cycleWeeks"] for c in sc["crops"]]
    ffd, bfd, ga = (F.placed[p] for p in PACKERS)
    share = {p: F.by_packer[p]["share"] for p in PACKERS}
    net = {p: F.by_packer[p]["net"] for p in PACKERS}
    ga_ms, ffd_ms = cmp("Packing", PACKERS[2], "ms"), cmp("Packing", PACKERS[0], "ms")
    claim(ga > ffd > bfd and F.look["hard"] == F.look["hard_total"] and F.edf_missed > 0, "the packer and scheduler ranking of slides 9 and 10")
    claim(F.dp_pct < 50 and F.greedy_pct > 95, "slide 8: the DP collapses and greedy is within 5%")
    claim(share[PACKERS[2]] > share[PACKERS[0]] > share[PACKERS[1]], "slide 9: plan share genetic > First-Fit > Best-Fit")

    # ------------------------------------------------------------------ 1 title
    title, _, subtitle = " ".join(team["title_lines"]).partition(" — ")
    s = deck.slide(
        title,
        "About 30 seconds. Say what the project is in one sentence: a planner for an indoor vertical farm that decides what to "
        "plant, where to put it and when to harvest, built from five algorithmic components that we wrote ourselves in C++. "
        "Name the four team members and the instructor. Then give the road map: the problem, why it counts as a Complex "
        "Computing Problem, the pipeline, one slide per component with measured results, how the algorithms scale, the "
        "limits of the work, and a short live demo. Roughly one minute per slide, fourteen slides.",
        number=False, title_size=46, title_color=WHITE, bar=False)
    band = deck.box(s, 0, 0, 13.333, 4.35, fill=DARK, shape=MSO_SHAPE.RECTANGLE)
    deck.send_to_back(s, band)
    s.shapes.title.left, s.shapes.title.top, s.shapes.title.width, s.shapes.title.height = Inches(0.8), Inches(1.25), Inches(11.7), Inches(1.2)
    deck.text(s, 0.8, 0.55, 11.7, 0.5, "%s  ·  %s" % (team["course"], team["kind"]), size=20, color="A9D6A9", bold=True)
    deck.text(s, 0.8, 2.55, 11.7, 1.4, subtitle, size=28, color=WHITE)
    members = team["members"]
    half = (len(members) + 1) // 2
    deck.text(s, 0.8, 4.75, 4, 0.4, "Submitted by", size=16, color=GREY, bold=True)
    for column, group in enumerate((members[:half], members[half:])):
        deck.text(s, 0.8 + column * 4.3, 5.2, 4.2, 1.6, ["**%s**   %s" % (m["name"], m["roll"]) for m in group], size=18, space_after=8)
    deck.text(s, 9.7, 4.75, 3.2, 0.4, team["advisor_label"].rstrip(":"), size=16, color=GREY, bold=True)
    deck.text(s, 9.7, 5.2, 3.2, 0.8, "**%s**" % team["advisor"], size=18)

    # ------------------------------------------------------------------ 2 the problem
    s = deck.slide(
        "The operator’s problem: what to plant, where to put it, when to harvest",
        "About one minute. A vertical farm grows crops on stacked shelves indoors, so space, light, water and labour are fixed "
        "and expensive. Every planning period the operator has to make three linked decisions. What to plant: %d candidate crops "
        "compete for the same five resources (labour, water, nutrient solution, energy and floor area). Where to put it: shelves "
        "differ in height, light and humidity, so a crop needs a compatible shelf, and some crops must never share one. When to "
        "harvest: crops take between %d and %d weeks to grow, and contract customers expect a delivery every week; a missed "
        "delivery costs a penalty. No formula gives the best plan, so we built a planner with algorithms we wrote ourselves."
        % (F.crop_count, min(cycles), max(cycles)))
    cards = [("What to plant?", "%d candidate crops compete for the same limited resources, so not every crop can be grown." % F.crop_count),
             ("Where to put it?", "Shelves differ in height, light and humidity. A crop needs a compatible shelf, and some crops may never share one."),
             ("When to harvest?", "Crops need %d to %d weeks to grow. Contract customers expect a delivery every week; a missed one costs a penalty." % (min(cycles), max(cycles)))]
    for i, (head, body) in enumerate(cards):
        deck.card(s, 0.62 + i * 4.1, 1.6, 3.9, 3.0, head, body, body_size=18, title_size=22)
    deck.text(s, 0.62, 4.9, 12.1, 0.5, "Five limited resources decide how much of all this is possible:", size=20, bold=True, color=DARK)
    for i, name in enumerate(RESOURCES):
        pill = deck.box(s, 0.62 + i * 2.45, 5.55, 2.2, 0.8, fill=[ACCENT, BLUE, BROWN, GREEN2, DARK][i], radius=0.3)
        deck.text(s, 0, 0, 0, 0, name, size=20, color=WHITE, bold=True, align="c", anchor="m", shape=pill)

    # ------------------------------------------------------------------ 3 why complex
    s = deck.slide(
        "Why it is a Complex Computing Problem",
        "About one and a half minutes. The Seoul Accord defines a Complex Computing Problem as one with some or all of nine "
        "characteristics; the quotations on this slide are its exact words (table D.4.1). We claim four of them strongly. "
        "One, conflicting requirements: on the sample farm plain earliest deadline first delivers %d of %d contract lots on "
        "time, while the lookahead scheduler delivers all of them but serves %d fewer spot lots. Two, depth of analysis: no "
        "formula gives the plan, so we designed the models, and the optimal crop mix of %d trays cannot be packed as chosen. "
        "Three, depth of knowledge: %d algorithms, each with a derived bound and a measured growth rate. Eight, interdependence: "
        "the five sub-problems feed each other, so changing the packer changes the net revenue by %s PKR. The other "
        "characteristics are met only in part, and the report says where."
        % (F.edf["hard"], F.edf["hard_total"], F.spot_given_up, F.selected_by_bnb, F.algorithms, n0(max(net.values()) - min(net.values()))))
    evidence = {
        1: "EDF: %d of %d contract lots on time. Lookahead: all %d, at the cost of %d spot lots." % (F.edf["hard"], F.edf["hard_total"], F.look["hard_total"], F.spot_given_up),
        2: "The best mix of %d trays cannot be packed as chosen: the packers place %d, %d and %d." % (F.selected_by_bnb, ffd, bfd, ga),
        3: "%d algorithms, each with a derived bound and a measured growth rate." % F.algorithms,
        8: "Same farm, three packers: net revenue from %s to %s PKR." % (n0(min(net.values())), n0(max(net.values()))),
    }
    by_number = {n: (name, wording) for n, name, wording in ccp.CHARACTERISTICS}
    for index, number in enumerate(QUOTED):
        name, wording = by_number[number]
        x, y = 0.62 + (index % 2) * 6.15, 1.5 + (index // 2) * 2.7
        deck.box(s, x, y, 5.95, 2.55, fill=PALE)
        deck.text(s, x + 0.2, y + 0.1, 5.6, 0.45, "%d   %s" % (number, name), size=18, bold=True, color=DARK)
        deck.text(s, x + 0.2, y + 0.6, 5.6, 1.1, "“%s”" % wording, size=15, italic=True)
        deck.text(s, x + 0.2, y + 1.72, 5.6, 0.8, evidence[number], size=14, color=ACCENT, bold=True)

    # ------------------------------------------------------------------ 4 pipeline
    s = deck.slide(
        "One pipeline: forecast, constrain, select, pack, refill, schedule",
        "About one minute. The pipeline answers the three questions in order. The forecast gives the demand for each crop. The "
        "constraint layer builds the compatibility matrix and the conflict graph. The lots stage turns deliveries into jobs. "
        "The knapsack decides how many trays of each crop to grow. Packing places the trays on shelves, and refill spends any "
        "budget the packer could not use. Scheduling decides when each batch is planted. The key idea is relax, pack, refill: "
        "the knapsack sees only the total floor area, so its optimum of %s PKR per week is an upper bound on every plan. The "
        "packer then fits the trays to real shelves, and the refill step re-spends the freed budget. We tried looping back "
        "from packing to the knapsack, but it never settled, so the pipeline runs once." % n1(F.relaxed))
    steps = [("Forecast", "spot demand per crop and week", ACCENT), ("Constraints", "compatibility matrix, conflict graph", BLUE),
             ("Lots", "deliveries as jobs, slots contracts need", BROWN), ("Knapsack", "trays per crop (relaxed optimum)", ACCENT),
             ("Packing", "placements and unplaced trays", BLUE), ("Refill", "final trays per crop = slots", GREEN2),
             ("Scheduling", "batches; deliveries met or missed", BROWN)]
    for i, (head, note, colour) in enumerate(steps):
        x = 0.62 + i * 1.77
        box = deck.box(s, x, 1.65, 1.5, 0.85, fill=colour)
        deck.text(s, 0, 0, 0, 0, head, size=16, color=WHITE, bold=True, align="c", anchor="m", shape=box)
        deck.text(s, x - 0.05, 2.6, 1.6, 0.9, note, size=12, color=GREY, align="c")
        if i < len(steps) - 1:
            arrow = s.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, Inches(x + 1.52), Inches(1.9), Inches(0.23), Inches(0.35))
            arrow.fill.solid()
            arrow.fill.fore_color.rgb = rgb(GREY)
            arrow.line.fill.background()
    ideas = [("1  Relax", "The knapsack sees only the total floor area. Its proven optimum, %s PKR per week, bounds every plan." % n1(F.relaxed)),
             ("2  Pack", "Trays must fit real shelves, on compatible shelves, without conflicting neighbours. The genetic packer places %d of %d." % (ga, F.selected_by_bnb)),
             ("3  Refill", "The budget freed by trays that did not fit is spent again. The final plan is worth %s%% of the bound." % n2(F.plan_share))]
    for i, (head, body) in enumerate(ideas):
        deck.card(s, 0.62 + i * 4.1, 3.85, 3.9, 2.85, head, body, body_size=17, title_size=22)

    # ------------------------------------------------------------------ 5 sample farm
    area = F.area
    s = deck.slide(
        "The sample farm: %d shelves, %d crops, %d contracts, five budgets" % (F.shelf_count, F.crop_count, F.contract_count),
        "About one minute. All data are generated from a seed, for a Pakistani market and in PKR, because no real farm data "
        "exists. The farm has %d racks of four levels, so %d shelves with %s square centimetres in all. There are %d crops, from "
        "lettuce to cherry tomato, five fictional contract customers with weekly deliveries and penalties, and weekly budgets "
        "of %s labour hours, %s litres of water, %s litres of nutrient solution and %s kilowatt-hours of energy, plus the floor "
        "area. The picture shows the final plan on the shelves: every tray is drawn to scale and labelled with its crop. "
        "A second scenario with constant demand exists so that the results can be checked against an independent reference."
        % (F.rack_count, F.shelf_count, n0(area), F.crop_count, n1(F.budget["labor"]), n0(F.budget["water"]), n1(F.budget["nutrient"]), n0(F.budget["energy"])))
    deck.picture(s, D.fig("fig_layout"), 0.62, 1.55, 8.4, 4.3)
    deck.caption(s, 0.62, 5.9, 8.4, "The final plan on all %d shelves, to scale. Source: farm_app --screenshot." % F.shelf_count, size=12)
    facts_ = [("%d shelves" % F.shelf_count, "%d racks of four levels, %s cm² in all" % (F.rack_count, n0(area))),
              ("%d crops" % F.crop_count, "from lettuce to cherry tomato, %d to %d weeks to grow" % (min(cycles), max(cycles))),
              ("5 budgets", "per week: %s labour h, %s L water, %s L nutrient, %s kWh, plus floor area" % (n1(F.budget["labor"]), n0(F.budget["water"]), n1(F.budget["nutrient"]), n0(F.budget["energy"]))),
              ("%d contracts" % F.contract_count, "weekly deliveries with penalties; %d agronomic conflict rules" % F.listed_conflicts)]
    for i, (big, small) in enumerate(facts_):
        deck.big_number(s, 9.35, 1.5 + i * 1.32, 3.5, big, small, number_size=30, label_size=13)

    # ------------------------------------------------------------------ 6 constraints
    s = deck.slide(
        "%d%% of crop–shelf placements are ruled out before any optimiser runs" % round(F.pruned),
        "About one minute. A crop may stand on a shelf only if its height plus a lamp gap fits the clearance, the shelf's light "
        "and humidity are inside the crop's ranges, and the tray fits the shelf. On the sample farm only %d of %d crop-shelf "
        "pairs pass, so %s percent of the placements are gone before any optimiser runs. Crops that conflict may not share a "
        "shelf: %d from agronomic rules, %d because the taller crop would shade the shorter one. Colouring the conflict graph "
        "gives a necessary condition, because the crops on one shelf must form an independent set. We implemented three "
        "colouring algorithms, Welsh-Powell, DSATUR and an exact backtracking search; all three find %d colours here, and the "
        "exact search proves it in %d nodes."
        % (F.pairs_ok, F.pairs_total, n1(F.pruned), F.edges_explicit, F.edges_height, F.colours[0], F.exact_nodes))
    deck.picture(s, D.crop("fig_constraints", (10, 65, 470, 432), "graph"), 0.62, 1.6, 5.7, 4.5)
    deck.caption(s, 0.62, 6.2, 5.7, "Conflict graph: a node is a crop, its number the DSATUR colour. Source: farm_app --screenshot.", size=12)
    deck.picture(s, D.crop("fig_constraints", (490, 10, 965, 235), "legend"), 6.55, 1.6, 6.15, 2.95)
    deck.picture(s, D.crop("fig_constraints", (6, 495, 910, 797), "table"), 6.55, 4.7, 6.15, 2.1)
    deck.caption(s, 6.55, 6.82, 6.15, "Crops against shelves: a cross is a placement ruled out.", size=12)

    # ------------------------------------------------------------------ 7 forecasting
    run = D.runs["default_ga"]
    holdout = run["forecast"][0]["holdoutLength"]
    chosen = {m: int(cmp("Forecast", m, "chosen for crops")) for m in facts.FORECASTERS}
    s = deck.slide(
        "Four forecasting methods; the best one is chosen per crop by hold-out error",
        "About one minute. Demand limits how many trays of a crop are worth growing, so each crop gets a forecast. We implemented "
        "four methods: seasonal naive, a moving average, simple exponential smoothing and additive Holt-Winters with a grid "
        "search. The selection rule holds out the last %d weeks of the history, lets every method forecast them, and keeps the "
        "method with the lowest mean absolute error; it then refits that method on the whole history. No method wins "
        "everywhere: on the default farm Holt-Winters is chosen for %d crops, seasonal naive for %d and a moving average for %d. "
        "The chart shows lettuce: history in blue, the hold-out predictions of the four methods, the chosen forecast and the "
        "contract level."
        % (holdout, chosen["Holt-Winters"], chosen["seasonal naive"], chosen["moving average"]))
    deck.picture(s, D.crop("fig_forecast", (0, 30, 1600, 782), "plot"), 0.62, 1.55, 8.6, 4.2)
    deck.caption(s, 0.62, 5.8, 8.6, "Lettuce: history, the four hold-out predictions (last %d weeks), the chosen forecast and the contract level. Source: farm_app --screenshot." % holdout, size=12)
    rows = [["Method", "MAE", "Chosen"]]
    for name, label in zip(facts.FORECASTERS, ("Seasonal naive", "Moving average", "Exp. smoothing", "Holt-Winters")):
        rows.append([label, n2(cmp("Forecast", name, "MAE")), "%d crops" % chosen[name]])
    deck.table(s, 9.5, 1.6, [1.45, 0.65, 1.1], rows, size=13, row_h=0.45, align=["l", "r", "r"])
    deck.caption(s, 9.5, 3.95, 3.2, "Mean hold-out error over the crops (kg per week).", size=11)
    deck.text(s, 9.5, 4.5, 3.2, 2.4, ["Hold out the last %d weeks" % holdout, "Score: MAE, RMSE, sMAPE", "Keep the lowest MAE, refit on all history"],
              size=15, bullets=True, space_after=6)

    # ------------------------------------------------------------------ 8 selection
    s = deck.slide(
        "Branch and bound proves the optimum; dynamic programming reaches only %d%%" % round(F.dp_pct),
        "About one and a half minutes. Crop selection is a multi-dimensional knapsack: choose how many trays of each crop so that "
        "labour, water, nutrient, energy and floor area stay within budget, and the value, which depends on the forecast demand "
        "and the contract penalties, is as large as possible. Greedy by value density takes %s ms and reaches %s percent of the "
        "best. Branch and bound is exact: it proves its optimum of %s PKR per week in %s nodes, using relaxation bounds to cut "
        "branches. Dynamic programming is exact only on a grid, and with five resources the exact grid would be far too large, so "
        "the program reduces it (its note reads “%s”) and the result reaches only %s percent. That is the point of including it: its table grows "
        "by a factor of 25 for every added budget, from %s cells with one budget to %s with four in the benchmark."
        % (n2(cmp("Crop selection", "greedy", "ms")), n2(F.greedy_pct), n1(F.relaxed), n0(F.bnb_nodes), F.dp_note, n1(F.dp_pct),
           n0(S["knap_dims"].median_of("aux1", "dp", param=1)), n0(S["knap_dims"].median_of("aux1", "dp", param=4))))
    deck.bar_chart(s, 0.62, 1.6, 6.4, 4.6, ["Greedy", "Dynamic programming", "Branch and bound"], [F.greedy_pct, F.dp_pct, 100.0],
                   [BLUE, RED, ACCENT], number_format='0.0"%"', maximum=118, size=14)
    deck.caption(s, 0.62, 6.25, 6.4, "Value as % of the proven optimum on the default farm. Source: farm_cli compare.", size=12)
    deck.text(s, 7.4, 1.7, 5.3, 4.9, [
        "**Branch and bound:** exact, proven in %s nodes (%s ms)" % (n0(F.bnb_nodes), n1(cmp("Crop selection", "branch and bound", "ms"))),
        "**Greedy:** %s%% of the optimum in %s ms" % (n1(F.greedy_pct), n2(cmp("Crop selection", "greedy", "ms"))),
        "**Dynamic programming:** %s; %s%%, %s s" % (F.dp_note, n1(F.dp_pct), n1(cmp("Crop selection", "dynamic programming", "ms") / 1000.0)),
        "Its table grows **×25 per added budget**"], size=18, bullets=True, space_after=12)

    # ------------------------------------------------------------------ 9 packing
    gain = 100.0 * (F.pack_value[PACKERS[2]] / F.pack_value[PACKERS[0]] - 1)
    s = deck.slide(
        "Packing: the genetic algorithm places %d of %d trays, First-Fit %d, Best-Fit %d" % (ga, F.selected_by_bnb, ffd, bfd),
        "About one and a half minutes. All three packers use one placement primitive on a row layout, so every layout is valid by "
        "construction. First-Fit Decreasing puts each tray, largest first, on the first place where it fits; Best-Fit "
        "Decreasing picks the tightest place. The genetic algorithm searches over the order of the trays and the policy, and "
        "starts from the two greedy layouts, so it can never be worse than either. Of the %d trays the knapsack chose it places "
        "%d against %d and %d, which is %s percent more value. After the refill step the final plan is worth %s percent of the "
        "relaxed optimum, against %s percent with First-Fit and %s percent with Best-Fit. The price is time: %s milliseconds "
        "against %s, about %s times as long. The chart on the right is the best value in each generation."
        % (F.selected_by_bnb, ga, ffd, bfd, n1(gain), n2(share[PACKERS[2]]), n2(share[PACKERS[0]]), n2(share[PACKERS[1]]),
           n0(ga_ms), n2(ffd_ms), n0(ga_ms / ffd_ms)))
    deck.bar_chart(s, 0.62, 1.55, 4.6, 3.5, ["First-Fit", "Best-Fit", "Genetic"], [ffd, bfd, ga], [BLUE, BROWN, ACCENT],
                   number_format="0", maximum=int(F.selected_by_bnb) + 12, size=14)
    deck.caption(s, 0.62, 5.05, 4.6, "Trays placed of the %d selected. Source: farm_cli compare." % F.selected_by_bnb, size=12)
    deck.picture(s, D.fig("fig_convergence"), 5.5, 1.5, 7.2, 3.6)
    deck.caption(s, 5.5, 5.1, 7.2, "Value of the best individual in each generation: from the First-Fit layout to the final one. Source: farm_app --screenshot.", size=12)
    deck.big_number(s, 0.62, 5.55, 4.0, "+%s%%" % n1(gain), "value of the placed trays, genetic against First-Fit", number_size=34, label_size=13)
    deck.big_number(s, 4.85, 5.55, 4.0, "%s%%" % n2(share[PACKERS[2]]), "plan share after refill (First-Fit %s%%, Best-Fit %s%%)" % (n2(share[PACKERS[0]]), n2(share[PACKERS[1]])), number_size=34, label_size=13)
    deck.big_number(s, 9.1, 5.55, 3.6, "×%s" % n0(ga_ms / ffd_ms), "time of the genetic algorithm against First-Fit", number_size=34, label_size=13, color=RED)

    # ------------------------------------------------------------------ 10 scheduling
    s = deck.slide(
        "Scheduling: lookahead delivers all %d contract lots, plain EDF misses %d" % (F.look["hard_total"], F.edf_missed),
        "About one and a half minutes. Scheduling is done one crop at a time. A lot is one tray's worth of one delivery; contract "
        "lots carry a penalty if they are missed. Plain earliest deadline first fills every free slot with the lot due soonest. "
        "That wastes slots on spot lots, and a contract lot that is released a week later finds every slot busy for the whole "
        "growing cycle: %d of %d contract lots are missed and %s PKR of penalties are paid. The lookahead scheduler leaves a "
        "slot idle when a spot batch would cost a contract delivery that was otherwise still achievable. It delivers all %d "
        "contract lots, gives up %d spot lots and ends %s PKR better. The timeline shows the final plan: one bar per batch, one "
        "row per slot, a thick outline for contract deliveries. For crops with at most 22 lots an exact search also exists; "
        "on this farm it was skipped for %s crops and its results appear in the benchmarks."
        % (F.edf_missed, F.hard_total, n0(F.edf["penalties"]), F.hard_total, F.spot_given_up, n0(F.net_gain), F.exact_fell_back.split()[0]))
    deck.picture(s, D.fig("fig_schedule"), 0.62, 1.5, 7.9, 4.95)
    deck.caption(s, 0.62, 6.5, 7.9, "Timeline of the final plan. Source: farm_app --screenshot.", size=12)
    deck.card(s, 8.8, 1.55, 3.9, 2.0, "Plain EDF", ["%d of %d contract lots on time" % (F.edf["hard"], F.edf["hard_total"]),
                                                     "penalties %s PKR" % n0(F.edf["penalties"]), "net %s PKR" % n0(F.edf["net"])],
              body_size=16, title_size=20, edge=RED, fill="F8ECEC")
    deck.card(s, 8.8, 3.7, 3.9, 2.0, "EDF with lookahead", ["%d of %d contract lots on time" % (F.look["hard"], F.look["hard_total"]),
                                                             "penalties %s PKR" % n0(F.look["penalties"]), "net %s PKR" % n0(F.look["net"])],
              body_size=16, title_size=20)
    deck.text(s, 8.8, 5.85, 3.9, 1.0, "Price: %d fewer spot lots served. Net: **+%s PKR**." % (F.spot_given_up, n0(F.net_gain)), size=16, color=DARK)

    # ------------------------------------------------------------------ 11 scaling
    dims = S["knap_dims"]
    dp_t = [t for _, t, _ in dims.series("dp", x="param")]
    bb_t = [t for _, t, _ in dims.series("bnb", x="param")]
    tight = S["pack_tight"]
    ga_t, ffd_t = tight.series("ga")[-1][1], tight.series("ffd")[-1][1]
    biggest, fill = int(tight.series("ga")[-1][0]), int(tight.rows[0]["param"])        # trays at the largest size, % of the shelf area they fill
    ffd_q = [q for _, _, q in tight.series("ffd")]
    claim(all(q == 100 for _, _, q in tight.series("ga")), "slide 11: the genetic packer places everything on tight instances")
    s = deck.slide(
        "Measured growth: dynamic programming explodes with budgets, the genetic packer costs time",
        "About one and a half minutes. We benchmarked every algorithm on instances of growing size, five seeds each, on one machine "
        "(%s). The left chart shows the knapsack with one to four budgets on a log scale: the dynamic-programming table grows "
        "by a factor of 25 per added budget and its time goes from %s to %s milliseconds, while branch and bound stays between "
        "%s and %s. The right chart shows packing when the trays fill %d percent of the shelves: the genetic algorithm places "
        "everything at every size, First-Fit places between %s and %s percent, but at %d trays the genetic "
        "algorithm needs %s seconds against %s milliseconds. The measured exponents are %s for First-Fit, %s for Best-Fit and "
        "%s for the genetic algorithm, close to the predicted two."
        % (D.bench.header["Machine"].split(",")[0], n3(dp_t[0]), n1(dp_t[-1]), n3(bb_t[0]), n3(bb_t[-1]), fill, n1(min(ffd_q)), n1(max(ffd_q)),
           biggest, n2(ga_t / 1000.0), n3(ffd_t), n2(e.ffd), n2(e.bfd), n2(e.ga)))
    deck.picture(s, D.fig("fig_bench_knap_dims"), 0.62, 1.5, 6.0, 3.75)
    deck.picture(s, D.fig("fig_bench_pack_tight"), 6.72, 1.5, 6.0, 3.75)
    deck.text(s, 0.62, 5.4, 6.0, 1.5, ["**Knapsack, 1 to 4 budgets:** the DP table grows ×25 per budget; its time goes from %s to %s ms. Branch and bound: %s to %s ms." % (n3(dp_t[0]), n1(dp_t[-1]), n3(bb_t[0]), n3(bb_t[-1]))], size=14)
    deck.text(s, 6.72, 5.4, 6.0, 1.5, ["**Packing, trays fill %d%% of the shelves:** exponents %s (First-Fit), %s (Best-Fit), %s (genetic). The genetic algorithm places 100%% of the tray area but needs %s s at %d trays; First-Fit needs %s ms." % (fill, n2(e.ffd), n2(e.bfd), n2(e.ga), n2(ga_t / 1000.0), biggest, n3(ffd_t))], size=14)

    # ------------------------------------------------------------------ 12 complexity table
    def md_row(component, algorithm):
        for row in complexity_md.summary_table():
            if row[0] == component and row[1] == algorithm:
                return row
        raise SystemExit("complexity-analysis.md has no row %s / %s" % (component, algorithm))

    def exps(suite, algo, param=None):
        value, points = S[suite].exponent(algo, param)
        return "none" if value is None else n2(value)

    def lead(text):
        return text.split(";")[0].split(", ")[0]

    dsatur = [x for x in e.dsatur if x]
    table = [["Algorithm", "Time", "Optimal?", "Measured exponent"]]
    spec = [("Constraints", "Welsh-Powell", "Welsh-Powell", "%s (50%% edges)" % exps("colouring", "wp", 50)),
            ("Constraints", "DSATUR", "DSATUR", "%s to %s" % (n2(min(dsatur)), n2(max(dsatur)))),
            ("Constraints", "Exact colouring", "Exact colouring", "none (under 0.05 ms)"),
            ("Forecast", "Holt-Winters with grid search", "Holt-Winters", "n fixed in the suite"),
            ("Selection", "Greedy", "Greedy knapsack", "none (under 0.1 ms)"),
            ("Selection", "Dynamic programming", "Dynamic programming", "%s; ×25 per budget" % exps("knap_1d", "dp")),
            ("Selection", "Branch and bound", "Branch and bound", "%s; %s (crops)" % (exps("knap_1d", "bnb"), exps("knap_farm", "bnb"))),
            ("Packing", "First-Fit and Best-Fit Decreasing", "FFD and BFD", "%s / %s" % (exps("pack_tight", "ffd"), exps("pack_tight", "bfd"))),
            ("Packing", "Genetic algorithm", "Genetic algorithm", exps("pack_tight", "ga")),
            ("Scheduling", "EDF", "EDF", "none (one point)"),
            ("Scheduling", "EDF with lookahead", "EDF with lookahead", "%s (2 points)" % exps("sched", "lookahead")),
            ("Scheduling", "Exact", "Exact scheduling", "none (under 0.05 ms)")]
    for component, md_name, label, measured in spec:
        row = md_row(component, md_name)
        table.append([label, row[3] if md_name == "EDF with lookahead" else lead(row[3]), lead(row[5]), measured])
    s = deck.slide(
        "Complexity: every bound derived from the code, the measured exponent beside it",
        "About one minute. This is the summary table of the complexity analysis. Every time bound was derived from the code as "
        "built, not copied from the plan; the full derivations, pseudo-code and proofs are in the report and in "
        "docs/complexity-analysis.md. The last column is the least-squares slope of log time against log size over the "
        "benchmark points; it is close to the predicted exponent where a polynomial bound exists: about two for the packers, "
        "dynamic programming in the number of copies and lookahead, about three for DSATUR. Where the bound is exponential we "
        "report counts instead, such as the factor of 25 per budget for the dynamic-programming table. A fit on two or three "
        "points is weak evidence, and the report says so. Knapsack, bin packing and graph colouring are NP-hard, which is why "
        "exact and heuristic methods are compared.")
    deck.table(s, 0.62, 1.55, [2.6, 4.5, 2.5, 2.5], table, size=12, row_h=0.39, align=["l", "l", "l", "l"])

    # ------------------------------------------------------------------ 13 limits and future work
    s = deck.slide(
        "Limitations and future work",
        "About one minute. We want to be honest about the limits. The data are synthetic, so our conclusions are about this "
        "model and these generated instances. The model leaves out crop rotation, stacking trays, free-form packing and labour "
        "peaks; each was left out on purpose to keep the problem tractable and explainable. All timings come from one machine, "
        "and several exponents rest on only two to five points. In theory, the minimality of our slot count is not proved for "
        "flexible harvest windows. For the future: calibrate with a real farm, lift the scope limits one at a time, starting "
        "with crop rotation, run the genetic algorithm in parallel, and re-plan on a rolling horizon as new orders arrive.")
    deck.card(s, 0.62, 1.55, 5.95, 5.15, "Limitations", [
        "**Synthetic data:** conclusions are about this model, not a real farm",
        "**Scope:** no crop rotation, no stacking, row-based packing, static slots",
        "**Measurements:** one machine; several exponents rest on 2 to 5 points",
        "**Theory:** minimal slot count not proved for flexible windows"], body_size=20, title_size=24, edge=RED, fill="F8F3EC", bullets=True)
    deck.card(s, 6.77, 1.55, 5.95, 5.15, "Future work", [
        "Calibrate the model with data from a real farm",
        "Lift the scope limits: crop rotation, labour peaks, several harvests per cycle",
        "Run the genetic algorithm in parallel; add stronger scheduling",
        "Re-plan on a rolling horizon as new orders arrive"], body_size=20, title_size=24, bullets=True)

    # ------------------------------------------------------------------ 14 demo
    s = deck.slide(
        "Demo and questions",
        "About three minutes for the demo, then questions. Start farm_app: the sample farm is planned automatically with branch and "
        "bound, the genetic packer and lookahead scheduling. Change a weekly budget and show that the plan re-runs by itself on a "
        "worker thread. Switch the packer from First-Fit to the genetic algorithm and show the Layout tab. Open Schedule, "
        "Constraints and Compare. In a terminal run farm_cli compare on the default scenario to show one table per stage. Every "
        "one of us can present any slide; if a question is about a number, the report names the file it came from.")
    deck.text(s, 0.62, 1.6, 5.7, 3.6, [
        "**farm_app:** the sample farm is planned automatically",
        "Change a budget: the plan re-runs",
        "Switch the packer: First-Fit → genetic algorithm",
        "Open Layout, Schedule, Constraints, Compare",
        "**farm_cli compare** on the default scenario"], size=19, bullets=True, space_after=10)
    deck.picture(s, D.fig("ui_overview"), 6.6, 1.55, 6.1, 3.8)
    deck.caption(s, 6.6, 5.4, 6.1, "The Overview tab. Source: farm_app --screenshot.", size=12)
    deck.text(s, 0.62, 5.5, 5.7, 1.2, "Questions?", size=44, bold=True, color=ACCENT)
    return deck


def check(path, team):
    """Checks of the finished file: slide count, speaker notes, the quoted Seoul Accord wording, the title page."""
    pages = slide_text(path)
    if not 12 <= len(pages) <= 14:
        sys.exit("The deck must have 12 to 14 slides, it has %d." % len(pages))
    for number, (text, notes) in enumerate(pages, 1):
        if len(notes.split()) < 40:
            sys.exit("Slide %d has no real speaker notes." % number)
        for bad in ("%s", "%d", "None", "{ref:"):
            if bad in text or bad in notes:
                sys.exit("Slide %d contains %r." % (number, bad))
    ccp.check_text("\n".join(text for text, _ in pages), numbers=set(QUOTED))
    if any(m["name"] not in pages[0][0] for m in team["members"]):
        sys.exit("A team member is missing from the title slide.")
    return len(pages)


def main():
    parser = argparse.ArgumentParser(description="Build the slides (.pptx) from the program outputs.")
    parser.add_argument("--no-refresh", action="store_true", help="reuse the files of the last run instead of running the programs")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--out", default=str(ROOT / "docs" / "slides" / "Smart_Vertical_Farm_Slides.pptx"))
    args = parser.parse_args()
    ccp.check_against_plan(ROOT / "plan" / "IMPLEMENTATION_PLAN.md")
    team = farm_data.load_team()
    D = farm_data.gather(args.build_dir, refresh=not args.no_refresh)
    F = facts.derive(D)
    deck = build(D, F, team)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    deck.save(out)
    print("wrote %s (%d slides)" % (out, check(out, team)))


if __name__ == "__main__":
    main()
