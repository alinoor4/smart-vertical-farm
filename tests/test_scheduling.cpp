// Component 5, first part (plan sections 11.1 to 11.3): lots and the slot arithmetic. The schedulers and their tests
// (11.4 to 11.9) come with milestone M6. Expected numbers come from plan Appendix A (G5, G6), from hand calculation
// where a comment says so, and from plan/reference_model.py.
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "check.h"
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
