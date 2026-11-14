// farm_app, milestone M0: a window that shows the Dear ImGui and ImPlot demo windows.
//   farm_app                      open the window
//   farm_app --screenshot DIR     render a few frames, write DIR/m0_demo.png, exit 0 (1 on failure)
// Structure follows examples/example_glfw_opengl3/main.cpp of the pinned ImGui.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <cstring>
#include <filesystem>
#include <fstream>
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

static std::string glfwLastError;
static void glfwErrorCallback(int code, const char* text) { glfwLastError = "GLFW error " + std::to_string(code) + ": " + text; }

// A GUI-subsystem program has no console: write the message to farm_app.log beside the executable and,
// when a person is at the keyboard, show a message box. Returns 1 so callers can `return fail(...)`.
static int fail(const std::string& message, bool showBox) {
    std::filesystem::path log = "farm_app.log";
#ifdef _WIN32
    char exe[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) log = std::filesystem::path(exe).replace_filename("farm_app.log");
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

int main(int argc, char** argv) {
    std::string shotDir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0) {
            if (++i == argc) return fail("--screenshot needs a directory", false);
            shotDir = argv[i];
        }
    }
    const bool shot = !shotDir.empty();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return fail("Could not start GLFW. " + glfwLastError, !shot);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);  // GL 3.0, GLSL 130: what the example uses outside Apple/ES
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    const float scale = shot ? 1.0f : ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    const int winW = shot ? 1600 : int(1280 * scale), winH = shot ? 1000 : int(800 * scale);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "Smart Vertical Farm", nullptr, nullptr);
    if (!window) {
        const std::string why = glfwLastError;
        glfwTerminate();
        return fail("Could not create the window or its OpenGL 3.0 context. " + why, !shot);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the layout is fixed, nothing to persist
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    ImPlot::StyleColorsDark();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    const char* segoe = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (std::filesystem::exists(segoe) && io.Fonts->AddFontFromFileTTF(segoe)) style.FontSizeBase = 17.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(nullptr);

    int frames = 0;
    bool saved = false;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (!shot && glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();  // each demo positions itself; the two do not overlap at 1600 x 1000
        ImPlot::ShowDemoWindow();

        ImGui::Render();
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Four frames let layouts and plot axes settle before the shot.
        if (shot && ++frames == 4) {
            std::error_code ec;
            std::filesystem::create_directories(shotDir, ec);
            const std::filesystem::path file = std::filesystem::path(shotDir) / "m0_demo.png";
            saved = saveScreenshot(window, file);
            if (!saved) fail("Could not write " + file.string(), false);
            break;
        }
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return shot && !saved ? 1 : 0;
}
