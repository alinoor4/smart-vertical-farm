// The tabs that are tables and charts: Overview, Forecast, Compare and Data (plan 14.4).
#include "ui.h"

#include <algorithm>
#include <cmath>

#include "implot.h"

namespace {

// One headline number: a label, the value in larger type, and a line of detail.
void statCell(const char* label, const std::string& value, const std::string& detail) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    dimWrapped(detail);
}

}  // namespace

void drawOverview(AppState& s) {
    if (needResult(s)) return;
    const PlanResult& r = s.result;
    const PlanMetrics& m = r.metrics;
    const int crops = int(r.cropNames.size());

    // The headline numbers of plan 12.4.
    if (ImGui::BeginTable("headline", 8, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
        statCell("Relaxed optimum", thousands(m.relaxedOptimum, 1), r.knapsack.provenOptimal ? "PKR/week, proven" : "PKR/week, not proven");
        statCell("Plan value", thousands(m.planValue, 1), format("%.1f%% of the optimum", 100 * m.planShare));
        statCell("Weekly profit", thousands(m.expectedWeeklyProfit, 1), "PKR/week, contract bonus left out");
        statCell("Space used", format("%.1f%%", 100 * m.spaceUtilisation), format("%.1f%% of the shelves in use", 100 * m.spaceUtilisationOfUsed));
        statCell("Contracts on time", format("%d / %d", m.hardOnTime, m.hardTotal), format("%.0f%%", 100 * m.contractReliability));
        statCell("Spot lots served", format("%d / %d", m.softServed, m.softTotal), format("%.0f%%", 100 * m.spotService));
        statCell("Idle slot-weeks", format("%d", m.idleSlotWeeks), "tray-weeks with nothing growing");
        statCell("Net over the plan", thousands(m.net, 0), format("PKR in %d weeks", s.planned.horizonWeeks));
        ImGui::EndTable();
    }
    // Budget use of the final plan, one number per resource (plan 12.4), red where a budget is exceeded. A ninth tile would not fit the 1280 px window.
    const char* const resource[kResources] = {"labour", "water", "nutrient", "energy", "area"};
    ImGui::TextDisabled("Budget use");
    for (int k = 0; k < kResources; ++k) {
        const bool over = m.budgetUse[size_t(k)] > 1.0 + EPS;
        if (over) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 90, 90, 255));
        ImGui::SameLine(0, 16.0f * s.scale);
        ImGui::Text("%s %.0f%%", resource[k], 100 * m.budgetUse[size_t(k)]);
        if (over) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // The selection table: what the plan holds of each crop.
    const char* const headers[] = {"Crop", "Selected", "Trays", "Area m2", "kg/week", "Demand kg/week", "Value PKR/week", "Labour h", "Water L", "Nutrient L", "Energy kWh"};
    if (ImGui::BeginTable("selection", 11, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(headers[0], ImGuiTableColumnFlags_WidthFixed, 150.0f * s.scale);
        for (int c = 1; c < 11; ++c) ImGui::TableSetupColumn(headers[c]);
        ImGui::TableHeadersRow();
        CropPlan total;
        for (int c = 0; c < crops; ++c) {
            const CropPlan& row = r.rows[size_t(c)];
            const bool unused = row.trays == 0 && row.selected == 0;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            cropLabel(c, r.cropNames[size_t(c)], unused);
            const std::string cells[10] = {format("%d", row.selected), format("%d", row.trays), format("%.2f", row.area / 10000.0), format("%.2f", row.weeklyKg),
                                           format("%.2f", row.demandKg), thousands(row.value, 1), format("%.2f", row.use[0]), format("%.0f", row.use[1]),
                                           format("%.2f", row.use[2]), format("%.1f", row.use[3])};
            for (const std::string& cell : cells) {
                ImGui::TableNextColumn();
                rightText(cell);
            }
            total.selected += row.selected;
            total.trays += row.trays;
            total.area += row.area;
            total.weeklyKg += row.weeklyKg;
            total.demandKg += row.demandKg;
            total.value += row.value;
            for (int k = 0; k < 4; ++k) total.use[size_t(k)] += row.use[size_t(k)];
        }
        ImGui::TableNextRow();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("All crops");
        const std::string cells[10] = {format("%d", total.selected), format("%d", total.trays), format("%.2f", total.area / 10000.0), format("%.2f", total.weeklyKg),
                                       format("%.2f", total.demandKg), thousands(total.value, 1), format("%.2f", total.use[0]), format("%.0f", total.use[1]),
                                       format("%.2f", total.use[2]), format("%.1f", total.use[3])};
        for (const std::string& cell : cells) {
            ImGui::TableNextColumn();
            rightText(cell);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // Trays per crop: the relaxed selection beside what the plan keeps.
    std::vector<double> values(2 * size_t(crops));
    std::vector<double> ticks(size_t(crops), 0.0);
    std::vector<const char*> names;
    for (int c = 0; c < crops; ++c) {
        values[size_t(c)] = r.rows[size_t(c)].selected;
        values[size_t(crops + c)] = r.rows[size_t(c)].trays;
        ticks[size_t(c)] = c;
        names.push_back(r.cropNames[size_t(c)].c_str());
    }
    const char* const series[2] = {"relaxed selection", "final plan"};
    const float room = ImGui::GetContentRegionAvail().y;
    if (ImPlot::BeginPlot("Trays per crop", ImVec2(-1, std::max(room, 260.0f * s.scale)))) {
        ImPlot::SetupAxes("trays", nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_Invert);   // inverted, so that the first crop is at the top
        ImPlot::SetupAxisTicks(ImAxis_Y1, ticks.data(), crops, names.data());
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
        ImPlot::PlotBarGroups(series, values.data(), 2, crops, 0.8, 0, {ImPlotProp_Flags, ImPlotBarGroupsFlags_Horizontal});
        ImPlot::EndPlot();
    }
}

// ---- Forecast ---------------------------------------------------------------------------------------------------------

void drawForecast(AppState& s) {
    if (needResult(s)) return;
    const PlanResult& r = s.result;
    const Scenario& sc = s.planned;
    const int crops = int(sc.crops.size());
    s.forecastCrop = std::clamp(s.forecastCrop, 0, crops - 1);
    const size_t crop = size_t(s.forecastCrop);

    ImGui::SetNextItemWidth(220.0f * s.scale);
    if (ImGui::BeginCombo("Crop", sc.crops[crop].name.c_str())) {
        for (int c = 0; c < crops; ++c)
            if (ImGui::Selectable(sc.crops[size_t(c)].name.c_str(), c == s.forecastCrop)) s.forecastCrop = c;
        ImGui::EndCombo();
    }
    const ForecastResult& f = r.forecast[crop];
    const std::vector<double>& history = sc.orderHistory[crop];
    const int n = int(history.size()), shown = std::min(n, 104);

    // Weeks are counted from now: the last week of the history is 0, the first forecast week is 1.
    std::vector<double> historyX(size_t(shown), 0.0), historyY(size_t(shown), 0.0);
    for (int i = 0; i < shown; ++i) {
        historyX[size_t(i)] = double(i - (shown - 1));
        historyY[size_t(i)] = history[size_t(n - shown + i)];
    }
    std::vector<double> forecastX(f.forecast.size(), 0.0);
    for (size_t j = 0; j < forecastX.size(); ++j) forecastX[j] = double(j + 1);
    double contractLevel = 0;
    for (const Contract& k : sc.contracts)
        if (k.cropId == s.forecastCrop) contractLevel += k.kgPerWeek;

    const float tableRoom = 215.0f * s.scale;
    if (ImPlot::BeginPlot("Weekly spot demand", ImVec2(-1, std::max(ImGui::GetContentRegionAvail().y - tableRoom, 220.0f * s.scale)))) {
        ImPlot::SetupAxes("weeks from now (0 is the last week of the history)", "kg per week", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_East, ImPlotLegendFlags_Outside);
        ImPlot::PlotLine("order history", historyX.data(), historyY.data(), shown, {ImPlotProp_LineWeight, 1.5f});
        for (int m = 0; m < 4; ++m) {   // what each method predicted for the weeks that were held out
            const std::vector<double>& predicted = f.holdoutPrediction[size_t(m)];
            if (predicted.empty()) continue;
            std::vector<double> x(predicted.size(), 0.0);
            for (size_t k = 0; k < x.size(); ++k) x[k] = double(int(k) - int(predicted.size()) + 1);
            ImPlot::PlotLine(format("%s (hold-out)", name(static_cast<ForecastMethod>(m + 1))).c_str(), x.data(), predicted.data(), int(predicted.size()));
        }
        ImPlot::PlotLine(format("forecast: %s", name(f.chosen)).c_str(), forecastX.data(), f.forecast.data(), int(f.forecast.size()), {ImPlotProp_LineWeight, 3.0f});
        if (contractLevel > 0) {   // a segment rather than an infinite line, so that the fit includes it
            const double x[2] = {historyX.front(), forecastX.empty() ? 0.0 : forecastX.back()}, y[2] = {contractLevel, contractLevel};
            ImPlot::PlotLine(format("contract level, %.1f kg/week", contractLevel).c_str(), x, y, 2, {ImPlotProp_LineWeight, 2.0f});
        }
        ImPlot::EndPlot();
    }

    if (f.holdoutLength > 0) ImGui::Text("The last %d weeks of the history were held out to score the methods. The chosen method is the one with the lowest MAE.", f.holdoutLength);
    else ImGui::TextDisabled("The history is too short to hold anything out, so no method was scored.");
    if (ImGui::BeginTable("methods", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Method", ImGuiTableColumnFlags_WidthFixed, 230.0f * s.scale);
        for (const char* column : {"MAE kg", "RMSE kg", "sMAPE %"}) ImGui::TableSetupColumn(column, ImGuiTableColumnFlags_WidthFixed, 110.0f * s.scale);
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int m = 0; m < 4; ++m) {
            const ForecastErrors& e = f.holdout[size_t(m)];
            const ForecastMethod method = static_cast<ForecastMethod>(m + 1);
            ImGui::TableNextRow();
            if (method == f.chosen) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 130, 80, 110));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name(method));
            for (double value : {e.mae, e.rmse, e.smape}) {
                ImGui::TableNextColumn();
                rightText(e.available ? format("%.2f", value) : "not available");
            }
            ImGui::TableNextColumn();
            if (method == f.chosen) ImGui::Text("chosen%s%s", f.parameters.empty() ? "" : ": ", f.parameters.c_str());
        }
        ImGui::EndTable();
    }
}

// ---- Compare ----------------------------------------------------------------------------------------------------------

namespace {

// Decimals of a column of the comparison tables: the times in milliseconds have three, whole numbers none, the rest two.
int decimalsOf(const CompareStage& stage, size_t column) {
    if (stage.columns[column] == "ms") return 3;
    for (const CompareRow& row : stage.rows)
        if (std::fabs(row.values[column] - std::round(row.values[column])) > 1e-9) return 2;
    return 0;
}

}  // namespace

void drawCompare(AppState& s) {
    const bool chartOnly = !s.showSidebar && s.convergenceOnly;   // the figure of the genetic algorithm's progress
    if (!chartOnly) {
        if (s.showSidebar) {
            ImGui::BeginDisabled(s.comparing);
            if (ImGui::Button(s.comparing ? "Comparing..." : "Run comparison")) startCompare(s);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Every algorithm of every stage on this scenario, with the options in the sidebar. The dynamic programme takes a few seconds.");
        }
        if (!s.hasCompare) ImGui::TextDisabled("Press Run comparison.");
        for (const CompareStage& stage : s.compare.stages) {
            ImGui::SeparatorText(stage.name.c_str());
            if (!ImGui::BeginTable(stage.name.c_str(), int(stage.columns.size()) + 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) continue;
            ImGui::TableSetupColumn("Algorithm", ImGuiTableColumnFlags_WidthFixed, 220.0f * s.scale);
            for (const std::string& column : stage.columns) ImGui::TableSetupColumn(column.c_str());
            ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();
            for (const CompareRow& row : stage.rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.name.c_str());
                for (size_t c = 0; c < stage.columns.size(); ++c) {
                    ImGui::TableNextColumn();
                    rightText(stage.columns[c] == "proven" ? (row.values[c] != 0 ? "yes" : "no") : thousands(row.values[c], decimalsOf(stage, c)));
                }
                ImGui::TableNextColumn();
                dimWrapped(row.note);
            }
            ImGui::EndTable();
        }
    }
    if (!s.showSidebar && !s.convergenceOnly) return;

    // How the genetic packer improved, from the last run.
    ImGui::SeparatorText("Genetic algorithm: best layout by generation");
    if (!s.hasResult) {
        ImGui::TextDisabled("Press Optimize to run the plan first.");
    } else if (s.result.pack.convergence.empty()) {
        ImGui::TextDisabled("The last run did not use the genetic packer.");
    } else {
        const std::vector<double>& best = s.result.pack.convergence;
        std::vector<double> generation(best.size(), 0.0);
        for (size_t g = 0; g < generation.size(); ++g) generation[g] = double(g);
        ImGui::Text("%d generations, from %s to %s PKR/week.", s.result.pack.generationsRun, thousands(best.front(), 1).c_str(), thousands(best.back(), 1).c_str());
        if (ImPlot::BeginPlot("##convergence", ImVec2(-1, chartOnly ? std::max(ImGui::GetContentRegionAvail().y, 260.0f * s.scale) : 280.0f * s.scale))) {
            ImPlot::SetupAxes("generation", "value of the best layout, PKR per week", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupLegend(ImPlotLocation_SouthEast);
            ImPlot::PlotLine("best layout", generation.data(), best.data(), int(best.size()), {ImPlotProp_LineWeight, 2.5f});
            ImPlot::EndPlot();
        }
    }
}

// ---- Data -------------------------------------------------------------------------------------------------------------

namespace {

struct Cell {
    std::string text;
    double number = 0;
    bool numeric = false;
};
using DataRow = std::vector<Cell>;

Cell textCell(const std::string& value) { return Cell{value, 0, false}; }
Cell numberCell(double value, int decimals = 0) { return Cell{format("%.*f", decimals, value), value, true}; }

// A table the person can sort by clicking a header. The rows are rebuilt every frame, so they are sorted every frame too.
void dataTable(const char* id, const std::vector<const char*>& headers, std::vector<DataRow> rows, const std::vector<float>& widths = {}) {
    if (!ImGui::BeginTable(id, int(headers.size()), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) return;
    for (size_t c = 0; c < headers.size(); ++c) ImGui::TableSetupColumn(headers[c], ImGuiTableColumnFlags_WidthStretch, c < widths.size() ? widths[c] : 1.0f);
    ImGui::TableHeadersRow();
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        const int column = specs->Specs[0].ColumnIndex;
        const bool ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        std::stable_sort(rows.begin(), rows.end(), [&](const DataRow& a, const DataRow& b) {
            const Cell &x = a[size_t(column)], &y = b[size_t(column)];
            const bool less = x.numeric ? x.number < y.number : x.text < y.text;
            const bool more = x.numeric ? y.number < x.number : y.text < x.text;
            return ascending ? less : more;
        });
    }
    for (const DataRow& row : rows) {
        ImGui::TableNextRow();
        for (const Cell& cell : row) {
            ImGui::TableNextColumn();
            if (cell.numeric) rightText(cell.text);
            else ImGui::TextUnformatted(cell.text.c_str());
        }
    }
    ImGui::EndTable();
}

const char* lightText(Light light) { return light == Light::Low ? "L" : light == Light::Medium ? "M" : "H"; }   // as in plan 6.1

}  // namespace

void drawData(AppState& s) {
    const Scenario& sc = s.scenario;
    ImGui::SeparatorText(format("Crops (%zu)", sc.crops.size()).c_str());
    std::vector<DataRow> crops;
    for (const Crop& c : sc.crops)
        crops.push_back({numberCell(c.id), textCell(c.name), textCell(c.category), textCell(format("%d x %d%s", c.trayW, c.trayD, c.canRotate ? "" : " fixed")), numberCell(c.height),
                         numberCell(c.cycleWeeks), numberCell(c.harvestWindowWeeks), textCell(format("%s-%s", lightText(c.lightMin), lightText(c.lightMax))),
                         textCell(format("%.0f-%.0f", c.humidityMin, c.humidityMax)), numberCell(c.laborPerWeek, 2), numberCell(c.waterPerWeek, 1),
                         numberCell(c.nutrientPerWeek, 2), numberCell(c.energyPerWeek, 1), numberCell(c.yieldKg, 2), numberCell(c.pricePerKg), numberCell(c.trayCost),
                         numberCell(c.salvageFraction, 2)});
    dataTable("crops", {"Id", "Crop", "Category", "Tray cm", "Height cm", "Cycle wk", "Window wk", "Light", "Humidity %", "Labour h", "Water L", "Nutrient L", "Energy kWh",
                        "Yield kg", "Price PKR/kg", "Tray cost", "Salvage"}, crops,
              {0.5f, 1.5f, 1.5f, 1.3f, 1.1f, 1.0f, 1.1f, 0.8f, 1.2f, 1.0f, 0.9f, 1.1f, 1.2f, 0.9f, 1.3f, 1.0f, 1.0f});

    ImGui::SeparatorText(format("Shelves (%zu)", sc.shelves.size()).c_str());
    std::vector<DataRow> shelves;
    for (const Shelf& sh : sc.shelves)
        shelves.push_back({numberCell(sh.id), textCell(sh.name), numberCell(sh.rack + 1), numberCell(sh.level), numberCell(sh.width), numberCell(sh.depth), numberCell(sh.clearance),
                           textCell(lightText(sh.light)), numberCell(sh.humidity)});
    dataTable("shelves", {"Id", "Shelf", "Rack", "Level", "Width cm", "Depth cm", "Clearance cm", "Light", "Humidity %"}, shelves);

    ImGui::SeparatorText(format("Contracts (%zu)", sc.contracts.size()).c_str());
    std::vector<DataRow> contracts;
    for (const Contract& k : sc.contracts)
        contracts.push_back({numberCell(k.id), textCell(k.customer), textCell(sc.crops[size_t(k.cropId)].name), numberCell(k.kgPerWeek, 2), numberCell(k.startWeek),
                             numberCell(k.endWeek), numberCell(k.penaltyPerKg)});
    dataTable("contracts", {"Id", "Customer", "Crop", "kg/week", "From week", "To week", "Penalty PKR/kg"}, contracts);
}
