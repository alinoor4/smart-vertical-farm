#pragma once
// Component 5 (plan section 11): delivery lots, slot arithmetic and, from milestone M6, the schedulers.
// The crop-selection knapsack needs the first two parts: how many slots a crop's contracts need.
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
CropDemand buildCropDemand(const Scenario& scenario, int cropId, const std::vector<double>& spotForecast);
