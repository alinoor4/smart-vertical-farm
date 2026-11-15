#include "scheduling.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>

namespace {

// Real farms have hundreds of lots per crop. A file that asks for more is a typing slip (kgPerWeek 1e9), and building
// the lots would then use all the memory; the slot search below is also quadratic in the number of hard lots.
constexpr double kMaxLots = 20000;

}  // namespace

std::vector<Lot> buildLots(const Crop& crop, int horizon, const std::vector<double>& hardKg,
                           const std::vector<double>& penaltyRate, const std::vector<double>& spot) {
    const int p = crop.cycleWeeks;
    const size_t weeks = size_t(horizon - 1 + p) + 1;
    if (hardKg.size() < weeks || penaltyRate.size() < weeks || spot.size() < weeks)
        throw std::invalid_argument("buildLots needs hardKg, penaltyRate and spot entries for weeks 0 .. horizon-1+cycle");

    std::vector<Lot> lots;
    double cum = 0, cumPrev = 0;   // spot kg so far, now and one week ago
    for (int w = p; w <= horizon - 1 + p; ++w) {
        const double h = hardKg[w];
        const double hardCount = h > EPS ? std::ceil(h / crop.yieldKg - EPS) : 0.0;
        cum += std::max(spot[w] - (hardCount * crop.yieldKg - h), 0.0);       // a contract tray's surplus goes to the spot market
        // Rounding the running total, not each week, so that 0.4 lots a week still gives two lots in five weeks.
        const double softCount = std::floor(cum / crop.yieldKg + 0.5) - std::floor(cumPrev / crop.yieldKg + 0.5);
        cumPrev = cum;
        if (!(double(lots.size()) + hardCount + softCount <= kMaxLots))   // written so that NaN also fails
            throw std::runtime_error("crop " + crop.name + " would need more than 20000 delivery lots; check kgPerWeek and the order history");

        for (int i = 0; i < int(hardCount); ++i) {
            Lot lot;
            lot.hard = true;
            lot.kg = std::min(crop.yieldKg, h - i * crop.yieldKg);   // the last tray may be only partly needed
            lot.value = lot.kg * crop.pricePerKg;
            lot.penalty = lot.kg * penaltyRate[w];
            lot.dueWeek = w;
            lots.push_back(lot);
        }
        for (int i = 0; i < int(softCount); ++i) {
            Lot lot;
            lot.kg = crop.yieldKg;
            lot.value = crop.yieldKg * crop.pricePerKg;
            lot.dueWeek = w;
            lots.push_back(lot);
        }
    }
    for (size_t i = 0; i < lots.size(); ++i) {
        Lot& lot = lots[i];
        lot.id = int(i);
        lot.cropId = crop.id;
        lot.latestStart = lot.dueWeek - p;
        lot.release = std::max(0, lot.latestStart - crop.harvestWindowWeeks);   // a harvest may precede its delivery by f weeks
    }
    return lots;
}

int hardMissed(const std::vector<int>& freeAt, const std::vector<Lot>& hardLotsSorted, int p) {
    std::priority_queue<int, std::vector<int>, std::greater<int>> free(freeAt.begin(), freeAt.end());   // when each slot frees
    int missed = 0;
    for (const Lot& lot : hardLotsSorted) {
        if (free.empty()) { ++missed; continue; }   // no slots at all
        const int a = free.top();
        free.pop();
        const int start = std::max(a, lot.release);
        if (start <= lot.latestStart) {
            free.push(start + p);
        } else {
            free.push(a);                            // the slot stays as it was; this lot is lost
            ++missed;
        }
    }
    return missed;
}

int minSlotsForHardLots(const std::vector<Lot>& lots, int p) {
    std::vector<Lot> hard;
    for (const Lot& lot : lots)
        if (lot.hard) hard.push_back(lot);
    std::sort(hard.begin(), hard.end(), [](const Lot& a, const Lot& b) {
        return a.latestStart != b.latestStart ? a.latestStart < b.latestStart : a.id < b.id;
    });
    const int n = int(hard.size());
    for (int slots = 0; slots < n; ++slots)
        if (hardMissed(std::vector<int>(slots, 0), hard, p) == 0) return slots;
    return n;   // one slot per hard lot always works
}

int intervalDepth(const std::vector<std::pair<int, int>>& intervals) {
    std::vector<std::pair<int, int>> events;   // (week, +1 when an interval starts, -1 when it ends)
    for (const auto& interval : intervals) {
        events.push_back({interval.first, +1});
        events.push_back({interval.second, -1});
    }
    std::sort(events.begin(), events.end());   // at the same week an end (-1) comes before a start (+1)
    int open = 0, deepest = 0;
    for (const auto& event : events) {
        open += event.second;
        deepest = std::max(deepest, open);
    }
    return deepest;
}

CropDemand buildCropDemand(const Scenario& scenario, int cropId, const std::vector<double>& spotForecast) {
    const Crop& crop = scenario.crops[cropId];
    const int horizon = scenario.horizonWeeks, p = crop.cycleWeeks;
    const int last = horizon - 1 + p;   // the last delivery week the plan can influence
    if (int(spotForecast.size()) < last)
        throw std::invalid_argument("buildCropDemand: the forecast must reach delivery week horizon-1+cycle");

    std::vector<double> spot(last + 1, 0.0), hardKg(last + 1, 0.0), penaltyKg(last + 1, 0.0), penaltyRate(last + 1, 0.0);
    for (int w = 1; w <= last; ++w) spot[w] = spotForecast[w - 1];
    for (const Contract& contract : scenario.contracts) {
        if (contract.cropId != cropId) continue;
        for (int w = std::max(contract.startWeek, p); w <= std::min(contract.endWeek, last); ++w) {
            hardKg[w] += contract.kgPerWeek;
            penaltyKg[w] += contract.kgPerWeek * contract.penaltyPerKg;
        }
    }
    for (int w = 0; w <= last; ++w)
        penaltyRate[w] = hardKg[w] > 0 ? penaltyKg[w] / hardKg[w] : 0.0;   // the kg-weighted mean penalty of the week's contracts

    CropDemand out;
    out.lots = buildLots(crop, horizon, hardKg, penaltyRate, spot);
    out.contractSlots = minSlotsForHardLots(out.lots, p);
    double contractKg = 0, spotKg = 0, penalties = 0;
    for (int w = p; w <= last; ++w) {
        contractKg += hardKg[w];
        spotKg += spot[w];
    }
    for (const Lot& lot : out.lots)
        if (lot.hard) penalties += lot.penalty;
    out.demand = (contractKg + spotKg) / horizon;
    out.penaltyPerWeek = penalties / horizon;
    return out;
}

// ---- the schedulers -----------------------------------------------------------------------------------------------

namespace {

// What a scheduler decides: which lots get a batch, and the batches.
struct Plan {
    std::vector<char> scheduled;
    std::vector<Batch> batches;
};

// Indexes into `lots` in the order of plan 11.4: due week, hard before soft, value descending, id ascending.
std::vector<int> pendingOrder(const std::vector<Lot>& lots) {
    std::vector<int> order(lots.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Lot& x = lots[a];
        const Lot& y = lots[b];
        if (x.dueWeek != y.dueWeek) return x.dueWeek < y.dueWeek;
        if (x.hard != y.hard) return x.hard;
        if (x.value != y.value) return x.value > y.value;
        if (x.id != y.id) return x.id < y.id;
        return a < b;
    });
    return order;
}

// The admission test of plan 11.5 for a soft lot that slot k would start in week t: it is allowed when it does not make
// more of the hard lots that fall due in weeks t .. t+L unreachable than leaving the slot alone does.
bool admitSoftLot(const std::vector<Lot>& lots, const std::vector<char>& scheduled, const std::vector<int>& freeAt, int k, int t,
                  int p, int lookahead) {
    std::vector<Lot> future;   // the hard lots still to do whose latest start lies in the window
    for (size_t i = 0; i < lots.size(); ++i)
        if (!scheduled[i] && lots[i].hard && lots[i].latestStart >= t && lots[i].latestStart <= static_cast<long long>(t) + lookahead)
            future.push_back(lots[i]);
    std::sort(future.begin(), future.end(), [](const Lot& a, const Lot& b) {
        return a.latestStart != b.latestStart ? a.latestStart < b.latestStart : a.id < b.id;
    });
    const int without = hardMissed(freeAt, future, p);
    std::vector<int> trial = freeAt;
    trial[k] = t + p;
    return hardMissed(trial, future, p) <= without;
}

// Variants 1 and 2: week by week, slot by slot, take the first lot that is released and still reachable.
Plan scheduleWeekly(const std::vector<Lot>& lots, int slots, int p, int horizon, bool lookahead, int lookaheadWeeks) {
    Plan plan;
    plan.scheduled.assign(lots.size(), 0);
    const std::vector<int> pending = pendingOrder(lots);
    std::vector<int> freeAt(slots, 0);
    for (int t = 0; t < horizon; ++t)
        for (int k = 0; k < slots; ++k) {
            if (freeAt[k] > t) continue;
            int firstReleased = -1, firstHard = -1;   // the first released lot, and the first released hard lot, in pending order
            for (int i : pending) {
                if (plan.scheduled[i] || lots[i].release > t || t > lots[i].latestStart) continue;
                if (firstReleased < 0) firstReleased = i;
                if (!lookahead || lots[i].hard) {     // plain EDF needs only the first; lookahead needs the first hard one too
                    if (lots[i].hard) firstHard = i;
                    break;
                }
            }
            int pick = -1;
            if (!lookahead) pick = firstReleased;
            else if (firstHard >= 0) pick = firstHard;
            else if (firstReleased >= 0 && admitSoftLot(lots, plan.scheduled, freeAt, k, t, p, lookaheadWeeks)) pick = firstReleased;
            if (pick < 0) continue;                   // the slot is idle this week
            plan.scheduled[pick] = 1;
            freeAt[k] = t + p;
            plan.batches.push_back({0, k, lots[pick].id, t, t + p});
        }
    return plan;
}

// Variant 3: depth-first over the lots by (latestStart, id); each lot is skipped or started at the earliest possible week on
// a slot (trying one slot per distinct free time). The bound is the value that all the remaining lots could still add.
struct ExactSearch {
    const std::vector<Lot>& lots;
    int slots, p, horizon;
    long long nodeLimit;

    std::vector<int> order;            // lot indexes by (latestStart, id)
    std::vector<double> gain;          // by position: value + penalty, what scheduling the lot is worth against skipping it
    std::vector<double> suffix;        // suffix[i] = sum of gain[i..]
    std::vector<int> freeAt;           // when each slot is free
    std::vector<int> slotOf, startOf;  // by position: the choice now being tried, -1 for skip
    std::vector<int> bestSlot, bestStart;
    double best = -1.0;                // best total gain so far (every real schedule has a gain of at least 0)
    long long nodes = 0;
    bool stopped = false;

    ExactSearch(const std::vector<Lot>& l, int s, int cycle, int h, long long limit)
        : lots(l), slots(s), p(cycle), horizon(h), nodeLimit(limit), freeAt(s, 0) {
        order.resize(lots.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (lots[a].latestStart != lots[b].latestStart) return lots[a].latestStart < lots[b].latestStart;
            return lots[a].id != lots[b].id ? lots[a].id < lots[b].id : a < b;
        });
        const size_t n = lots.size();
        gain.resize(n);
        suffix.assign(n + 1, 0.0);
        for (size_t i = 0; i < n; ++i) gain[i] = lots[order[i]].value + lots[order[i]].penalty;
        for (size_t i = n; i-- > 0;) suffix[i] = suffix[i + 1] + gain[i];
        slotOf.assign(n, -1);
        startOf.assign(n, 0);
        bestSlot.assign(n, -1);
        bestStart.assign(n, 0);
    }

    void search(int i, double current) {
        if (stopped) return;
        if (++nodes > nodeLimit) { stopped = true; return; }
        if (current + suffix[i] <= best + EPS) return;   // even scheduling everything left would not beat the best
        if (i == int(order.size())) {
            best = current;
            bestSlot = slotOf;
            bestStart = startOf;
            return;
        }
        const Lot& lot = lots[order[i]];
        std::vector<int> tried;   // slots that free at the same week are alike, so only the first of them is tried
        for (int k = 0; k < slots && !stopped; ++k) {
            if (std::find(tried.begin(), tried.end(), freeAt[k]) != tried.end()) continue;
            tried.push_back(freeAt[k]);
            const int start = std::max(freeAt[k], lot.release);
            if (start > lot.latestStart || start >= horizon) continue;
            const int before = freeAt[k];
            freeAt[k] = start + p;
            slotOf[i] = k;
            startOf[i] = start;
            search(i + 1, current + gain[i]);
            freeAt[k] = before;
        }
        slotOf[i] = -1;
        search(i + 1, current);
    }
};

}  // namespace

CropSchedule scheduleCrop(const std::vector<Lot>& lots, int slots, int cycle, int horizon, SchedAlgo algo, int lookaheadWeeks,
                          long long exactNodeLimit) {
    if (slots < 0 || horizon < 0 || cycle < 1 || lookaheadWeeks < -1)
        throw std::invalid_argument("scheduleCrop: slots and horizon must not be negative, the cycle at least 1 and the lookahead at least -1");

    CropSchedule out;
    out.cropId = lots.empty() ? 0 : lots.front().cropId;
    out.slots = slots;
    out.lots = lots;
    const int lookahead = lookaheadWeeks < 0 ? cycle : lookaheadWeeks;

    Plan plan;
    if (algo == SchedAlgo::Edf) {
        plan = scheduleWeekly(lots, slots, cycle, horizon, false, 0);
    } else if (algo == SchedAlgo::EdfLookahead) {
        plan = scheduleWeekly(lots, slots, cycle, horizon, true, lookahead);
    } else if (int(lots.size()) > kExactMaxLots) {
        out.note = "too many lots for the exact search (" + std::to_string(lots.size()) + " > " + std::to_string(kExactMaxLots) + "); the lookahead schedule is shown";
        plan = scheduleWeekly(lots, slots, cycle, horizon, true, lookahead);
    } else {
        ExactSearch search(lots, slots, cycle, horizon, exactNodeLimit);
        search.search(0, 0.0);
        if (search.stopped) {
            out.note = "the exact search stopped at " + std::to_string(exactNodeLimit) + " nodes; the lookahead schedule is shown";
            plan = scheduleWeekly(lots, slots, cycle, horizon, true, lookahead);
        } else {
            plan.scheduled.assign(lots.size(), 0);
            for (size_t i = 0; i < search.order.size(); ++i) {
                if (search.bestSlot[i] < 0) continue;
                plan.scheduled[search.order[i]] = 1;
                plan.batches.push_back({0, search.bestSlot[i], lots[search.order[i]].id, search.bestStart[i], search.bestStart[i] + cycle});
            }
            std::sort(plan.batches.begin(), plan.batches.end(), [](const Batch& a, const Batch& b) {
                return a.start != b.start ? a.start < b.start : a.slot < b.slot;
            });
        }
    }

    out.scheduled = plan.scheduled;
    out.batches = plan.batches;
    for (Batch& batch : out.batches) batch.cropId = out.cropId;
    long long busy = 0;
    for (const Batch& batch : out.batches) busy += std::min(batch.end, horizon) - batch.start;
    out.idleSlotWeeks = int(static_cast<long long>(slots) * horizon - busy);
    for (size_t i = 0; i < lots.size(); ++i) {
        const Lot& lot = lots[i];
        if (lot.hard) ++out.hardTotal; else ++out.softTotal;
        if (out.scheduled[i]) {
            out.revenue += lot.value;
            if (lot.hard) ++out.hardOnTime; else ++out.softServed;
        } else if (lot.hard) {
            out.penalties += lot.penalty;
        }
    }
    return out;
}
