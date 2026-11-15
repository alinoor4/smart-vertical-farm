#pragma once
// Component 5 (plan section 11): delivery lots, slot arithmetic and the schedulers (earliest deadline first, the same with
// lookahead, and an exact search for small cases). The crop-selection knapsack needs the first two parts: how many
// slots a crop's contracts need.
#include <string>
#include <utility>
#include <vector>

#include "model.h"

// A lot is one tray's worth of one delivery (plan 11.1). It may be planted at any week s with
// release <= s <= latestStart, and is harvested p weeks later.
struct Lot {
    int id = 0, cropId = 0, dueWeek = 0;
    bool hard = false;               // contract delivery; soft = forecast spot demand
    double kg = 0, value = 0, penalty = 0;
    int release = 0, latestStart = 0;
};

// The lots of one crop for the delivery weeks p .. horizon-1+p (plan 11.2), numbered in creation order: in each week
// the hard lots first, then the soft lots. hardKg, penaltyRate and spot are indexed by delivery week and need an
// entry for every week 0 .. horizon-1+p. Throws std::invalid_argument when they are too short, and std::runtime_error
// when the crop would need more than 20 000 lots (a kgPerWeek or a history value that is off by orders of magnitude).
// Time O(horizon + J) for J lots.
std::vector<Lot> buildLots(const Crop& crop, int horizon, const std::vector<double>& hardKg,
                           const std::vector<double>& penaltyRate, const std::vector<double>& spot);

// If the slots become free at the weeks in freeAt, how many of these hard lots (sorted by latestStart, then id) miss
// their deadline when each is taken in that order by the slot that frees earliest (plan 11.3). Time O(H log k) for
// H lots and k slots.
int hardMissed(const std::vector<int>& freeAt, const std::vector<Lot>& hardLotsSorted, int p);

// The fewest slots, all free at week 0, that meet every hard lot's deadline: the "contract slots" of section 9.
// Tries 0, 1, 2, .. slots, so time O(Jh^2 log k) for Jh hard lots.
int minSlotsForHardLots(const std::vector<Lot>& lots, int p);

// The largest number of half-open intervals [start, end) that are open at the same moment. Time O(J log J).
int intervalDepth(const std::vector<std::pair<int, int>>& intervals);

// What the knapsack needs to know about one crop's market (plan 9.2, pipeline step 4 of 12.2).
struct CropDemand {
    std::vector<Lot> lots;
    int contractSlots = 0;           // the fewest slots that let every contract delivery be met
    double demand = 0;               // mean weekly sellable kg: contract kg plus forecast spot kg over the delivery window
    double penaltyPerWeek = 0;       // the penalties of all the crop's contract lots, divided by the horizon
};

// Contract weeks, lots, contract slots, demand and penalty for crop cropId. spotForecast is the crop's forecast with
// element 0 being delivery week 1; it must reach week horizon-1+cycle. The scenario must have passed validateScenario.
// Time O(T + J + Jh^2 log k) for J lots, Jh of them hard, and k contract slots: the slot search dominates.
CropDemand buildCropDemand(const Scenario& scenario, int cropId, const std::vector<double>& spotForecast);

// ---- the schedulers (plan 11.4 to 11.7): one crop at a time, its slots identical machines, its lots jobs of length p ----

enum class SchedAlgo { Edf, EdfLookahead, Exact };

// One batch: lot lotId is planted on slot `slot` in week `start` and harvested in week `end` = start + p.
struct Batch { int cropId = 0, slot = 0, lotId = 0, start = 0, end = 0; };

struct CropSchedule {
    int cropId = 0, slots = 0;
    std::vector<Lot> lots;
    std::vector<char> scheduled;                 // parallel to lots: 1 when the lot gets a batch
    std::vector<Batch> batches;
    int hardTotal = 0, hardOnTime = 0, softTotal = 0, softServed = 0, idleSlotWeeks = 0;
    double revenue = 0, penalties = 0;           // the value of the scheduled lots, and the penalty of the missed hard lots
    std::string note;                            // e.g. why the exact search showed the lookahead schedule instead
};

// The most lots the exact search accepts, and the nodes it may visit, before it shows the lookahead schedule instead.
constexpr int kExactMaxLots = 22;
constexpr long long kExactNodeLimit = 2000000;

// Schedules the lots of one crop on `slots` slots, all free from week 0, with plants in weeks 0 .. horizon-1.
//  Edf: each week, each free slot takes the first released lot in the order (due week, hard first, value, id).
//  EdfLookahead: the same, but a soft lot is taken only if starting it does not make a hard lot due within the next
//    lookaheadWeeks weeks unreachable; -1 means the crop's cycle, 0 means no lookahead.
//  Exact: the schedule of greatest value minus missed penalties, by depth-first search; for at most kExactMaxLots lots
//    and exactNodeLimit nodes, otherwise the lookahead schedule with a note saying so.
// A crop with lots but no slots gets a schedule too, with every hard lot missed. Throws std::invalid_argument for negative
// slots or horizon, a cycle below 1 or a lookahead below -1. cropId is taken from the first lot (0 without lots).
// Time: EDF O(J log J + T k J); lookahead adds O(T k Hw log k) for Hw hard lots in the window; exact O((k + 1)^J).
CropSchedule scheduleCrop(const std::vector<Lot>& lots, int slots, int cycle, int horizon, SchedAlgo algo,
                          int lookaheadWeeks = -1, long long exactNodeLimit = kExactNodeLimit);
