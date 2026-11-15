#pragma once
// The planning problem as plain structs (plan section 5). Units: cm, cm^2, whole weeks, kg, PKR;
// labour h, water L, nutrient solution L and energy kWh, each per week.
#include <string>
#include <vector>

// The one tolerance (plan section 5.1): "need fits" is need <= have + EPS, and "a is better than b" is
// a > b + EPS (a < b - EPS when lower is better), so rounding noise never changes a decision.
constexpr double EPS = 1e-9;

// The knapsack resources in the fixed order of plan section 5.1: 0 labour, 1 water, 2 nutrient, 3 energy, 4 area.
// Every array of five (weights, capacities, units, switches) uses this order.
constexpr int kResources = 5;

enum class Light { Low = 0, Medium = 1, High = 2 };

struct Crop {
    int id = 0;                      // equals its index in Scenario::crops
    std::string name, category;      // category: "leafy", "herb", "microgreen", "fruiting"
    int trayW = 0, trayD = 0;        // tray footprint, cm
    bool canRotate = true;           // may be turned 90 degrees on the shelf
    int height = 0;                  // mature plant including tray, cm
    int cycleWeeks = 1;              // p: planting to harvest
    int harvestWindowWeeks = 0;      // f: a harvest may happen up to f weeks before its delivery week
    Light lightMin = Light::Low, lightMax = Light::High;
    double humidityMin = 0, humidityMax = 100;       // acceptable relative humidity, %
    double laborPerWeek = 0, waterPerWeek = 0, nutrientPerWeek = 0, energyPerWeek = 0;   // per tray
    double yieldKg = 0;              // per tray per harvest
    double pricePerKg = 0;
    double trayCost = 0;             // seed and substrate, per tray per cycle
    double salvageFraction = 0.2;    // share of the price obtained for unsold surplus
};

struct Shelf {
    int id = 0; std::string name;    // e.g. "R2-L3"; id equals its index in Scenario::shelves
    int rack = 0, level = 0;
    int width = 0, depth = 0, clearance = 0;         // cm
    Light light = Light::Medium;
    double humidity = 60;            // %
};

struct Resources { double labor = 0, water = 0, nutrient = 0, energy = 0; };

struct Contract {
    int id = 0; std::string customer;
    int cropId = 0;
    double kgPerWeek = 0;
    int startWeek = 1, endWeek = 9999;               // delivery weeks, inclusive
    double penaltyPerKg = 0;                         // charged for every contracted kg not delivered
};

struct ConflictPair { int a = 0, b = 0; std::string reason; };

struct Scenario {
    std::string name; unsigned long long seed = 42; std::string currency = "PKR";
    int horizonWeeks = 12;           // T
    int seasonLength = 52;           // L, weeks
    int lightGapCm = 10;             // free space required between canopy and lamp
    double heightRatioLimit = 2.0;   // crops may not share a shelf when the taller is at least this many times the shorter
    Resources budget;                // per week
    Resources unitCost;              // PKR per hour, litre, litre, kWh
    std::vector<Crop> crops;
    std::vector<Shelf> shelves;
    std::vector<Contract> contracts;
    std::vector<ConflictPair> conflicts;               // explicit agronomic rules
    std::vector<std::vector<double>> orderHistory;     // [crop][week], oldest first, kg of spot orders
};
