#pragma once
// The desktop app (plan section 14): its state, the worker threads that run the plan, and the functions that draw it.
#include <future>
#include <string>
#include <vector>

#include "imgui.h"

#include "bench.h"
#include "generator.h"
#include "pipeline.h"

enum Tab { TabOverview, TabLayout, TabSchedule, TabForecast, TabConstraints, TabCompare, TabData, TabCount };
extern const char* const kTabNames[TabCount];

// One row of the "Compare packers" table of the Layout tab.
struct PackerRow {
    std::string name;
    int trays = 0, placed = 0, shelves = 0;
    double value = 0, utilisationPercent = 0, ms = 0;
};

struct AppState {
    // What the person is working on.
    Scenario scenario;
    std::string scenarioPath;                    // the file it came from; empty for a generated one
    std::string dataDir;                         // the folder of the sample scenarios; empty when none was found
    std::vector<std::string> files;              // the .json files in dataDir
    int fileIndex = 0;
    PlanOptions options;
    GeneratorParams generator;                   // the values of the Generate popup
    char saveName[96] = "my_scenario";

    // The last result, with the scenario it was computed from: the tabs draw the two together, and the person may have edited
    // the live scenario since.
    PlanResult result;
    Scenario planned;
    bool hasResult = false;
    CompareTable compare;
    bool hasCompare = false;
    std::vector<PackerRow> packers;
    bool hasPackers = false;

    // The benchmark tables farm_cli bench wrote (plan 15), for the Scaling part of the Compare tab.
    std::vector<BenchSuiteView> bench;
    std::string benchDir;                        // where they were read from; empty when none were found
    std::string benchStatus;                     // why there are none, for the tab
    int benchSuite = 0;                          // the suite on show

    // The workers (plan 14.2): each gets copies and returns a value, so nothing is shared and nothing is locked.
    std::future<PlanResult> pending;
    Scenario pendingScenario;
    std::future<CompareTable> pendingCompare;
    std::future<std::vector<PackerRow>> pendingPackers;
    bool running = false, comparing = false, packing = false;
    bool dirty = true, autoRun = true, requestRun = false;
    int runsFinished = 0;                        // runs that ended, with a result or with an error (the self-test counts them)

    std::string status;                          // the last error or information line
    bool statusIsError = false;

    // The window.
    float scale = 1;
    bool showSidebar = true;                     // false: only the selected tab, for the figures of the report
    bool convergenceOnly = false;                // with the sidebar hidden: the convergence chart alone
    bool scalingOnly = false;                    // with the sidebar hidden: the charts of one benchmark suite alone (fig_bench_<suite>)
    int tab = TabOverview;                       // the tab on show
    int requestTab = -1;                         // a tab to select on the next frame
    int forecastCrop = 0;
};

// The options the app starts with: the genetic packer, because that combination gives the best plan.
PlanOptions startingOptions();

// Looks for the data folder and loads the scenario (scenarioFile, or data/default_scenario.json, or the built-in sample farm).
// Returns false, with s.status set, when scenarioFile cannot be used; the app then still holds a usable scenario.
bool initState(AppState& s, const std::string& exeDir, const std::string& scenarioFile);

// Once a frame: collects finished workers and starts a new run when the inputs changed.
void updateState(AppState& s);

// The plan, the comparison and the packer table on this thread; the screenshot mode waits for them.
void runSync(AppState& s);

// The buttons of the Compare and Layout tabs: start a worker unless one is already running (and, for the packers, there is a result).
void startCompare(AppState& s);
void startPackers(AppState& s);

// The whole window: the sidebar and the tab bar.
void drawApp(AppState& s);

// One function per tab (ui_tabs.cpp and ui_views.cpp).
void drawOverview(AppState& s);
void drawLayout(AppState& s);
void drawSchedule(AppState& s);
void drawForecast(AppState& s);
void drawConstraints(AppState& s);
void drawCompare(AppState& s);
void drawData(AppState& s);

// ---- helpers the drawing code shares ----------------------------------------------------------------------------------

ImU32 cropColour(int cropId);                    // twelve fixed colours, indexed by cropId % 12
ImU32 textColourOn(ImU32 fill);                  // black or white, by the luminance of the fill
ImU32 darker(ImU32 colour, float factor = 0.6f);
std::string format(const char* fmt, ...);        // printf into a std::string
std::string thousands(double value, int decimals);   // 1234567.5 as "1,234,567.5"
void rightText(const std::string& text);         // text flush right in the current table cell
void dimWrapped(const std::string& text);        // wrapped text in the disabled colour
void cropLabel(int cropId, const std::string& name, bool dim);   // a square in the crop's colour, then its name
bool needResult(const AppState& s);              // draws "press Optimize" and returns true when there is no result yet
void setStatus(AppState& s, const std::string& text, bool isError);
