// Component 5 (plan section 11): lots, the slot arithmetic (11.1 to 11.3) and the schedulers (11.4 to 11.9). Expected
// numbers come from plan Appendix A (G5, G6), from hand calculation where a comment says so, and from plan/reference_model.py.
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "farm.h"
#include "forecast.h"
#include "generator.h"
#include "rng.h"
#include "scheduling.h"

namespace {

// A crop with only the fields the lot arithmetic reads.
Crop simpleCrop(int cycle, int window, double yield, double price) {
    Crop c;
    c.id = 3;
    c.name = "Test";
    c.cycleWeeks = cycle;
    c.harvestWindowWeeks = window;
    c.yieldKg = yield;
    c.pricePerKg = price;
    return c;
}

// A hard lot with only the fields the slot arithmetic reads.
Lot hardLot(int id, int release, int latestStart) {
    Lot lot;
    lot.id = id;
    lot.hard = true;
    lot.release = release;
    lot.latestStart = latestStart;
    return lot;
}

// The lots of plan Appendix A, G5: crop with p = 2, f = 0, T = 6; for every delivery week 2 to 7 two soft lots, and
// from week 3 one hard lot as well (kg 1, value 10, penalty 5), the hard lot created first.
std::vector<Lot> g5Lots() {
    const Crop crop = simpleCrop(2, 0, 1.0, 10.0);
    std::vector<double> hardKg(8, 0.0), penaltyRate(8, 5.0), spot(8, 2.0);
    for (int w = 3; w <= 7; ++w) hardKg[w] = 1.0;
    return buildLots(crop, 6, hardKg, penaltyRate, spot);
}

int hardCount(const std::vector<Lot>& lots) {
    return int(std::count_if(lots.begin(), lots.end(), [](const Lot& l) { return l.hard; }));
}

}  // namespace

// Cumulative rounding: 0.4 lots a week is two lots in five weeks (weeks 2 and 4), where rounding each week gives none.
TEST(lots_round_the_running_total) {
    const Crop crop = simpleCrop(1, 0, 1.0, 10.0);
    const std::vector<double> none(6, 0.0), spot = {0, 0.4, 0.4, 0.4, 0.4, 0.4};   // weeks 0 .. 5, delivery weeks 1 .. 5
    const std::vector<Lot> lots = buildLots(crop, 5, none, none, spot);
    CHECK(lots.size() == 2);
    CHECK(lots[0].dueWeek == 2 && lots[1].dueWeek == 4);
    for (const Lot& lot : lots) {
        CHECK(!lot.hard && lot.kg == 1.0 && lot.value == 10.0 && lot.penalty == 0 && lot.cropId == 3);
        CHECK(lot.latestStart == lot.dueWeek - 1 && lot.release == lot.latestStart);   // f = 0: one possible start
    }
    CHECK(lots[0].id == 0 && lots[1].id == 1);
}

// Hard lots by hand: p = 2, f = 1, yield 1 kg, price 100, a contract of 2.5 kg a week in weeks 2 to 4 at a mean penalty
// of 40 PKR/kg, and 1 kg of spot demand a week. Three trays a week (1, 1 and 0.5 kg); the half tray's surplus of 0.5 kg
// goes to the spot market, which with the 1 kg forecast gives 0.5 kg a week: one soft lot in week 2, none in week 3
// (running total 1.0), one in week 4 (1.5).
TEST(lots_hard_lots_and_the_contract_surplus) {
    const Crop crop = simpleCrop(2, 1, 1.0, 100.0);
    const std::vector<double> hardKg = {0, 0, 2.5, 2.5, 2.5}, rate = {0, 0, 40, 40, 40}, spot = {0, 0, 1, 1, 1};
    const std::vector<Lot> lots = buildLots(crop, 3, hardKg, rate, spot);
    CHECK(lots.size() == 11);                                // 3 hard + 1 soft, 3 hard, 3 hard + 1 soft
    const int due[11] = {2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4};
    const bool hard[11] = {true, true, true, false, true, true, true, true, true, true, false};
    const double kg[11] = {1, 1, 0.5, 1, 1, 1, 0.5, 1, 1, 0.5, 1};
    for (int i = 0; i < 11; ++i) {
        CHECK(lots[i].id == i && lots[i].dueWeek == due[i] && lots[i].hard == hard[i]);
        CHECK_NEAR(lots[i].kg, kg[i], 1e-12);
        CHECK_NEAR(lots[i].value, kg[i] * 100, 1e-12);
        CHECK_NEAR(lots[i].penalty, hard[i] ? kg[i] * 40 : 0, 1e-12);
        CHECK(lots[i].latestStart == due[i] - 2);
        CHECK(lots[i].release == std::max(0, due[i] - 2 - 1));   // a harvest may come one week early
    }
}

// Rounding at the edges: kilograms that are a whole number of trays up to floating-point error, a contract too small to
// count, a surplus larger than the spot forecast, and a forecast that changes from week to week.
TEST(lots_edge_cases) {
    // 2.1 kg of 0.3 kg trays is 7 trays, though 2.1 / 0.3 is 7.000000000000001 in floating point (without EPS: 8).
    const Crop third = simpleCrop(2, 0, 0.3, 10.0);
    const std::vector<double> zero(5, 0.0), contract = {0, 0, 2.1, 2.1, 2.1};
    const std::vector<Lot> seven = buildLots(third, 3, contract, zero, zero);
    CHECK(seven.size() == 21 && hardCount(seven) == 21);
    for (const Lot& lot : seven) CHECK_NEAR(lot.kg, 0.3, 1e-12);

    // A contract of 5e-10 kg is below EPS: no lot (it would be one lot of 0.1 kg trays if every positive amount counted).
    const Crop tenth = simpleCrop(2, 0, 0.1, 10.0);
    const std::vector<double> sliver = {0, 0, 5e-10, 5e-10, 5e-10};
    CHECK(buildLots(tenth, 3, sliver, zero, zero).empty());

    // Contract of 0.1 kg for a 1 kg tray (horizon 2, weeks 2 and 3). The tray's 0.9 kg surplus is sold on the spot market:
    // a 2 kg forecast leaves 1.1 kg, one soft lot in week 2 (and 2 kg without the surplus rule would give two lots).
    const Crop whole = simpleCrop(2, 0, 1.0, 10.0);
    const std::vector<double> zero4(4, 0.0), little = {0, 0, 0.1, 0};
    const std::vector<Lot> surplus = buildLots(whole, 2, little, zero4, {0, 0, 2, 0});
    CHECK(surplus.size() == 2 && surplus[0].hard && !surplus[1].hard && surplus[1].dueWeek == 2);
    // A 0.2 kg forecast is less than the surplus: the spot market gets nothing that week, and the shortfall is not
    // carried into week 3, where 2.4 kg is two soft lots. (With a negative running total there would be three.)
    const std::vector<Lot> shortfall = buildLots(whole, 2, little, zero4, {0, 0, 0.2, 2.4});
    CHECK(shortfall.size() == 3 && shortfall[0].hard && shortfall[1].dueWeek == 3 && shortfall[2].dueWeek == 3);
}

// A forecast that is not constant reaches the lots and the demand at the right delivery week (element 0 is week 1).
TEST(crop_demand_uses_the_forecast_by_week) {
    Scenario s = generateScenario(GeneratorParams());
    s.contracts.clear();
    std::vector<double> spot;
    for (int w = 1; w <= 23; ++w) spot.push_back(w);         // 1 kg in week 1, 2 kg in week 2, ...
    const CropDemand d = buildCropDemand(s, 0, spot);        // Lettuce: weeks 5 .. 16, so 5 + 6 + .. + 16 = 126 kg
    CHECK_NEAR(d.demand, 126.0 / 12, 1e-12);
    CHECK(d.lots.size() == 70);                              // 126 kg / 1.8 kg = 70 trays, rounded as a running total
    CHECK(d.lots.front().dueWeek == 5 && d.lots.back().dueWeek == 16);
    CHECK(std::count_if(d.lots.begin(), d.lots.end(), [](const Lot& l) { return l.dueWeek == 5; }) == 3);   // 5 kg is 2.8 trays
    CHECK(d.contractSlots == 0 && d.penaltyPerWeek == 0);
}

// Several contracts in one week: the penalty rate is the kg-weighted mean, so that a lot's penalty equals the sum of
// the penalties it avoids. Contracts outside the delivery window and for other crops are ignored.
TEST(crop_demand_contract_weeks_and_penalty_rate) {
    Scenario s = generateScenario(GeneratorParams());
    s.contracts.clear();
    const int crop = 0;                                      // Lettuce: p = 5, yield 1.8, T = 12, so weeks 5 .. 16
    auto contract = [&](int cropId, double kg, int start, int end, double penalty) {
        Contract c;
        c.id = int(s.contracts.size());
        c.cropId = cropId; c.kgPerWeek = kg; c.startWeek = start; c.endWeek = end; c.penaltyPerKg = penalty;
        s.contracts.push_back(c);
    };
    contract(crop, 0.6, 5, 9999, 100);                       // 0.6 kg at 100 and 1.2 kg at 200 together fill one 1.8 kg tray
    contract(crop, 1.2, 5, 9999, 200);                       //   at a mean rate of (60 + 240) / 1.8 = 166.67 per kg
    contract(crop, 5.0, 1, 3, 9999);                         // ends before the first delivery week: no lots
    contract(1, 7.0, 5, 9999, 9999);                         // another crop
    const std::vector<double> spot(30, 0.0);
    const CropDemand d = buildCropDemand(s, crop, spot);
    CHECK(d.lots.size() == 12);                              // one hard lot in each of the 12 delivery weeks
    for (const Lot& lot : d.lots) {
        CHECK(lot.hard);
        CHECK_NEAR(lot.kg, 1.8, 1e-12);
        CHECK_NEAR(lot.penalty, 0.6 * 100 + 1.2 * 200, 1e-9);
    }
    CHECK_NEAR(d.demand, 12 * 1.8 / 12, 1e-12);              // 1.8 kg a week, no spot demand
    CHECK_NEAR(d.penaltyPerWeek, 300.0, 1e-9);               // 12 lots * 300 / 12 weeks
    CHECK(d.contractSlots == 5);                             // one lot a week, each needs 5 weeks of a slot

    // A contract that starts late and ends early covers only the weeks in between (here weeks 7 and 8).
    s.contracts.clear();
    contract(crop, 1.8, 7, 8, 50);
    const CropDemand part = buildCropDemand(s, crop, spot);
    CHECK(part.lots.size() == 2 && part.lots[0].dueWeek == 7 && part.lots[1].dueWeek == 8);
    CHECK_NEAR(part.demand, 2 * 1.8 / 12, 1e-12);

    CHECK_THROWS(buildCropDemand(s, crop, std::vector<double>(15, 0.0)));   // the forecast must reach week 16
}

// Inputs that would make the program allocate without end are refused, and so are vectors that are too short.
TEST(lots_reject_absurd_or_short_input) {
    const Crop crop = simpleCrop(2, 0, 1.0, 10.0);
    std::vector<double> zero(8, 0.0), huge(8, 0.0);
    huge[3] = 1e9;                                           // a contract of a billion kg a week
    CHECK_THROWS(buildLots(crop, 6, huge, zero, zero));
    CHECK_THROWS(buildLots(crop, 6, zero, zero, huge));      // the same through the spot forecast
    huge[3] = std::numeric_limits<double>::infinity();
    CHECK_THROWS(buildLots(crop, 6, huge, zero, zero));
    CHECK_THROWS(buildLots(crop, 6, std::vector<double>(7, 0.0), zero, zero));   // needs 8 entries
    CHECK(buildLots(crop, 6, zero, zero, zero).empty());
}

// hardMissed: lots taken in the given order by the slot that frees earliest, a lot that cannot start in time is lost.
TEST(hard_missed_examples) {
    // Two slots free at week 0, p = 2: the first two lots take one slot each, the third (latest start 1) finds both busy.
    const std::vector<Lot> three = {hardLot(0, 0, 0), hardLot(1, 0, 0), hardLot(2, 1, 1)};
    CHECK(hardMissed({0, 0}, three, 2) == 1);
    CHECK(hardMissed({0, 0, 0}, three, 2) == 0);
    CHECK(hardMissed({}, three, 2) == 3);                    // no slots: every lot is missed
    CHECK(hardMissed({0, 0}, {}, 2) == 0);                   // no lots
    CHECK(hardMissed({5}, {hardLot(0, 0, 3)}, 2) == 1);      // the slot is free only from week 5

    // A missed lot leaves its slot as it was: X takes the slot until week 3, Y (latest start 1) is lost, and the slot
    // is still free at week 3 for Z.
    const std::vector<Lot> xyz = {hardLot(0, 0, 0), hardLot(1, 1, 1), hardLot(2, 3, 3)};
    CHECK(hardMissed({0}, xyz, 3) == 1);

    // The earliest-free slot is used, and a lot waits for its release: slots free at 0 and 4, lot released at 2.
    CHECK(hardMissed({4, 0}, {hardLot(0, 2, 2), hardLot(1, 0, 4)}, 2) == 0);   // first lot starts at 2 on the slot free at 0
    CHECK(hardMissed({4, 0}, {hardLot(0, 2, 2), hardLot(1, 0, 3)}, 2) == 1);   // second: slots free at 4 and 4 > 3
}

// intervalDepth: ends sort before starts at the same week, so back-to-back intervals share a slot.
TEST(interval_depth_examples) {
    CHECK(intervalDepth({}) == 0);
    CHECK(intervalDepth({{0, 3}}) == 1);
    CHECK(intervalDepth({{0, 2}, {2, 4}}) == 1);             // one ends at 2 as the other starts
    CHECK(intervalDepth({{0, 2}, {1, 3}, {2, 4}}) == 2);
    CHECK(intervalDepth({{0, 10}, {1, 2}, {3, 4}, {5, 6}}) == 2);   // nested
    CHECK(intervalDepth({{0, 3}, {0, 3}, {0, 3}}) == 3);
    CHECK(intervalDepth({{0, 3}, {1, 4}, {2, 5}, {3, 6}, {4, 7}}) == 3);   // G5
}

// G5: the lots, the two slots the hard lots need, and the interval example, both ways.
TEST(slot_arithmetic_g5) {
    const std::vector<Lot> lots = g5Lots();
    CHECK(lots.size() == 17);                                // 12 soft, 5 hard
    CHECK(hardCount(lots) == 5);
    // Week by week: soft lots first in week 2, then in each later week the hard lot before the two soft lots.
    CHECK(!lots[0].hard && !lots[1].hard);
    CHECK(lots[2].hard && !lots[3].hard && !lots[4].hard && lots[2].dueWeek == 3 && lots[4].dueWeek == 3);
    CHECK(lots[5].hard && lots[5].dueWeek == 4);
    for (const Lot& lot : lots) {
        CHECK(lot.release == lot.dueWeek - 2 && lot.latestStart == lot.dueWeek - 2);   // p = 2, f = 0
        CHECK(lot.kg == 1.0 && lot.value == 10.0 && lot.penalty == (lot.hard ? 5.0 : 0.0));
    }
    CHECK(minSlotsForHardLots(lots, 2) == 2);

    // Five hard lots with fixed intervals [0,3) [1,4) [2,5) [3,6) [4,7) (p = 3): depth 3, and three slots.
    std::vector<Lot> fixed;
    std::vector<std::pair<int, int>> intervals;
    for (int s = 0; s < 5; ++s) {
        fixed.push_back(hardLot(s, s, s));
        intervals.push_back({s, s + 3});
    }
    CHECK(intervalDepth(intervals) == 3);
    CHECK(minSlotsForHardLots(fixed, 3) == 3);
}

// With f = 0 every lot has one possible interval, and the fewest slots is the depth of those intervals.
TEST(min_slots_equals_interval_depth_for_fixed_windows) {
    Rng rng(2026);
    int deepest = 0;
    for (int round = 0; round < 300; ++round) {
        const int p = 1 + rng.below(4), count = rng.below(13);
        std::vector<Lot> lots;
        std::vector<std::pair<int, int>> intervals;
        for (int i = 0; i < count; ++i) {
            const int start = rng.below(11);
            Lot lot = hardLot(i, start, start);
            lot.hard = rng.below(4) != 0;                    // some soft lots, which the slot count ignores
            lots.push_back(lot);
            if (lot.hard) intervals.push_back({start, start + p});
        }
        const int depth = intervalDepth(intervals);
        CHECK(minSlotsForHardLots(lots, p) == depth);
        deepest = std::max(deepest, depth);
    }
    CHECK(deepest >= 4);                                     // the instances do reach several slots
}

// A flexible window can save a slot. Lots due in weeks 3 and 4 with p = 2 have fixed intervals [1,3) and [2,4), which
// overlap; with f = 1 the first may start at week 0 and the second at 2.
TEST(min_slots_uses_flexible_windows) {
    const Lot fixedA = hardLot(0, 1, 1), fixedB = hardLot(1, 2, 2);
    CHECK(minSlotsForHardLots({fixedA, fixedB}, 2) == 2);
    const Lot flexA = hardLot(0, 0, 1), flexB = hardLot(1, 1, 2);
    CHECK(minSlotsForHardLots({flexA, flexB}, 2) == 1);
    CHECK(minSlotsForHardLots({}, 2) == 0);
    CHECK(minSlotsForHardLots({Lot()}, 2) == 0);             // a soft lot needs no contract slot
}

// The lots are taken in order of latest start, then id, whatever order they are listed in.
TEST(min_slots_orders_lots_by_deadline) {
    // Listed latest-first: one slot does both when the early lot goes first (0 to 2, then 5), but not the other way round.
    CHECK(minSlotsForHardLots({hardLot(0, 5, 5), hardLot(1, 0, 0)}, 2) == 1);
    // Same latest start: the lower id first. Lot 0 (release 0) then lot 1 (release 2) fits one slot, lot 1 first does not.
    CHECK(minSlotsForHardLots({hardLot(1, 2, 3), hardLot(0, 0, 3)}, 2) == 1);
}

// G6 on the flat farm: the number of lots of each crop (hard + soft; the eight crops of the G6 schedule table match it,
// the other four come from the reference model), the contract slots, the demand and the penalty a week.
TEST(crop_demand_flat_farm) {
    GeneratorParams params;
    params.flat = true;
    const Scenario s = generateScenario(params);
    const int horizon = s.horizonWeeks - 1 + 12;             // the longest cycle is Cherry Tomato's 12 weeks
    const int hard[12] = {24, 12, 0, 0, 0, 12, 0, 0, 24, 0, 12, 0};
    const int soft[12] = {60, 60, 22, 28, 32, 36, 30, 48, 60, 36, 24, 6};
    const int slots[12] = {10, 4, 0, 0, 0, 4, 0, 0, 4, 0, 10, 0};
    const double demand[12] = {12.6, 7.2, 4.8, 4.0, 2.4, 3.6, 2.0, 2.4, 2.1, 1.35, 10.5, 3.5};
    const double penalty[12] = {2160, 840, 0, 0, 0, 1350, 0, 0, 1800, 0, 5250, 0};
    for (const Crop& c : s.crops) {
        const ForecastResult f = forecastSeries(s.orderHistory[c.id], s.seasonLength, horizon);
        const CropDemand d = buildCropDemand(s, c.id, f.forecast);
        CHECK(hardCount(d.lots) == hard[c.id]);
        CHECK(int(d.lots.size()) == hard[c.id] + soft[c.id]);
        CHECK(d.contractSlots == slots[c.id]);
        CHECK_NEAR(d.demand, demand[c.id], 1e-9);
        CHECK_NEAR(d.penaltyPerWeek, penalty[c.id], 1e-9);
        for (size_t i = 0; i < d.lots.size(); ++i) {
            CHECK(d.lots[i].id == int(i) && d.lots[i].cropId == c.id);
            CHECK(d.lots[i].latestStart == d.lots[i].dueWeek - c.cycleWeeks);
            CHECK(d.lots[i].release == std::max(0, d.lots[i].latestStart - c.harvestWindowWeeks));
            CHECK(d.lots[i].dueWeek >= c.cycleWeeks && d.lots[i].dueWeek <= s.horizonWeeks - 1 + c.cycleWeeks);
        }
    }
    // Plan 9.3's worked example: Lettuce has 24 hard lots, two a week, of 1.8 kg with a penalty of 1080 each.
    const ForecastResult f = forecastSeries(s.orderHistory[0], s.seasonLength, horizon);
    const CropDemand lettuce = buildCropDemand(s, 0, f.forecast);
    for (const Lot& lot : lettuce.lots)
        if (lot.hard) CHECK_NEAR(lot.penalty, 1080, 1e-9);
}

// ==== The schedulers (plan 11.4 to 11.9, milestone M6) =========================================================================

namespace {

// One lot for the scheduler tests: due week w with cycle p and window f gives latestStart w - p and release max(0, ls - f).
Lot schedLot(int id, int due, bool hard, double value, double penalty, int p, int f, int cropId = 0) {
    Lot lot;
    lot.id = id;
    lot.cropId = cropId;
    lot.dueWeek = due;
    lot.hard = hard;
    lot.kg = 1.0;
    lot.value = value;
    lot.penalty = hard ? penalty : 0.0;
    lot.latestStart = due - p;
    lot.release = std::max(0, lot.latestStart - f);
    return lot;
}

struct Spec { int slot, start, end; char kind; int due; };

// The batches of a schedule as (slot, start, end, kind, due), in the order the scheduler made them.
std::vector<Spec> specsOf(const CropSchedule& s) {
    std::vector<Spec> out;
    for (const Batch& b : s.batches) out.push_back({b.slot, b.start, b.end, s.lots[b.lotId].hard ? 'H' : 'S', s.lots[b.lotId].dueWeek});
    return out;
}

bool sameSpecs(const std::vector<Spec>& a, const std::vector<Spec>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].start != b[i].start || a[i].end != b[i].end || a[i].kind != b[i].kind || a[i].due != b[i].due) return false;
    return true;
}

double netValue(const CropSchedule& s) { return s.revenue - s.penalties; }

// True when the call throws std::invalid_argument and not some other exception that a bad argument happens to cause.
template <class F>
bool throwsInvalid(F call) {
    try {
        call();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
    }
    return false;
}

// Plan 11.9: no overlap on a slot, each lot at most once, every start inside its window and before the horizon; and the
// statistics agree with the batches.
void checkValidSchedule(const CropSchedule& s, int p, int horizon) {
    std::vector<int> times(s.lots.size(), 0);
    std::vector<std::vector<std::pair<int, int>>> perSlot(s.slots);
    long long busy = 0;
    for (const Batch& b : s.batches) {
        CHECK(b.lotId >= 0 && b.lotId < int(s.lots.size()) && b.slot >= 0 && b.slot < s.slots);
        if (b.lotId < 0 || b.lotId >= int(s.lots.size()) || b.slot < 0 || b.slot >= s.slots) return;
        const Lot& lot = s.lots[b.lotId];
        ++times[b.lotId];
        CHECK(b.start >= lot.release && b.start <= lot.latestStart && b.start < horizon && b.start >= 0);
        CHECK(b.end == b.start + p && b.cropId == s.cropId);
        for (const auto& other : perSlot[b.slot]) CHECK(b.end <= other.first || other.second <= b.start);
        perSlot[b.slot].push_back({b.start, b.end});
        busy += std::min(b.end, horizon) - b.start;
    }
    int hardTotal = 0, hardOk = 0, softTotal = 0, softOk = 0;
    double revenue = 0, penalties = 0;
    for (size_t i = 0; i < s.lots.size(); ++i) {
        CHECK(times[i] <= 1 && (times[i] == 1) == (s.scheduled[i] != 0));
        if (s.lots[i].hard) { ++hardTotal; hardOk += s.scheduled[i] ? 1 : 0; if (!s.scheduled[i]) penalties += s.lots[i].penalty; }
        else { ++softTotal; softOk += s.scheduled[i] ? 1 : 0; }
        if (s.scheduled[i]) revenue += s.lots[i].value;
    }
    CHECK(s.scheduled.size() == s.lots.size());
    CHECK(s.hardTotal == hardTotal && s.hardOnTime == hardOk && s.softTotal == softTotal && s.softServed == softOk);
    CHECK_NEAR(s.revenue, revenue, 1e-9);
    CHECK_NEAR(s.penalties, penalties, 1e-9);
    CHECK(s.idleSlotWeeks == s.slots * horizon - busy);
}

}  // namespace

// G5: one crop with p = 2, f = 0, two slots and six weeks. EDF fills both slots with soft lots and misses three hard lots;
// lookahead keeps a slot free for each hard lot and misses none. The exact optimum is 60.
TEST(schedule_g5_edf_lookahead_and_exact) {
    const std::vector<Lot> lots = g5Lots();
    const CropSchedule edf = scheduleCrop(lots, 2, 2, 6, SchedAlgo::Edf);
    CHECK(edf.hardOnTime == 2 && edf.hardTotal == 5 && edf.softServed == 4 && edf.softTotal == 12 && edf.idleSlotWeeks == 0);
    CHECK_NEAR(netValue(edf), 45, 1e-9);
    CHECK(sameSpecs(specsOf(edf), {{0, 0, 2, 'S', 2}, {1, 0, 2, 'S', 2}, {0, 2, 4, 'H', 4}, {1, 2, 4, 'S', 4}, {0, 4, 6, 'H', 6}, {1, 4, 6, 'S', 6}}));
    checkValidSchedule(edf, 2, 6);

    const CropSchedule look = scheduleCrop(lots, 2, 2, 6, SchedAlgo::EdfLookahead);
    CHECK(look.hardOnTime == 5 && look.hardTotal == 5 && look.softServed == 1 && look.softTotal == 12 && look.idleSlotWeeks == 1);
    CHECK_NEAR(netValue(look), 60, 1e-9);
    CHECK(sameSpecs(specsOf(look), {{0, 0, 2, 'S', 2}, {1, 1, 3, 'H', 3}, {0, 2, 4, 'H', 4}, {1, 3, 5, 'H', 5}, {0, 4, 6, 'H', 6}, {1, 5, 7, 'H', 7}}));
    CHECK(look.penalties == 0 && look.revenue == 60 && edf.revenue == 60 && edf.penalties == 15);
    CHECK(look.note.empty() && edf.note.empty() && look.cropId == 3 && look.slots == 2);
    checkValidSchedule(look, 2, 6);

    const CropSchedule exact = scheduleCrop(lots, 2, 2, 6, SchedAlgo::Exact);
    CHECK_NEAR(netValue(exact), 60, 1e-9);
    CHECK(exact.note.empty() && exact.hardOnTime == 5);
    checkValidSchedule(exact, 2, 6);
    for (size_t i = 1; i < exact.batches.size(); ++i) {   // the exact schedule lists its batches by week, then slot
        const Batch& a = exact.batches[i - 1];
        const Batch& b = exact.batches[i];
        CHECK(a.start < b.start || (a.start == b.start && a.slot < b.slot));
    }
    CHECK(exact.batches.size() >= 5);

    // Lookahead with L = 0 never schedules fewer hard lots than plain EDF here; L = -1 is the cycle.
    CHECK(scheduleCrop(lots, 2, 2, 6, SchedAlgo::EdfLookahead, 0).hardOnTime >= edf.hardOnTime);
    CHECK(sameSpecs(specsOf(scheduleCrop(lots, 2, 2, 6, SchedAlgo::EdfLookahead, 2)), specsOf(look)));
}

// The order EDF takes lots in: earliest due week first, then a hard lot before a soft one, then the higher value, then the
// lower id. One slot, p = 1, so only one lot can go per week.
TEST(schedule_edf_order) {
    // All due in week 1 (latest start 0), one week: the hard lot goes although it is worth the least.
    std::vector<Lot> lots = {schedLot(0, 1, false, 5, 0, 1, 0), schedLot(1, 1, false, 9, 0, 1, 0), schedLot(2, 1, true, 1, 3, 1, 0)};
    CropSchedule s = scheduleCrop(lots, 1, 1, 1, SchedAlgo::Edf);
    CHECK(s.batches.size() == 1 && s.batches[0].lotId == 2);
    // Without the hard lot, the higher value.
    lots.pop_back();
    s = scheduleCrop(lots, 1, 1, 1, SchedAlgo::Edf);
    CHECK(s.batches.size() == 1 && s.batches[0].lotId == 1);
    // Equal values: the lower id, wherever it stands in the list.
    lots = {schedLot(7, 1, false, 9, 0, 1, 0), schedLot(3, 1, false, 9, 0, 1, 0)};
    s = scheduleCrop(lots, 1, 1, 1, SchedAlgo::Edf);
    CHECK(s.batches.size() == 1 && s.batches[0].lotId == 3);
    // An earlier due week wins over a better lot: week 0 takes the lot due in week 1, week 1 the one due in week 2.
    lots = {schedLot(0, 2, true, 100, 50, 1, 1), schedLot(1, 1, false, 1, 0, 1, 1)};
    s = scheduleCrop(lots, 1, 1, 2, SchedAlgo::Edf);
    CHECK(s.batches.size() == 2 && s.batches[0].lotId == 1 && s.batches[0].start == 0 && s.batches[1].lotId == 0 && s.batches[1].start == 1);
    // A lot that is not released yet is skipped, and one past its latest start is gone: nothing at week 0 for a lot that
    // may start at week 1 only, and the slot stays idle that week.
    lots = {schedLot(0, 2, false, 5, 0, 1, 0)};
    s = scheduleCrop(lots, 1, 1, 3, SchedAlgo::Edf);
    CHECK(s.batches.size() == 1 && s.batches[0].start == 1 && s.idleSlotWeeks == 2);
    lots = {schedLot(0, 1, false, 5, 0, 1, 0), schedLot(1, 1, false, 4, 0, 1, 0)};   // both only at week 0; one slot
    s = scheduleCrop(lots, 1, 1, 3, SchedAlgo::Edf);
    CHECK(s.batches.size() == 1 && s.batches[0].lotId == 0 && s.softServed == 1 && s.softTotal == 2);
    // Slots are taken in order within a week, and a slot busy for p weeks is skipped.
    lots = {schedLot(0, 2, false, 5, 0, 2, 2), schedLot(1, 3, false, 5, 0, 2, 2), schedLot(2, 4, false, 5, 0, 2, 2)};
    s = scheduleCrop(lots, 2, 2, 4, SchedAlgo::Edf);
    CHECK(s.batches.size() == 3 && s.batches[0].slot == 0 && s.batches[1].slot == 1 && s.batches[2].slot == 0 && s.batches[2].start == 2);
    checkValidSchedule(s, 2, 4);
}

// Lookahead: a released hard lot is taken before an earlier-due soft lot; a soft lot is held back only when it would cost a
// hard lot that is due soon; the window L says how far ahead it looks.
TEST(schedule_lookahead_rules) {
    // Week 0: a soft lot due at week 1 (ls 0) and a hard lot due at week 2 (ls 1, release 0 because f = 1) are both released.
    // EDF takes the soft lot first (earlier due); lookahead takes the hard lot.
    std::vector<Lot> lots = {schedLot(0, 1, false, 5, 0, 1, 1), schedLot(1, 2, true, 1, 9, 1, 1)};
    CHECK(scheduleCrop(lots, 1, 1, 1, SchedAlgo::Edf).batches[0].lotId == 0);
    CHECK(scheduleCrop(lots, 1, 1, 1, SchedAlgo::EdfLookahead).batches[0].lotId == 1);

    // One slot, p = 2. At week 0 a soft lot (ls 0) is released; a hard lot has ls = 1 and release = 1. Taking the soft lot
    // blocks the slot until week 2, past the hard lot's latest start: it is refused when the window reaches week 1 (L >= 1)
    // and taken when it does not (L = 0).
    lots = {schedLot(0, 2, false, 5, 0, 2, 0), schedLot(1, 3, true, 1, 9, 2, 0)};
    CHECK(scheduleCrop(lots, 1, 2, 4, SchedAlgo::EdfLookahead, 0).batches[0].lotId == 0);
    CHECK(scheduleCrop(lots, 1, 2, 4, SchedAlgo::EdfLookahead, 1).batches[0].lotId == 1);
    CHECK(scheduleCrop(lots, 1, 2, 4, SchedAlgo::EdfLookahead, -1).batches[0].lotId == 1);   // -1 is the cycle, 2
    CHECK(scheduleCrop(lots, 1, 2, 4, SchedAlgo::EdfLookahead, 1).hardOnTime == 1);
    CHECK(scheduleCrop(lots, 1, 2, 4, SchedAlgo::EdfLookahead, 0).hardOnTime == 0);
    // The window runs from t to t + L, both ends included. One slot, p = 3: a soft lot due in week 3 (start 0) and a hard lot due
    // in week 5 (start 2 only). Starting the soft lot blocks the slot until week 3, which loses the hard lot. At week 0 the hard
    // lot is 2 weeks ahead: outside a window of 1 (the soft lot is taken, the hard lot lost), inside a window of 2 (refused).
    lots = {schedLot(0, 3, false, 5, 0, 3, 0), schedLot(1, 5, true, 1, 9, 3, 0)};
    CropSchedule edge = scheduleCrop(lots, 1, 3, 6, SchedAlgo::EdfLookahead, 1);
    CHECK(edge.softServed == 1 && edge.hardOnTime == 0);
    edge = scheduleCrop(lots, 1, 3, 6, SchedAlgo::EdfLookahead, 2);
    CHECK(edge.softServed == 0 && edge.hardOnTime == 1 && edge.batches.size() == 1 && edge.batches[0].start == 2);
    // A hard lot whose start the soft lot does not touch never holds it back: the slot is free again in time.
    lots = {schedLot(0, 2, false, 5, 0, 2, 0), schedLot(1, 5, true, 1, 9, 2, 0)};   // soft starts at 0 and ends at 2; hard starts at 3
    CHECK(scheduleCrop(lots, 1, 2, 6, SchedAlgo::EdfLookahead, 3).softServed == 1);
    CHECK(scheduleCrop(lots, 1, 2, 6, SchedAlgo::EdfLookahead, 3).hardOnTime == 1);
    // A second slot makes room: both lots are served.
    lots = {schedLot(0, 2, false, 5, 0, 2, 0), schedLot(1, 3, true, 1, 9, 2, 0)};
    const CropSchedule two = scheduleCrop(lots, 2, 2, 4, SchedAlgo::EdfLookahead, 1);
    CHECK(two.hardOnTime == 1 && two.softServed == 1);
    checkValidSchedule(two, 2, 4);
}

// Details of the admission test that a few lots can show: the default window is exactly the cycle, hard lots with the same
// latest start are tried in id order, and hard lots that are lost anyway do not hold back a soft lot.
TEST(schedule_admission_details) {
    // p = 2, one slot, four weeks. The soft lot (start 0 or 1) is taken at week 0 when the window is 2 weeks (it leaves the
    // slot free at week 2, in time for the hard lot due in week 4, which has latest start 2); a window of 3 also sees the
    // hard lot due in week 5 (latest start 3), which the soft lot would cost, so it is refused.
    const std::vector<Lot> plus = {schedLot(0, 5, true, 10, 7, 2, 1), schedLot(1, 4, true, 5, 5, 2, 1), schedLot(2, 3, false, 10, 0, 2, 1)};
    CHECK(sameSpecs(specsOf(scheduleCrop(plus, 1, 2, 4, SchedAlgo::EdfLookahead)), {{0, 0, 2, 'S', 3}, {0, 2, 4, 'H', 4}}));
    CHECK(sameSpecs(specsOf(scheduleCrop(plus, 1, 2, 4, SchedAlgo::EdfLookahead, 2)), {{0, 0, 2, 'S', 3}, {0, 2, 4, 'H', 4}}));
    CHECK(sameSpecs(specsOf(scheduleCrop(plus, 1, 2, 4, SchedAlgo::EdfLookahead, 3)), {{0, 1, 3, 'H', 4}, {0, 3, 5, 'H', 5}}));
    // The other way: a window of 1 week misses the hard lot with latest start 2 that a window of 2 weeks sees.
    const std::vector<Lot> minus = {schedLot(0, 4, true, 5, 8, 2, 0), schedLot(1, 3, true, 5, 4, 2, 0), schedLot(2, 2, false, 5, 0, 2, 0)};
    CHECK(sameSpecs(specsOf(scheduleCrop(minus, 1, 2, 4, SchedAlgo::EdfLookahead)), {{0, 0, 2, 'S', 2}, {0, 2, 4, 'H', 4}}));
    CHECK(sameSpecs(specsOf(scheduleCrop(minus, 1, 2, 4, SchedAlgo::EdfLookahead, 1)), {{0, 1, 3, 'H', 3}}));

    // Two hard lots with the same latest start (5) and different release weeks (3 and 1): in id order the first one blocks
    // the second, with or without the soft lot, so the soft lot costs nothing and is taken; in the opposite order it would
    // look as if it cost a lot. p = 3, one slot, a window of 5 weeks.
    const std::vector<Lot> ties = {schedLot(0, 8, true, 10, 5, 3, 2), schedLot(1, 8, true, 10, 5, 3, 4), schedLot(2, 3, false, 10, 0, 3, 0)};
    const CropSchedule tied = scheduleCrop(ties, 1, 3, 7, SchedAlgo::EdfLookahead, 5);
    CHECK(sameSpecs(specsOf(tied), {{0, 0, 3, 'S', 3}, {0, 3, 6, 'H', 8}}));
    CHECK(tied.hardOnTime == 1 && tied.softServed == 1);
    // The same lots with the ids swapped (so that the lot with the early release has the higher id) take the other branch.
    const std::vector<Lot> swapped = {schedLot(1, 8, true, 10, 5, 3, 2), schedLot(0, 8, true, 10, 5, 3, 4), schedLot(2, 3, false, 10, 0, 3, 0)};
    const CropSchedule other = scheduleCrop(swapped, 1, 3, 7, SchedAlgo::EdfLookahead, 5);
    CHECK(other.hardOnTime == 2 && other.softServed == 0);

    // Two hard lots that can only be started at week 5, so one of them is lost whatever happens before: the soft lot at week
    // 0 changes nothing (1 lost either way) and is admitted. Admitting only when nothing is lost would refuse it.
    const std::vector<Lot> doomed = {schedLot(0, 3, false, 10, 0, 3, 0), schedLot(1, 8, true, 10, 5, 3, 0), schedLot(2, 8, true, 10, 5, 3, 0)};
    const CropSchedule lost = scheduleCrop(doomed, 1, 3, 7, SchedAlgo::EdfLookahead, 5);
    CHECK(sameSpecs(specsOf(lost), {{0, 0, 3, 'S', 3}, {0, 5, 8, 'H', 8}}));
    CHECK(lost.hardOnTime == 1 && lost.hardTotal == 2 && lost.softServed == 1);
}

TEST(schedule_edge_cases_and_arguments) {
    const std::vector<Lot> lots = g5Lots();
    // No lots, no slots, no weeks.
    for (SchedAlgo algo : {SchedAlgo::Edf, SchedAlgo::EdfLookahead, SchedAlgo::Exact}) {
        const CropSchedule none = scheduleCrop({}, 3, 2, 6, algo);
        CHECK(none.batches.empty() && none.hardTotal == 0 && none.softTotal == 0 && none.idleSlotWeeks == 18 && none.revenue == 0 && none.cropId == 0);
        // A crop with lots but no slots: every hard lot is missed, with its penalty (plan 11.7).
        const CropSchedule noSlots = scheduleCrop(lots, 0, 2, 6, algo);
        CHECK(noSlots.batches.empty() && noSlots.hardTotal == 5 && noSlots.hardOnTime == 0 && noSlots.softServed == 0 && noSlots.idleSlotWeeks == 0);
        CHECK_NEAR(noSlots.penalties, 25, 1e-9);
        CHECK(noSlots.cropId == 3 && noSlots.lots.size() == 17 && noSlots.scheduled.size() == 17);
        const CropSchedule noWeeks = scheduleCrop(lots, 2, 2, 0, algo);
        CHECK(noWeeks.batches.empty() && noWeeks.idleSlotWeeks == 0);
    }
    // Arguments.
    for (SchedAlgo algo : {SchedAlgo::Edf, SchedAlgo::EdfLookahead, SchedAlgo::Exact}) {
        CHECK(throwsInvalid([&] { scheduleCrop(lots, -1, 2, 6, algo); }));
        CHECK(throwsInvalid([&] { scheduleCrop(lots, 2, 2, -1, algo); }));
        CHECK(throwsInvalid([&] { scheduleCrop(lots, 2, 0, 6, algo); }));
        CHECK(throwsInvalid([&] { scheduleCrop(lots, 2, -3, 6, algo); }));
        CHECK(throwsInvalid([&] { scheduleCrop(lots, 2, 2, 6, algo, -2); }));
        CHECK(!throwsInvalid([&] { scheduleCrop(lots, 2, 2, 6, algo, -1); }));   // -1 and 0 are fine
        CHECK(!throwsInvalid([&] { scheduleCrop(lots, 2, 1, 6, algo, 0); }));
    }
    // A batch that runs past the horizon counts only the weeks inside it: one slot, p = 3, a lot started at week 4 of 5.
    const CropSchedule late = scheduleCrop({schedLot(0, 7, false, 5, 0, 3, 0)}, 1, 3, 5, SchedAlgo::Edf);
    CHECK(late.batches.size() == 1 && late.batches[0].start == 4 && late.batches[0].end == 7 && late.idleSlotWeeks == 5 - 1);
    // No batch may start at or after the horizon: the lot above with a horizon of 4 cannot be started.
    CHECK(scheduleCrop({schedLot(0, 7, false, 5, 0, 3, 0)}, 1, 3, 4, SchedAlgo::Exact).batches.empty());
    // The exact schedule lists its batches by start week even when the search finds them in the other order: the lot with the
    // earlier latest start (3) is placed first, at week 3, and the lot due later but startable at week 0 goes on the second slot.
    const CropSchedule listed = scheduleCrop({schedLot(0, 5, true, 10, 5, 2, 0), schedLot(1, 6, true, 10, 5, 2, 6)}, 2, 2, 6, SchedAlgo::Exact);
    CHECK(listed.batches.size() == 2 && listed.batches[0].lotId == 1 && listed.batches[0].start == 0 && listed.batches[0].slot == 1);
    CHECK(listed.batches[1].lotId == 0 && listed.batches[1].start == 3 && listed.batches[1].slot == 0);
    checkValidSchedule(listed, 2, 6);
    // The exact search shows the lookahead schedule when it cannot run, and says why.
    std::vector<Lot> many;
    for (int i = 0; i < kExactMaxLots + 1; ++i) many.push_back(schedLot(i, 2 + i % 4, i % 3 == 0, 10, 5, 2, 0));
    const CropSchedule big = scheduleCrop(many, 3, 2, 6, SchedAlgo::Exact);
    CHECK(big.note.find("too many lots") != std::string::npos);
    CHECK(sameSpecs(specsOf(big), specsOf(scheduleCrop(many, 3, 2, 6, SchedAlgo::EdfLookahead))));
    many.pop_back();   // exactly the limit is still searched (and may stop at the node limit instead, which is another note)
    CHECK(scheduleCrop(many, 3, 2, 6, SchedAlgo::Exact).note.find("too many") == std::string::npos);
    const CropSchedule limited = scheduleCrop(lots, 2, 2, 6, SchedAlgo::Exact, -1, 5);
    CHECK(limited.note.find("stopped at 5 nodes") != std::string::npos);
    CHECK(sameSpecs(specsOf(limited), specsOf(scheduleCrop(lots, 2, 2, 6, SchedAlgo::EdfLookahead))));
    CHECK(scheduleCrop(lots, 2, 2, 6, SchedAlgo::Exact, -1, 0).note.find("stopped") != std::string::npos);   // a limit of 0 stops at once
    CHECK(scheduleCrop(lots, 2, 2, 6, SchedAlgo::Exact, -1, 1000000).note.empty());
}

namespace {

// The best value over every way to give each lot no batch or a batch on any slot at any week of its window, found by trying
// them all: no assumption that a batch starts as early as it can.
void bruteForce(const std::vector<Lot>& lots, size_t i, int p, int horizon, std::vector<std::vector<std::pair<int, int>>>& slots,
                double current, double& best) {
    if (i == lots.size()) { best = std::max(best, current); return; }
    const Lot& lot = lots[i];
    bruteForce(lots, i + 1, p, horizon, slots, current - (lot.hard ? lot.penalty : 0.0), best);   // skipped
    for (auto& slot : slots)
        for (int start = lot.release; start <= lot.latestStart && start < horizon; ++start) {
            bool free = true;
            for (const auto& taken : slot) free = free && (start + p <= taken.first || taken.second <= start);
            if (!free) continue;
            slot.push_back({start, start + p});
            bruteForce(lots, i + 1, p, horizon, slots, current + lot.value, best);
            slot.pop_back();
        }
}

struct SmallInstance {
    std::vector<Lot> lots;
    int slots = 1, p = 1, horizon = 5;
};

// Plan 11.9: p in 1..3, f in 0..2, T in 5..7, 1 or 2 slots, 3 to 6 lots.
SmallInstance randomSmall(Rng& rng) {
    SmallInstance in;
    in.p = 1 + rng.below(3);
    const int f = rng.below(3);
    in.horizon = 5 + rng.below(3);
    in.slots = 1 + rng.below(2);
    const int count = 3 + rng.below(4);
    for (int i = 0; i < count; ++i) {
        const int due = in.p + rng.below(in.horizon);
        const bool hard = rng.below(2) == 0;
        in.lots.push_back(schedLot(i, due, hard, 5 + rng.below(20), 1 + rng.below(15), in.p, f));
    }
    return in;
}

}  // namespace

// Plan 11.9, the property: on 300 random small instances every schedule is valid and the exact value is at least that of EDF
// and of lookahead. Not asserted: that lookahead beats EDF, which it does not always (17 of 400 cases in the plan's check).
// And an independent brute force over every slot and week agrees with the exact search on every instance.
TEST(schedule_random_small_instances) {
    Rng rng(11);
    int exactBeatsLookahead = 0;
    for (int round = 0; round < 300; ++round) {
        const SmallInstance in = randomSmall(rng);
        const CropSchedule edf = scheduleCrop(in.lots, in.slots, in.p, in.horizon, SchedAlgo::Edf);
        const CropSchedule look = scheduleCrop(in.lots, in.slots, in.p, in.horizon, SchedAlgo::EdfLookahead);
        const CropSchedule exact = scheduleCrop(in.lots, in.slots, in.p, in.horizon, SchedAlgo::Exact);
        for (const CropSchedule* s : {&edf, &look, &exact}) checkValidSchedule(*s, in.p, in.horizon);
        CHECK(exact.note.empty());
        CHECK(netValue(exact) >= netValue(edf) - 1e-9 && netValue(exact) >= netValue(look) - 1e-9);
        std::vector<std::vector<std::pair<int, int>>> slots(in.slots);
        double best = -1e18;
        bruteForce(in.lots, 0, in.p, in.horizon, slots, 0.0, best);
        CHECK_NEAR(netValue(exact), best, 1e-9);
        if (netValue(exact) > netValue(look) + 1e-9) ++exactBeatsLookahead;
    }
    CHECK(exactBeatsLookahead > 10);   // the heuristics are not already optimal on these instances
}

// G6: on the slot counts of the FFD final plan, EDF and lookahead per crop and in total, and the effect of the window.
TEST(schedule_g6_flat_farm) {
    const Farm farm = makeFarm(flatScenario());
    struct Row { int crop, slots; int edfHard, hardTotal, edfSoft, softTotal, edfIdle; int laHard, laSoft, laIdle; };
    const Row rows[] = {
        {0, 10, 12, 24, 18, 60, 3, 24, 0, 20},    // Lettuce
        {1, 24, 12, 12, 60, 60, 36, 12, 60, 36},  // Baby Spinach
        {2, 10, 0, 0, 20, 22, 14, 0, 20, 14},     // Kale
        {4, 1, 0, 0, 4, 32, 0, 0, 4, 0},          // Arugula
        {5, 16, 12, 12, 36, 36, 24, 12, 36, 24},  // Basil
        {8, 14, 24, 24, 60, 60, 7, 24, 60, 7},    // Microgreens
        {9, 6, 0, 0, 36, 36, 3, 0, 36, 3},        // Pea Shoots
        {10, 13, 7, 12, 12, 24, 15, 12, 7, 36},   // Strawberry
    };
    struct Totals { int hard = 0, soft = 0, idle = 0; double revenue = 0, penalties = 0; } edfTotal, laTotal;
    for (const Row& r : rows) {
        const Crop& crop = farm.scenario.crops[r.crop];
        const std::vector<Lot>& lots = farm.demand[r.crop].lots;
        const CropSchedule edf = scheduleCrop(lots, r.slots, crop.cycleWeeks, farm.scenario.horizonWeeks, SchedAlgo::Edf);
        const CropSchedule la = scheduleCrop(lots, r.slots, crop.cycleWeeks, farm.scenario.horizonWeeks, SchedAlgo::EdfLookahead);
        CHECK(edf.hardTotal == r.hardTotal && la.hardTotal == r.hardTotal && edf.softTotal == r.softTotal && la.softTotal == r.softTotal);
        CHECK(edf.hardOnTime == r.edfHard && edf.softServed == r.edfSoft && edf.idleSlotWeeks == r.edfIdle);
        CHECK(la.hardOnTime == r.laHard && la.softServed == r.laSoft && la.idleSlotWeeks == r.laIdle);
        checkValidSchedule(edf, crop.cycleWeeks, farm.scenario.horizonWeeks);
        checkValidSchedule(la, crop.cycleWeeks, farm.scenario.horizonWeeks);
        edfTotal.hard += edf.hardOnTime; edfTotal.soft += edf.softServed; edfTotal.idle += edf.idleSlotWeeks;
        edfTotal.revenue += edf.revenue; edfTotal.penalties += edf.penalties;
        laTotal.hard += la.hardOnTime; laTotal.soft += la.softServed; laTotal.idle += la.idleSlotWeeks;
        laTotal.revenue += la.revenue; laTotal.penalties += la.penalties;
    }
    CHECK(edfTotal.hard == 67 && edfTotal.soft == 246 && edfTotal.idle == 102);
    CHECK_NEAR(edfTotal.revenue, 690360, 1e-6);
    CHECK_NEAR(edfTotal.penalties, 39210, 1e-6);
    CHECK_NEAR(edfTotal.revenue - edfTotal.penalties, 651150, 1e-6);
    CHECK(laTotal.hard == 84 && laTotal.soft == 223 && laTotal.idle == 140);
    CHECK_NEAR(laTotal.revenue, 678480, 1e-6);
    CHECK_NEAR(laTotal.penalties, 0, 1e-6);
    CHECK_NEAR(laTotal.revenue - laTotal.penalties, 678480, 1e-6);

    // The depth of the lookahead on the same input: hard lots on time out of 84. Plain EDF 67; L = 0 67; L = 1 69; L = the
    // cycle (the default) 84; unlimited 84.
    auto hardOnTime = [&](SchedAlgo algo, int lookahead) {
        int total = 0;
        for (const Row& r : rows)
            total += scheduleCrop(farm.demand[r.crop].lots, r.slots, farm.scenario.crops[r.crop].cycleWeeks, farm.scenario.horizonWeeks, algo, lookahead).hardOnTime;
        return total;
    };
    CHECK(hardOnTime(SchedAlgo::Edf, -1) == 67);
    CHECK(hardOnTime(SchedAlgo::EdfLookahead, 0) == 67);
    CHECK(hardOnTime(SchedAlgo::EdfLookahead, 1) == 69);
    CHECK(hardOnTime(SchedAlgo::EdfLookahead, -1) == 84);
    CHECK(hardOnTime(SchedAlgo::EdfLookahead, 1000000) == 84);
    CHECK(hardOnTime(SchedAlgo::EdfLookahead, 2147483647) == 84);   // no overflow at the largest window

    // A crop with lots but no slots shows its contract as missed: Lettuce without slots misses all 24 hard lots at 1080 each.
    const CropSchedule abandoned = scheduleCrop(farm.demand[0].lots, 0, farm.scenario.crops[0].cycleWeeks, farm.scenario.horizonWeeks, SchedAlgo::EdfLookahead);
    CHECK(abandoned.hardOnTime == 0 && abandoned.hardTotal == 24 && abandoned.softServed == 0);
    CHECK_NEAR(abandoned.penalties, 24 * 1080, 1e-6);
}
