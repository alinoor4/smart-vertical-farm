// The app's state, its worker threads (plan 14.2), the window with its sidebar (plan 14.3) and the helpers the tabs share.
#include "ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "json_io.h"
#include "knapsack.h"
#include "packing.h"

namespace fs = std::filesystem;

const char* const kTabNames[TabCount] = {"Overview", "Layout", "Schedule", "Forecast", "Constraints", "Compare", "Data"};

// ---- helpers ----------------------------------------------------------------------------------------------------------

ImU32 cropColour(int cropId) {
    static const ImU32 palette[12] = {
        IM_COL32(0x4E, 0x79, 0xA7, 255), IM_COL32(0xF2, 0x8E, 0x2B, 255), IM_COL32(0xE1, 0x57, 0x59, 255), IM_COL32(0x76, 0xB7, 0xB2, 255),
        IM_COL32(0x59, 0xA1, 0x4F, 255), IM_COL32(0xED, 0xC9, 0x48, 255), IM_COL32(0xB0, 0x7A, 0xA1, 255), IM_COL32(0xFF, 0x9D, 0xA7, 255),
        IM_COL32(0x9C, 0x75, 0x5F, 255), IM_COL32(0xBA, 0xB0, 0xAC, 255), IM_COL32(0xA0, 0xCB, 0xE8, 255), IM_COL32(0xD3, 0x72, 0x95, 255)};
    return palette[((cropId % 12) + 12) % 12];
}

ImU32 textColourOn(ImU32 fill) {
    const float r = ((fill >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f, g = ((fill >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f, b = ((fill >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;   // no gamma correction: good enough to choose between black and white
    return luminance > 0.55f ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
}

ImU32 darker(ImU32 colour, float factor) {
    auto part = [&](int shift) { return int(((colour >> shift) & 0xFF) * factor); };
    return IM_COL32(part(IM_COL32_R_SHIFT), part(IM_COL32_G_SHIFT), part(IM_COL32_B_SHIFT), 255);
}

std::string format(const char* fmt, ...) {
    va_list args, again;
    va_start(args, fmt);
    va_copy(again, args);
    const int length = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string text(length > 0 ? size_t(length) : 0, '\0');
    if (length > 0) std::vsnprintf(text.data(), size_t(length) + 1, fmt, again);
    va_end(again);
    return text;
}

// 1234567.5 as "1,234,567.5".
std::string thousands(double value, int decimals) {
    std::string text = format("%.*f", decimals, std::fabs(value));
    const size_t end = text.find('.') == std::string::npos ? text.size() : text.find('.');
    for (int i = int(end) - 3; i > 0; i -= 3) text.insert(size_t(i), ",");
    return (value < 0 ? "-" : "") + text;
}

void rightText(const std::string& text) {
    const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text.c_str()).x;
    if (room > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
    ImGui::TextUnformatted(text.c_str());
}

void dimWrapped(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// A small filled square in the crop's colour, then its name.
void cropLabel(int cropId, const std::string& name, bool dim) {
    const float side = ImGui::GetTextLineHeight() * 0.8f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + 2), ImVec2(at.x + side, at.y + 2 + side), cropColour(cropId), 2.0f);
    ImGui::Dummy(ImVec2(side, side));
    ImGui::SameLine();
    if (dim) ImGui::TextDisabled("%s", name.c_str());
    else ImGui::TextUnformatted(name.c_str());
}

bool needResult(const AppState& s) {
    if (s.hasResult) return false;
    ImGui::TextDisabled("%s", s.running ? "Running the plan..." : "Press Optimize to see this tab.");
    return true;
}

void setStatus(AppState& s, const std::string& text, bool isError) {
    s.status = text;
    s.statusIsError = isError;
}

// ---- state ------------------------------------------------------------------------------------------------------------

PlanOptions startingOptions() {
    PlanOptions options;
    options.packer = PackAlgo::Genetic;   // PlanOptions itself keeps First-Fit Decreasing so that tests need no seed
    return options;
}

// The first of ./data, <exe>/data and <exe>/../data that holds the default scenario.
static std::string findDataDir(const std::string& exeDir) {
    const fs::path candidates[] = {fs::path("data"), fs::path(exeDir) / "data", fs::path(exeDir) / ".." / "data"};
    for (const fs::path& dir : candidates) {
        std::error_code ec;
        if (fs::exists(dir / "default_scenario.json", ec)) return fs::absolute(dir, ec).lexically_normal().string();
    }
    return "";
}

static void refreshFiles(AppState& s) {
    s.files.clear();
    std::error_code ec;
    if (!s.dataDir.empty())
        for (const fs::directory_entry& entry : fs::directory_iterator(s.dataDir, ec))
            if (entry.is_regular_file() && entry.path().extension() == ".json") s.files.push_back(entry.path().filename().string());
    std::sort(s.files.begin(), s.files.end());
    s.fileIndex = std::clamp(s.fileIndex, 0, std::max(0, int(s.files.size()) - 1));
}

// Takes a scenario that passed validateScenario as the one to plan.
static void useScenario(AppState& s, Scenario scenario, const std::string& path) {
    s.scenario = std::move(scenario);
    s.scenarioPath = path;
    s.forecastCrop = 0;
    s.dirty = true;
    if (!path.empty()) {
        const std::string name = fs::path(path).filename().string();
        const auto found = std::find(s.files.begin(), s.files.end(), name);
        if (found != s.files.end()) s.fileIndex = int(found - s.files.begin());
    }
}

static bool loadFile(AppState& s, const std::string& path) {
    try {
        Scenario loaded = loadScenario(path);
        const std::vector<std::string> problems = validateScenario(loaded);
        if (!problems.empty()) {
            setStatus(s, "Cannot use " + path + ": " + problems[0] + (problems.size() > 1 ? format(" (and %zu more)", problems.size() - 1) : ""), true);
            return false;
        }
        useScenario(s, std::move(loaded), path);
        setStatus(s, "Loaded " + fs::path(path).filename().string(), false);
        return true;
    } catch (const std::exception& e) {
        setStatus(s, "Cannot load " + path + ": " + e.what(), true);
        return false;
    }
}

bool initState(AppState& s, const std::string& exeDir, const std::string& scenarioFile) {
    s.options = startingOptions();
    s.dataDir = findDataDir(exeDir);
    refreshFiles(s);
    if (!scenarioFile.empty() && loadFile(s, scenarioFile)) return true;
    const std::string failure = s.status;
    if (!s.dataDir.empty() && loadFile(s, (fs::path(s.dataDir) / "default_scenario.json").string())) {
        if (!scenarioFile.empty()) setStatus(s, failure + " The default scenario is shown instead.", true);
        return scenarioFile.empty();
    }
    useScenario(s, generateScenario(GeneratorParams()), "");
    setStatus(s, scenarioFile.empty() ? "The data folder was not found; the built-in sample farm is shown." : failure, !scenarioFile.empty());
    return scenarioFile.empty();
}

// The three packers on the plan's selection, as the Layout tab's table shows them.
static std::vector<PackerRow> comparePackers(const Scenario& scenario, const PlanResult& result) {
    const CropValues values = valuesByCrop(result.problem, scenario.crops.size());
    const std::vector<Tray> trays = traysForCounts(result.selected);
    const PackResult packs[3] = {packFirstFitDecreasing(trays, scenario, result.constraints, values),
                                 packBestFitDecreasing(trays, scenario, result.constraints, values),
                                 packGenetic(trays, scenario, result.constraints, values, result.options.ga)};
    std::vector<PackerRow> rows;
    for (const PackResult& p : packs) {
        PackerRow row;
        row.name = p.algorithm;
        row.trays = int(trays.size());
        row.placed = int(p.state.placements.size());
        row.value = p.placedValue;
        row.shelves = p.shelvesUsed;
        row.utilisationPercent = p.totalShelfArea > 0 ? 100.0 * double(p.placedArea) / double(p.totalShelfArea) : 0.0;
        row.ms = 1000.0 * p.seconds;
        rows.push_back(row);
    }
    return rows;
}

static void startRun(AppState& s) {
    s.dirty = false;
    s.requestRun = false;
    s.running = true;
    s.pendingScenario = s.scenario;
    s.pending = std::async(std::launch::async, [scenario = s.scenario, options = s.options] { return runPipeline(scenario, options); });
}

void startCompare(AppState& s) {
    if (s.comparing) return;
    s.comparing = true;
    s.pendingCompare = std::async(std::launch::async, [scenario = s.scenario, options = s.options] { return compareAll(scenario, options); });
}

void startPackers(AppState& s) {
    if (s.packing || !s.hasResult) return;
    s.packing = true;
    s.pendingPackers = std::async(std::launch::async, [scenario = s.planned, result = s.result] { return comparePackers(scenario, result); });
}

// A worker is done when its future is ready; get() hands over the value, or rethrows what the worker threw.
template <class T> static bool collect(std::future<T>& future, bool& flag, T& into, AppState& s, const char* what) {
    if (!flag || future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    flag = false;
    try {
        into = future.get();
        return true;
    } catch (const std::exception& e) {
        setStatus(s, std::string(what) + " failed: " + e.what(), true);
        return false;
    }
}

void updateState(AppState& s) {
    if (s.running && s.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        PlanResult result;
        const bool ok = collect(s.pending, s.running, result, s, "The plan");
        if (ok) {
            s.result = std::move(result);
            s.planned = std::move(s.pendingScenario);
            s.hasResult = true;
            setStatus(s, format("Plan ready in %.2f s.", s.result.totalSeconds), false);
        }
        ++s.runsFinished;
    }
    if (collect(s.pendingCompare, s.comparing, s.compare, s, "The comparison")) {
        s.hasCompare = true;
        setStatus(s, "Comparison ready.", false);
    }
    if (collect(s.pendingPackers, s.packing, s.packers, s, "The packer comparison")) s.hasPackers = true;
    if (!s.running && (s.requestRun || (s.dirty && s.autoRun))) startRun(s);
}

void runSync(AppState& s) {
    s.result = runPipeline(s.scenario, s.options);
    s.planned = s.scenario;
    s.hasResult = true;
    s.dirty = false;
    s.compare = compareAll(s.scenario, s.options);
    s.hasCompare = true;
    s.packers = comparePackers(s.planned, s.result);
    s.hasPackers = true;
}

// ---- the sidebar (plan 14.3) ------------------------------------------------------------------------------------------

// Inputs that take a typed value apply it when the person leaves the field, so that "120" does not start runs for "1" and "12".
static bool inputDouble(const char* label, double* value, double lo, double hi, const char* fmt = "%.2f") {
    ImGui::InputDouble(label, value, 0.0, 0.0, fmt);
    if (!ImGui::IsItemDeactivatedAfterEdit()) return false;
    *value = std::isfinite(*value) ? std::clamp(*value, lo, hi) : lo;
    return true;
}

static bool sliderInt(const char* label, int* value, int lo, int hi) {
    ImGui::SliderInt(label, value, lo, hi, "%d", ImGuiSliderFlags_AlwaysClamp);
    return ImGui::IsItemDeactivatedAfterEdit();
}

template <class E> static bool enumCombo(const char* label, E* value, std::initializer_list<E> choices) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name(*value))) {
        for (E choice : choices)
            if (ImGui::Selectable(name(choice), choice == *value)) {
                *value = choice;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

static void sidebarScenario(AppState& s) {
    if (s.files.empty()) {
        ImGui::TextDisabled("No scenario files found.");
    } else {
        ImGui::SetNextItemWidth(-62.0f * s.scale);
        if (ImGui::BeginCombo("##file", s.files[size_t(s.fileIndex)].c_str())) {
            for (int i = 0; i < int(s.files.size()); ++i)
                if (ImGui::Selectable(s.files[size_t(i)].c_str(), i == s.fileIndex)) s.fileIndex = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load")) loadFile(s, (fs::path(s.dataDir) / s.files[size_t(s.fileIndex)]).string());
    }
    ImGui::TextWrapped("%s", s.scenario.name.c_str());
    ImGui::TextDisabled("%zu crops, %zu shelves, %zu contracts", s.scenario.crops.size(), s.scenario.shelves.size(), s.scenario.contracts.size());

    if (ImGui::Button("Generate...")) ImGui::OpenPopup("Generate a scenario");
    if (ImGui::BeginPopupModal("Generate a scenario", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        GeneratorParams& g = s.generator;
        int seed = int(std::min<unsigned long long>(g.seed, 2000000000ULL));
        ImGui::InputInt("Seed", &seed);
        g.seed = (unsigned long long)std::max(seed, 0);
        ImGui::InputInt("Crops", &g.crops);
        g.crops = std::clamp(g.crops, 1, 30);
        ImGui::InputInt("Racks", &g.racks);
        g.racks = std::clamp(g.racks, 1, 8);
        ImGui::Checkbox("Flat demand", &g.flat);
        if (ImGui::Button("Generate")) {
            Scenario made = generateScenario(g);
            const std::vector<std::string> problems = validateScenario(made);
            if (problems.empty()) {
                useScenario(s, std::move(made), "");
                setStatus(s, "Generated " + s.scenario.name, false);
            } else {
                setStatus(s, "The generated scenario is not valid: " + problems[0], true);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextItemWidth(-62.0f * s.scale);
    ImGui::InputText("##save", s.saveName, sizeof s.saveName);
    ImGui::SameLine();
    if (ImGui::Button("Save as") && s.saveName[0] != '\0') {
        fs::path file = fs::path(s.dataDir.empty() ? "." : s.dataDir) / s.saveName;
        if (file.extension() != ".json") file += ".json";
        try {
            saveScenario(s.scenario, file.string());
            refreshFiles(s);
            s.scenarioPath = file.string();
            setStatus(s, "Saved " + file.string(), false);
        } catch (const std::exception& e) {
            setStatus(s, std::string("Cannot save: ") + e.what(), true);
        }
    }
}

static void sidebarBudgets(AppState& s) {
    double* budget[4] = {&s.scenario.budget.labor, &s.scenario.budget.water, &s.scenario.budget.nutrient, &s.scenario.budget.energy};
    const char* label[kResources] = {"Labour h", "Water L", "Nutrient L", "Energy kWh", "Area cm2"};
    if (!ImGui::BeginTable("budgets", 4, ImGuiTableFlags_SizingFixedFit)) return;
    ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, 26.0f * s.scale);
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 82.0f * s.scale);
    ImGui::TableSetupColumn("budget", ImGuiTableColumnFlags_WidthFixed, 88.0f * s.scale);
    ImGui::TableSetupColumn("use", ImGuiTableColumnFlags_WidthStretch);
    for (int r = 0; r < kResources; ++r) {
        ImGui::PushID(r);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Checkbox("##active", &s.options.activeResource[size_t(r)])) s.dirty = true;
        ImGui::SetItemTooltip("Count this resource when choosing the crops. The packer always keeps to the real space.");
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label[r]);
        ImGui::TableNextColumn();
        if (r < 4) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (inputDouble("##budget", budget[r], 0.0, 1e9)) s.dirty = true;
        } else {
            long long area = 0;
            for (const Shelf& shelf : s.scenario.shelves) area += static_cast<long long>(shelf.width) * shelf.depth;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%lld", area);
        }
        ImGui::TableNextColumn();
        if (s.hasResult) {
            const double use = s.result.metrics.budgetUse[size_t(r)];
            const bool over = use > 1.0 + 1e-9;
            if (over) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(200, 60, 60, 255));
            ImGui::ProgressBar(float(std::min(use, 1.0)), ImVec2(-FLT_MIN, 0), format("%.0f%%", 100.0 * use).c_str());
            if (over) ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

static void sidebarAlgorithms(AppState& s) {
    PlanOptions& o = s.options;
    if (enumCombo("Forecast", &o.forecast, {ForecastMethod::Auto, ForecastMethod::SeasonalNaive, ForecastMethod::MovingAverage, ForecastMethod::Ses, ForecastMethod::HoltWinters})) s.dirty = true;
    if (enumCombo("Crop selection", &o.knapsack, {KnapsackAlgo::Greedy, KnapsackAlgo::Dp, KnapsackAlgo::BranchAndBound})) s.dirty = true;
    if (enumCombo("Packing", &o.packer, {PackAlgo::FirstFitDecreasing, PackAlgo::BestFitDecreasing, PackAlgo::Genetic})) s.dirty = true;
    if (enumCombo("Scheduling", &o.scheduler, {SchedAlgo::Edf, SchedAlgo::EdfLookahead, SchedAlgo::Exact})) s.dirty = true;
    if (!ImGui::CollapsingHeader("Advanced")) return;
    GaParams& ga = o.ga;
    if (sliderInt("GA population", &ga.population, 3, 200)) s.dirty = true;
    if (sliderInt("GA generations", &ga.generations, 0, 1000)) s.dirty = true;
    if (sliderInt("GA stall limit", &ga.stallGenerations, 1, 1000)) s.dirty = true;
    if (inputDouble("GA crossover", &ga.crossoverRate, 0.0, 1.0)) s.dirty = true;
    if (inputDouble("GA mutation", &ga.mutationRate, 0.0, 1.0)) s.dirty = true;
    ImGui::InputScalar("GA seed", ImGuiDataType_U64, &ga.seed);
    if (ImGui::IsItemDeactivatedAfterEdit()) s.dirty = true;
    if (sliderInt("Lookahead (weeks)", &o.lookaheadWeeks, -1, 26)) s.dirty = true;
    ImGui::SetItemTooltip("-1: each crop's own cycle. 0: no lookahead.");
    ImGui::InputScalar("B&B node limit", ImGuiDataType_S64, &o.bnbNodeLimit);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        o.bnbNodeLimit = std::clamp<long long>(o.bnbNodeLimit, 1, 1000000000LL);
        s.dirty = true;
    }
    if (inputDouble("B&B seconds", &o.bnbTimeLimitSeconds, 0.1, 600.0, "%.1f")) s.dirty = true;
    unsigned long long states = o.dpMaxStates;
    ImGui::InputScalar("DP state limit", ImGuiDataType_U64, &states);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        o.dpMaxStates = size_t(std::clamp<unsigned long long>(states, 1000ULL, 20000000ULL));
        s.dirty = true;
    }
    if (ImGui::Checkbox("Refill after packing", &o.refill)) s.dirty = true;
}

static void sidebarRun(AppState& s) {
    ImGui::BeginDisabled(s.running);
    if (ImGui::Button(s.running ? "Running..." : "Optimize", ImVec2(-FLT_MIN, 0))) s.requestRun = true;
    ImGui::EndDisabled();
    ImGui::Checkbox("Auto-run on change", &s.autoRun);
    if (!s.status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, s.statusIsError ? IM_COL32(235, 90, 90, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", s.status.c_str());
        ImGui::PopStyleColor();
    }
    if (s.hasResult && ImGui::BeginTable("stages", 4, ImGuiTableFlags_SizingStretchProp)) {
        for (const auto& [stage, seconds] : s.result.stageSeconds) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", stage.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%.3f s", seconds);
        }
        ImGui::EndTable();
    }
}

static void sidebarWarnings(const AppState& s) {
    for (const std::string& warning : s.result.warnings) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 170, 60, 255));
        ImGui::TextWrapped("- %s", warning.c_str());
        ImGui::PopStyleColor();
    }
}

static void drawSidebar(AppState& s) {
    ImGui::PushItemWidth(-125.0f * s.scale);
    if (ImGui::CollapsingHeader("Scenario", ImGuiTreeNodeFlags_DefaultOpen)) sidebarScenario(s);
    if (ImGui::CollapsingHeader("Weekly budgets", ImGuiTreeNodeFlags_DefaultOpen)) sidebarBudgets(s);
    if (ImGui::CollapsingHeader("Planning", ImGuiTreeNodeFlags_DefaultOpen))
        if (sliderInt("Horizon (weeks)", &s.scenario.horizonWeeks, 4, 26)) s.dirty = true;
    if (ImGui::CollapsingHeader("Algorithms", ImGuiTreeNodeFlags_DefaultOpen)) sidebarAlgorithms(s);
    if (ImGui::CollapsingHeader("Run", ImGuiTreeNodeFlags_DefaultOpen)) sidebarRun(s);
    if (s.hasResult && !s.result.warnings.empty() && ImGui::CollapsingHeader(format("Warnings (%zu)", s.result.warnings.size()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        sidebarWarnings(s);
    ImGui::PopItemWidth();
}

// ---- the window (plan 14.1) -------------------------------------------------------------------------------------------

static void drawTab(AppState& s, int tab) {
    ImGui::BeginChild("##content", ImVec2(0, 0));
    switch (tab) {
        case TabOverview: drawOverview(s); break;
        case TabLayout: drawLayout(s); break;
        case TabSchedule: drawSchedule(s); break;
        case TabForecast: drawForecast(s); break;
        case TabConstraints: drawConstraints(s); break;
        case TabCompare: drawCompare(s); break;
        default: drawData(s); break;
    }
    ImGui::EndChild();
}

static void drawTabs(AppState& s) {
    if (!ImGui::BeginTabBar("tabs")) return;
    for (int i = 0; i < TabCount; ++i)
        if (ImGui::BeginTabItem(kTabNames[i], nullptr, s.requestTab == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            s.tab = i;
            drawTab(s, i);
            ImGui::EndTabItem();
        }
    ImGui::EndTabBar();
    s.requestTab = -1;
}

void drawApp(AppState& s) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Smart Vertical Farm", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (s.showSidebar) {
        ImGui::BeginChild("sidebar", ImVec2(340.0f * s.scale, 0), ImGuiChildFlags_Borders);
        drawSidebar(s);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("main", ImVec2(0, 0));
        drawTabs(s);
        ImGui::EndChild();
    } else {
        if (s.requestTab >= 0) s.tab = s.requestTab;
        s.requestTab = -1;
        drawTab(s, s.tab);
    }
    ImGui::End();
}
