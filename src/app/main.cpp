// farm_app: the Dear ImGui desktop app (plan section 14).
//   farm_app [--scenario FILE] [--theme light|dark]    open the window
//   farm_app --screenshot DIR [...]                    write the PNG files of plan 14.5 into DIR, exit 0 (1 on failure)
//   farm_app --selftest [--scenario FILE]              run the script of plan 14.6 instead of a person, exit 0 (1 on failure)
// Structure follows examples/example_glfw_opengl3/main.cpp of the pinned ImGui.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>  // Also pulls in the system OpenGL 1.1 header (glReadPixels)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "ui.h"

static std::string glfwLastError;
static void glfwErrorCallback(int code, const char* text) { glfwLastError = "GLFW error " + std::to_string(code) + ": " + text; }

static std::filesystem::path exePath(const char* argv0) {
#ifdef _WIN32
    char exe[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return exe;
#endif
    return std::filesystem::absolute(argv0);
}

// A GUI-subsystem program has no console: write the message to farm_app.log beside the executable and,
// when a person is at the keyboard, show a message box. Returns 1 so callers can `return fail(...)`.
static int fail(const std::string& message, bool showBox) {
    const std::filesystem::path log = exePath("farm_app").replace_filename("farm_app.log");
#ifdef _WIN32
    if (showBox) MessageBoxA(nullptr, message.c_str(), "Smart Vertical Farm", MB_OK | MB_ICONERROR);
#else
    (void)showBox;
#endif
    std::ofstream(log, std::ios::app) << message << '\n';
    return 1;
}

// Reads the back buffer (call before swapping) and writes it as a PNG.
static bool saveScreenshot(GLFWwindow* window, const std::filesystem::path& file) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    std::vector<unsigned char> pixels(size_t(w) * size_t(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);  // OpenGL rows run bottom to top
    return stbi_write_png(file.string().c_str(), w, h, 3, pixels.data(), w * 3) != 0;
}

// One frame into the back buffer: collect the workers, build the interface, draw it. The caller swaps, or reads the pixels first.
static void renderFrame(GLFWwindow* window, AppState& state, bool light) {
    updateState(state);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawApp(state);
    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    if (light) glClearColor(0.94f, 0.94f, 0.94f, 1.0f);
    else glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ---- screenshot mode (plan 14.5) ----------------------------------------------------------------------------------------

struct Shot {
    std::string name;
    int tab;
    bool sidebar, convergenceOnly;
    int benchSuite;   // the benchmark suite to show alone (an index into AppState::bench), or -1
};

// The figures with the sidebar hidden are the ones the report uses: the benchmark figures (fig_bench_<suite>) come last, one for
// each suite with a table.
static std::vector<Shot> shotList(const AppState& state) {
    std::vector<Shot> shots = {
        {"ui_overview", TabOverview, true, false, -1},        {"ui_layout", TabLayout, true, false, -1},   {"ui_schedule", TabSchedule, true, false, -1},
        {"ui_forecast", TabForecast, true, false, -1},        {"ui_constraints", TabConstraints, true, false, -1}, {"ui_compare", TabCompare, true, false, -1},
        {"ui_data", TabData, true, false, -1},                {"fig_layout", TabLayout, false, false, -1}, {"fig_schedule", TabSchedule, false, false, -1},
        {"fig_forecast", TabForecast, false, false, -1},      {"fig_constraints", TabConstraints, false, false, -1}, {"fig_compare", TabCompare, false, false, -1},
        {"fig_convergence", TabCompare, false, true, -1}};
    for (size_t i = 0; i < state.bench.size(); ++i) shots.push_back({"fig_bench_" + state.bench[i].suite, TabCompare, false, false, int(i)});
    return shots;
}

static int runScreenshots(GLFWwindow* window, AppState& state, bool light, const std::string& dir) {
    runSync(state);   // the plan, the comparison and the packer table, all before the first shot
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int failed = 0;
    if (state.bench.size() < benchSuiteNames().size()) {   // a missing table is a missing figure
        fail("Only " + std::to_string(state.bench.size()) + " of " + std::to_string(benchSuiteNames().size()) + " benchmark tables were found. " + state.benchStatus, false);
        ++failed;
    }
    for (const Shot& shot : shotList(state)) {
        state.requestTab = shot.tab;
        state.showSidebar = shot.sidebar;
        state.convergenceOnly = shot.convergenceOnly;
        state.scalingOnly = shot.benchSuite >= 0;
        if (shot.benchSuite >= 0) state.benchSuite = shot.benchSuite;
        glfwPollEvents();
        for (int i = 0; i < 4; ++i) {   // four frames let layouts and plot axes settle
            renderFrame(window, state, light);
            if (i < 3) glfwSwapBuffers(window);
        }
        const std::filesystem::path file = std::filesystem::path(dir) / (shot.name + ".png");
        if (!saveScreenshot(window, file)) {
            fail("Could not write " + file.string(), false);
            ++failed;
        }
        glfwSwapBuffers(window);
    }
    return failed == 0 ? 0 : 1;
}

// ---- self-test mode (plan 14.6) -----------------------------------------------------------------------------------------

// A script in place of a person, run by the normal main loop: it checks the worker thread and the dirty flag, which screenshots cannot.
struct SelfTest {
    int step = 0, frames = 0;
    std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
    std::string error;
    bool done = false;

    void next() { ++step; frames = 0; stepStart = std::chrono::steady_clock::now(); }
    void check(bool ok, const char* what) { if (!ok && error.empty()) error = what; }
};

static bool idle(const AppState& s) { return !s.running && !s.dirty && !s.requestRun; }

static void selfTestFrame(AppState& s, SelfTest& t) {
    if (std::chrono::steady_clock::now() - t.stepStart > std::chrono::seconds(60)) {
        t.error = "step " + std::to_string(t.step) + " did not finish within 60 seconds";
        return;
    }
    switch (t.step) {
        case 0:   // the first result arrives by itself
            if (s.hasResult && idle(s) && s.runsFinished == 1) {
                s.scenario.budget.labor *= 0.8;   // a changed budget starts a new run by itself
                s.dirty = true;
                t.next();
            }
            break;
        case 1:
            if (s.runsFinished == 2 && idle(s)) {
                t.check(s.result.problem.capacity[0] == s.scenario.budget.labor, "the re-run did not use the changed labour budget");
                s.options.packer = PackAlgo::BestFitDecreasing;
                s.dirty = true;
                t.next();
            }
            break;
        case 2:
            if (s.runsFinished == 3 && idle(s)) {
                t.check(s.result.pack.algorithm == "Best-Fit Decreasing", "the re-run did not use the new packer");
                startCompare(s);
                startPackers(s);
                t.next();
            }
            break;
        case 3:
            if (s.hasCompare && s.hasPackers && !s.comparing && !s.packing) {
                t.check(s.compare.stages.size() == 5 && s.packers.size() == 3, "the comparison is incomplete");
                t.next();
            }
            break;
        case 4: {   // every tab for two frames; the frame after the request is the one in which the tab opens
            const int tab = t.frames / 3;
            if (tab >= TabCount) t.done = true;
            else if (t.frames % 3 == 0) s.requestTab = tab;
            else if (t.frames % 3 == 2) t.check(s.tab == tab, "a tab did not open");
            break;
        }
        default: break;
    }
    ++t.frames;
}

// ---- main ----------------------------------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string shotDir, scenarioFile, theme = "dark";
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        auto value = [&](std::string& into) {
            if (++i == argc) throw std::invalid_argument(std::string(argv[i - 1]) + " needs a value");
            into = argv[i];
        };
        try {
            if (std::strcmp(argv[i], "--screenshot") == 0) value(shotDir);
            else if (std::strcmp(argv[i], "--scenario") == 0) value(scenarioFile);
            else if (std::strcmp(argv[i], "--theme") == 0) value(theme);
            else if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
            else throw std::invalid_argument(std::string("unknown option ") + argv[i]);
        } catch (const std::exception& e) {
            return fail(e.what(), false);
        }
    }
    if (theme != "dark" && theme != "light") return fail("--theme must be dark or light", false);
    const bool shot = !shotDir.empty(), light = theme == "light";

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return fail("Could not start GLFW. " + glfwLastError, !shot && !selftest);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);  // GL 3.0, GLSL 130: what the example uses outside Apple/ES
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    const float scale = shot ? 1.0f : ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    const int winW = shot ? 1600 : int(1280 * scale), winH = shot ? 1000 : int(800 * scale);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "Smart Vertical Farm", nullptr, nullptr);
    if (!window) {
        const std::string why = glfwLastError;
        glfwTerminate();
        return fail("Could not create the window or its OpenGL 3.0 context. " + why, !shot && !selftest);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot::GetStyle().FitPadding = ImVec2(0.03f, 0.08f);   // a fitted plot keeps a margin, so that a line is not drawn on the frame
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the layout is fixed, nothing to persist
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (shot) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;   // a pointer left over the window would show a tooltip in the picture

    ImGuiStyle& style = ImGui::GetStyle();
    if (light) {
        ImGui::StyleColorsLight();
        ImPlot::StyleColorsLight();
    } else {
        ImGui::StyleColorsDark();
        ImPlot::StyleColorsDark();
    }
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    const char* segoe = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (std::filesystem::exists(segoe) && io.Fonts->AddFontFromFileTTF(segoe)) style.FontSizeBase = 17.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(nullptr);

    int exitCode = 0;
    try {
        AppState state;
        state.scale = scale;
        const bool scenarioOk = initState(state, exePath(argv[0]).parent_path().string(), scenarioFile);
        if (!scenarioOk && (shot || selftest)) {   // a script has nobody to read the message in the sidebar
            exitCode = fail(state.status, false);
        } else if (shot) {
            exitCode = runScreenshots(window, state, light, shotDir);
        } else {
            SelfTest test;
            while (!glfwWindowShouldClose(window)) {
                glfwPollEvents();
                if (!selftest && glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
                    ImGui_ImplGlfw_Sleep(10);
                    continue;
                }
                renderFrame(window, state, light);
                glfwSwapBuffers(window);
                if (selftest) {
                    selfTestFrame(state, test);
                    if (!test.error.empty() || test.done) {
                        if (!test.error.empty()) fail("Self-test failed: " + test.error, false);
                        exitCode = test.error.empty() ? 0 : 1;
                        break;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        exitCode = fail(std::string("Unexpected error: ") + e.what(), !shot && !selftest);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
