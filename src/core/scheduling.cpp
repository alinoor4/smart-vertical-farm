#include "scheduling.h"

#include <algorithm>
#include <cmath>
#include <functional>
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
