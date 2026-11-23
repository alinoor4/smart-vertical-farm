"""Named numbers taken from the program outputs, shared by the report and the slides.

Every value here is read from `farm_data.Data` (the JSON files of farm_cli, the benchmark tables,
the scenario files). Nothing is typed in by hand: when the program's output changes, so do the
documents.
"""
from types import SimpleNamespace

RESOURCES = ["labour", "water", "nutrient", "energy", "floor area"]
PACKERS = ["First-Fit Decreasing", "Best-Fit Decreasing", "Genetic algorithm"]
SELECTORS = ["greedy", "dynamic programming", "branch and bound"]
SCHEDULERS = ["EDF", "EDF with lookahead", "exact"]
FORECASTERS = ["seasonal naive", "moving average", "simple exponential smoothing", "Holt-Winters"]
COLOURERS = ["Welsh-Powell", "DSATUR", "exact"]


def derive(D):
    F = SimpleNamespace()
    sc = D.scenario
    F.crop_count, F.shelf_count = len(sc["crops"]), len(sc["shelves"])
    F.rack_count = len({s["rack"] for s in sc["shelves"]})
    F.contract_count, F.listed_conflicts = len(sc["contracts"]), len(sc["conflicts"])
    F.horizon, F.season, F.history = sc["horizonWeeks"], sc["seasonLength"], len(sc["orderHistory"][0])
    F.area = sum(s["width"] * s["depth"] for s in sc["shelves"])
    F.budget = sc["budget"]
    F.seed = sc["seed"]

    # the final plan: the app's starting options (genetic packer, lookahead scheduler)
    ga = D.runs["default_ga"]
    m = ga["metrics"]
    F.relaxed, F.plan_value, F.plan_share = m["relaxedOptimum"], m["planValue"], 100 * m["planShare"]
    F.profit, F.net, F.revenue, F.penalties = m["expectedWeeklyProfit"], m["net"], m["revenue"], m["penalties"]
    F.budget_use = dict(zip(RESOURCES, [100 * u for u in m["budgetUse"]]))
    F.space_use = 100 * m["spaceUtilisation"]
    F.hard_total, F.hard_on_time, F.soft_total, F.soft_served = m["hardTotal"], m["hardOnTime"], m["softTotal"], m["softServed"]
    F.idle = m["idleSlotWeeks"]
    F.spot_service = 100 * m["spotService"]
    F.final_trays = sum(row["trays"] for row in ga["selection"])
    F.selected_trays = sum(row["selected"] for row in ga["selection"])
    F.dropped = F.selected_trays - F.final_trays
    F.ga_generations = ga["packing"]["generationsRun"]
    F.warnings = ga["warnings"]

    # pruning and colouring
    k = ga["constraints"]
    F.pairs_total, F.pairs_ok = k["pairsTotal"], k["pairsCompatible"]
    F.pruned = 100.0 * (F.pairs_total - F.pairs_ok) / F.pairs_total
    F.edges_explicit, F.edges_height, F.edges_none = (k["conflictEdges"][x] for x in ("explicit", "height", "noCommonShelf"))
    F.edges = F.edges_explicit + F.edges_height + F.edges_none
    F.colours = (k["colours"]["welshPowell"], k["colours"]["dsatur"], k["colours"]["exact"])
    F.exact_nodes = k["colours"]["exactNodes"]
    F.shelves_per_crop = k["compatibleShelvesPerCrop"]

    # compare tables on the default farm
    c = D.compare["default"]
    F.cmp = c
    F.algorithms = sum(len(rows) for rows in c.stages.values())
    F.selected_by_bnb = c("Packing", PACKERS[0], "trays")
    F.placed = {p: int(c("Packing", p, "placed")) for p in PACKERS}
    F.pack_value = {p: c("Packing", p, "value") for p in PACKERS}
    F.edf = {key: c("Scheduling", "EDF", col) for key, col in (
        ("hard", "hard on time"), ("hard_total", "hard lots"), ("soft", "soft served"), ("soft_total", "soft lots"),
        ("idle", "idle slot-weeks"), ("revenue", "revenue"), ("penalties", "penalties"), ("net", "net"))}
    F.look = {key: c("Scheduling", "EDF with lookahead", col) for key, col in (
        ("hard", "hard on time"), ("hard_total", "hard lots"), ("soft", "soft served"), ("soft_total", "soft lots"),
        ("idle", "idle slot-weeks"), ("revenue", "revenue"), ("penalties", "penalties"), ("net", "net"))}
    F.edf_missed = int(F.edf["hard_total"] - F.edf["hard"])
    F.spot_given_up = int(F.edf["soft"] - F.look["soft"])
    F.net_gain = F.look["net"] - F.edf["net"]
    F.exact_fell_back = c("Scheduling", "exact", "note")
    F.dp_note = c("Crop selection", "dynamic programming", "note")
    F.dp_states = int(c("Crop selection", "dynamic programming", "states"))
    F.dp_pct = c("Crop selection", "dynamic programming", "% of best")
    F.greedy_pct = c("Crop selection", "greedy", "% of best")
    F.bnb_nodes = int(c("Crop selection", "branch and bound", "nodes"))
    F.bnb_bound = c("Crop selection", "branch and bound", "bound")

    # the same plan under three packers (three runs of farm_cli run)
    F.by_packer = {}
    for key, name in (("default_ffd", PACKERS[0]), ("default_bfd", PACKERS[1]), ("default_ga", PACKERS[2])):
        mm = D.runs[key]["metrics"]
        F.by_packer[name] = dict(plan=mm["planValue"], share=100 * mm["planShare"], net=mm["net"],
                                 hard=mm["hardOnTime"], hard_total=mm["hardTotal"], soft=mm["softServed"],
                                 placed=sum(r["trays"] for r in D.runs[key]["selection"]))

    # measured exponents (bench/results/summary.md); None when fewer than two points reach 0.05 ms
    s = D.bench.suites
    F.exp = SimpleNamespace(
        dp=s["knap_1d"].exponent("dp")[0], bnb_1d=s["knap_1d"].exponent("bnb")[0],
        bnb_farm=s["knap_farm"].exponent("bnb")[0],
        wp50=s["colouring"].exponent("wp", 50)[0],
        dsatur=[s["colouring"].exponent("dsatur", p)[0] for p in (10, 30, 50)],
        ffd=s["pack_tight"].exponent("ffd")[0], bfd=s["pack_tight"].exponent("bfd")[0], ga=s["pack_tight"].exponent("ga")[0],
        ffd_loose=s["pack_loose"].exponent("ffd")[0], bfd_loose=s["pack_loose"].exponent("bfd")[0], ga_loose=s["pack_loose"].exponent("ga")[0],
        look=s["sched"].exponent("lookahead")[0])

    # the flat farm: the reference model's golden case
    fl = D.runs["flat"]
    F.flat_relaxed, F.flat_nodes = fl["knapsack"]["value"], fl["knapsack"]["nodes"]
    F.flat_ffd_placed = fl["packing"]["placed"]
    F.flat_ffd_unplaced = len(fl["packing"]["unplaced"])
    F.flat_trays = F.flat_ffd_placed + F.flat_ffd_unplaced
    F.flat_net = fl["metrics"]["net"]
    F.flat_dp_pct = D.compare["flat"]("Crop selection", "dynamic programming", "% of best")
    return F
