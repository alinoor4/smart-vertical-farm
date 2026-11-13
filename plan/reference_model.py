#!/usr/bin/env python3
"""Reference model for plan/IMPLEMENTATION_PLAN.md  (Smart Vertical Farm).

PLANNING AID ONLY. This is not part of the product and must not be ported line by line:
the product is C++17 and its structure is described in the plan. This file exists so that

  * every formula in the plan has one unambiguous, executable definition, and
  * the "golden" numbers quoted in the plan can be reproduced:   python plan/reference_model.py

When the C++ result for one of the golden cases differs from this file, read the matching
function here before assuming the C++ is right. Standard library only; no arguments.
The genetic packer is specified in the plan only (its random stream cannot match across
languages), so it has no reference here.
"""
import itertools
import math
import time
from dataclasses import dataclass

LOW, MED, HIGH = 0, 1, 2
RES = ["labor", "water", "nutrient", "energy", "area"]          # knapsack dimension order
UNIT_COST = dict(labor=300.0, water=0.5, nutrient=400.0, energy=50.0)   # PKR per h / L / L / kWh
LIGHT_GAP_CM = 10
HEIGHT_RATIO = 2.0
EPS = 1e-9


# =====================================================================================
# 1. Data model and the default sample farm                                  (plan §5, §6)
# =====================================================================================
@dataclass
class Crop:
    id: int
    name: str
    category: str
    tray_w: int          # cm
    tray_d: int          # cm
    can_rotate: bool
    height: int          # cm, mature plant incl. tray
    cycle: int           # weeks from planting to harvest (p)
    harvest_window: int  # weeks a harvest may precede its delivery week (f)
    light_min: int
    light_max: int
    hum_min: float
    hum_max: float
    labor: float         # per tray per week
    water: float
    nutrient: float
    energy: float
    yield_kg: float      # per tray per harvest
    price: float         # PKR per kg
    tray_cost: float     # PKR per tray per cycle (seed + substrate)
    salvage: float       # fraction of price obtained for unsold surplus
    base_demand: float   # mean weekly spot demand, kg (used by the generator)


@dataclass
class Shelf:
    id: int
    name: str
    rack: int
    level: int
    width: int
    depth: int
    clearance: int
    light: int
    humidity: float


@dataclass
class Contract:
    crop: int
    kg_per_week: float
    start_week: int
    end_week: int
    penalty_per_kg: float


def catalog():
    crops = []

    def add(name, cat, tw, td, h, cyc, hw, lmin, lmax, hmin, hmax, lab, wat, nut, en, yld, price, cost, dem):
        crops.append(Crop(len(crops), name, cat, tw, td, True, h, cyc, hw, lmin, lmax, hmin, hmax,
                          lab, wat, nut, en, yld, price, cost, 0.2, dem))
    #    name             category      tw  td   h cyc hw  lmin  lmax hmin hmax  labor water nutr  energy yield price cost demand
    add("Lettuce",       "leafy",       60, 40, 25,  5, 0, MED,  HIGH, 60, 80,  0.15,  8, 0.08,  3.5, 1.8,  1100,  80, 9.0)
    add("Baby Spinach",  "leafy",       60, 30, 20,  4, 0, MED,  HIGH, 60, 80,  0.14,  6, 0.06,  2.6, 1.2,  1300,  50, 6.0)
    add("Kale",          "leafy",       60, 45, 40,  6, 1, MED,  HIGH, 60, 80,  0.16,  9, 0.09,  4.0, 2.6,  1200,  90, 4.8)
    add("Pak Choi",      "leafy",       50, 40, 25,  4, 0, MED,  HIGH, 60, 80,  0.14,  7, 0.07,  3.0, 1.7,   900,  60, 4.0)
    add("Arugula",       "leafy",       45, 30, 20,  3, 0, MED,  HIGH, 60, 80,  0.16,  5, 0.05,  2.2, 0.9,  1800,  45, 2.4)
    add("Basil",         "herb",        50, 30, 35,  4, 0, MED,  HIGH, 50, 70,  0.20,  7, 0.07,  4.0, 0.9,  2600,  90, 2.7)
    add("Mint",          "herb",        40, 30, 30,  4, 1, MED,  HIGH, 55, 75,  0.13,  6, 0.06,  2.4, 0.8,  1500,  50, 2.0)
    add("Coriander",     "herb",        40, 25, 25,  4, 0, MED,  HIGH, 50, 70,  0.15,  5, 0.05,  2.2, 0.6,  1900,  40, 2.4)
    add("Microgreens",   "microgreen",  50, 25, 10,  2, 0, LOW,  MED,  50, 70,  0.30,  4, 0.02,  1.5, 0.3,  5000, 250, 1.5)
    add("Pea Shoots",    "microgreen",  55, 28, 15,  2, 0, LOW,  MED,  50, 70,  0.30,  5, 0.02,  1.6, 0.45, 3200, 220, 1.35)
    add("Strawberry",    "fruiting",   100, 30, 30, 10, 1, HIGH, HIGH, 55, 70,  0.30, 12, 0.15,  6.0, 3.5,  2400, 600, 7.0)
    add("Cherry Tomato", "fruiting",    60, 60, 70, 12, 1, HIGH, HIGH, 55, 70,  0.40, 20, 0.25,  9.0, 7.0,  1700, 400, 3.5)
    return crops


def default_shelves():
    shelves = []
    # (width, depth, humidity %, [(clearance cm, light) for level 1 (bottom) .. 4 (top)])
    racks = [
        (240, 60, 72, [(55, MED), (45, MED), (45, MED), (40, HIGH)]),
        (240, 60, 68, [(55, MED), (45, MED), (45, HIGH), (40, HIGH)]),
        (200, 80, 62, [(95, HIGH), (60, HIGH), (45, HIGH), (45, HIGH)]),
        (180, 50, 58, [(35, LOW), (35, LOW), (35, MED), (35, MED)]),
    ]
    for r, (w, d, hum, levels) in enumerate(racks):
        for lv, (clear, light) in enumerate(levels):
            shelves.append(Shelf(len(shelves), f"R{r + 1}-L{lv + 1}", r, lv, w, d, clear, light, hum))
    return shelves


DEFAULT_BUDGET = dict(labor=21.0, water=800.0, nutrient=8.5, energy=400.0)
DEFAULT_HORIZON = 12


def default_contracts(crops):
    by = {c.name: c for c in crops}

    def k(name, trays_per_week, penalty):
        c = by[name]
        return Contract(c.id, trays_per_week * c.yield_kg, c.cycle, 9999, penalty)
    return [k("Lettuce", 2, 600), k("Basil", 1, 1500), k("Microgreens", 2, 3000),
            k("Strawberry", 1, 1500), k("Baby Spinach", 1, 700)]


def default_explicit_conflicts(crops):
    by = {c.name: c.id for c in crops}
    pairs = set()
    for a in crops:
        for b in crops:
            if a.category == "herb" and b.category == "fruiting":
                pairs.add((min(a.id, b.id), max(a.id, b.id)))
    pairs.add((min(by["Mint"], by["Basil"]), max(by["Mint"], by["Basil"])))
    pairs.add((min(by["Kale"], by["Strawberry"]), max(by["Kale"], by["Strawberry"])))
    return pairs


# =====================================================================================
# 2. Constraint layer                                                           (plan §7)
# =====================================================================================
def orientations(c):
    """(w', d') pairs: w' runs along the shelf width, d' along its depth."""
    o = [(c.tray_w, c.tray_d)]
    if c.can_rotate and c.tray_w != c.tray_d:
        o.append((c.tray_d, c.tray_w))
    return o


def build_constraints(crops, shelves, explicit):
    n, m = len(crops), len(shelves)
    compat = [[False] * m for _ in range(n)]
    for c in crops:
        for s in shelves:
            fits = any(w <= s.width and d <= s.depth for (w, d) in orientations(c))
            compat[c.id][s.id] = (c.height + LIGHT_GAP_CM <= s.clearance
                                  and c.light_min <= s.light <= c.light_max
                                  and c.hum_min <= s.humidity <= c.hum_max and fits)
    conflict = [[None] * n for _ in range(n)]          # None or a reason string
    for a in crops:
        for b in crops:
            if a.id >= b.id:
                continue
            reason = None
            if (a.id, b.id) in explicit:
                reason = "explicit"
            elif max(a.height, b.height) >= HEIGHT_RATIO * min(a.height, b.height):
                reason = "height"
            elif not any(compat[a.id][s] and compat[b.id][s] for s in range(m)):
                reason = "no-common-shelf"
            conflict[a.id][b.id] = conflict[b.id][a.id] = reason
    return compat, conflict


def max_trays_by_area(c, shelves, compat):
    return sum((s.width * s.depth) // (c.tray_w * c.tray_d) for s in shelves if compat[c.id][s.id])


def welsh_powell(adj):
    n = len(adj)
    colour = [-1] * n
    for v in sorted(range(n), key=lambda v: (-len(adj[v]), v)):
        used = {colour[u] for u in adj[v] if colour[u] >= 0}
        k = 0
        while k in used:
            k += 1
        colour[v] = k
    return colour


def dsatur(adj):
    n = len(adj)
    colour = [-1] * n
    for _ in range(n):
        best, key = -1, None
        for v in range(n):
            if colour[v] >= 0:
                continue
            sat = len({colour[u] for u in adj[v] if colour[u] >= 0})
            k = (sat, len(adj[v]), -v)                # most saturated, then highest degree, then lowest index
            if key is None or k > key:
                best, key = v, k
        used = {colour[u] for u in adj[best] if colour[u] >= 0}
        k = 0
        while k in used:
            k += 1
        colour[best] = k
    return colour


def exact_chromatic(adj):
    n = len(adj)
    if n == 0:
        return 0
    order = sorted(range(n), key=lambda v: (-len(adj[v]), v))
    best = [max(dsatur(adj)) + 1]
    colour = [-1] * n

    def rec(i, used):
        if used >= best[0]:
            return
        if i == n:
            best[0] = used
            return
        v = order[i]
        forbidden = {colour[u] for u in adj[v] if colour[u] >= 0}
        for k in range(used + 1):                      # colours 0..used-1 plus one new colour
            if k not in forbidden:
                colour[v] = k
                rec(i + 1, max(used, k + 1))
                colour[v] = -1
    rec(0, 0)
    return best[0]


# =====================================================================================
# 3. Demand forecasting                                                         (plan §8)
# =====================================================================================
def seasonal_naive(y, L, H):
    n = len(y)
    if n < L:
        return [y[-1]] * H
    return [y[n - L + (h % L)] for h in range(H)]


def moving_average(y, H):
    best_k, best_sse = None, None
    for k in (4, 8, 12):
        if k >= len(y):
            continue
        sse = sum((y[t] - sum(y[t - k:t]) / k) ** 2 for t in range(k, len(y)))
        if best_sse is None or sse < best_sse - EPS:
            best_k, best_sse = k, sse
    if best_k is None:
        best_k = len(y)
    return [sum(y[-best_k:]) / best_k] * H, best_k


def ses(y, H):
    best = None
    for a10 in range(1, 10):
        alpha = a10 / 10.0
        level, sse = y[0], 0.0
        for t in range(1, len(y)):
            sse += (y[t] - level) ** 2
            level = alpha * y[t] + (1 - alpha) * level
        if best is None or sse < best[0] - EPS:
            best = (sse, alpha, level)
    return [best[2]] * H, best[1]


def holt_winters_fit(y, L, alpha, beta, gamma, H):
    """Additive Holt-Winters. Needs len(y) >= 2L. Returns (forecast, in-sample SSE)."""
    n = len(y)
    m1 = sum(y[:L]) / L
    m2 = sum(y[L:2 * L]) / L
    trend = (m2 - m1) / L
    level = m1 + trend * (L - 1) / 2.0                 # level at time index L-1
    season = [y[i] - (m1 + trend * (i - (L - 1) / 2.0)) for i in range(L)]
    sse = 0.0
    for t in range(L, n):
        s_old = season[t % L]
        sse += (y[t] - (level + trend + s_old)) ** 2
        new_level = alpha * (y[t] - s_old) + (1 - alpha) * (level + trend)
        trend = beta * (new_level - level) + (1 - beta) * trend
        season[t % L] = gamma * (y[t] - new_level) + (1 - gamma) * s_old
        level = new_level
    return [level + (h + 1) * trend + season[(n + h) % L] for h in range(H)], sse


def holt_winters(y, L, H):
    if len(y) < 2 * L:
        return None, None
    best = None
    for alpha in (0.1, 0.3, 0.5, 0.7, 0.9):
        for beta in (0.01, 0.05, 0.1, 0.3):
            for gamma in (0.05, 0.1, 0.3, 0.5):
                fc, sse = holt_winters_fit(y, L, alpha, beta, gamma, H)
                if best is None or sse < best[0] - EPS:
                    best = (sse, (alpha, beta, gamma), fc)
    return best[2], best[1]


METHODS = ["seasonal_naive", "moving_average", "ses", "holt_winters"]


def run_method(name, y, L, H):
    if name == "seasonal_naive":
        fc = seasonal_naive(y, L, H)
    elif name == "moving_average":
        fc = moving_average(y, H)[0]
    elif name == "ses":
        fc = ses(y, H)[0]
    else:
        fc = holt_winters(y, L, H)[0]
    return None if fc is None else [max(0.0, v) for v in fc]


def errors(actual, pred):
    h = len(actual)
    mae = sum(abs(a - p) for a, p in zip(actual, pred)) / h
    rmse = math.sqrt(sum((a - p) ** 2 for a, p in zip(actual, pred)) / h)
    smape = 100.0 / h * sum(0.0 if abs(a) + abs(p) < EPS else 2 * abs(a - p) / (abs(a) + abs(p)) for a, p in zip(actual, pred))
    return mae, rmse, smape


def forecast(y, L, H):
    """Hold out the last min(12, n//4) weeks, pick the method with the lowest MAE, refit on everything."""
    n = len(y)
    h_test = min(12, n // 4)
    if h_test == 0:                                    # fewer than four observations: nothing to hold out
        return run_method("seasonal_naive", y, L, H), "seasonal_naive", {}
    train, test = y[:n - h_test], y[n - h_test:]
    table, chosen = {}, None
    for name in METHODS:
        pred = run_method(name, train, L, h_test)
        if pred is None:
            continue
        table[name] = errors(test, pred)
        if chosen is None or table[name][0] < table[chosen][0] - EPS:
            chosen = name
    return run_method(chosen, y, L, H), chosen, table


# =====================================================================================
# 4. Delivery lots and the slot arithmetic                                      (plan §11)
# =====================================================================================
def build_lots(c, T, hard_kg, pen_rate, spot_kg):
    """Lots for delivery weeks w in [p, T-1+p]. hard_kg/pen_rate/spot_kg: dict week -> value."""
    lots, cum, cum_prev = [], 0.0, 0.0
    for w in range(c.cycle, T + c.cycle):
        h = hard_kg.get(w, 0.0)
        nh = int(math.ceil(h / c.yield_kg - EPS)) if h > EPS else 0
        spot = max(spot_kg.get(w, 0.0) - (nh * c.yield_kg - h), 0.0)       # contract surplus feeds the spot market
        cum += spot
        ns = int(math.floor(cum / c.yield_kg + 0.5)) - int(math.floor(cum_prev / c.yield_kg + 0.5))
        cum_prev = cum
        for i in range(nh):
            kg = min(c.yield_kg, h - i * c.yield_kg)
            lots.append(dict(due=w, hard=True, kg=kg, value=kg * c.price, penalty=kg * pen_rate.get(w, 0.0)))
        for i in range(ns):
            lots.append(dict(due=w, hard=False, kg=c.yield_kg, value=c.yield_kg * c.price, penalty=0.0))
    for i, lot in enumerate(lots):
        lot["id"] = i
        lot["ls"] = lot["due"] - c.cycle                                    # latest start
        lot["r"] = max(0, lot["ls"] - c.harvest_window)                     # release (earliest start)
    return lots


def hard_missed(free_at, hard_lots, p):
    """How many of hard_lots (sorted by (ls, id)) miss if slots become free at free_at?  EDF, earliest slot."""
    fa = sorted(free_at)
    missed = 0
    for lot in hard_lots:
        if fa and max(fa[0], lot["r"]) <= lot["ls"]:
            fa[0] = max(fa[0], lot["r"]) + p
            fa.sort()
        else:
            missed += 1
    return missed


def min_slots(lots, p):
    """Fewest slots that let every hard lot meet its deadline (generalised interval partitioning)."""
    hard = sorted([x for x in lots if x["hard"]], key=lambda x: (x["ls"], x["id"]))
    for m in range(0, len(hard) + 1):
        if hard_missed([0] * m, hard, p) == 0:
            return m
    return len(hard)


def interval_depth(intervals):
    """Classic interval partitioning: maximum number of half-open intervals [s, e) overlapping."""
    events = sorted([(s, 1) for s, e in intervals] + [(e, -1) for s, e in intervals])   # ends before starts at ties
    cur = best = 0
    for _, delta in events:
        cur += delta
        best = max(best, cur)
    return best


# =====================================================================================
# 5. Crop selection: multi-dimensional bounded knapsack                         (plan §9)
# =====================================================================================
def op_cost(c):
    return (c.tray_cost / c.cycle + c.labor * UNIT_COST["labor"] + c.water * UNIT_COST["water"]
            + c.nutrient * UNIT_COST["nutrient"] + c.energy * UNIT_COST["energy"])


def weights(c):
    return (c.labor, c.water, c.nutrient, c.energy, float(c.tray_w * c.tray_d))


def marginal_values(c, demand, n_contract_slots, penalty_per_week, cap):
    """v[k-1] = weekly value of the k-th tray of crop c (non-increasing in k); stops at the first value <= 0."""
    out = c.yield_kg / c.cycle                                               # kg per week per tray
    vals = []
    for k in range(1, cap + 1):
        sold = min(max(demand - (k - 1) * out, 0.0), out)
        revenue = c.price * (sold + c.salvage * (out - sold))
        bonus = penalty_per_week / n_contract_slots if (n_contract_slots > 0 and k <= n_contract_slots) else 0.0
        v = revenue - op_cost(c) + bonus
        if v <= EPS:
            break
        vals.append(v)
    return vals


def density(v, w, B):
    den = sum(w[r] / B[r] for r in range(len(B)) if B[r] > 0)
    return v / den if den > 0 else math.inf


def greedy(crops, vals, B):
    items = []
    for c in crops:
        w = weights(c)
        for k, v in enumerate(vals[c.id], start=1):
            items.append((-density(v, w, B), -v, c.id, k, v, w))
    items.sort()
    rem, x, total = list(B), {c.id: 0 for c in crops}, 0.0
    for _, _, cid, k, v, w in items:
        if k != x[cid] + 1:
            continue
        if all(w[r] <= rem[r] + EPS for r in range(len(B))):
            for r in range(len(B)):
                rem[r] -= w[r]
            x[cid] += 1
            total += v
    return x, total


def branch_and_bound(crops, vals, B, node_limit=5_000_000):
    R = len(B)
    cs = sorted([c for c in crops if vals[c.id]], key=lambda c: (-density(vals[c.id][0], weights(c), B), c.id))
    n = len(cs)
    W = [weights(c) for c in cs]
    prefix = []
    for c in cs:
        acc = [0.0]
        for v in vals[c.id]:
            acc.append(acc[-1] + v)
        prefix.append(acc)
    # copies sorted by value density, once per single resource and once for the surrogate constraint
    orders = []
    for r in range(R):
        lst = [((-(v / W[pos][r]) if W[pos][r] > 0 else -math.inf), pos, k, v, W[pos][r])
               for pos, c in enumerate(cs) for k, v in enumerate(vals[c.id], start=1)]
        lst.sort()
        orders.append(lst)
    sur_w = [sum(W[pos][r] / B[r] for r in range(R) if B[r] > 0) for pos in range(n)]
    surrogate = sorted([((-(v / sur_w[pos]) if sur_w[pos] > 0 else -math.inf), pos, k, v, sur_w[pos])
                        for pos, c in enumerate(cs) for k, v in enumerate(vals[c.id], start=1)])

    def fractional(lst, depth, cap):
        total = 0.0
        for _, pos, _k, v, w in lst:
            if pos < depth:
                continue
            if w <= cap:
                cap -= w
                total += v
            else:
                return total + v * cap / w
        return total

    def bound(depth, rem):
        ub = fractional(surrogate, depth, sum(rem[r] / B[r] for r in range(R) if B[r] > 0))
        for r in range(R):
            if B[r] > 0:
                ub = min(ub, fractional(orders[r], depth, rem[r]))
        return ub

    gx, gval = greedy(crops, vals, B)
    best = dict(value=gval, x=dict(gx), nodes=0, proven=True)
    x = [0] * n

    def rec(depth, rem, cur):
        best["nodes"] += 1
        if best["nodes"] > node_limit:
            best["proven"] = False
            return
        if depth == n:
            if cur > best["value"] + EPS:
                best["value"] = cur
                best["x"] = {cs[i].id: x[i] for i in range(n)}
            return
        if cur + bound(depth, rem) <= best["value"] + EPS:
            return
        kmax = len(vals[cs[depth].id])
        for r in range(R):
            if W[depth][r] > 0:
                kmax = min(kmax, int((rem[r] + EPS) // W[depth][r]))
        for k in range(kmax, -1, -1):
            x[depth] = k
            rec(depth + 1, [rem[r] - k * W[depth][r] for r in range(R)], cur + prefix[depth][k])
            if not best["proven"]:
                break
        x[depth] = 0

    root = bound(0, list(B))
    rec(0, list(B), 0.0)
    sol = {c.id: 0 for c in crops}
    sol.update(best["x"])
    return sol, best["value"], best["nodes"], best["proven"], root


def knapsack_dp(items, caps):
    """Exact 0/1 DP on integer weights. items: [(value, (w_0..w_{m-1}))]; caps: integer capacities.
    The m-dimensional table is flattened in mixed radix; returns (best value, chosen item indices)."""
    m = len(caps)
    radix = [1] * m
    for r in range(1, m):
        radix[r] = radix[r - 1] * (caps[r - 1] + 1)
    states = radix[-1] * (caps[-1] + 1)
    coords = [tuple((s // radix[r]) % (caps[r] + 1) for r in range(m)) for s in range(states)]
    dp = [0.0] * states
    take = []
    for value, w in items:
        off = sum(w[r] * radix[r] for r in range(m))
        row = [False] * states
        for s in range(states - 1, -1, -1):            # descending: dp[s - off] is still the previous item's row
            if all(coords[s][r] >= w[r] for r in range(m)) and dp[s - off] + value > dp[s] + EPS:
                dp[s] = dp[s - off] + value
                row[s] = True
        take.append(row)
    s, chosen = states - 1, []
    for i in range(len(items) - 1, -1, -1):
        if take[i][s]:
            chosen.append(i)
            s -= sum(items[i][1][r] * radix[r] for r in range(m))
    return dp[states - 1], sorted(chosen)


# =====================================================================================
# 6. Grow-space packing                                                        (plan §10)
# =====================================================================================
class PackState:
    def __init__(self, shelves):
        self.shelves = shelves
        self.rows = {s.id: [] for s in shelves}          # row = [y, depth, used_width]
        self.used_depth = {s.id: 0 for s in shelves}
        self.present = {s.id: set() for s in shelves}    # crop ids on the shelf
        self.place = {}                                  # tray id -> (shelf, x, y, w, d)


def row_orientation(c, shelf, row):
    best = None
    for (w, d) in orientations(c):
        if row[2] + w <= shelf.width and d <= row[1] and (best is None or w < best[0]):
            best = (w, d)                                # narrowest orientation that fits the row
    return best


def new_row_orientation(c, shelf, used_depth):
    best, key = None, None
    for (w, d) in orientations(c):
        if w <= shelf.width and used_depth + d <= shelf.depth:
            k = ((shelf.width // w) * ((shelf.depth - used_depth) // d), w)   # block count, then wider
            if key is None or k > key:
                best, key = (w, d), k
    return best


def place_one(st, tray_id, c, compat, conflict, policy):
    cand = [s for s in st.shelves
            if compat[c.id][s.id] and not any(conflict[c.id][o] for o in st.present[s.id])]
    chosen = None
    if policy == "FF":
        for s in cand:
            for ri, row in enumerate(st.rows[s.id]):
                o = row_orientation(c, s, row)
                if o:
                    chosen = (s, ri, o)
                    break
            if not chosen:
                o = new_row_orientation(c, s, st.used_depth[s.id])
                if o:
                    chosen = (s, -1, o)
            if chosen:
                break
    else:                                                # "BF"
        best = None
        for s in cand:
            for ri, row in enumerate(st.rows[s.id]):
                o = row_orientation(c, s, row)
                if o:
                    key = (s.width - row[2] - o[0], row[1] - o[1], s.id, ri)
                    if best is None or key < best[0]:
                        best = (key, (s, ri, o))
        if best is None:
            for s in cand:
                o = new_row_orientation(c, s, st.used_depth[s.id])
                if o:
                    key = (0 if st.rows[s.id] else 1, s.depth - st.used_depth[s.id] - o[1], s.id)
                    if best is None or key < best[0]:
                        best = (key, (s, -1, o))
        chosen = best[1] if best else None
    if not chosen:
        return False
    s, ri, (w, d) = chosen
    if ri < 0:
        st.rows[s.id].append([st.used_depth[s.id], d, 0])
        st.used_depth[s.id] += d
        ri = len(st.rows[s.id]) - 1
    row = st.rows[s.id][ri]
    st.place[tray_id] = (s.id, row[2], row[0], w, d)
    row[2] += w
    st.present[s.id].add(c.id)
    return True


def sort_decreasing(trays, crops):
    """trays: [(tray id, crop id)]. Longest side first, then shorter side, then crop id, then tray id."""
    return sorted(trays, key=lambda t: (-max(crops[t[1]].tray_w, crops[t[1]].tray_d),
                                        -min(crops[t[1]].tray_w, crops[t[1]].tray_d), t[1], t[0]))


def decode(order, crops, shelves, compat, conflict, policy):
    st = PackState(shelves)
    unplaced = [t for t in order if not place_one(st, t[0], crops[t[1]], compat, conflict, policy)]
    return st, unplaced


def validate_packing(st, trays, crops, shelves, compat, conflict):
    crop_of = dict(trays)
    by_shelf = {}
    for tid, (sid, x, y, w, d) in st.place.items():
        s = shelves[sid]
        assert x >= 0 and y >= 0 and x + w <= s.width and y + d <= s.depth, "tray outside shelf"
        assert compat[crop_of[tid]][sid], "tray on incompatible shelf"
        assert {w, d} == {crops[crop_of[tid]].tray_w, crops[crop_of[tid]].tray_d}, "tray size changed"
        by_shelf.setdefault(sid, []).append((tid, x, y, w, d))
    for lst in by_shelf.values():
        for a, b in itertools.combinations(lst, 2):
            assert a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1] or a[2] + a[4] <= b[2] or b[2] + b[4] <= a[2], "overlap"
            assert not conflict[crop_of[a[0]]][crop_of[b[0]]], "conflicting crops share a shelf"


def refill(st, counts, crops, vals, B, compat, conflict, policy):
    """Spend leftover budget on trays that verifiably fit. Mutates st; returns (new counts, number added)."""
    counts = dict(counts)
    rem = [B[r] - sum(counts[c.id] * weights(c)[r] for c in crops) for r in range(len(B))]
    items = []
    for c in crops:
        w = weights(c)
        for k in range(counts[c.id] + 1, len(vals[c.id]) + 1):
            v = vals[c.id][k - 1]
            items.append((-density(v, w, B), -v, c.id, k, v, w))
    items.sort()
    full, added, next_id = set(), 0, 1_000_000
    for _, _, cid, k, v, w in items:
        if cid in full or k != counts[cid] + 1:
            continue
        if not all(w[r] <= rem[r] + EPS for r in range(len(B))):
            continue
        if place_one(st, next_id, crops[cid], compat, conflict, policy):
            next_id += 1
            counts[cid] += 1
            added += 1
            for r in range(len(B)):
                rem[r] -= w[r]
        else:
            full.add(cid)                                 # no shelf can take this crop any more
    return counts, added


# =====================================================================================
# 7. Harvest and replant scheduling (one crop at a time)                       (plan §11)
# =====================================================================================
def schedule(lots, n_slots, p, T, mode, lookahead=None):
    """mode 'edf' or 'lookahead'. Returns (batches [(slot, lot id, start, end)], set of scheduled lot ids)."""
    L = p if lookahead is None else lookahead
    free_at = [0] * n_slots
    pending = sorted(lots, key=lambda x: (x["due"], not x["hard"], -x["value"], x["id"]))
    done, batches = set(), []
    for t in range(T):
        for k in range(n_slots):
            if free_at[k] > t:
                continue
            cand = [x for x in pending if x["id"] not in done and x["r"] <= t <= x["ls"]]
            if not cand:
                continue
            pick = None
            if mode == "edf":
                pick = cand[0]
            else:
                hard_now = [x for x in cand if x["hard"]]
                if hard_now:
                    pick = hard_now[0]
                else:
                    future = sorted([x for x in pending if x["id"] not in done and x["hard"] and t <= x["ls"] <= t + L],
                                    key=lambda x: (x["ls"], x["id"]))
                    without = hard_missed(free_at, future, p)
                    trial = list(free_at)
                    trial[k] = t + p
                    if hard_missed(trial, future, p) <= without:      # admit only if no extra hard lot is lost
                        pick = cand[0]
            if pick is not None:
                done.add(pick["id"])
                free_at[k] = t + p
                batches.append((k, pick["id"], t, t + p))
    return batches, done


def schedule_value(lots, done):
    return (sum(x["value"] for x in lots if x["id"] in done)
            - sum(x["penalty"] for x in lots if x["hard"] and x["id"] not in done))


def schedule_exact(lots, n_slots, p, T):
    """Optimal schedule_value by DFS over lots in (ls, id) order: skip, or start at the earliest feasible
    time on any slot with a distinct free time. Exponential; only for small instances."""
    order = sorted(lots, key=lambda x: (x["ls"], x["id"]))
    gain = [x["value"] + x["penalty"] for x in order]
    suffix = [0.0] * (len(order) + 1)
    for i in range(len(order) - 1, -1, -1):
        suffix[i] = suffix[i + 1] + gain[i]
    best = [-1.0]

    def rec(i, free_at, cur):
        if cur + suffix[i] <= best[0] + EPS:
            return
        if i == len(order):
            best[0] = cur
            return
        lot, tried = order[i], set()
        for k in range(n_slots):
            if free_at[k] in tried:
                continue
            tried.add(free_at[k])
            start = max(free_at[k], lot["r"])
            if start <= lot["ls"] and start < T:
                nxt = list(free_at)
                nxt[k] = start + p
                rec(i + 1, nxt, cur + gain[i])
        rec(i + 1, free_at, cur)
    rec(0, [0] * n_slots, 0.0)
    return best[0] - sum(x["penalty"] for x in order)


def schedule_stats(lots, done, batches, n_slots, T):
    hard = [x for x in lots if x["hard"]]
    soft = [x for x in lots if not x["hard"]]
    busy = sum(min(end, T) - start for (_, _, start, end) in batches)
    return dict(hard=len(hard), hard_ok=sum(1 for x in hard if x["id"] in done),
                soft=len(soft), soft_ok=sum(1 for x in soft if x["id"] in done),
                idle=n_slots * T - busy,
                revenue=sum(x["value"] for x in lots if x["id"] in done),
                penalty=sum(x["penalty"] for x in hard if x["id"] not in done))


# =====================================================================================
# 8. Golden numbers quoted in the plan
# =====================================================================================
def golden_unit_cases():
    print("== G1 graph colouring ==")

    def graph(n, edges):
        adj = [[] for _ in range(n)]
        for a, b in edges:
            adj[a].append(b)
            adj[b].append(a)
        return adj
    crown = graph(6, [(0, 3), (0, 5), (2, 1), (2, 5), (4, 1), (4, 3)])
    print("  crown: welsh_powell", welsh_powell(crown), "=", max(welsh_powell(crown)) + 1, "colours; dsatur", dsatur(crown), "=",
          max(dsatur(crown)) + 1, "; exact", exact_chromatic(crown))
    c5 = graph(5, [(0, 1), (1, 2), (2, 3), (3, 4), (4, 0)])
    k4 = graph(4, [(a, b) for a in range(4) for b in range(a + 1, 4)])
    petersen = graph(10, [(0, 1), (1, 2), (2, 3), (3, 4), (4, 0), (0, 5), (1, 6), (2, 7), (3, 8), (4, 9),
                          (5, 7), (7, 9), (9, 6), (6, 8), (8, 5)])
    print("  exact chromatic numbers: C5", exact_chromatic(c5), " K4", exact_chromatic(k4), " Petersen", exact_chromatic(petersen))

    print("== G2 forecasting ==")
    print("  seasonal_naive([1..8], L=4, H=3) =", seasonal_naive([1, 2, 3, 4, 5, 6, 7, 8], 4, 3))
    y = [3.0, 5.0, 4.0, 6.0, 5.0, 7.0]
    print("  moving_average(", y, ") =", moving_average(y, 1))
    lvl = 10.0
    for v in (20.0, 30.0):
        lvl = 0.5 * v + 0.5 * lvl
    print("  SES recursion alpha=0.5 on [10,20,30] -> level", lvl)
    print("  ses([10,20,30]) with alpha grid ->", ses([10.0, 20.0, 30.0], 1))
    series = [12, 18, 25, 15, 14, 21, 28, 17, 16, 23, 31, 19]
    fc, sse = holt_winters_fit([float(v) for v in series], 4, 0.5, 0.3, 0.2, 4)
    print("  holt_winters_fit(", series, ", L=4, a=.5, b=.3, g=.2) forecast", [round(v, 4) for v in fc], "SSE", round(sse, 4))
    pat = [5.0, -3.0, -4.0, 2.0]
    clean = [100 + 0.5 * t + pat[t % 4] for t in range(24)]
    truth = [100 + 0.5 * t + pat[t % 4] for t in range(24, 28)]
    fc, _ = holt_winters_fit(clean, 4, 0.5, 0.3, 0.2, 4)
    print("  noiseless trend+season: max abs forecast error", f"{max(abs(a - b) for a, b in zip(fc, truth)):.1e}")
    wig = [0.6, -0.9, 0.3, 1.1, -0.4, -0.7, 0.8, 0.2, -1.0, 0.5, 0.9, -0.3, -0.6, 0.4, 1.0, -0.8, 0.1, -0.2, 0.7, -0.5, 0.3, -0.9, 0.6, 0.0]
    noisy = [round(40 + 0.3 * t + [6.0, -2.0, -5.0, 1.0][t % 4] + wig[t], 2) for t in range(24)]
    fc, chosen, table = forecast(noisy, 4, 4)
    print("  model selection on", noisy)
    for name in METHODS:
        print(f"    {name:15s} MAE {table[name][0]:.4f} RMSE {table[name][1]:.4f} sMAPE {table[name][2]:.4f}")
    print("    chosen:", chosen, " forecast:", [round(v, 4) for v in fc])

    print("== G3 knapsack ==")
    items = [(7.0, (6,)), (5.0, (5,)), (5.0, (5,))]
    print("  one resource, capacity 10, items (value, weight)", [(v, w[0]) for v, w in items], "-> DP", knapsack_dp(items, [10]))
    items2 = [(9.0, (3, 2)), (6.0, (2, 2)), (7.0, (2, 3)), (4.0, (1, 2)), (8.0, (3, 3)), (3.0, (1, 1))]
    caps2 = [6, 6]
    brute = max((sum(items2[i][0] for i in sub), sub) for n in range(len(items2) + 1) for sub in itertools.combinations(range(len(items2)), n)
                if all(sum(items2[i][1][r] for i in sub) <= caps2[r] for r in range(2)))
    print("  two resources, capacities", caps2, "items", items2)
    print("    DP", knapsack_dp(items2, caps2), " brute force", brute)

    print("== G4 packing ==")
    shelves = [Shelf(0, "S0", 0, 0, 100, 70, 100, MED, 50), Shelf(1, "S1", 0, 1, 100, 60, 100, MED, 50)]
    sizes = [(80, 30), (60, 40), (45, 35), (60, 40)]
    crops = [Crop(i, f"T{i}", "x", w, d, True, 20, 4, 0, LOW, HIGH, 0, 100, 0, 0, 0, 0, 1, 1, 0, 0, 0) for i, (w, d) in enumerate(sizes)]
    trays = [(i, i) for i in range(4)]
    compat = [[True, True] for _ in crops]
    conflict = [[None] * 4 for _ in range(4)]
    order = sort_decreasing(trays, crops)
    print("  shelves S0 100x70, S1 100x60; trays", sizes, "; sorted order", [t[0] for t in order])
    for policy in ("FF", "BF"):
        st, unplaced = decode(order, crops, shelves, compat, conflict, policy)
        validate_packing(st, trays, crops, shelves, compat, conflict)
        print(f"  {policy}: placed {len(st.place)}/4  tray -> (shelf, x, y, w, d): {dict(sorted(st.place.items()))}  unplaced {[t[0] for t in unplaced]}")
    probe = Crop(0, "probe", "x", 0, 0, True, 20, 4, 0, LOW, HIGH, 0, 100, 0, 0, 0, 0, 1, 1, 0, 0, 0)
    for (W, D, w, d) in [(240, 60, 60, 40), (240, 60, 50, 25), (240, 60, 50, 30), (200, 80, 60, 40), (200, 80, 100, 30), (180, 50, 50, 25)]:
        probe.tray_w, probe.tray_d = w, d
        print(f"  new-row orientation on empty {W}x{D} shelf for a {w}x{d} tray:", new_row_orientation(probe, Shelf(0, "s", 0, 0, W, D, 100, MED, 50), 0))

    print("== G5 scheduling ==")
    lots = []
    for w in range(2, 8):
        if w >= 3:
            lots.append(dict(due=w, hard=True, kg=1.0, value=10.0, penalty=5.0))
        lots.append(dict(due=w, hard=False, kg=1.0, value=10.0, penalty=0.0))
        lots.append(dict(due=w, hard=False, kg=1.0, value=10.0, penalty=0.0))
    for i, x in enumerate(lots):
        x["id"], x["ls"], x["r"] = i, x["due"] - 2, x["due"] - 2
    print("  crop p=2, f=0, 2 slots, T=6; per delivery week 2..7: two soft lots, plus one hard lot from week 3 (value 10, penalty 5)")
    for mode in ("edf", "lookahead"):
        batches, done = schedule(lots, 2, 2, 6, mode)
        st = schedule_stats(lots, done, batches, 2, 6)
        print(f"  {mode:9s}: hard {st['hard_ok']}/{st['hard']} soft {st['soft_ok']}/{st['soft']} idle {st['idle']} value {schedule_value(lots, done):.0f}"
              f"  batches (slot, start, end, kind, due): {[(k, s, e, 'H' if lots[i]['hard'] else 'S', lots[i]['due']) for (k, i, s, e) in batches]}")
    print("  exact optimum value:", schedule_exact(lots, 2, 2, 6), " min_slots:", min_slots(lots, 2))
    iv = [(0, 3), (1, 4), (2, 5), (3, 6), (4, 7)]
    hard = [dict(id=i, due=e, hard=True, ls=s, r=s, value=1.0, penalty=1.0, kg=1.0) for i, (s, e) in enumerate(iv)]
    print("  interval_depth", iv, "=", interval_depth(iv), " min_slots (p=3, f=0) =", min_slots(hard, 3))


def golden_flat_scenario():
    print("\n== G6 flat default scenario (constant order history, so forecast == base demand) ==")
    crops, shelves, T = catalog(), default_shelves(), DEFAULT_HORIZON
    contracts = default_contracts(crops)
    compat, conflict = build_constraints(crops, shelves, default_explicit_conflicts(crops))
    n = len(crops)
    adj = [[j for j in range(n) if conflict[i][j]] for i in range(n)]
    kinds = {}
    for i in range(n):
        for j in range(i + 1, n):
            if conflict[i][j]:
                kinds[conflict[i][j]] = kinds.get(conflict[i][j], 0) + 1
    print("  compatible shelves per crop:", {c.name: sum(compat[c.id]) for c in crops})
    print("  conflict edges by reason:", kinds, " colours: welsh_powell", max(welsh_powell(adj)) + 1, "dsatur", max(dsatur(adj)) + 1,
          "exact", exact_chromatic(adj))
    print("  crop-shelf pairs compatible:", sum(sum(r) for r in compat), "of", n * len(shelves))

    history = {c.id: [c.base_demand] * 156 for c in crops}
    pmax = max(c.cycle for c in crops)
    H = T - 1 + pmax
    spot, lots, slots_needed, demand, pen_week, caps, vals = {}, {}, {}, {}, {}, {}, {}
    for c in crops:
        fc, chosen, _ = forecast(history[c.id], 52, H)
        assert chosen == "seasonal_naive" and all(abs(v - c.base_demand) < 1e-12 for v in fc)
        spot[c.id] = {w: fc[w - 1] for w in range(1, H + 1)}
        hard_kg, pen_num = {}, {}
        for k in contracts:
            if k.crop == c.id:
                for w in range(max(k.start_week, c.cycle), min(k.end_week, T - 1 + c.cycle) + 1):
                    hard_kg[w] = hard_kg.get(w, 0.0) + k.kg_per_week
                    pen_num[w] = pen_num.get(w, 0.0) + k.kg_per_week * k.penalty_per_kg
        pen_rate = {w: pen_num[w] / hard_kg[w] for w in hard_kg}
        lots[c.id] = build_lots(c, T, hard_kg, pen_rate, spot[c.id])
        slots_needed[c.id] = min_slots(lots[c.id], c.cycle)
        window = range(c.cycle, T + c.cycle)
        demand[c.id] = (sum(hard_kg.get(w, 0.0) for w in window) + sum(spot[c.id][w] for w in window)) / T
        pen_week[c.id] = sum(x["penalty"] for x in lots[c.id] if x["hard"]) / T
        caps[c.id] = min(400, max_trays_by_area(c, shelves, compat))
        vals[c.id] = marginal_values(c, demand[c.id], slots_needed[c.id], pen_week[c.id], caps[c.id])
    print("  per crop: weekly profit of a fully sold tray | demand kg/wk | contract slots | cap | usable copies | first/last marginal value")
    for c in crops:
        v = vals[c.id]
        print(f"    {c.name:14s} {c.yield_kg * c.price / c.cycle - op_cost(c):8.2f} | {demand[c.id]:6.3f} | {slots_needed[c.id]:2d} | {caps[c.id]:3d} | {len(v):2d} |"
              f" {v[0] if v else 0:9.3f} / {v[-1] if v else 0:9.3f}")

    area = float(sum(s.width * s.depth for s in shelves))
    B = [DEFAULT_BUDGET["labor"], DEFAULT_BUDGET["water"], DEFAULT_BUDGET["nutrient"], DEFAULT_BUDGET["energy"], area]
    gx, gval = greedy(crops, vals, B)
    t0 = time.time()
    bx, bval, nodes, proven, root = branch_and_bound(crops, vals, B)
    print(f"  items N = {sum(len(v) for v in vals.values())}, total shelf area {area:.0f} cm2")
    print(f"  greedy value {gval:.4f}  selection {dict((c.name, gx[c.id]) for c in crops if gx[c.id])}")
    print(f"  B&B    value {bval:.4f}  selection {dict((c.name, bx[c.id]) for c in crops if bx[c.id])}")
    print(f"         nodes {nodes}, proven {proven}, root bound {root:.4f}, {time.time() - t0:.2f}s (python)")
    use = [sum(bx[c.id] * weights(c)[r] for c in crops) for r in range(5)]
    print("  B&B usage of budget: " + ", ".join(f"{RES[r]} {100 * use[r] / B[r]:.2f}%" for r in range(5)))

    trays = [(tid, cid) for tid, cid in enumerate(cid for c in crops for cid in [c.id] * bx[c.id])]

    def value_of(counts):
        return sum(sum(vals[c.id][:counts[c.id]]) for c in crops)
    final = {}
    for policy in ("FF", "BF"):
        st, unplaced = decode(sort_decreasing(trays, crops), crops, shelves, compat, conflict, policy)
        validate_packing(st, trays, crops, shelves, compat, conflict)
        counts = {c.id: sum(1 for tid, cid in trays if cid == c.id and tid in st.place) for c in crops}
        placed_area = sum(w * d for (_, _, _, w, d) in st.place.values())
        used = {v[0] for v in st.place.values()}
        print(f"  {policy}D on the B&B selection: placed {len(st.place)}/{len(trays)}, value {value_of(counts):.4f}, shelves used {len(used)}, "
              f"placed area {placed_area} ({100 * placed_area / area:.2f}%), unplaced {dict((crops[c].name, sum(1 for t in unplaced if t[1] == c)) for c in sorted({t[1] for t in unplaced}))}")
        counts2, added = refill(st, counts, crops, vals, B, compat, conflict, policy)
        print(f"       refill: +{added} trays -> {sum(counts2.values())} trays, value {value_of(counts2):.4f} ({100 * value_of(counts2) / bval:.2f}% of the relaxed optimum); "
              f"final {dict((c.name, counts2[c.id]) for c in crops if counts2[c.id])}")
        final[policy] = counts2

    counts = final["FF"]
    print("  schedule on the final FF plan (per crop: slots; edf | lookahead as hard on-time/total, soft done/total, idle slot-weeks):")
    total = {m: dict(hard=0, hard_ok=0, soft=0, soft_ok=0, idle=0, revenue=0.0, penalty=0.0) for m in ("edf", "lookahead")}
    for c in crops:
        if not counts[c.id]:
            continue
        line = f"    {c.name:14s} {counts[c.id]:3d}"
        for mode in ("edf", "lookahead"):
            batches, done = schedule(lots[c.id], counts[c.id], c.cycle, T, mode)
            st = schedule_stats(lots[c.id], done, batches, counts[c.id], T)
            line += f" | {st['hard_ok']:2d}/{st['hard']:2d} {st['soft_ok']:2d}/{st['soft']:2d} {st['idle']:3d}"
            for key in total[mode]:
                total[mode][key] += st[key]
        print(line)
    for mode in ("edf", "lookahead"):
        t = total[mode]
        print(f"    {mode:9s}: hard {t['hard_ok']}/{t['hard']}, soft {t['soft_ok']}/{t['soft']}, idle {t['idle']}, revenue {t['revenue']:.2f}, "
              f"penalties {t['penalty']:.2f}, net {t['revenue'] - t['penalty']:.2f}")


if __name__ == "__main__":
    golden_unit_cases()
    golden_flat_scenario()
