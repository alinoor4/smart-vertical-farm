// The tabs that are drawings: Layout, Schedule and Constraints (plan 14.4).
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "implot.h"

namespace {

const char* lightName(Light light) { return light == Light::Low ? "low" : light == Light::Medium ? "medium" : "high"; }

std::string initials(const std::string& name) {
    std::string out;
    bool wordStart = true;
    for (char ch : name) {
        if (ch == ' ') {
            wordStart = true;
        } else if (wordStart) {
            out += ch;
            wordStart = false;
        }
    }
    return out;
}

// The longest of the name, its first four letters, its initials and its first letter that fits in the box; "" when none does.
std::string fitLabel(const std::string& name, float width, float height) {
    if (height < ImGui::GetTextLineHeight()) return "";
    const std::string options[4] = {name, name.substr(0, 4), initials(name), name.substr(0, 1)};
    for (const std::string& option : options)
        if (ImGui::CalcTextSize(option.c_str()).x <= width) return option;
    return "";
}

}  // namespace

// ---- Layout -----------------------------------------------------------------------------------------------------------

void drawLayout(AppState& s) {
    if (needResult(s)) return;
    const PlanResult& r = s.result;
    const Scenario& sc = s.planned;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float line = ImGui::GetTextLineHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text), dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    // The crops that stand on the shelves, as a legend: the trays carry their names too, where they have room.
    {
        const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        bool first = true;
        for (const Crop& c : sc.crops) {
            if (r.finalCount[size_t(c.id)] == 0) continue;
            const std::string label = format("%s (%d)", c.name.c_str(), r.finalCount[size_t(c.id)]);
            const float width = line + ImGui::CalcTextSize(label.c_str()).x + 2 * style.ItemSpacing.x;
            if (!first && ImGui::GetItemRectMax().x + 2 * style.ItemSpacing.x + width < rightEdge) ImGui::SameLine(0, 2 * style.ItemSpacing.x);
            cropLabel(c.id, label, false);
            first = false;
        }
    }
    ImGui::Spacing();

    // Racks side by side, each shelf drawn to scale as seen from above, the top level of a rack first.
    struct RackView { int rack = 0, widest = 0; std::vector<int> shelves; };
    std::map<int, RackView> byRack;
    for (const Shelf& sh : sc.shelves) {
        RackView& view = byRack[sh.rack];
        view.rack = sh.rack;
        view.widest = std::max(view.widest, sh.width);
        view.shelves.push_back(sh.id);
    }
    float totalCm = 0;
    for (auto& entry : byRack) {
        RackView& view = entry.second;
        std::sort(view.shelves.begin(), view.shelves.end(), [&](int a, int b) { return sc.shelves[size_t(a)].level > sc.shelves[size_t(b)].level; });
        totalCm += float(view.widest);
    }
    const float gap = 18.0f * s.scale;
    const float pxPerCm = std::clamp((ImGui::GetContentRegionAvail().x - gap * float(byRack.size() - 1)) / std::max(totalCm, 1.0f), 0.4f * s.scale, 2.2f * s.scale);

    std::vector<std::vector<int>> onShelf(sc.shelves.size());
    std::vector<long long> filled(sc.shelves.size(), 0);
    for (size_t i = 0; i < r.layout.placements.size(); ++i) {
        const Placement& p = r.layout.placements[i];
        onShelf[size_t(p.shelfId)].push_back(int(i));
        filled[size_t(p.shelfId)] += static_cast<long long>(p.w) * p.d;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const Placement* hovered = nullptr;
    const Shelf* hoveredShelf = nullptr;
    float x = origin.x, tallest = 0;
    for (const auto& entry : byRack) {
        const RackView& view = entry.second;
        float y = origin.y;
        dl->AddText(ImVec2(x, y), textColour, format("Rack %d", view.rack + 1).c_str());
        y += line + 4.0f * s.scale;
        for (int id : view.shelves) {
            const Shelf& sh = sc.shelves[size_t(id)];
            const ImVec2 p0(x, y), p1(x + float(sh.width) * pxPerCm, y + float(sh.depth) * pxPerCm);
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg));
            for (int index : onShelf[size_t(id)]) {
                const Placement& p = r.layout.placements[size_t(index)];
                const ImVec2 a(p0.x + float(p.x) * pxPerCm, p0.y + float(p.y) * pxPerCm), b(a.x + float(p.w) * pxPerCm, a.y + float(p.d) * pxPerCm);
                const ImU32 fill = cropColour(p.cropId);
                dl->AddRectFilled(a, b, fill);
                dl->AddRect(a, b, darker(fill), 0.0f, 1.5f);
                const std::string label = fitLabel(sc.crops[size_t(p.cropId)].name, b.x - a.x - 4.0f, b.y - a.y);
                if (!label.empty()) {
                    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
                    dl->AddText(ImVec2((a.x + b.x - size.x) / 2, (a.y + b.y - size.y) / 2), textColourOn(fill), label.c_str());
                }
                if (ImGui::IsMouseHoveringRect(a, b)) {
                    hovered = &p;
                    hoveredShelf = &sh;
                }
            }
            dl->AddRect(p0, p1, dimColour, 0.0f, 1.5f);
            const double percent = 100.0 * double(filled[size_t(id)]) / (double(sh.width) * sh.depth);
            dl->AddText(ImVec2(x, p1.y + 2), textColour, format("%s   %.0f%% full", sh.name.c_str(), percent).c_str());
            dl->AddText(ImVec2(x, p1.y + 2 + line), dimColour, format("%d cm | %s light | %.0f%% hum.", sh.clearance, lightName(sh.light), sh.humidity).c_str());
            y = p1.y + 2 * line + 10.0f * s.scale;
        }
        tallest = std::max(tallest, y - origin.y);
        x += float(view.widest) * pxPerCm + gap;
    }
    ImGui::Dummy(ImVec2(x - origin.x, tallest));
    if (hovered && ImGui::IsWindowHovered()) {
        const Crop& c = sc.crops[size_t(hovered->cropId)];
        ImGui::SetTooltip("%s\ntray %d x %d cm as placed%s\nshelf %s", c.name.c_str(), hovered->w, hovered->d, hovered->w != c.trayW ? " (turned)" : "", hoveredShelf->name.c_str());
    }

    if (!s.showSidebar) return;   // the figure is the racks alone

    // What did not fit.
    ImGui::SeparatorText("Trays that did not fit");
    int selected = 0;
    for (int n : r.selected) selected += n;
    ImGui::Text("The packer placed %zu of the %d selected trays; the refill then added %d.", r.pack.state.placements.size(), selected, r.refilled);
    bool any = false;
    for (const Crop& c : sc.crops) any = any || r.selected[size_t(c.id)] > r.finalCount[size_t(c.id)];
    if (!any) {
        ImGui::TextDisabled("Every selected tray found a place.");
    } else if (ImGui::BeginTable("unplaced", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Crop", ImGuiTableColumnFlags_WidthFixed, 150.0f * s.scale);
        ImGui::TableSetupColumn("Selected");
        ImGui::TableSetupColumn("In the plan");
        ImGui::TableSetupColumn("Unplaced");
        ImGui::TableHeadersRow();
        for (const Crop& c : sc.crops) {
            const int unplaced = r.selected[size_t(c.id)] - r.finalCount[size_t(c.id)];
            if (unplaced <= 0) continue;
            ImGui::TableNextColumn();
            cropLabel(c.id, c.name, false);
            ImGui::TableNextColumn();
            rightText(format("%d", r.selected[size_t(c.id)]));
            ImGui::TableNextColumn();
            rightText(format("%d", r.finalCount[size_t(c.id)]));
            ImGui::TableNextColumn();
            rightText(format("%d", unplaced));
        }
        ImGui::EndTable();
    }

    // The three packers on this selection.
    ImGui::SeparatorText("Compare packers");
    ImGui::BeginDisabled(s.packing || s.running);
    if (ImGui::Button(s.packing ? "Comparing..." : "Compare packers")) startPackers(s);
    ImGui::EndDisabled();
    if (s.hasPackers && ImGui::BeginTable("packers", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        const char* const headers[7] = {"Packer", "Trays", "Placed", "Value PKR/week", "Shelves used", "Utilisation %", "ms"};
        for (const char* header : headers) ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (const PackerRow& row : s.packers) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.name.c_str());
            const std::string cells[6] = {format("%d", row.trays), format("%d", row.placed), thousands(row.value, 1), format("%d", row.shelves),
                                          format("%.1f", row.utilisationPercent), format("%.1f", row.ms)};
            for (const std::string& cell : cells) {
                ImGui::TableNextColumn();
                rightText(cell);
            }
        }
        ImGui::EndTable();
    }
}

// ---- Schedule ---------------------------------------------------------------------------------------------------------

void drawSchedule(AppState& s) {
    if (needResult(s)) return;
    const PlanResult& r = s.result;
    const Scenario& sc = s.planned;
    const bool figure = !s.showSidebar;   // for the report: the timeline alone, with every row on show
    const int horizon = sc.horizonWeeks;
    int longestCycle = 1;
    for (const Crop& c : sc.crops) longestCycle = std::max(longestCycle, c.cycleWeeks);
    const int weeks = horizon + longestCycle;
    const float line = ImGui::GetTextLineHeight();
    const float headerH = line + 6.0f * s.scale, axisH = line + 6.0f * s.scale;
    float slotH = line + 4.0f * s.scale;

    // One header row per crop that has lots or slots, then one row per slot.
    struct Row { int crop, slot; };   // slot -1: the crop's header
    std::vector<Row> rows;
    int headers = 0, slotRows = 0;
    for (const Crop& c : sc.crops) {
        const CropSchedule& cs = r.schedules[size_t(c.id)];
        if (cs.lots.empty() && cs.slots == 0) continue;
        rows.push_back({c.id, -1});
        ++headers;
        for (int k = 0; k < cs.slots; ++k) {
            rows.push_back({c.id, k});
            ++slotRows;
        }
    }
    if (rows.empty()) {
        ImGui::TextDisabled("Nothing is planted, so there is nothing to schedule.");
        return;
    }
    ImGui::TextDisabled("One bar per batch, from planting to harvest. A thick outline marks a contract delivery. The red line is the end of the planting horizon T.");

    const float room = ImGui::GetContentRegionAvail().y;
    const float childH = figure ? room : std::max(220.0f * s.scale, room * 0.5f);
    if (figure) slotH = std::clamp((childH - axisH - float(headers) * headerH - 24.0f * s.scale) / float(std::max(slotRows, 1)), 5.0f, slotH);
    const float totalH = axisH + float(headers) * headerH + float(slotRows) * slotH;

    ImGui::BeginChild("timeline", ImVec2(0, childH), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text), dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float labelW = 64.0f * s.scale;
    const float perWeek = std::max(6.0f, (ImGui::GetContentRegionAvail().x - labelW - 14.0f * s.scale) / float(weeks));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float left = origin.x + labelW, right = left + float(weeks) * perWeek, bottom = origin.y + totalH;

    // The week grid, and the line at T.
    const int labelStep = perWeek < 20.0f * s.scale ? 2 : 1;
    const ImU32 gridColour = ImGui::GetColorU32(ImGuiCol_Border);
    for (int w = 0; w <= weeks; ++w) {
        const float gx = left + float(w) * perWeek;
        dl->AddLine(ImVec2(gx, origin.y + axisH - 4.0f), ImVec2(gx, bottom), gridColour);
        if (w < weeks && w % labelStep == 0) dl->AddText(ImVec2(gx + 2.0f, origin.y), dimColour, format("%d", w).c_str());
    }
    dl->AddText(ImVec2(origin.x, origin.y), dimColour, "week");
    const float tx = left + float(horizon) * perWeek;
    dl->AddLine(ImVec2(tx, origin.y), ImVec2(tx, bottom), IM_COL32(230, 90, 70, 255), 2.0f);

    float y = origin.y + axisH;
    const Batch* hotBatch = nullptr;
    const CropSchedule* hotSchedule = nullptr;
    for (const Row& row : rows) {
        const Crop& c = sc.crops[size_t(row.crop)];
        const CropSchedule& cs = r.schedules[size_t(row.crop)];
        if (row.slot < 0) {
            dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(right, y + headerH), ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
            const float side = line * 0.8f;
            dl->AddRectFilled(ImVec2(origin.x + 2.0f, y + 3.0f), ImVec2(origin.x + 2.0f + side, y + 3.0f + side), cropColour(c.id), 2.0f);
            dl->AddText(ImVec2(origin.x + side + 8.0f, y + 3.0f), textColour, c.name.c_str());
            const bool missed = cs.hardOnTime < cs.hardTotal;
            const std::string stats = format("%d trays | contracts %d / %d on time | spot lots %d / %d served | idle %d slot-weeks", cs.slots, cs.hardOnTime, cs.hardTotal,
                                             cs.softServed, cs.softTotal, cs.idleSlotWeeks);
            const float nameEnd = origin.x + side + 16.0f + ImGui::CalcTextSize(c.name.c_str()).x;
            dl->AddText(ImVec2(std::max(left + 4.0f, nameEnd), y + 3.0f), missed ? IM_COL32(235, 90, 90, 255) : textColour, stats.c_str());
            y += headerH;
            continue;
        }
        if (slotH >= line * 0.9f) dl->AddText(ImVec2(origin.x + 14.0f * s.scale, y + 1.0f), dimColour, format("slot %d", row.slot + 1).c_str());
        for (const Batch& b : cs.batches) {
            if (b.slot != row.slot) continue;
            const ImVec2 a(left + float(b.start) * perWeek + 1.0f, y + 1.0f), e(left + float(b.end) * perWeek - 1.0f, y + slotH - 1.0f);
            const ImU32 fill = cropColour(c.id);
            dl->AddRectFilled(a, e, fill);
            if (cs.lots[size_t(b.lotId)].hard) dl->AddRect(a, e, textColour, 0.0f, slotH >= line ? 2.0f : 1.0f);
            else dl->AddRect(a, e, darker(fill), 0.0f, 1.0f);
            if (ImGui::IsMouseHoveringRect(a, e)) {
                hotBatch = &b;
                hotSchedule = &cs;
            }
        }
        y += slotH;
    }
    ImGui::Dummy(ImVec2(right - origin.x + 8.0f, totalH));
    if (hotBatch && ImGui::IsWindowHovered()) {
        const Lot& lot = hotSchedule->lots[size_t(hotBatch->lotId)];
        ImGui::SetTooltip("%s, slot %d\n%s\nplanted in week %d, harvested in week %d, due in week %d\n%.2f kg, worth %s PKR%s", sc.crops[size_t(hotBatch->cropId)].name.c_str(),
                          hotBatch->slot + 1, lot.hard ? "contract delivery" : "spot demand", hotBatch->start, hotBatch->end, lot.dueWeek, lot.kg,
                          thousands(lot.value, 0).c_str(), lot.hard ? format(" (a miss costs %s PKR)", thousands(lot.penalty, 0).c_str()).c_str() : "");
    }
    ImGui::EndChild();
    if (figure) return;

    // The lots of each delivery week: what was due and what was delivered.
    std::vector<int> contractDue(size_t(weeks), 0), contractDone(size_t(weeks), 0), spotWanted(size_t(weeks), 0), spotServed(size_t(weeks), 0);
    int firstWeek = weeks, lastWeek = -1;
    for (const CropSchedule& cs : r.schedules)
        for (size_t i = 0; i < cs.lots.size(); ++i) {
            const Lot& lot = cs.lots[i];
            if (lot.dueWeek < 0 || lot.dueWeek >= weeks) continue;
            firstWeek = std::min(firstWeek, lot.dueWeek);
            lastWeek = std::max(lastWeek, lot.dueWeek);
            const bool done = cs.scheduled[i] != 0;
            (lot.hard ? contractDue : spotWanted)[size_t(lot.dueWeek)] += 1;
            (lot.hard ? contractDone : spotServed)[size_t(lot.dueWeek)] += done ? 1 : 0;
        }
    ImGui::SeparatorText("Deliveries by week");
    if (lastWeek >= firstWeek && ImGui::BeginTable("weekly", 2 + lastWeek - firstWeek, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Delivery week", ImGuiTableColumnFlags_WidthFixed, 150.0f * s.scale);
        for (int w = firstWeek; w <= lastWeek; ++w) ImGui::TableSetupColumn(format("%d", w).c_str());
        ImGui::TableHeadersRow();
        const char* const labels[4] = {"Contract lots due", "Contract lots delivered", "Spot lots wanted", "Spot lots served"};
        const std::vector<int>* counts[4] = {&contractDue, &contractDone, &spotWanted, &spotServed};
        for (int k = 0; k < 4; ++k) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(labels[k]);
            for (int w = firstWeek; w <= lastWeek; ++w) {
                ImGui::TableNextColumn();
                const int n = (*counts[k])[size_t(w)];
                const bool fewer = k == 1 && n < contractDue[size_t(w)];   // a contract lot that was not delivered
                if (fewer) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 90, 90, 255));
                if (n == 0) ImGui::TextDisabled("0");
                else ImGui::Text("%d", n);
                if (fewer) ImGui::PopStyleColor();
            }
        }
        ImGui::EndTable();
    }

    // The weekly use of each resource against its budget.
    ImGui::SeparatorText("Weekly resource use");
    const double budget[4] = {sc.budget.labor, sc.budget.water, sc.budget.nutrient, sc.budget.energy};
    const char* const names[4] = {"labour", "water", "nutrient solution", "energy"};
    std::vector<double> weekAxis(size_t(horizon), 0.0), percent[4];
    double top = 110.0;
    for (int t = 0; t < horizon; ++t) weekAxis[size_t(t)] = t;
    for (int k = 0; k < 4; ++k)
        for (int t = 0; t < horizon && budget[k] > 0; ++t) {
            percent[k].push_back(100.0 * r.weeklyUse[size_t(t)][size_t(k)] / budget[k]);
            top = std::max(top, percent[k].back() * 1.08);
        }
    if (ImPlot::BeginPlot("##resources", ImVec2(-1, std::max(ImGui::GetContentRegionAvail().y, 200.0f * s.scale)))) {
        ImPlot::SetupAxes("week", "% of the weekly budget", ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, top, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_SouthEast);
        for (int k = 0; k < 4; ++k)
            if (!percent[k].empty()) ImPlot::PlotLine(names[k], weekAxis.data(), percent[k].data(), horizon, {ImPlotProp_LineWeight, 2.0f});
        const double hundred = 100.0;
        ImPlot::PlotInfLines("100% of the budget", &hundred, 1, {ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal});
        ImPlot::EndPlot();
    }
}

// ---- Constraints ------------------------------------------------------------------------------------------------------

namespace {

// Why a crop may not stand on a shelf, one line per failed check.
std::string failedChecks(const Crop& c, const Shelf& sh, unsigned char why, int lightGap) {
    std::string text;
    auto add = [&](const std::string& line) { text += (text.empty() ? "" : "\n") + line; };
    if (why & Clearance) add(format("clearance: %d cm tall plus %d cm for the lamp, the shelf has %d cm", c.height, lightGap, sh.clearance));
    if (why & LightMismatch) add(format("light: the crop needs %s to %s, the shelf is %s", lightName(c.lightMin), lightName(c.lightMax), lightName(sh.light)));
    if (why & HumidityMismatch) add(format("humidity: the crop needs %.0f to %.0f%%, the shelf has %.0f%%", c.humidityMin, c.humidityMax, sh.humidity));
    if (why & Footprint) add(format("footprint: a %d x %d cm tray does not fit a %d x %d cm shelf", c.trayW, c.trayD, sh.width, sh.depth));
    return text;
}

const char* reasonName(ConflictReason reason) {
    return reason == ConflictReason::Explicit ? "agronomic rule" : reason == ConflictReason::Height ? "height ratio" : "no shelf both can use";
}

ImU32 reasonColour(ConflictReason reason) {
    return reason == ConflictReason::Explicit ? IM_COL32(220, 60, 60, 255) : reason == ConflictReason::Height ? IM_COL32(240, 160, 40, 255) : IM_COL32(150, 150, 150, 255);
}

}  // namespace

void drawConstraints(AppState& s) {
    if (needResult(s)) return;
    const PlanResult& r = s.result;
    const Scenario& sc = s.planned;
    const ConstraintModel& m = r.constraints;
    const int crops = int(sc.crops.size());
    const float line = ImGui::GetTextLineHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

    // The conflict graph: crops on a circle, coloured by their DSATUR class, with an edge for every pair that may not share a shelf.
    const std::vector<int> colourClass = colourDsatur(m.adjacency);
    const float box = std::min(ImGui::GetContentRegionAvail().x * 0.46f, 480.0f * s.scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 centre(origin.x + box / 2, origin.y + box / 2);
    const float ringRadius = box / 2 - 100.0f * s.scale, nodeRadius = 14.0f * s.scale;   // room for the names around the ring
    std::vector<ImVec2> at(static_cast<size_t>(crops));
    for (int i = 0; i < crops; ++i) {
        const float angle = 6.2831853f * float(i) / float(std::max(crops, 1)) - 1.5707963f;
        at[size_t(i)] = ImVec2(centre.x + ringRadius * std::cos(angle), centre.y + ringRadius * std::sin(angle));
    }
    int pairsByReason[4] = {0, 0, 0, 0};
    for (int a = 0; a < crops; ++a)
        for (int b = a + 1; b < crops; ++b) {
            const ConflictReason why = m.conflict[size_t(a)][size_t(b)];
            if (why == ConflictReason::None) continue;
            ++pairsByReason[int(why)];
            dl->AddLine(at[size_t(a)], at[size_t(b)], reasonColour(why), 2.0f);
        }
    int hoveredNode = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int i = 0; i < crops; ++i) {
        const ImU32 fill = cropColour(colourClass[size_t(i)]);
        dl->AddCircleFilled(at[size_t(i)], nodeRadius, fill);
        dl->AddCircle(at[size_t(i)], nodeRadius, darker(fill), 0, 2.0f);
        const std::string number = format("%d", colourClass[size_t(i)] + 1);
        const ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
        dl->AddText(ImVec2(at[size_t(i)].x - numberSize.x / 2, at[size_t(i)].y - numberSize.y / 2), textColourOn(fill), number.c_str());
        const std::string& name = sc.crops[size_t(i)].name;
        const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        const float dx = at[size_t(i)].x - centre.x, dy = at[size_t(i)].y - centre.y;
        const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
        const float reach = nodeRadius + 6.0f * s.scale;
        const ImVec2 anchor(at[size_t(i)].x + dx / length * reach, at[size_t(i)].y + dy / length * reach);
        dl->AddText(ImVec2(dx < -1.0f ? anchor.x - nameSize.x : (dx > 1.0f ? anchor.x : anchor.x - nameSize.x / 2), dy < -1.0f ? anchor.y - nameSize.y : (dy > 1.0f ? anchor.y : anchor.y - nameSize.y / 2)),
                    textColour, name.c_str());
        if (std::hypot(mouse.x - at[size_t(i)].x, mouse.y - at[size_t(i)].y) <= nodeRadius) hoveredNode = i;
    }
    ImGui::Dummy(ImVec2(box, box));
    if (hoveredNode >= 0 && ImGui::IsWindowHovered()) {
        std::string text = format("%s: colour class %d", sc.crops[size_t(hoveredNode)].name.c_str(), colourClass[size_t(hoveredNode)] + 1);
        for (int other : m.adjacency[size_t(hoveredNode)])
            text += format("\nconflicts with %s (%s)", sc.crops[size_t(other)].name.c_str(), reasonName(m.conflict[size_t(hoveredNode)][size_t(other)]));
        ImGui::SetTooltip("%s", text.c_str());
    }

    // Beside it: the legend and the numbers.
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SeparatorText("Edges: why two crops may not share a shelf");
    const ConflictReason reasons[3] = {ConflictReason::Explicit, ConflictReason::Height, ConflictReason::NoCommonShelf};
    for (ConflictReason why : reasons) {
        const ImVec2 here = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(here.x, here.y + line / 2), ImVec2(here.x + 32.0f * s.scale, here.y + line / 2), reasonColour(why), 3.0f);
        ImGui::Dummy(ImVec2(32.0f * s.scale, line));
        ImGui::SameLine();
        ImGui::Text("%s: %d pairs", reasonName(why), pairsByReason[int(why)]);
    }
    ImGui::TextDisabled("Node colour and number: the DSATUR colour class. Hover a node for its conflicts.");
    ImGui::SeparatorText("Pruning");
    ImGui::Text("%lld of %lld crop-shelf pairs can be used (%.0f%% pruned).", m.pairsCompatible, m.pairsTotal,
                m.pairsTotal > 0 ? 100.0 * double(m.pairsTotal - m.pairsCompatible) / double(m.pairsTotal) : 0.0);
    ImGui::SeparatorText("Colour groups needed");
    ImGui::Text("Welsh-Powell %d, DSATUR %d, exact %d (%s, %lld search nodes).", r.colours.welshPowell, r.colours.dsatur, r.colours.exact,
                r.colours.exactProven ? "proven" : "the search was cut off", r.colours.exactNodes);
    ImGui::TextDisabled("Crops of one colour class may share shelves; each shelf holds one class.");
    ImGui::EndGroup();

    // Which crop may stand on which shelf.
    ImGui::SeparatorText("Crops and shelves");
    const int shelves = int(sc.shelves.size());
    if (!ImGui::BeginTable("compat", 2 + shelves, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) return;
    ImGui::TableSetupColumn("Crop", ImGuiTableColumnFlags_WidthFixed, 150.0f * s.scale);
    for (const Shelf& sh : sc.shelves) ImGui::TableSetupColumn(sh.name.c_str());
    ImGui::TableSetupColumn("Max trays");
    ImGui::TableHeadersRow();
    for (const Crop& c : sc.crops) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        cropLabel(c.id, c.name, false);
        for (const Shelf& sh : sc.shelves) {
            ImGui::TableNextColumn();
            const float side = line * 0.7f;
            const float shift = (ImGui::GetContentRegionAvail().x - side) * 0.5f;
            if (shift > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
            const ImVec2 here = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(side, side));
            const bool ok = m.compat[size_t(c.id)][size_t(sh.id)] != 0;
            auto point = [&](float fx, float fy) { return ImVec2(here.x + fx * side, here.y + fy * side); };
            if (ok) {
                dl->AddLine(point(0.05f, 0.55f), point(0.38f, 0.9f), IM_COL32(80, 190, 90, 255), 2.5f);
                dl->AddLine(point(0.38f, 0.9f), point(0.95f, 0.1f), IM_COL32(80, 190, 90, 255), 2.5f);
            } else {
                dl->AddLine(point(0.1f, 0.1f), point(0.9f, 0.9f), IM_COL32(225, 70, 70, 255), 2.5f);
                dl->AddLine(point(0.9f, 0.1f), point(0.1f, 0.9f), IM_COL32(225, 70, 70, 255), 2.5f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s on %s\n%s", c.name.c_str(), sh.name.c_str(), failedChecks(c, sh, m.whyNot[size_t(c.id)][size_t(sh.id)], sc.lightGapCm).c_str());
            }
        }
        ImGui::TableNextColumn();
        rightText(format("%d", m.maxTraysByArea[size_t(c.id)]));
    }
    ImGui::EndTable();
}
