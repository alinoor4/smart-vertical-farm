#include "generator.h"

#include <algorithm>
#include <cmath>

#include "rng.h"

namespace {

constexpr Light Low = Light::Low, Med = Light::Medium, High = Light::High;

// Plan section 6.1, one row per crop, in the column order of reference_model.py's catalog().
struct CatalogRow {
    const char* name; const char* category;
    int trayW, trayD, height, cycleWeeks, harvestWindowWeeks;
    Light lightMin, lightMax; double humidityMin, humidityMax;
    double labor, water, nutrient, energy, yieldKg, pricePerKg, trayCost, baseDemand;   // baseDemand: kg per week, not stored in a Scenario
};
constexpr int catalogSize = 12;
const CatalogRow catalog[catalogSize] = {
    {"Lettuce",       "leafy",       60, 40, 25,  5, 0, Med,  High, 60, 80, 0.15,  8, 0.08, 3.5, 1.8,  1100,  80, 9.0},
    {"Baby Spinach",  "leafy",       60, 30, 20,  4, 0, Med,  High, 60, 80, 0.14,  6, 0.06, 2.6, 1.2,  1300,  50, 6.0},
    {"Kale",          "leafy",       60, 45, 40,  6, 1, Med,  High, 60, 80, 0.16,  9, 0.09, 4.0, 2.6,  1200,  90, 4.8},
    {"Pak Choi",      "leafy",       50, 40, 25,  4, 0, Med,  High, 60, 80, 0.14,  7, 0.07, 3.0, 1.7,   900,  60, 4.0},
    {"Arugula",       "leafy",       45, 30, 20,  3, 0, Med,  High, 60, 80, 0.16,  5, 0.05, 2.2, 0.9,  1800,  45, 2.4},
    {"Basil",         "herb",        50, 30, 35,  4, 0, Med,  High, 50, 70, 0.20,  7, 0.07, 4.0, 0.9,  2600,  90, 2.7},
    {"Mint",          "herb",        40, 30, 30,  4, 1, Med,  High, 55, 75, 0.13,  6, 0.06, 2.4, 0.8,  1500,  50, 2.0},
    {"Coriander",     "herb",        40, 25, 25,  4, 0, Med,  High, 50, 70, 0.15,  5, 0.05, 2.2, 0.6,  1900,  40, 2.4},
    {"Microgreens",   "microgreen",  50, 25, 10,  2, 0, Low,  Med,  50, 70, 0.30,  4, 0.02, 1.5, 0.3,  5000, 250, 1.5},
    {"Pea Shoots",    "microgreen",  55, 28, 15,  2, 0, Low,  Med,  50, 70, 0.30,  5, 0.02, 1.6, 0.45, 3200, 220, 1.35},
    {"Strawberry",    "fruiting",   100, 30, 30, 10, 1, High, High, 55, 70, 0.30, 12, 0.15, 6.0, 3.5,  2400, 600, 7.0},
    {"Cherry Tomato", "fruiting",    60, 60, 70, 12, 1, High, High, 55, 70, 0.40, 20, 0.25, 9.0, 7.0,  1700, 400, 3.5},
};

// Plan section 6.2: four rack templates of four levels each, level 1 (bottom) first.
struct RackTemplate { int width, depth; double humidity; int clearance[4]; Light light[4]; };
const RackTemplate rackTemplates[4] = {
    {240, 60, 72, {55, 45, 45, 40}, {Med,  Med,  Med,  High}},
    {240, 60, 68, {55, 45, 45, 40}, {Med,  Med,  High, High}},
    {200, 80, 62, {95, 60, 45, 45}, {High, High, High, High}},
    {180, 50, 58, {35, 35, 35, 35}, {Low,  Low,  Med,  Med}},
};

// Plan section 6.3: the five sample contracts, sized in trays per week.
struct ContractRow { const char* customer; int cropIndex; int traysPerWeek; double penaltyPerKg; };
const ContractRow contractTable[5] = {
    {"Restaurant group (sample)",  0, 2,  600},
    {"Hotel kitchen (sample)",     5, 1, 1500},
    {"Salad subscription (sample)", 8, 2, 3000},
    {"Dessert cafe (sample)",     10, 1, 1500},
    {"Supermarket (sample)",       1, 1,  700},
};

}  // namespace

Scenario generateScenario(const GeneratorParams& p) {
    Rng rng(p.seed);   // one stream, drawn in the order of the plan's rules: crop jitter first, then histories
    Scenario s;
    s.name = "Sample farm, seed " + std::to_string(p.seed) + ", " + std::to_string(p.crops) + " crops, " +
             std::to_string(p.racks) + " racks" + (p.flat ? ", flat demand" : "");
    s.seed = p.seed;
    s.horizonWeeks = p.horizonWeeks;
    const double rackScale = p.racks / 4.0;   // the default budget is for four racks
    s.budget = {21.0 * rackScale, 800.0 * rackScale, 8.5 * rackScale, 400.0 * rackScale};
    s.unitCost = {300.0, 0.5, 400.0, 50.0};

    std::vector<double> baseDemand;
    for (int i = 0; i < p.crops; ++i) {
        const CatalogRow& row = catalog[i % catalogSize];
        Crop c;
        c.id = i; c.name = row.name; c.category = row.category;
        c.trayW = row.trayW; c.trayD = row.trayD; c.height = row.height;
        c.cycleWeeks = row.cycleWeeks; c.harvestWindowWeeks = row.harvestWindowWeeks;
        c.lightMin = row.lightMin; c.lightMax = row.lightMax;
        c.humidityMin = row.humidityMin; c.humidityMax = row.humidityMax;
        c.laborPerWeek = row.labor; c.waterPerWeek = row.water; c.nutrientPerWeek = row.nutrient; c.energyPerWeek = row.energy;
        c.yieldKg = row.yieldKg; c.pricePerKg = row.pricePerKg; c.trayCost = row.trayCost;
        double demand = row.baseDemand;
        double jitter = p.jitter;
        if (i >= catalogSize) {   // a variant of a catalogue crop: "Lettuce B"
            c.name += ' ';
            c.name += char('A' + i / catalogSize);
            jitter = std::max(jitter, 0.15);
        }
        if (jitter > 0)   // the draw order is part of the specification
            for (double* field : {&c.pricePerKg, &c.yieldKg, &demand, &c.laborPerWeek, &c.waterPerWeek, &c.nutrientPerWeek, &c.energyPerWeek})
                *field *= 1 + jitter * (2 * rng.unit() - 1);
        s.crops.push_back(c);
        baseDemand.push_back(demand);
    }

    for (int r = 0; r < p.racks; ++r) {
        const RackTemplate& t = rackTemplates[r % 4];
        for (int level = 0; level < 4; ++level) {
            Shelf sh;
            sh.id = int(s.shelves.size());
            sh.name = "R" + std::to_string(r + 1) + "-L" + std::to_string(level + 1);
            sh.rack = r; sh.level = level;
            sh.width = t.width; sh.depth = t.depth; sh.clearance = t.clearance[level];
            sh.light = t.light[level]; sh.humidity = t.humidity;
            s.shelves.push_back(sh);
        }
    }

    for (const ContractRow& row : contractTable) {
        if (row.cropIndex >= p.crops) continue;   // the contract's crop was not generated
        const Crop& c = s.crops[row.cropIndex];
        Contract k;
        k.id = int(s.contracts.size());
        k.customer = row.customer;
        k.cropId = row.cropIndex;
        k.kgPerWeek = row.traysPerWeek * c.yieldKg;
        k.startWeek = c.cycleWeeks;   // the first week a harvest is possible
        k.endWeek = 9999;
        k.penaltyPerKg = row.penaltyPerKg;
        s.contracts.push_back(k);
    }

    // Every herb conflicts with every fruiting crop, plus two named pairs (Basil-Mint, Kale-Strawberry).
    for (int a = 0; a < p.crops; ++a)
        for (int b = a + 1; b < p.crops; ++b) {
            const std::string& ca = s.crops[a].category;
            const std::string& cb = s.crops[b].category;
            if ((ca == "herb" && cb == "fruiting") || (ca == "fruiting" && cb == "herb"))
                s.conflicts.push_back({a, b, "herb next to fruiting crop"});
        }
    if (p.crops > 6) s.conflicts.push_back({5, 6, "agronomic rule (sample data)"});
    if (p.crops > 10) s.conflicts.push_back({2, 10, "agronomic rule (sample data)"});

    // Order history: base demand with a yearly trend and season plus noise (a sum of 12 uniforms minus 6
    // is roughly standard normal). Each crop draws g, a, phi, sigma, then 12 units per week.
    const double pi = 3.14159265358979323846;
    const int n = p.historyWeeks;
    for (int i = 0; i < p.crops; ++i) {
        const double b = baseDemand[i];
        std::vector<double> series(n, b);   // the flat history, which draws nothing
        if (!p.flat) {
            const double g = -0.05 + 0.20 * rng.unit();
            const double a = 0.05 + 0.30 * rng.unit();
            const double phi = 52 * rng.unit();
            const double sigma = 0.05 + 0.10 * rng.unit();
            for (int t = 0; t < n; ++t) {
                double sum = 0;
                for (int k = 0; k < 12; ++k) sum += rng.unit();
                const double z = sum - 6;
                const double y = b * (1 + g * (t - (n - 1)) / 52.0) * (1 + a * std::sin(2 * pi * (t + phi) / 52.0)) + sigma * b * z;
                series[t] = std::round(std::max(0.0, y) * 100) / 100;
            }
        }
        s.orderHistory.push_back(series);
    }
    return s;
}
