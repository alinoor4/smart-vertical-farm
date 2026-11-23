"""Report, part A: front matter, introduction, the CCP table, system overview, formal models."""
import ccp
from docx_kit import ACCENT, DARK
from farm_data import n0, n1, n2
from facts import PACKERS, RESOURCES

LIGHT = {0: "L", 1: "M", 2: "H"}
GREEN2 = "5E9C5E"
BLUE = "2F6F8F"
BROWN = "8A6A3B"


def write(r, D, F, team):
    front(r, D, F, team)
    intro(r, D, F)
    ccp_section(r, D, F)
    overview(r, D, F)
    models(r, D, F)


# ------------------------------------------------------------------ front matter

def front(r, D, F, team):
    r.title_page(team["course"], team["kind"], team["title_lines"],
                 [(m["name"], m["roll"]) for m in team["members"]], team["advisor_label"], team["advisor"])
    r.plain_heading("Abstract")
    r.p("A vertical farm has more candidate crops than shelf space, and every crop draws on the same five limited "
        "resources: labour, water, nutrient solution, energy and floor area. This report presents a planning system "
        "that decides what to plant, where to place it and when to harvest. Five algorithmic components form one "
        "pipeline: a constraint graph with three colouring algorithms, four demand-forecasting methods, a "
        "multi-dimensional knapsack for crop selection, row-based shelf packing, and harvest scheduling. The "
        "components offer %d hand-written C++17 algorithms in all, which a desktop application, a command-line tool, "
        "%d automated tests and ten benchmark suites compare. On the sample farm the final plan is worth %s%% of the "
        "proven upper bound, and lookahead scheduling delivers all %d contract lots on time where earliest deadline "
        "first delivers %d on the same plan. The report states the model, derives and measures the complexity of every "
        "algorithm, and maps the project to the nine characteristics of a Complex Computing Problem."
        % (F.algorithms, D.tests, n2(F.plan_share), F.hard_total, F.edf["hard"]))
    r.p("**Keywords:** vertical farming, multi-dimensional knapsack, bin packing, graph colouring, scheduling, "
        "demand forecasting, algorithm analysis.", size=10)
    r.contents()
    r.page_break()


# ------------------------------------------------------------------ 3 introduction

def intro(r, D, F):
    r.h(1, "intro", "Introduction and problem description")
    r.h(2, "problem", "The problem")
    r.p("Indoor vertical farms (shipping-container farms, greenhouse shelving, hydroponic towers) have a fixed and "
        "expensive resource: cubic space, light exposure, and nutrient and water throughput. An operator has many "
        "candidate crops, each with its own tray footprint, growth cycle, light and water needs and market value, but "
        "only finite shelf space, finite grow-lights and finite labour hours per week. The project proposal asks for a "
        "platform that helps the operator decide *what to plant, where to plant it and when to harvest*, so that the "
        "available space and resources produce the greatest profit while physical and biological constraints are "
        "respected: a strawberry tray cannot share a shelf with something twice its height, some crops cannot stand "
        "side by side because of light or humidity conflicts, and some customers require a guaranteed delivery every week.")
    r.p("This is a Complex Computing Problem in the sense of the Seoul Accord: no formula gives the answer, it joins "
        "several NP-hard sub-problems that feed one another, and the proposal does not say what a “best plan” is. "
        "Section %s makes that claim precise; the rest of the report shows how the system answers it." % r.ref("ccp"))

    r.h(2, "components", "The five components of the proposal")
    r.table("tab:components", "The five algorithmic components of the proposal and what was built for each.",
            ["Component", "Technique named in the proposal", "Algorithms built"],
            [["Constraint layer", "Incompatibility constraints as a graph; prune invalid packings before optimising",
              "Compatibility matrix and conflict graph; Welsh-Powell, DSATUR and exact backtracking colouring"],
             ["Demand forecasting", "Predict near-term demand per crop from past orders and seasonality",
              "Seasonal naive, moving average, simple exponential smoothing, additive Holt-Winters; automatic selection by hold-out error"],
             ["Crop selection", "Multi-dimensional knapsack over weekly resource budgets",
              "Greedy by value density, dynamic programming, branch and bound"],
             ["Grow-space packing", "2D/3D bin packing: First-Fit Decreasing, Best-Fit, or a genetic algorithm",
              "First-Fit Decreasing, Best-Fit Decreasing and a genetic algorithm on a row-based shelf layout"],
             ["Harvest and replant scheduling", "Interval scheduling or earliest deadline first with lookahead",
              "Earliest deadline first, EDF with lookahead, exact search; interval partitioning for slot requirements"]],
            [3.4, 5.9, 7.1], size=9, first_col_bold=True)
    r.p("Each component has several algorithm variants so that they can be compared; the comparison is the main "
        "experimental result. Between them the five components offer %d algorithms, which `farm_cli compare` lists in "
        "one table per stage (%s)." % (F.algorithms, r.sec("walk")))

    r.h(2, "contrib", "Contributions and structure of this report")
    r.bullets([
        "A **planning model** that joins the five components: the forecast sets the value of each tray, the constraint "
        "graph limits where a tray may stand, the knapsack chooses how many trays of each crop, the packer places them, "
        "and the scheduler decides when each batch is planted and harvested (%s and %s)." % (r.sec("overview"), r.sec("models")),
        "**%d algorithms**, all written by hand in C++17, each with pseudo-code, a complexity bound derived from the code "
        "and an optimality argument (%s). The algorithmic code uses only the C++ standard library." % (F.algorithms, r.sec("algorithms")),
        "A desktop application, a command-line tool, **%d automated tests** and a benchmark harness with ten suites (%s)." % (D.tests, r.sec("impl")),
        "An **experimental evaluation**: a walk-through of the sample farm and a measured growth rate for each algorithm (%s)." % r.sec("eval"),
        "A discussion of when each algorithm wins and of the limits of this work (%s)." % r.sec("discussion"),
    ])
    r.p("Section %s maps the project to the nine characteristics of a Complex Computing Problem. %s explains how to "
        "build and run the program, %s shows the application’s screens and %s is left for the team’s individual "
        "contributions." % (r.ref("ccp"), r.ref("app:build"), r.ref("app:shots"), r.ref("app:contrib")))


# ------------------------------------------------------------------ 4 the CCP table

def ccp_section(r, D, F):
    e = F.exp
    r.h(1, "ccp", "Why this is a Complex Computing Problem")
    r.p("The Seoul Accord (*Section D - Graduate Attributes*, table D.4.1 “Range of Problem Solving”) defines a "
        "Complex Computing Problem as “%s” and lists nine characteristics. %s maps each one to the system as it "
        "was built. The columns “Characteristic” and “Seoul Accord wording” are quotations. The last "
        "column is ours; every figure in it comes from a program output (the source of each is named in section %s)."
        % (ccp.DEFINITION, r.ref("tab:ccp"), r.ref("setup")))
    r.p("The definition says “some or all”, so the table does not claim all nine characteristics at full strength. "
        "Rows 1, 2, 3, 8 and 9 carry the argument. Rows 4 to 7 are met in part, and each of those rows states its limit.")
    sec = r.sec
    ours = {
        1: ["Profit pulls against contract reliability, space use against crop separation, and the five budgets "
            "(labour, water, nutrient, energy, floor area) pull the crop mix in different directions. The conflict is "
            "measured: on the default farm earliest deadline first serves %d of %d contract lots on time and %d of %d "
            "spot lots; the lookahead scheduler serves %d of %d contract lots and %d spot lots, so it gives up %d spot "
            "sales to protect %d contract deliveries, and its net revenue is %s PKR against %s PKR (%s)."
            % (F.edf["hard"], F.edf["hard_total"], F.edf["soft"], F.edf["soft_total"], F.look["hard"], F.look["hard_total"],
               F.look["soft"], F.spot_given_up, F.edf_missed, n0(F.look["net"]), n0(F.edf["net"]), r.ref("tab:sched")),
            "*Shown in %s.*" % sec("walk")],
        2: ["No formula gives the plan. The abstractions had to be designed: trays as knapsack copies with falling "
            "marginal value, shelves as row-packed bins under a conflict graph, harvests as lots with due weeks on "
            "identical slots, and a relax, pack, refill scheme to join them, because the selection optimum does not "
            "survive packing: the optimum selects %d trays and the three packers place %d, %d and %d of them. Selection "
            "and packing are NP-hard (%s), which is why exact and heuristic methods are compared."
            % (F.selected_by_bnb, F.placed[PACKERS[0]], F.placed[PACKERS[1]], F.placed[PACKERS[2]], sec("hard")),
            "*Shown in %s.*" % r.secs("models", "walk")],
        3: ["Greedy methods, dynamic programming, branch and bound with relaxation bounds, backtracking, graph "
            "colouring, a genetic algorithm and exponential smoothing, each with a stated complexity bound (%s) and a "
            "measured growth rate: the exponent of dynamic programming is %s in the number of copies, that of branch "
            "and bound %s in the number of crops, of DSATUR up to %s, of First-Fit Decreasing %s and of the genetic "
            "algorithm %s in the number of trays. Added to this is domain knowledge of controlled-environment farming: "
            "growth cycles, light, humidity and clearance."
            % (sec("algorithms"), n2(e.dp), n2(e.bnb_farm), n2(max(x for x in e.dsatur if x)), n2(e.ffd), n2(e.ga)),
            "*Shown in %s.*" % r.secs("algorithms", "bench")],
        4: ["What is rarely met is the combination: humidity ranges, vertical clearance, shading between crops and "
            "delivery contracts in one model, and a best crop mix that cannot be packed as chosen (Best-Fit Decreasing "
            "places %d of the %d trays of the optimal mix). **Limit:** each sub-problem alone belongs to a familiar "
            "family (knapsack, bin packing, scheduling)." % (F.placed[PACKERS[1]], F.selected_by_bnb),
            "*Shown in %s.*" % sec("walk")],
        5: ["No standard, code of practice or ready-made procedure prescribes how to plan a vertical farm, so the team "
            "defined the model, the objective, the metrics and the validation. **Limit:** the individual algorithms are "
            "textbook methods; it is the problem as posed that no standard covers.",
            "*Shown in %s.*" % sec("models")],
        6: ["The model carries five parties whose needs differ: the operator (profit), contract customers (on-time "
            "delivery, enforced by penalties), spot buyers (availability), workers (the labour-hours budget) and the "
            "resource side (water and energy). **Limit:** nobody was consulted; these needs enter as parameters of "
            "generated sample data.", "*Shown in %s.*" % r.secs("data", "models")],
        7: ["Financial: missed contract deliveries cost penalties; under earliest deadline first the default farm pays "
            "%s PKR over the plan, under lookahead %s PKR. Operational: a placement holds its slot for the whole plan. "
            "Environmental: water and energy use are budgeted and reported; the final plan uses %s%% of the water "
            "budget and %s%% of the energy budget (labour %s%%). **Limit:** these are consequences inside a simulation "
            "on generated data, not measurements from a farm."
            % (n0(F.edf["penalties"]), n0(F.look["penalties"]), n1(F.budget_use["water"]), n1(F.budget_use["energy"]),
               n1(F.budget_use["labour"])), "*Shown in %s.*" % sec("walk")],
        8: ["Five sub-problems feed each other: the forecast sets the value of each tray, the constraint graph limits "
            "packing, packing fixes the slots, the slots decide which deliveries can be met, and contract penalties "
            "feed back into tray values. Swapping one algorithm changes the results of the others: the same farm under "
            "First-Fit Decreasing, Best-Fit Decreasing and the genetic packer ends with plans worth %s, %s and %s PKR "
            "per week and with net revenues of %s, %s and %s PKR over the plan."
            % (n0(F.by_packer[PACKERS[0]]["plan"]), n0(F.by_packer[PACKERS[1]]["plan"]), n0(F.by_packer[PACKERS[2]]["plan"]),
               n0(F.by_packer[PACKERS[0]]["net"]), n0(F.by_packer[PACKERS[1]]["net"]), n0(F.by_packer[PACKERS[2]]["net"])),
            "*Shown in %s.*" % r.secs("pipeline", "walk")],
        9: ["The proposal names five components but gives no data, no objective and no formal constraints. The team had "
            "to decide what “best plan” means, what the planning unit is, how demand enters and what a missed "
            "contract costs. Causes also had to be diagnosed, not read off: why the best crop mix cannot be packed (the "
            "knapsack sees only the total area) and why earliest deadline first misses %d contract deliveries that "
            "lookahead keeps (it fills a slot with a spot lot that falls due soon, and a contract lot released a week "
            "later finds every slot busy)." % F.edf_missed,
            "*Shown in %s.*" % r.secs("pipeline", "sched")],
    }
    rows = [[str(number), name, wording, ours[number]] for number, name, wording in ccp.CHARACTERISTICS]
    r.table("tab:ccp", "The nine characteristics of a Complex Computing Problem (Seoul Accord, table D.4.1) and how this project shows each.",
            ["#", "Characteristic", "Seoul Accord wording", "How this project shows it"], rows,
            [0.7, 2.6, 3.7, 9.4], size=8.5, source="Seoul Accord, www.seoulaccord.org (columns 2 and 3); farm_cli run and compare, bench/results (column 4).")


# ------------------------------------------------------------------ 5 system overview

def overview(r, D, F):
    r.h(1, "overview", "System overview")
    r.h(2, "model", "The planning model in brief")
    r.p("Time is counted in whole weeks. Week 0 is the present and the plan covers plantings in weeks 0 to T−1, where "
        "T = %d is the planning horizon. The farm has *shelves*, one rack level each: a rectangle with a vertical "
        "clearance, a light level and a humidity. A crop grows in *trays* of a fixed footprint. A tray stays where it "
        "is placed for the whole plan and always grows the same crop; a placed tray is called a *slot*. A batch planted "
        "in a slot in week s occupies it until week s + p, where p is the crop’s growth cycle, and is harvested "
        "then; the slot can be replanted at once. The pipeline answers three questions in order:" % F.horizon)
    r.numbered([
        "**What to plant.** How many trays of each crop: a multi-dimensional knapsack over the weekly labour, water, "
        "nutrient and energy budgets and the total shelf area. The value of a tray falls as more trays of one crop are "
        "chosen, because the forecast demand for the crop is limited.",
        "**Where.** Which shelf and position each tray takes: two-dimensional packing in which a tray may only stand on "
        "a compatible shelf, and crops that conflict may not share a shelf.",
        "**When.** For each crop, when each batch is planted, so that contract deliveries are met and slots do not sit "
        "idle: scheduling of equal-length jobs on identical slots."])

    r.h(2, "pipeline", "The pipeline")
    r.flow("fig:pipeline", "The pipeline: each stage and what it hands to the next. The forecast, the constraints and the lots all feed the knapsack; the constraints also limit packing.",
           [("Forecast", "spot demand per crop and week", ACCENT),
            ("Constraints", "compatibility matrix, conflict graph", BLUE),
            ("Lots", "deliveries as jobs, slots the contracts need", BROWN),
            ("Knapsack", "trays per crop (relaxed optimum)", ACCENT),
            ("Packing", "placements and unplaced trays", BLUE),
            ("Refill", "final trays per crop = slots", GREEN2),
            ("Scheduling", "batches; deliveries met or missed", BROWN)],
           source="pipeline.cpp, plan section 4.3.")
    r.p("The knapsack knows only the total shelf area; geometry and conflicts are enforced by the packer. Trays that do "
        "not fit are dropped, and the budget they freed is spent again, greedily, on trays that verifiably fit "
        "(the *refill* step). The number of trays finally placed is the number of slots the scheduler may use. The first "
        "stage is therefore a *relaxation* of the whole problem, and when branch and bound proves it optimal its value is "
        "an upper bound on every plan; the ratio of the plan’s value to that bound (the *plan share*) is the headline "
        "quality measure of the report.")
    r.p("A loop from packing back to the knapsack was tried during the design and rejected: re-solving the knapsack with "
        "tightened caps and re-packing from scratch never settled, because a crop that fitted in one round lost trays "
        "in the next when the layout shifted. Dropping what does not fit and refilling into the existing layout only "
        "ever adds trays and ends after one pass.")

    r.h(2, "modules", "Modules")
    r.table("tab:modules", "The modules of the program. All algorithms are in the library `farm_core`; the command-line tool, the application and the tests are built on it.",
            ["Module (source file)", "Role", "Algorithms"],
            [["`constraints.cpp`", "Compatibility matrix, conflict graph, pruning statistics", "Welsh-Powell, DSATUR, exact colouring"],
             ["`forecast.cpp`", "Demand forecast per crop; automatic selection by hold-out error", "Seasonal naive, moving average, SES, Holt-Winters"],
             ["`knapsack.cpp`", "Crop selection; tray values; lots and slot arithmetic are in `scheduling.cpp`", "Greedy, dynamic programming, branch and bound"],
             ["`packing.cpp`", "Row-based placement primitive, validation, three packers", "First-Fit and Best-Fit Decreasing, genetic algorithm"],
             ["`scheduling.cpp`", "Lots, slot arithmetic, three schedulers", "Interval depth, EDF, EDF with lookahead, exact search"],
             ["`pipeline.cpp`", "Runs the stages in order, refill, warnings, metrics; `compareAll` runs every variant of every stage", "(joins the above)"],
             ["`generator.cpp`, `json_io.cpp`", "Seeded sample-data generator; scenario and result files (JSON)", "SplitMix64 random numbers"],
             ["`bench.cpp`", "Ten benchmark suites, CSV files, summary with measured exponents", "(measures the above)"],
             ["`cli/`, `app/`", "`farm_cli` (generate, validate, run, compare, bench); `farm_app` (Dear ImGui desktop application)", "(none)"]],
            [4.0, 7.4, 5.0], size=8.5, source="the repository, src/ (plan section 4.2).")

    r.h(2, "data", "The sample farm")
    sc = D.scenario
    shelves = sc["shelves"]
    r.p("No real farm data exists, so all data are synthetic, for the Pakistani market and in PKR. The default scenario "
        "(`data/default_scenario.json`, seed %d) has %d crops (%s), %d racks of four levels (%d shelves, %s cm² of "
        "shelf area in all), weekly budgets of %s labour hours, %s litres of water, %s litres of nutrient solution and "
        "%s kWh of energy, a planning horizon of %d weeks, and %d weeks of order history per crop, generated from a "
        "seeded trend, a yearly pattern and noise. It has %d delivery contracts (fictional customers) and %d explicit "
        "agronomic conflict rules; further conflicts follow from the crops’ heights."
        % (F.seed, F.crop_count, "twelve catalogue crops from lettuce to cherry tomato" if F.crop_count == 12 else "crops",
           F.rack_count, F.shelf_count, n0(F.area), n1(F.budget["labor"]), n0(F.budget["water"]), n1(F.budget["nutrient"]),
           n0(F.budget["energy"]), F.horizon, F.history, F.contract_count, F.listed_conflicts))
    rows = []
    for c in sc["crops"]:
        rows.append([c["name"], c["category"], "%d × %d" % (c["trayW"], c["trayD"]), str(c["height"]), str(c["cycleWeeks"]),
                     str(c["harvestWindowWeeks"]), "%s–%s" % (LIGHT[c["lightMin"]], LIGHT[c["lightMax"]]),
                     "%d–%d" % (c["humidityMin"], c["humidityMax"]), n2(c["yieldKg"]), n0(c["pricePerKg"])])
    r.table("tab:crops", "The crop catalogue of the default scenario.",
            ["Crop", "Category", "Tray cm", "Height cm", "Cycle p (weeks)", "Window f", "Light", "Humidity %", "Yield kg/tray", "Price PKR/kg"],
            rows, [2.7, 2.0, 1.7, 1.3, 1.5, 1.2, 1.2, 1.6, 1.6, 1.6], size=8.5, align=["l", "l", "r", "r", "r", "r", "c", "c", "r", "r"],
            source="data/default_scenario.json.",
            notes="Light: L low, M medium, H high. The window f is how many weeks before its delivery a harvest may happen.")
    r.p("A second scenario, `data/flat_scenario.json`, has the same farm with a constant order history. With a constant "
        "history every forecasting method returns the base demand, so the results of this scenario can be checked against "
        "an independent reference implementation of the formulas (%s)." % r.sec("impl"))


# ------------------------------------------------------------------ 6 formal models

def models(r, D, F):
    r.h(1, "models", "Formal models")
    r.p("This section states the three optimisation problems that the pipeline solves in turn. Section %s describes the "
        "algorithms that solve them." % r.ref("algorithms"))
    r.h(2, "notation", "Notation")
    r.table("tab:notation", "Sets, parameters and decision variables.",
            ["Symbol", "Meaning"],
            [["C, S, R", "crops, shelves, and the five resources R = {labour, water, nutrient, energy, floor area}"],
             ["T, p_{c}, f_{c}", "planning horizon in weeks; growth cycle and harvest window of crop c in weeks"],
             ["y_{c}, π_{c}, κ_{c}, σ_{c}", "yield per tray (kg), price (PKR/kg), tray cost (PKR per cycle) and salvage fraction of crop c"],
             ["w_{c,r}", "what one tray of crop c uses of resource r per week (floor area: the tray footprint in cm²)"],
             ["B_{r}", "weekly budget of resource r (floor area: the total shelf area)"],
             ["d_{c}, q_{c}, P_{c}", "mean weekly sellable demand (kg), fewest slots that serve every contract lot, weekly contract penalty at risk"],
             ["v_{c,k}", "value of the k-th tray of crop c in PKR per week; non-increasing in k"],
             ["x_{c}, u_{c}", "decision variable: number of trays of crop c; the most that can be worth choosing"],
             ["W_{s}, D_{s}", "width and depth of shelf s in cm (shelf area W_{s}D_{s})"],
             ["J, r_{j}, l_{j}", "lots of one crop; earliest and latest planting week of lot j (r_{j} = max(0, l_{j} − f))"],
             ["k", "number of slots of a crop (trays finally placed)"]],
            [4.3, 12.1], size=9)

    r.h(2, "m1", "Crop selection (model M1)")
    r.p("Decide x_{c}, the number of trays of each crop c. The value of the k-th tray is v_{c,k}; a solution takes the "
        "first x_{c} copies of every crop, which is optimal because the values do not increase.")
    r.formula("maximise    Σ_{c ∈ C}  V_{c}(x_{c}),      V_{c}(x) = v_{c,1} + v_{c,2} + … + v_{c,x}", "1")
    r.formula("subject to    Σ_{c ∈ C}  w_{c,r} · x_{c}  ≤  B_{r}      for every r ∈ R", "2")
    r.formula("0 ≤ x_{c} ≤ u_{c},   x_{c} integer", "3")
    r.p("The value of a tray joins the forecast and the contracts to the optimisation. With out_{c} = y_{c} / p_{c} the "
        "weekly output of one slot:")
    r.formula("sold_{c,k} = min( max( d_{c} − (k − 1) · out_{c}, 0 ), out_{c} )", "4")
    r.formula("v_{c,k} = π_{c} · ( sold_{c,k} + σ_{c} · (out_{c} − sold_{c,k}) )  −  cost_{c}  +  bonus_{c,k}", "5")
    r.p("Here cost_{c} = κ_{c} / p_{c} plus the priced weekly use of labour, water, nutrient and energy, and "
        "bonus_{c,k} = P_{c} / q_{c} for k ≤ q_{c} and 0 otherwise. The first trays of a crop are sold at full "
        "price until the forecast demand d_{c} is used up; later trays earn only the salvage price. The bonus is how "
        "contracts enter: the q_{c} trays a contract needs carry the penalty they avoid, so that a contract that is not "
        "worth honouring under the budgets is dropped by the optimiser and reported, instead of being forced in.")
    r.p("M1 is a multi-dimensional knapsack problem whose items come in groups of copies with equal weights and falling "
        "values. It contains the ordinary multi-dimensional 0/1 knapsack (every group with one copy), so it is NP-hard "
        "(%s)." % r.sec("hard"))

    r.h(2, "m2", "Grow-space packing (model M2)")
    r.p("Let I be the trays to place (tray i grows crop c(i) and has footprint a_{i} × b_{i}, which it may turn by "
        "90° when the crop allows) and S the shelves. Choose for each tray a shelf, an orientation and a position "
        "(x_{i}, y_{i}), or leave it unplaced, so that:")
    r.formula("each tray stands on at most one shelf, and only on a shelf that is compatible with its crop", "6")
    r.formula("two crops that conflict never share a shelf", "7")
    r.formula("the trays on a shelf lie inside it, [x_{i}, x_{i} + w_{i}) × [y_{i}, y_{i} + d_{i}) ⊆ [0, W_{s}) × [0, D_{s}), and do not overlap", "8")
    r.p("A crop is compatible with a shelf when its mature height plus the lamp gap fits the clearance, the shelf’s "
        "light level and humidity are inside the crop’s ranges, and some orientation of the tray fits the shelf. "
        "Two crops conflict when an agronomic rule lists them, when the taller is at least twice the height of the "
        "shorter (it would shade it), or when no shelf is compatible with both. The objective is lexicographic: first the "
        "value of the placed trays (the sum over crops of the prefix of v_{c,k} up to the number placed), then the "
        "fewest shelves used, then the most concentrated fill.")
    r.p("The implementation restricts positions to a *row layout*: a shelf is filled in rows along its width and the "
        "first tray of a row fixes the row’s depth. This keeps (8) true by construction and makes every packer, "
        "and the refill step, a sequence of calls to one placement primitive, which never produces an invalid layout. "
        "Free-form rectangle packing and stacking trays are outside the scope (%s)." % r.sec("limits"))

    r.h(2, "m3", "Harvest and replant scheduling (model M3)")
    r.p("Scheduling is done one crop at a time. The k slots of the crop are identical machines and every job has the "
        "same length p. A *lot* is one tray’s worth of one delivery: it has a due week, a value, and, when it comes "
        "from a contract (*hard*), a penalty if it is missed; forecast spot demand gives *soft* lots without a penalty. "
        "A lot due in week e can be planted in any week s with r_{j} ≤ s ≤ l_{j}, where l_{j} = e − p is "
        "the latest start that still meets the delivery and r_{j} = max(0, l_{j} − f) the earliest, because a "
        "harvest may precede its delivery by at most f weeks. Choose for each lot a start week and a slot, or leave it "
        "unscheduled, so that:")
    r.formula("batches on one slot do not overlap: s_{i} + p ≤ s_{j} or s_{j} + p ≤ s_{i}", "9")
    r.formula("maximise    Σ_{j scheduled}  value_{j}  −  Σ_{j hard, not scheduled}  penalty_{j}", "10")
    r.p("The number of slots comes from the packing stage. For the contract lots alone, the smallest number of slots that "
        "serves all of them is the quantity q_{c} used in (5); with a single possible start per lot it is the depth of "
        "an interval graph, and for flexible starts it is found by simulation (%s)." % r.sec("sched"))

    r.h(2, "m0", "How the three models join")
    r.p("M1 is solved first and sees only the total shelf area, so it is a relaxation of the joint problem: with a proven "
        "optimum its value bounds the value of every plan. M2 tries to realise the chosen counts; trays that do not "
        "fit are dropped, and the freed budget is re-spent by the greedy rule on trays that do fit. The final tray "
        "count of each crop is the slot count k of its M3. The plan share, the plan’s value divided by M1’s "
        "optimum, measures how much the packing and the refill cost.")
