# Smart Vertical Farm: Implementation Plan

| | |
|---|---|
| Prepared | 2026-11-13 |
| For | The agent that will implement the project (you start with no other context) |
| Source | `Smart Vertical Farm Proposal.pdf` in the project root (2 pages), plus decisions the owner made while this plan was written |
| Course | Design and Analysis of Algorithms, "Complex Computing Problem" (CCP) submission, team of four |
| Companion file | `plan/reference_model.py`: an executable definition of every formula in this plan and the source of its golden numbers |

## Contents

0. [Read this first](#0-read-this-first)
1. [Decisions already made](#1-decisions-already-made)
2. [Scope](#2-scope)
3. [Environment and toolchain](#3-environment-and-toolchain)
4. [Architecture](#4-architecture)
5. [Data model](#5-data-model)
6. [Sample data and the generator](#6-sample-data-and-the-generator)
7. [Component 1: constraint layer](#7-component-1-constraint-layer)
8. [Component 2: demand forecasting](#8-component-2-demand-forecasting)
9. [Component 3: crop selection as a knapsack](#9-component-3-crop-selection-as-a-knapsack)
10. [Component 4: grow-space packing](#10-component-4-grow-space-packing)
11. [Component 5: harvest and replant scheduling](#11-component-5-harvest-and-replant-scheduling)
12. [Pipeline](#12-pipeline)
13. [Command-line tool](#13-command-line-tool)
14. [Desktop app](#14-desktop-app)
15. [Benchmarks](#15-benchmarks)
16. [Tests](#16-tests)
17. [Complexity analysis document](#17-complexity-analysis-document)
18. [Report and slides](#18-report-and-slides)
19. [Milestones](#19-milestones)
20. [Git, GitHub and privacy](#20-git-github-and-privacy)
21. [Risks and fallbacks](#21-risks-and-fallbacks)
22. [Definition of done](#22-definition-of-done)

Appendices: [A. Golden numbers](#appendix-a-golden-numbers) · [B. Verified library facts](#appendix-b-verified-library-facts) · [C. Glossary](#appendix-c-glossary)

---

## 0. Read this first

**What you are building.** A C++17 desktop application that helps the operator of an indoor vertical farm decide what to plant, where to put it, and when to harvest. Five algorithmic components (constraint graph, demand forecast, knapsack crop selection, 2D shelf packing, harvest scheduling) run as one pipeline. Each component has several algorithm variants so that they can be compared, because the comparison is what a Design and Analysis of Algorithms project is graded on. Around the pipeline sit a command-line tool, a Dear ImGui desktop app, a benchmark harness, tests, a complexity analysis, a written report and a slide deck.

**How to use this plan.**

1. Read sections 0 to 6 completely before running any command.
2. Work through the milestones in section 19 in order. Each one names the sections that specify it and ends with acceptance checks. Do not start a milestone while the previous one's checks fail.
3. Sections 7 to 12 are normative, tie-breaking rules included. Where prose and `plan/reference_model.py` seem to disagree, the reference model is the definition; tell the owner about the discrepancy.

**Rules of engagement.**

1. **The decisions in section 1 belong to the owner.** Do not change them. If one blocks you, stop and ask.
2. **Every algorithm is written by hand.** `src/core` may use only the C++ standard library (plus the JSON library inside `json_io.cpp`). No solver, optimisation, graph, statistics or machine-learning library anywhere. This is a course requirement in spirit: the team must be able to defend every line in a viva.
3. **Results must be deterministic.** Same input and same seed give the same output on every run and every machine. No `std::rand`, no time-based seeds, no `std::uniform_*_distribution` (implementation-defined), no iteration over `std::unordered_*` where order can reach the output. Every sort uses a comparator that is a total order, with the tie-breaks this plan gives.
4. **Build only what is specified.** Section 2 lists things that look tempting and are out of scope. A smaller correct system beats a larger shaky one.
5. **Verify instead of assuming.** The GUI libraries pinned here were released in the months before this plan was written and their APIs changed this year. Appendix B lists what was checked. Read the headers that CMake downloads before writing UI code.
6. **Report honestly.** If a test fails, say so and show the output. Never put a number in the report, the slides or the README that did not come out of the program. If something in this plan turns out to be wrong, say what you found and what you did instead.
7. **Ask before you:** push code to GitHub for the first time (section 20), install anything this plan does not list, commit a file containing the students' names or roll numbers, or delete anything you did not create.
8. **Code must be explainable by second-year students.** Plain structs, free functions, `std::vector`. No inheritance hierarchies, no template metaprogramming, no clever one-liners. Each algorithm lives in its own function with a short header comment stating what it computes and its time complexity.

---

## 1. Decisions already made

The owner answered these on 2026-11-13.

| Topic | Decision |
|---|---|
| Language | C++ (use C++17) |
| Interface | Native desktop application. An earlier answer of "web dashboard" was explicitly replaced by "C++ with desktop app" |
| GUI toolkit | Dear ImGui + ImPlot |
| Algorithm depth | Several variants per component, compared against each other |
| Deliverables | Working program, complexity analysis, benchmark comparison, full written report, presentation slides |
| Document formats | Report as Word `.docx`, slides as PowerPoint `.pptx` |
| Report requirement | Must map the project to the Complex Computing Problem attributes, using the Seoul Accord list: the nine characteristics of its table D.4.1. The owner supplied the official text, reproduced in section 18.4 |
| Sample data | Generated, Pakistani market, prices in PKR. No real farm data exists |
| Toolchain | You may install MinGW-w64 g++ and CMake with winget (approved) |
| Version control | Git, with remote `git@github.com:alinoor4/smart-vertical-farm.git` |

Engineering choices made while planning. You may revise one of these if it proves unworkable, but say so and say why.

| Topic | Choice | Why |
|---|---|---|
| Window and renderer | GLFW + OpenGL 3 backend of Dear ImGui | Best documented path, has an official MinGW build, works on other operating systems for free |
| Build | CMake + Ninja, one `build.ps1` wrapper | Works in any IDE; the wrapper hides a PATH problem described in section 3 |
| Dependencies | CMake `FetchContent`, pinned to exact commits, downloaded into `.deps/` | Nothing vendored, reproducible, survives a clean rebuild |
| JSON | nlohmann/json | One macro line per struct instead of a hand-written parser |
| Tests | A 30-line hand-written `CHECK` harness run by CTest | A test framework is not worth a dependency here |
| Planning model | Static layout plus a weekly timeline (section 4.3) | Smallest model in which all five proposal components do real work |

---

## 2. Scope

**In scope**

- The five components of the proposal, each with the variants listed below, joined into one pipeline.
- A seeded sample-data generator and two committed scenarios.
- A command-line tool, a desktop app, a benchmark harness, a test suite.
- Complexity analysis, report, slides, README.

| Component | Variants to implement |
|---|---|
| Constraint layer | Compatibility matrix, conflict graph; colouring by Welsh-Powell, DSATUR, and exact backtracking |
| Forecasting | Seasonal naive, moving average, simple exponential smoothing, additive Holt-Winters; automatic selection by hold-out error |
| Crop selection | Greedy by value density, dynamic programming, branch and bound |
| Packing | First-Fit Decreasing, Best-Fit Decreasing, genetic algorithm |
| Scheduling | Earliest-deadline-first, EDF with lookahead, exact search for small instances; interval partitioning for slot requirements |

**Out of scope. Do not build these.** Each was considered and left out on purpose.

- Growing a different crop in a slot after a harvest (crop rotation), or filling an idle slot with another crop. It would make the conflict constraints depend on time.
- Conflicts between shelves that are above or below each other. A conflict means "may not share a shelf".
- True 3D packing. Trays are never stacked on one shelf, because plants need light from above; the vertical dimension is handled by the shelf clearance constraint.
- Free-form rectangle packing (MaxRects, skyline, guillotine). Packing is row based (section 10).
- Labour peaks at planting or harvest, crops harvested several times per cycle, batches already growing at time zero.
- Editing crops, shelves or contracts inside the app. They are edited in the JSON file and reloaded. Budgets, horizon and algorithm choices are editable in the app.
- Native file dialogs, installers, auto-update, databases, user accounts, networking, sensors, localisation, continuous integration.

---

## 3. Environment and toolchain

### 3.1 Facts about the owner's machine (checked 2026-11-13)

| Fact | Value |
|---|---|
| OS | Windows 11 Pro Education, build 26300. PowerShell 7 is the main shell; Git Bash is available |
| Project folder | The folder that holds this `plan/` directory. **Its path contains spaces.** |
| Contents before you start | `Smart Vertical Farm Proposal.pdf` (ignored by git), `.gitignore` and the `plan/` folder. Already a git repository on branch `main` with remote `origin` |
| C++ compiler, CMake, Ninja, Make | **None installed** |
| Other tools present | Python 3.14.7, uv 0.11.6, Node 24.22.1, Java 26, git 2.55.0, GitHub CLI 2.101.0 (logged in as `alinoor4`, SSH), winget, VS Code |
| Disk | **C: has 8.5 GB free. D: has 79 GB free.** Install large things on D: |
| WSL | Only the Docker Desktop distribution. Do not use WSL |
| Git identity | `user.name = alinoor4`, `init.defaultBranch = main`. This repository's local `user.email` is the owner's GitHub noreply address, so the owner's personal address stays out of the public history. Do not change it |
| Remote | `git@github.com:alinoor4/smart-vertical-farm.git` is **public** and holds the planning commits (`plan/` and `.gitignore`), first pushed on 2026-11-13. SSH access works |

### 3.2 Install the toolchain

One winget package provides the compiler, CMake, Ninja and Make. Its release notes state that it bundles GCC 16.2.0, MinGW-w64 14.0.0 (UCRT, POSIX threads), GNU Make 4.4.1, CMake 4.4.4 and Ninja 1.13.2.

```powershell
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --location D:\Tools\winlibs `
  --accept-package-agreements --accept-source-agreements
```

Then confirm, in this order:

1. Find the real `g++.exe`. Expect `D:\Tools\winlibs\mingw64\bin\g++.exe`. If `--location` was not honoured it will be under `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*`, which is on C:. If C: would be left with under 4 GB free, stop and ask the owner.
2. `g++ --version` reports 16.2.0, and a hello-world file compiles with `g++ -std=c++17 -static hello.cpp -o hello.exe` and runs. Compile a real file: a `g++` that prints its version but cannot find `cc1plus` is a broken link, not a working compiler.
3. `cmake --version` and `ninja --version` work. If either is missing from the package, install it: `winget install -e --id Kitware.CMake` or `winget install -e --id Ninja-build.Ninja` (both ids were confirmed to exist).

**PATH trap.** A shell that was already running when winget finished does not see the new PATH, and that includes every command you run from this session. `build.ps1` (section 4.4) rebuilds PATH from the registry on every call. Use it for every build instead of calling `cmake` directly.

### 3.3 Pinned dependencies

All are fetched by CMake. Pin by commit so a moved tag cannot change the build.

| Library | Version | Fetch URL | Used for | Licence |
|---|---|---|---|---|
| Dear ImGui | v1.93.0 (2026-10-14) | `https://github.com/ocornut/imgui/archive/c1aaac65e896a45f81f47a4d68f9959e0f86c859.tar.gz` | GUI widgets and drawing | MIT |
| ImPlot | v1.1 (2026-10-25) | `https://github.com/epezent/implot/archive/819f200639455a58b6b3d4156b70e4271aeb2d00.tar.gz` | Charts | MIT |
| GLFW | 3.5.1 (2026-07-31) | `https://github.com/glfw/glfw/archive/d9d6f0f1f967807ffade6598ea9a631ebaf37a56.tar.gz` | Window and OpenGL context | zlib |
| nlohmann/json | v3.12.0 | `https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz` | Scenario and result files | MIT |
| stb_image_write.h | v1.16 | `https://raw.githubusercontent.com/nothings/stb/a570af1f275acc0efac14ead09bcd6fbc961c98c/stb_image_write.h` | Saving PNG screenshots | Public domain / MIT |

Notes:

- ImPlot 1.1's own release notes say its example builds against ImGui v1.93.0, and its CI includes a MinGW build. That pairing is the reason for these two versions.
- GLFW 3.5.1 declares `cmake_minimum_required(VERSION 3.16...3.28)` and nlohmann/json 3.12.0 declares `3.5...4.0`; CMake 4 accepts both. ImGui's GLFW backend supports any GLFW from 3.0 and recommends 3.4 or later.
- `stb_image_write.h` is pinned to a 2022 commit on purpose. The file has had a burst of changes in the last two weeks, one of them described upstream as untested.
- Fallback GLFW pin if 3.5.1 will not build: 3.4, commit `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`.

---

## 4. Architecture

### 4.1 Targets

| Target | Kind | Contents | Depends on |
|---|---|---|---|
| `farm_core` | static library | Everything algorithmic, data model, JSON, generator, benchmark suites | nlohmann/json only |
| `farm_cli` | console executable | Argument parsing and text output | `farm_core` |
| `farm_tests` | console executable | Unit, golden and property tests | `farm_core` |
| `farm_app` | GUI executable | Dear ImGui user interface, screenshot mode | `farm_core`, ImGui, ImPlot, GLFW, OpenGL, stb |

`farm_core` must compile without any GUI header. A CMake option `FARM_BUILD_APP` (default ON) lets the core, CLI and tests build on a machine with no OpenGL.

### 4.2 Directory layout

```
CMakeLists.txt
build.ps1                     one-command build and test (Windows)
README.md
.gitignore
data/
  default_scenario.json       generated once, committed
  flat_scenario.json          generated once, committed; input of the golden test
src/
  core/
    model.h                   structs and enums (section 5)
    rng.h                     SplitMix64 (section 6.5)
    json_io.h/.cpp            load, save, validate
    generator.h/.cpp          sample-data generator
    constraints.h/.cpp        section 7
    forecast.h/.cpp           section 8
    knapsack.h/.cpp           section 9
    packing.h/.cpp            section 10
    scheduling.h/.cpp         section 11
    pipeline.h/.cpp           section 12
    bench.h/.cpp              section 15
  cli/main.cpp                section 13
  app/
    main.cpp                  window, main loop, screenshot mode
    ui.h, ui.cpp              application state, sidebar, tab bar
    ui_tabs.cpp               one function per tab (split further if it passes about 600 lines)
tests/
  check.h, test_main.cpp
  test_constraints.cpp, test_forecast.cpp, test_knapsack.cpp,
  test_packing.cpp, test_scheduling.cpp, test_pipeline.cpp, test_io.cpp
bench/results/                CSV files written by `farm_cli bench`, committed
docs/
  complexity-analysis.md
  screenshots/                PNG files written by `farm_app --screenshot`
  report/, slides/, tools/    section 18
plan/                         this plan; see section 20 before committing it
```

### 4.3 The planning model in one page

Understand this before reading the component sections.

- The farm has **shelves** (one rack level each). A shelf is a rectangle of `width x depth` centimetres with a vertical clearance, a light level and a humidity.
- Each **crop** grows in **trays** of a fixed footprint. A tray stays in one place for the whole plan and always grows the same crop. A placed tray is called a **slot**.
- **Time** is whole weeks. `t = 0` is now. The plan covers plantings at times `0 .. T-1`, where `T` is `horizonWeeks`. A batch planted at time `s` in a slot of a crop with cycle `p` occupies the slot during `[s, s+p)` and is harvested at time `s+p`; the slot can be replanted at that same time.
- The pipeline answers three questions in order:
  1. **What to plant:** how many trays of each crop. A multi-dimensional knapsack over weekly labour, water, nutrient and energy budgets plus total shelf area. Demand comes from the forecast, so growing more than can be sold earns little.
  2. **Where:** place those trays on shelves. 2D packing in which a tray may only go on a shelf it is compatible with, and crops that conflict may not share a shelf.
  3. **When:** for each crop, schedule successive batches in its slots so that contract deliveries are met and slots do not sit idle.
- The knapsack only knows the total shelf area. Geometry and conflicts are enforced by the packer. Trays that do not fit are dropped, and the freed budget is re-spent greedily on trays that verifiably fit (the **refill** step, section 12).

```
 order history ──> FORECAST ──> demand per crop per week ─────────────┐
                                                                       │
 crops, shelves ─> CONSTRAINTS ─> compat matrix, conflict graph ──┐    │
                                                                  │    v
 contracts ──────> LOTS (deliveries as jobs) ─> contract slots ──>│  KNAPSACK ──> trays per crop
                        │                                         │    │            (relaxed optimum)
                        │                                         v    v
                        │                                       PACKING ──> placements, unplaced
                        │                                         │
                        │                                         v
                        │                                       REFILL ──> final trays per crop = slots
                        │                                         │
                        └────────────────────────────────────────>v
                                                               SCHEDULING ──> batches, deliveries met or missed
```

### 4.4 Build files

`CMakeLists.txt` requirements:

- `cmake_minimum_required(VERSION 3.24)`, languages `C CXX`, `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`. Default `CMAKE_BUILD_TYPE` to `Release` when unset.
- `set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/.deps CACHE PATH "")` so downloads survive deleting `build/`.
- `FetchContent_Declare` each dependency with the `URL` from section 3.3. For the stb header add `DOWNLOAD_NO_EXTRACT TRUE`. Before `FetchContent_MakeAvailable(glfw)` force `GLFW_BUILD_DOCS`, `GLFW_BUILD_TESTS`, `GLFW_BUILD_EXAMPLES` and `GLFW_INSTALL` to `OFF`.
- ImGui and ImPlot ship no CMake file. Build one static library `imgui_all` from `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_demo.cpp`, `backends/imgui_impl_glfw.cpp`, `backends/imgui_impl_opengl3.cpp`, `implot.cpp`, `implot_items.cpp`, `implot_demo.cpp`. Public include directories: the ImGui root, its `backends/`, the ImPlot root. Link `glfw` and `OpenGL::GL` (`find_package(OpenGL REQUIRED)`); on Windows also `imm32`. Add `target_compile_definitions(imgui_all PUBLIC "ImDrawIdx=unsigned int")` so large drawings cannot overflow 16-bit indices.
- Warnings on our own targets only: `-Wall -Wextra -Wpedantic` for GCC and Clang, `/W4` for MSVC. Not `-Werror`. Aim for zero warnings in `src/` and `tests/`.
- With MinGW, link every executable with `-static` so the `.exe` runs on a machine that has no MinGW. Give `farm_app` the GUI subsystem (`-mwindows`) so no console window opens beside it.
- `enable_testing()` and three tests: `farm_tests`; `farm_cli run --scenario data/flat_scenario.json`; `farm_cli bench --quick --out build/bench_quick`. Set `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` on each.

`build.ps1`:

```powershell
param([switch]$Test, [switch]$Clean, [string]$Config = 'Release', [string]$BuildDir = 'build')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
# Tools installed by winget are invisible to shells that were already open: rebuild PATH from the registry.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $probe = @('D:\Tools\winlibs\mingw64\bin') + @(Get-Item "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*\mingw64\bin" -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $bin = $probe | Where-Object { Test-Path (Join-Path $_ 'g++.exe') } | Select-Object -First 1
    if (-not $bin) { throw 'g++ not found. Install it: winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --location D:\Tools\winlibs' }
    $env:Path = "$bin;$env:Path"
}
if ($Clean) { Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue }
cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $BuildDir
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Test) { ctest --test-dir $BuildDir --output-on-failure; exit $LASTEXITCODE }
```

Because `farm_app.exe` uses the GUI subsystem, PowerShell does not wait for it. Run it as `Start-Process -Wait -PassThru build\farm_app.exe -ArgumentList ...` and read `.ExitCode`.

---

## 5. Data model

### 5.1 Units and conventions

| Quantity | Unit | C++ type |
|---|---|---|
| Lengths | centimetres | `int` |
| Area | cm² | `long long` |
| Time | weeks | `int` |
| Mass | kilograms | `double` |
| Money | PKR | `double` |
| Labour, water, nutrient solution, energy | hours, litres, litres, kWh, each per week | `double` |

- **Resource order is fixed everywhere:** 0 labour, 1 water, 2 nutrient, 3 energy, 4 area. Arrays of five use this order.
- **One tolerance:** `EPS = 1e-9`. Every capacity check is `need <= have + EPS`. Every "is this better" check on doubles is `a > b + EPS`. Integer division of a capacity by a weight is `floor((have + EPS) / weight)`. This is not optional: the optimal default plan uses exactly 100% of the labour budget, and a strict comparison silently returns a worse answer (see section 9.6).
- Naming: types `PascalCase`, functions and variables `camelCase`, files `snake_case`.

### 5.2 Structs (`src/core/model.h`)

```cpp
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
    int id = 0; std::string name;    // e.g. "R2-L3"
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
```

### 5.3 JSON (`json_io`)

One scenario is one JSON file whose keys are the member names above. `Light` is stored as 0, 1 or 2. Use `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` so missing keys take the defaults. Write with two-space indentation; write `orderHistory` values rounded to two decimals.

`std::vector<std::string> validateScenario(const Scenario&)` returns an empty list when the scenario is usable. Files are a trust boundary: run it after every load, and refuse to plan when it returns anything. It must reject:

- no crops or no shelves; an `id` that differs from the index; duplicate names;
- any dimension, `cycleWeeks` or `yieldKg` that is not positive; a negative `harvestWindowWeeks`, price, cost, rate, budget or unit cost; `salvageFraction` outside `[0, 1]`; `lightMin > lightMax`; `humidityMin > humidityMax`;
- `horizonWeeks` outside `[1, 52]`; `seasonLength < 2`; `heightRatioLimit <= 1`;
- a contract or conflict that names a missing crop; a conflict of a crop with itself; `startWeek > endWeek`; negative kg or penalty;
- `orderHistory` whose outer size differs from the number of crops, an empty series, or a negative or non-finite value.

`loadScenario` throws `std::runtime_error` with a readable message on unreadable files, malformed JSON and wrong types. The CLI and the app catch it at the top level; the app keeps the scenario it already had.

---

## 6. Sample data and the generator

All values are synthetic. They were tuned with the reference model so that the default farm is interesting: several budgets nearly binding, space tight enough that the packers differ, contracts that plain EDF misses.

### 6.1 Crop catalogue (twelve crops)

Light: L low, M medium, H high. Labour h, water L, nutrient L and energy kWh are per tray per week. "Base demand" is the mean weekly spot demand in kg used by the generator; it is not stored in the scenario.

| # | Name | Category | Tray W x D | Height | Cycle p | Window f | Light | Humidity % | Labour | Water | Nutrient | Energy | Yield kg | Price | Tray cost | Base demand |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Lettuce | leafy | 60 x 40 | 25 | 5 | 0 | M-H | 60-80 | 0.15 | 8 | 0.08 | 3.5 | 1.8 | 1100 | 80 | 9.0 |
| 1 | Baby Spinach | leafy | 60 x 30 | 20 | 4 | 0 | M-H | 60-80 | 0.14 | 6 | 0.06 | 2.6 | 1.2 | 1300 | 50 | 6.0 |
| 2 | Kale | leafy | 60 x 45 | 40 | 6 | 1 | M-H | 60-80 | 0.16 | 9 | 0.09 | 4.0 | 2.6 | 1200 | 90 | 4.8 |
| 3 | Pak Choi | leafy | 50 x 40 | 25 | 4 | 0 | M-H | 60-80 | 0.14 | 7 | 0.07 | 3.0 | 1.7 | 900 | 60 | 4.0 |
| 4 | Arugula | leafy | 45 x 30 | 20 | 3 | 0 | M-H | 60-80 | 0.16 | 5 | 0.05 | 2.2 | 0.9 | 1800 | 45 | 2.4 |
| 5 | Basil | herb | 50 x 30 | 35 | 4 | 0 | M-H | 50-70 | 0.20 | 7 | 0.07 | 4.0 | 0.9 | 2600 | 90 | 2.7 |
| 6 | Mint | herb | 40 x 30 | 30 | 4 | 1 | M-H | 55-75 | 0.13 | 6 | 0.06 | 2.4 | 0.8 | 1500 | 50 | 2.0 |
| 7 | Coriander | herb | 40 x 25 | 25 | 4 | 0 | M-H | 50-70 | 0.15 | 5 | 0.05 | 2.2 | 0.6 | 1900 | 40 | 2.4 |
| 8 | Microgreens | microgreen | 50 x 25 | 10 | 2 | 0 | L-M | 50-70 | 0.30 | 4 | 0.02 | 1.5 | 0.3 | 5000 | 250 | 1.5 |
| 9 | Pea Shoots | microgreen | 55 x 28 | 15 | 2 | 0 | L-M | 50-70 | 0.30 | 5 | 0.02 | 1.6 | 0.45 | 3200 | 220 | 1.35 |
| 10 | Strawberry | fruiting | 100 x 30 | 30 | 10 | 1 | H-H | 55-70 | 0.30 | 12 | 0.15 | 6.0 | 3.5 | 2400 | 600 | 7.0 |
| 11 | Cherry Tomato | fruiting | 60 x 60 | 70 | 12 | 1 | H-H | 55-70 | 0.40 | 20 | 0.25 | 9.0 | 7.0 | 1700 | 400 | 3.5 |

Every crop has `canRotate = true` and `salvageFraction = 0.2`.

### 6.2 Shelves (four rack templates, four levels each)

| Rack | Width x Depth | Humidity % | Level 1 (bottom) | Level 2 | Level 3 | Level 4 (top) |
|---|---|---|---|---|---|---|
| R1 | 240 x 60 | 72 | 55 cm, M | 45 cm, M | 45 cm, M | 40 cm, H |
| R2 | 240 x 60 | 68 | 55 cm, M | 45 cm, M | 45 cm, H | 40 cm, H |
| R3 | 200 x 80 | 62 | 95 cm, H | 60 cm, H | 45 cm, H | 45 cm, H |
| R4 | 180 x 50 | 58 | 35 cm, L | 35 cm, L | 35 cm, M | 35 cm, M |

Each cell is clearance and light. Shelf ids run rack by rack, level 1 first: R1-L1 is 0, R1-L4 is 3, R2-L1 is 4, and R4-L4 is 15. Total area is 215 200 cm².

### 6.3 Budgets, costs, contracts, conflicts

- Weekly budget: labour 21 h, water 800 L, nutrient 8.5 L, energy 400 kWh.
- Unit costs: labour 300 PKR/h, water 0.5 PKR/L, nutrient 400 PKR/L, energy 50 PKR/kWh.
- `horizonWeeks = 12`, `seasonLength = 52`, `lightGapCm = 10`, `heightRatioLimit = 2.0`.
- Contracts, all with `endWeek = 9999` and `startWeek` equal to the crop's cycle (the first week a harvest is possible):

| Customer (fictional) | Crop | kg per week | Trays per week | Penalty PKR/kg |
|---|---|---|---|---|
| Restaurant group (sample) | Lettuce | 3.6 | 2 | 600 |
| Hotel kitchen (sample) | Basil | 0.9 | 1 | 1500 |
| Salad subscription (sample) | Microgreens | 0.6 | 2 | 3000 |
| Dessert cafe (sample) | Strawberry | 3.5 | 1 | 1500 |
| Supermarket (sample) | Baby Spinach | 1.2 | 1 | 700 |

- Explicit conflicts: every herb with every fruiting crop (six pairs, reason "herb next to fruiting crop"), plus Mint with Basil and Kale with Strawberry (reason "agronomic rule (sample data)"). Eight pairs in total.

### 6.4 Generator (`generator.h`)

```cpp
struct GeneratorParams {
    unsigned long long seed = 42;
    int crops = 12;          // more than 12 creates jittered variants of the catalogue
    int racks = 4;           // rack r uses template r % 4
    bool flat = false;       // constant order history
    double jitter = 0.0;     // relative jitter on numeric crop fields
    int historyWeeks = 156;
    int horizonWeeks = 12;
};
Scenario generateScenario(const GeneratorParams&);
```

Rules, in this order so the random stream is reproducible:

1. Create one `Rng` from the seed.
2. Crops `i = 0 .. crops-1`: copy catalogue entry `i % 12`. When `i >= 12`, append a space and the letter `'A' + i / 12` to the name ("Lettuce B") and use an effective jitter of `max(jitter, 0.15)`; otherwise use `jitter`. When the effective jitter `j` is positive, multiply these seven fields, in this order, each by `1 + j * (2 * rng.unit() - 1)`: price, yield, base demand, labour, water, nutrient, energy. Draw nothing when `j` is zero.
3. Shelves: rack `r` copies template `r % 4`; names `R{r+1}-L{level+1}`.
4. Budget is the default budget times `racks / 4.0`. Unit costs and the other scalars are the defaults.
5. Contracts: the five in section 6.3 whose crop is among the first twelve crops generated, with `kgPerWeek` equal to the table's trays per week times the generated crop's yield. Conflicts: the category rule over all generated crops, plus the two named pairs when both crops exist.
6. Order history for each crop in index order, with base demand `b` and `n = historyWeeks`:
   - `flat`: every value is `b`. Draw nothing.
   - otherwise draw `g = -0.05 + 0.20 * u`, `a = 0.05 + 0.30 * u`, `phi = 52 * u`, `sigma = 0.05 + 0.10 * u` (four draws, in that order), then for `i = 0 .. n-1` draw twelve units, let `z` be their sum minus 6, and set
     `y_i = max(0, b * (1 + g * (i - (n-1)) / 52) * (1 + a * sin(2 * pi * (i + phi) / 52)) + sigma * b * z)`, rounded to two decimals. Compute this in `double` throughout; `(i - (n-1)) / 52` must not be an integer division.

Commit two generated files and never regenerate them casually, because the report's numbers depend on them:

- `data/default_scenario.json`: `seed 42, crops 12, racks 4, flat false, jitter 0`.
- `data/flat_scenario.json`: the same with `flat true`. With a constant history every forecasting method returns the base demand, so this scenario's results can be checked against Appendix A.

### 6.5 Random numbers (`rng.h`)

```cpp
struct Rng {                                   // SplitMix64: identical output on every compiler
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed) {}
    unsigned long long next() {
        s += 0x9E3779B97F4A7C15ull;
        unsigned long long z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    int below(int n) { return int(next() % (unsigned long long)n); }              // 0 .. n-1
};
```

Self-check for the tests: with seed 42 the first three `next()` values are `13679457532755275413`, `2949826092126892291`, `5139283748462763858`.

---

## 7. Component 1: constraint layer

*Proposal: "Encode incompatibility constraints as a graph, and prune invalid packing configurations before optimization runs."*

### 7.1 What it computes

```cpp
enum class ConflictReason { None, Explicit, Height, NoCommonShelf };
enum WhyNot : unsigned char { FitsOk = 0, Clearance = 1, LightMismatch = 2, HumidityMismatch = 4, Footprint = 8 };

struct ConstraintModel {
    std::vector<std::vector<char>> compat;              // [crop][shelf]: 1 when the crop may stand on the shelf
    std::vector<std::vector<unsigned char>> whyNot;     // [crop][shelf]: bitmask of failed checks, for tooltips
    std::vector<std::vector<ConflictReason>> conflict;  // [crop][crop], symmetric
    std::vector<std::vector<int>> adjacency;            // conflict graph, neighbours ascending
    std::vector<int> maxTraysByArea;                    // [crop]
    long long pairsTotal = 0, pairsCompatible = 0;      // crop-shelf pairs, for the "pruned x%" readout
};
ConstraintModel buildConstraints(const Scenario&);
```

**Orientations.** A tray has orientation `(trayW, trayD)` and, when `canRotate` and the sides differ, `(trayD, trayW)`. In a pair `(w', d')`, `w'` lies along the shelf width and `d'` along its depth.

**Compatibility.** `compat[c][s]` is true when all four hold; each failure sets its bit in `whyNot`:

1. `crop.height + lightGapCm <= shelf.clearance`
2. `crop.lightMin <= shelf.light <= crop.lightMax`
3. `crop.humidityMin <= shelf.humidity <= crop.humidityMax`
4. some orientation has `w' <= shelf.width` and `d' <= shelf.depth`

**Conflicts.** For each pair `a < b`, the first rule that applies gives the reason:

1. `Explicit`: the pair is listed in `Scenario::conflicts`.
2. `Height`: `max(height) >= heightRatioLimit * min(height)`. The taller crop shades the shorter one.
3. `NoCommonShelf`: no shelf is compatible with both. This adds no restriction; it records in the graph a separation that the compatibility matrix already forces.

**Capacity bound.** `maxTraysByArea[c]` is the sum over compatible shelves of `floor(shelfArea / trayArea)`.

### 7.2 How it prunes

Report these in the app and in the report: they are the "pruning before optimisation".

- A crop with no compatible shelf is removed from selection, with a warning naming the checks that fail.
- The knapsack may not choose more than `maxTraysByArea[c]` trays of crop `c`.
- The packers only ever try compatible, conflict-free shelves, so no invalid configuration is generated and none has to be penalised or repaired. This holds for the genetic algorithm too.
- Show `pairsCompatible` of `pairsTotal`. On the default farm it is 100 of 192, so 48% of tray-to-shelf assignments are eliminated before any optimiser runs.
- Colouring gives a necessary condition. Crops on one shelf must be pairwise non-conflicting, so they form an independent set of the conflict graph. If the selected crops need more colours than there are shelves, no packing exists; warn when the exact chromatic number of the selected crops' subgraph exceeds the number of shelves.

### 7.3 Graph colouring: three variants

Each takes adjacency lists and returns one colour per vertex, colours numbered from 0.

- **Welsh-Powell.** Order vertices by degree descending, ties by index ascending. Give each, in that order, the smallest colour no coloured neighbour uses.
- **DSATUR.** Repeat until all are coloured: among uncoloured vertices pick the one with the most distinct neighbour colours, ties by higher degree, then by lower index. Give it the smallest free colour.
- **Exact.** Backtracking over vertices in Welsh-Powell order. Vertex `i` may take any colour already in use or one new colour, which removes colour-permutation symmetry. Start with DSATUR's count as the incumbent and prune a branch as soon as its colour count reaches the incumbent. Take a node limit (default 2 000 000); return the count and a `proven` flag.

```cpp
std::vector<int> colourWelshPowell(const std::vector<std::vector<int>>& adj);
std::vector<int> colourDsatur(const std::vector<std::vector<int>>& adj);
struct ExactColouring { int colours = 0; bool proven = true; long long nodes = 0; };
ExactColouring colourExact(const std::vector<std::vector<int>>& adj, long long nodeLimit = 2000000);
```

### 7.4 Complexity

With `C` crops, `S` shelves, `E` conflict edges: compatibility `O(C*S)`; conflict graph `O(C^2 * S)`; Welsh-Powell `O(C log C + C + E)`; DSATUR as specified `O(C * (C + E))`; exact colouring exponential in `C` in the worst case.

### 7.5 Tests

- Appendix A, G1: the crown graph gives 3 colours with Welsh-Powell and 2 with DSATUR and exact; C5 needs 3, K4 needs 4, the Petersen graph needs 3.
- Appendix A, G6: on the default farm, compatible-shelf counts per crop, 30 conflict edges (22 height, 8 explicit), 5 colours by all three algorithms.
- One case built to produce `NoCommonShelf`: two crops with disjoint humidity ranges and similar heights. The default data never produces this reason, so without this case the branch is untested.
- Property, on 200 seeded random graphs of 6 to 14 vertices: every colouring is proper, and the exact count is at most both heuristic counts. Do not assert that DSATUR never uses more colours than Welsh-Powell; that is not a theorem.

---

## 8. Component 2: demand forecasting

*Proposal: "Predict near-term demand per crop from past orders/contracts and seasonality, to bias the knapsack toward what will actually sell."*

### 8.1 Contract

Input: one crop's weekly spot-order history `y[0..n-1]` in kg, a season length `L`, a horizon `H`. Output: `H` forecasts for the weeks after the history ends. Element 0 is delivery week 1.

The pipeline needs `H = T - 1 + Pmax`, where `Pmax` is the longest crop cycle. Contracts are known commitments and are added later; the forecast covers spot demand only.

### 8.2 Four methods

Clip every forecast at zero.

1. **Seasonal naive.** If `n >= L`: forecast `h` (from 0) is `y[n - L + (h mod L)]`. Otherwise every forecast is `y[n-1]`.
2. **Moving average.** For each `k` in `{4, 8, 12}` with `k < n`, compute the one-step in-sample SSE: the sum over `t = k .. n-1` of `(y[t] - mean(y[t-k .. t-1]))^2`. Keep the `k` with the lowest SSE; a later candidate replaces the current best only if it is lower by more than `EPS`. Every forecast is the mean of the last `k` values. If no candidate qualifies, use `k = n`.
3. **Simple exponential smoothing.** For `alpha` in `0.1, 0.2, .., 0.9`: `level = y[0]`; for `t = 1 .. n-1` add `(y[t] - level)^2` to the SSE, then `level = alpha * y[t] + (1 - alpha) * level`. Keep the alpha with the lowest SSE, same replacement rule. Every forecast is the final level.
4. **Additive Holt-Winters.** Available only when `n >= 2L`. For one parameter triple:

   ```
   m1 = mean(y[0 .. L-1]);  m2 = mean(y[L .. 2L-1])
   trend  = (m2 - m1) / L
   level  = m1 + trend * (L - 1) / 2                         // level at index L-1
   season[i] = y[i] - (m1 + trend * (i - (L - 1) / 2))       // i = 0 .. L-1
   for t = L .. n-1:
       sOld     = season[t mod L]
       SSE     += (y[t] - (level + trend + sOld))^2
       newLevel = alpha * (y[t] - sOld) + (1 - alpha) * (level + trend)
       trend    = beta * (newLevel - level) + (1 - beta) * trend
       season[t mod L] = gamma * (y[t] - newLevel) + (1 - gamma) * sOld
       level    = newLevel
   forecast[h] = level + (h + 1) * trend + season[(n + h) mod L]          // h = 0 .. H-1
   ```

   Search `alpha` in `{0.1, 0.3, 0.5, 0.7, 0.9}`, `beta` in `{0.01, 0.05, 0.1, 0.3}`, `gamma` in `{0.05, 0.1, 0.3, 0.5}`, alpha outermost and gamma innermost; keep the lowest SSE with the same replacement rule.

   This initialisation is deliberate. It reproduces a noiseless "linear trend plus periodic pattern" series exactly. The common shortcut `level = m1` does not, and leaves a visible bias.

### 8.3 Automatic selection

1. `hTest = min(12, n / 4)` with integer division. If `hTest` is 0 (fewer than four observations), skip the hold-out, use seasonal naive, and leave the error table marked unavailable. Otherwise train on `y[0 .. n-hTest-1]` and test on the rest.
2. Run every available method on the training part with horizon `hTest`. Compute `MAE`, `RMSE` and `sMAPE = (100 / hTest) * sum(2 * |a - p| / (|a| + |p|))`, where a term whose denominator is below `EPS` counts as 0.
3. Choose the lowest MAE. Methods are considered in the order seasonal naive, moving average, SES, Holt-Winters; a later one replaces the current choice only if its MAE is lower by more than `EPS`.
4. Refit the chosen method on the whole history and forecast `H` weeks.

When the user forces a method, skip the choice but still fill the error table. If the forced method is unavailable, fall back to automatic and add a warning.

```cpp
enum class ForecastMethod { Auto, SeasonalNaive, MovingAverage, Ses, HoltWinters };
struct ForecastErrors { bool available = false; double mae = 0, rmse = 0, smape = 0; };
struct ForecastResult {
    ForecastMethod chosen = ForecastMethod::SeasonalNaive;
    std::vector<double> forecast;                         // H values
    std::array<ForecastErrors, 4> holdout;                // indexed by method - 1
    std::array<std::vector<double>, 4> holdoutPrediction; // for the chart
    int holdoutLength = 0;
    std::string parameters;                               // e.g. "alpha=0.5 beta=0.1 gamma=0.3"
};
ForecastResult forecastSeries(const std::vector<double>& history, int seasonLength, int horizon,
                              ForecastMethod method = ForecastMethod::Auto);
```

### 8.4 Complexity

Per series of length `n`: seasonal naive `O(H)`; moving average `O(n)` per candidate with a running sum; SES `O(9n)`; Holt-Winters `O(80n)` for the 80 grid points; selection runs each method twice. Space `O(n + L)`.

### 8.5 Tests

- Appendix A, G2: every value listed there.
- A noiseless series `100 + 0.5t + pattern[t mod 4]` with `pattern = {5, -3, -4, 2}`, 24 points, `L = 4`: the Holt-Winters fit with `(0.5, 0.3, 0.2)` forecasts the next four values within `1e-9`.
- A constant series: every method forecasts the constant, and seasonal naive is chosen.
- Fallbacks: a series shorter than `2L` makes Holt-Winters unavailable; a series shorter than `L` makes seasonal naive repeat the last value.

---

## 9. Component 3: crop selection as a knapsack

*Proposal: "select the combination of crops/batches to plant that maximizes expected profit — a multi-dimensional knapsack problem."*

### 9.1 The model

Decide `x_c`, the number of trays of crop `c`. Maximise the summed value subject to five constraints: weekly labour, water, nutrient and energy within budget, and total tray area within total shelf area. A tray of crop `c` weighs `(laborPerWeek, waterPerWeek, nutrientPerWeek, energyPerWeek, trayW * trayD)`.

The value of the k-th tray of a crop falls as k grows, which is how the demand forecast "biases the knapsack": trays beyond what the market absorbs earn only the salvage price. Each crop therefore becomes a group of **copies** with equal weights and non-increasing values, and a solution always takes a prefix of each group.

### 9.2 Inputs from the other components

For crop `c` with cycle `p`, yield `y`, and delivery window `W = {p, .., T-1+p}`:

- `spot[w]`: forecast spot demand in week `w` (section 8).
- `hardKg[w]`: the sum of `kgPerWeek` over contracts for `c` active in week `w` (`startWeek <= w <= endWeek`), for `w` in `W`.
- `demand_c = (sum over W of hardKg[w] + spot[w]) / T`: mean weekly sellable kg.
- `contractSlots_c`: the fewest slots that let every contract delivery be met (section 11.3).
- `penaltyPerWeek_c`: the summed penalty of all the crop's contract lots (section 11.2) divided by `T`.
- `cap_c = min(400, maxTraysByArea[c])`.

### 9.3 Value of the k-th tray (PKR per week)

```
out      = y / p                                               // kg per week from one slot
opCost   = trayCost / p + labor * cLabor + water * cWater + nutrient * cNutrient + energy * cEnergy
sold     = clamp(demand_c - (k - 1) * out, 0, out)
revenue  = price * (sold + salvageFraction * (out - sold))
bonus    = penaltyPerWeek_c / contractSlots_c   if contractSlots_c > 0 and k <= contractSlots_c, else 0
v[c][k]  = revenue - opCost + bonus
```

Build `v[c][1], v[c][2], ..` up to `cap_c` and stop at the first value that is not above `EPS`. The group's size `u_c` is the number of values kept.

The bonus is how contracts enter the optimisation. Instead of forcing contract trays in, which would need special handling when budgets cannot cover them, the trays a contract needs carry the penalty they avoid. If a contract is not worth honouring under the budgets, the optimiser drops it and the app reports it.

Worked example, Lettuce in the flat scenario: `out = 0.36`, `opCost = 16 + 45 + 4 + 32 + 175 = 272`, revenue at full price 396, so the plain profit is 124. Its contract produces 24 lots with a penalty of 1080 each, so `penaltyPerWeek = 25 920 / 12 = 2160`; `contractSlots = 10`; the bonus is 216. Hence `v = 340` for `k = 1..10` and `124` for `k = 11..35`. `demand = 12.6` and `12.6 / 0.36 = 35`, so the 36th tray would be pure surplus, is worth less than zero, and the group ends at 35.

### 9.4 Interface

```cpp
struct KnapsackGroup { int cropId = 0; std::array<double, 5> weight{}; std::vector<double> values; };
struct KnapsackProblem {
    std::vector<KnapsackGroup> groups;        // crops with at least one positive value, in crop-id order
    std::array<double, 5> capacity{};
    std::array<bool, 5> active{{true, true, true, true, true}};   // an inactive resource is ignored entirely
};
struct KnapsackResult {
    std::vector<int> count;                   // per group
    double value = 0;                         // sum of the chosen prefixes
    double upperBound = 0;                    // branch and bound: the root bound
    long long nodes = 0;                      // branch and bound
    std::size_t states = 0;                   // dynamic programming
    bool provenOptimal = false;               // exact for the problem as given
    std::string note;                         // e.g. "grid coarsened to 17 steps per resource"
    double seconds = 0;
};
KnapsackProblem buildKnapsackProblem(/* scenario, forecasts, constraint model, contract data, options */);
KnapsackResult solveGreedy(const KnapsackProblem&);
KnapsackResult solveBranchAndBound(const KnapsackProblem&, long long nodeLimit, double timeLimitSeconds);
KnapsackResult solveDp(const KnapsackProblem&, const std::array<double, 5>& unit, std::size_t maxStates);
```

Below, "resources" means the active ones. `density(v, w) = v / sum over resources of (w_r / B_r)`, taken as plus infinity when the sum is zero. A budget of zero cannot be divided by. A group that needs any of a zero-budget resource can never be chosen, so drop such groups while building the problem and then treat that resource as inactive: the groups that remain do not use it.

### 9.5 Variant 1: greedy

1. List every copy `(crop, k)` with its density.
2. Sort by density descending, then value descending, then crop id ascending, then `k` ascending.
3. Scan once. Take a copy when it is its crop's next one (`k = count + 1`) and its weights fit the remaining capacity.

`O(N log N + N*m)` for `N` copies and `m` resources. No optimality guarantee.

### 9.6 Variant 2: branch and bound (the default)

Exact. Branch on one crop's tray count at a time.

1. **Order** the groups by the density of their first copy descending, then crop id ascending. "Position" below means the index in this order.
2. **Precompute** `m + 1` sorted lists of all copies: one per resource `r`, sorted by `v / w_r` descending (copies with `w_r = 0` first), and one for the surrogate weight `ws = sum over resources of (w_r / B_r)`, sorted by `v / ws` descending. Break ties by position, then `k`.
3. **Bound.** `fractional(list, depth, capacity)` walks a list, skips copies whose position is below `depth`, takes whole copies while they fit, takes the fitting fraction of the first one that does not, and stops. The bound at a node is the minimum of `fractional(list_r, depth, remaining_r)` over the resources and `fractional(surrogateList, depth, sum of remaining_r / B_r)`. Each term relaxes the problem to a single constraint and is therefore a valid upper bound; the surrogate term is the one that makes the search fast when several budgets bind together.
4. **Incumbent.** Start from the greedy solution, so the result is never worse than greedy.
5. **Search.** Depth-first. At a node of depth `d`: if `d` equals the number of groups, update the incumbent when `current > best + EPS`. Otherwise prune when `current + bound <= best + EPS`. Otherwise let `kmax` be the smaller of the group size and `floor((remaining_r + EPS) / w_r)` over resources with positive weight, and recurse for `k = kmax` down to `0` with the group's prefix value added.
6. **Limits.** Stop at `nodeLimit` (default 5 000 000) or `timeLimitSeconds` (default 5), check the clock every 4096 nodes, return the best found with `provenOptimal = false`. Always return `upperBound`, the bound at the root.

Worst case exponential in the number of crops, `O(m*N)` per node. On the flat default scenario it proves optimality in 2269 nodes.

**Tolerance signature.** On the flat scenario the optimum is 39 486.5 and uses exactly 21.0 labour hours. If you get **39 450.5**, with Pak Choi 2 and Strawberry 12 in the selection, a capacity comparison somewhere is missing `EPS`.

### 9.7 Variant 3: dynamic programming

The classic table DP extended to `m` resources. It exists to be exact on small integer problems, where it cross-checks branch and bound, and to demonstrate why it cannot be the main solver.

1. **Grid.** For each resource, `steps_r = floor(capacity_r / unit_r + EPS)`. If the product of `(steps_r + 1)` exceeds `maxStates` (default 2 000 000), find the largest integer `G >= 1` for which the product of `(min(steps_r, G) + 1)` is at most `maxStates`; for each resource with more than `G` steps use `unit_r = capacity_r / G` and `steps_r = G`, and record the coarsening in `note`.
2. **Integer weights.** `wInt_r = ceil(w_r / unit_r - EPS)`. Rounding weights up and capacities down keeps every DP solution feasible for the real problem.
3. **Table.** A state is a vector of used-capacity allowances `(c_0, .., c_{m-1})` with `0 <= c_r <= steps_r`, flattened in mixed radix: `index = sum of c_r * radix_r`, `radix_0 = 1`, `radix_r = radix_{r-1} * (steps_{r-1} + 1)`. `dp[index]` is the best value within that allowance.
4. **Fill.** For each copy in group order, with `offset = sum of wInt_r * radix_r`: visit the states in **descending** index order; when every coordinate of the state is at least the copy's integer weight and `dp[s - offset] + value > dp[s] + EPS`, set `dp[s]` and set the copy's bit for `s`. Descending order matters: `dp[s - offset]` must still hold the value from before this copy.
5. **Reconstruct.** Start at the last state and walk the copies backwards, subtracting the offset whenever the bit is set. Count the chosen copies per group; `value` is the sum of the prefixes of those counts.
6. `provenOptimal` is true only when the grid was not coarsened and every weight and every capacity is a whole multiple of its unit within `EPS`.

Default units for farm scenarios: labour 0.01 h, water 1 L, nutrient 0.01 L, energy 0.1 kWh, area 10 cm².

Time `O(N * states * m)`, space `O(states)` doubles plus `N * states` bits.

**Expected behaviour. Do not "fix" it.** With all five resources the default farm needs about `2100 * 800 * 850 * 4000 * 21 520` states, so the grid is coarsened to 17 steps per resource; every tray's labour then rounds up to a seventeenth of the budget and the plan holds at most 17 trays. That poor result is the demonstration. The table is pseudo-polynomial in each budget and exponential in the number of budgets. Switch off all but one resource in the app and the DP becomes exact and matches branch and bound. The report should show both.

### 9.8 Tests

- Appendix A, G3: greedy returns 7 where the optimum is 10; the two-resource instance has optimum 19. Assert values, not chosen sets, because that instance has two optimal sets.
- Appendix A, G6: the value table, `N = 181`, greedy 38 941.5, branch and bound 39 486.5 with the listed selection, `provenOptimal`, root bound 39 486.5.
- Property, on 300 seeded random instances with 1 to 3 resources, integer weights 0 to 4, capacities 4 to 9, 3 to 9 single-copy groups: brute force over all subsets equals the DP value and the branch-and-bound value; every result fits the capacities; greedy is at most the optimum.
- Property with grouped copies and non-increasing values: branch and bound equals a brute force over all count vectors.
- A resource switched off is ignored: the result equals that of the same problem with that capacity set enormous.

---

## 10. Component 4: grow-space packing

*Proposal: "Use 2D/3D bin-packing heuristics (First-Fit Decreasing, Best-Fit, or a genetic algorithm) to pack trays of different crops into the available shelving."*

### 10.1 Row-based layout

Each shelf is filled in **rows** that run along its width and stack along its depth. A row has a `y` offset, a depth, and a used width. The first tray of a row fixes the row's depth; later trays in that row are placed left to right and must be no deeper than the row.

```cpp
struct Tray { int id = 0; int cropId = 0; };
struct Placement { int trayId = 0, cropId = 0, shelfId = 0, x = 0, y = 0, w = 0, d = 0; };
enum class FitPolicy { FirstFit, BestFit };

struct Row { int y = 0, depth = 0, usedWidth = 0; };
struct ShelfState { std::vector<Row> rows; int usedDepth = 0; std::vector<char> hasCrop; /* indexed by crop id */ };
struct PackState { std::vector<ShelfState> shelves; std::vector<Placement> placements; };

bool placeOne(PackState&, const Tray&, const Scenario&, const ConstraintModel&, FitPolicy);
```

`placeOne` is the single placement primitive. Every packer and the refill step are built on it, and it never produces an invalid layout.

### 10.2 `placeOne`, exactly

**Candidate shelves:** shelves compatible with the tray's crop on which no crop already present conflicts with it, in ascending shelf id.

**Orientation in an existing row.** An orientation `(w', d')` fits a row when `usedWidth + w' <= shelf.width` and `d' <= row.depth`. Among those that fit, take the one with the smaller `w'`.

**Orientation for a new row.** An orientation fits when `w' <= shelf.width` and `usedDepth + d' <= shelf.depth`. Among those that fit, take the one with the larger *block count* `floor(shelf.width / w') * floor((shelf.depth - usedDepth) / d')`, ties to the larger `w'`. The block count asks which orientation packs more trays of this kind into the space that is left. This rule matters a great deal: a 60 x 40 tray on a 240 x 60 shelf placed long-side-along gives 4 trays and wastes a third of the shelf; turned, it gives 6 and wastes nothing.

**First-Fit.** For each candidate shelf in order: try its rows in order and take the first row with a fitting orientation; if none, try a new row on this shelf; if that fits, take it. Otherwise go to the next shelf. A new row on shelf 1 is tried before an existing row on shelf 2.

**Best-Fit.**

1. Over every row of every candidate shelf that has a fitting orientation, choose the smallest key `(shelf.width - usedWidth - w', row.depth - d', shelfId, rowIndex)`: least leftover width, then best depth match.
2. Only if no existing row fits anywhere: over every candidate shelf where a new row fits, choose the smallest key `(shelf has rows ? 0 : 1, shelf.depth - usedDepth - d', shelfId)`: shelves already in use first, then the tightest remaining depth.

**Commit.** For a new row append `Row{usedDepth, d', 0}` and add `d'` to `usedDepth`. Place the tray at `(x = row.usedWidth, y = row.y)` with size `(w', d')`, add `w'` to the row's used width, mark the crop present. Return false, changing nothing, when no placement exists.

### 10.3 Variants 1 and 2: First-Fit Decreasing and Best-Fit Decreasing

Sort the trays by longer side descending, then shorter side descending, then crop id ascending, then tray id ascending. Call `placeOne` on each in that order with the policy. Trays for which it returns false are the unplaced list.

`O(n log n)` to sort and `O(n * (S + R))` to place `n` trays on `S` shelves with `R` rows in total. Since `R <= n`, that is `O(n^2)` in the worst case.

These are heuristics with no approximation guarantee for this constrained variant. The report may quote the classical ratios for unconstrained one-dimensional bin packing and strip packing as context only (section 18.5).

### 10.4 Variant 3: genetic algorithm

An order-based GA whose decoder is `placeOne`. Individuals are valid packings by construction.

- **Chromosome:** a permutation of the tray indices, plus one policy gene (First-Fit or Best-Fit).
- **Decoding:** start from an empty `PackState` and call `placeOne` on the trays in chromosome order with the chromosome's policy.
- **Fitness**, compared lexicographically and maximised:
  1. `placedValue`: the sum over crops of the prefix value of the number of trays placed (trays of one crop are interchangeable, so the lowest-value copies are the ones lost);
  2. fewer shelves used;
  3. the sum over used shelves of `(placed area / shelf area)^2`, which rewards concentrating trays.

  Compare the doubles with `EPS`.
- **Initial population** of size `P`: (a) decreasing order with First-Fit; (b) decreasing order with Best-Fit; (c) trays ordered by `v[crop][1] / trayArea` descending, then crop id, then tray id, with First-Fit; the remainder are Fisher-Yates shuffles of the decreasing order with a random policy.
- **Each generation:** copy the best two individuals unchanged. Fill the rest with children. To make a child, pick two parents by tournament (three uniform draws with replacement, the fittest wins). With probability `crossoverRate` apply order crossover: draw two cut points `i <= j`, copy `parent1[i..j]` to the same positions, fill the remaining positions left to right with the genes of `parent2` in their order, skipping those already present. Otherwise copy parent 1. The child takes its policy from either parent with equal probability. With probability `mutationRate` swap two random positions. With probability 0.05 flip the policy.
- **Stop** after `generations`, or after `stallGenerations` generations in which the best fitness did not improve.
- **Random numbers:** one `Rng` seeded from the parameters. Draw in a fixed order so runs repeat.

```cpp
struct GaParams { int population = 40, generations = 150, stallGenerations = 40;
                  double crossoverRate = 0.9, mutationRate = 0.3; unsigned long long seed = 42; };
```

Because individuals (a) and (b) are in the first population and the best two always survive, the result is never worse than both FFD and BFD. Assert that in the tests. Record the best `placedValue` of every generation for a convergence chart.

`O(G * P * n * (S + R))` time and `O(P * n)` space for `G` generations.

### 10.5 Result and validation

```cpp
struct PackResult {
    std::string algorithm;
    PackState state;                           // final state, reused by the refill step
    std::vector<Tray> unplaced;
    std::vector<int> placedPerCrop;
    double placedValue = 0;
    int shelvesUsed = 0;
    long long placedArea = 0, usedShelfArea = 0, totalShelfArea = 0;
    int generationsRun = 0; std::vector<double> convergence;   // genetic algorithm only
    double seconds = 0;
};
std::vector<std::string> validatePacking(const PackState&, const Scenario&, const ConstraintModel&);
```

`validatePacking` returns an empty list when all of these hold; otherwise one message per violation:

- every placement lies inside its shelf, and its size is one of its crop's orientations;
- no two placements on a shelf overlap;
- every placement is on a compatible shelf, and no two conflicting crops share a shelf;
- no tray id appears twice.

Call it in the tests after every packer, and once in the pipeline after refill, where a violation becomes a warning marked as a bug.

### 10.6 Tests

- Appendix A, G4: the two-shelf, four-tray instance where First-Fit places 4 and Best-Fit places 3, with the exact coordinates; and the six new-row orientation cases.
- Appendix A, G6: on the given selection FFD places 94 of 102 (Kale 1 and Arugula 7 unplaced, area 185 290) and BFD places 95 of 102 (Kale 1 and Arugula 6 unplaced, area 186 640).
- Property, on 200 seeded random instances (10 tray types, 16 shelves of five different sizes, conflict probability 0.2, compatibility probability 0.8): all three packers return layouts that `validatePacking` accepts; every tray is either placed or unplaced, never both; GA fitness is at least the fitness of FFD and of BFD; the GA returns the same result twice for the same seed.
- Edge cases: no trays; a tray larger than every shelf; a crop compatible with no shelf; two conflicting crops and a single shelf.

---

## 11. Component 5: harvest and replant scheduling

*Proposal: "reharvest and replant slots must be scheduled so beds don't sit idle and deliveries aren't missed, using interval scheduling / earliest-deadline-first with lookahead."*

Scheduling is done **one crop at a time**. The crop's slots are identical machines. Its deliveries are jobs that all have the same length, the crop's cycle `p`.

### 11.1 Lots

A **lot** is one tray's worth of one delivery.

```cpp
struct Lot {
    int id = 0, cropId = 0, dueWeek = 0;
    bool hard = false;               // contract delivery; soft = forecast spot demand
    double kg = 0, value = 0, penalty = 0;
    int release = 0, latestStart = 0;
};
```

- `latestStart = dueWeek - p`: the last planting time that still meets the delivery.
- `release = max(0, latestStart - f)`: the earliest planting time, since a harvest may not precede its delivery by more than `f` weeks.
- A lot may be started at any time `s` with `release <= s <= latestStart`.

### 11.2 Building a crop's lots

For delivery weeks `w = p .. T-1+p`, in order, keeping two running totals `cum` and `cumPrev` that start at 0:

1. `h = hardKg[w]`. `nHard = ceil(h / yield - EPS)` when `h > EPS`, else 0.
2. `spotAdj = max(spot[w] - (nHard * yield - h), 0)`. A contract tray's surplus goes to the spot market.
3. `cum += spotAdj`; `nSoft = floor(cum / yield + 0.5) - floor(cumPrev / yield + 0.5)`; `cumPrev = cum`. Rounding the cumulative total avoids the bias that rounding each week would add.
4. Append `nHard` hard lots: lot `i` (from 0) has `kg = min(yield, h - i * yield)`, `value = kg * price`, `penalty = kg * penaltyRate[w]`, where `penaltyRate[w]` is the kg-weighted mean `penaltyPerKg` of the contracts active in week `w`.
5. Append `nSoft` soft lots with `kg = yield`, `value = yield * price`, `penalty = 0`.

Number the lots in the order created. Only deliveries the plan can influence get lots: nothing is due before week `p`, because an empty farm cannot harvest sooner, and nothing after week `T-1+p`, because that would need a planting outside the plan.

### 11.3 Slot arithmetic

```cpp
int hardMissed(const std::vector<int>& freeAt, const std::vector<Lot>& hardLotsSorted, int p);
int minSlotsForHardLots(const std::vector<Lot>& lots, int p);
int intervalDepth(std::vector<std::pair<int, int>> intervals);
```

- **`hardMissed`** answers: if the slots become free at the times in `freeAt`, how many of these hard lots, sorted by `(latestStart, id)`, miss their deadline when taken in that order and each given to the slot that frees earliest? Keep the free times in a min-heap. For each lot pop the smallest `a`; `start = max(a, release)`; if `start <= latestStart` push `start + p`, otherwise push `a` back and count a miss. `O(H log k)` for `H` lots and `k` slots.
- **`minSlotsForHardLots`**: the smallest `m >= 0` for which `hardMissed` of `m` slots free at time 0 is zero. Try `m = 0, 1, 2, ..`; it never exceeds the number of hard lots. This is interval partitioning generalised to flexible start windows, and its result is the `contractSlots` of section 9.
- **`intervalDepth`**: the classic sweep. Sort the endpoints with ends before starts at equal times, and return the largest number of half-open intervals `[start, end)` open at once. When `f = 0` every lot has one possible interval and `minSlotsForHardLots` must equal this depth; that equality is a test.

### 11.4 Variant 1: EDF

```
pending = lots sorted by (dueWeek ascending, hard before soft, value descending, id ascending)
freeAt[k] = 0 for every slot k
for t = 0 .. T-1:
    for k = 0 .. slots-1:
        if freeAt[k] > t: continue
        pick = first lot in pending that is unscheduled and has release <= t <= latestStart
        if there is none: continue                    // the slot is idle this week
        schedule pick on slot k from t to t + p;  freeAt[k] = t + p
```

Plain earliest-deadline-first. It never keeps a slot empty for a lot that is not yet released, and that is its weakness: it fills slots with spot lots due soon, and a contract lot released a week later finds every slot taken for `p` weeks.

### 11.5 Variant 2: EDF with lookahead (the default)

Same loop. The pick changes:

1. If a released hard lot exists, pick the first one in `pending` order.
2. Otherwise take the first released soft lot and run an **admission test**:
   - `future` = the unscheduled hard lots with `t <= latestStart <= t + L`, sorted by `(latestStart, id)`;
   - `without = hardMissed(freeAt, future, p)`;
   - `with = hardMissed(freeAt with freeAt[k] replaced by t + p, future, p)`;
   - admit the soft lot only when `with <= without`. Otherwise leave the slot idle this week.

`L` is `lookaheadWeeks`. The default, and the meaning of `-1`, is the crop's cycle `p`, because a batch started now blocks the slot for exactly that long. `L = 0` shows the effect of having no lookahead. In words: a spot batch is started only if it does not cost a contract delivery that was otherwise still achievable.

### 11.6 Variant 3: exact, for small instances

Maximises `sum of value over scheduled lots - sum of penalty over unscheduled hard lots`.

Depth-first over the lots sorted by `(latestStart, id)`. For each lot, either skip it, or schedule it on a slot at `start = max(freeAt[k], release)` provided `start <= latestStart` and `start < T`; try only slots whose `freeAt` values are distinct. With `gain = value + penalty`, prune when `current + (sum of gains of the remaining lots) <= best + EPS`. The answer is the best gain minus the total penalty of all hard lots.

This canonical search was checked against a true brute force over every slot and start time on 400 random instances and agreed on all of them. It is exponential. Run it only when the crop has at most 22 lots and stop at 2 000 000 nodes; in either case fall back to the lookahead schedule and say so in the result's note.

### 11.7 Interface and statistics

```cpp
enum class SchedAlgo { Edf, EdfLookahead, Exact };
struct Batch { int cropId = 0, slot = 0, lotId = 0, start = 0, end = 0; };
struct CropSchedule {
    int cropId = 0, slots = 0;
    std::vector<Lot> lots; std::vector<char> scheduled;    // parallel arrays
    std::vector<Batch> batches;
    int hardTotal = 0, hardOnTime = 0, softTotal = 0, softServed = 0, idleSlotWeeks = 0;
    double revenue = 0, penalties = 0;                     // net = revenue - penalties
    std::string note;
};
CropSchedule scheduleCrop(const std::vector<Lot>&, int slots, int cycle, int horizon, SchedAlgo, int lookaheadWeeks);
```

- `idleSlotWeeks = slots * T - sum over batches of (min(end, T) - start)`.
- `revenue` sums `value` over scheduled lots; `penalties` sums `penalty` over unscheduled hard lots.
- A crop with lots but no slots still gets a `CropSchedule`, so a contract the plan abandoned shows up as missed deliveries with their penalty.

### 11.8 Complexity

For one crop with `J` lots, `k` slots, horizon `T`, and `Hw` hard lots inside a lookahead window:

| Algorithm | Time | Notes |
|---|---|---|
| Build lots | `O(T + J)` | |
| `intervalDepth` | `O(J log J)` | |
| `minSlotsForHardLots` | `O(Jh^2 log k)` | linear search over `m`; `Jh` hard lots |
| EDF | `O(J log J + T * k * J)` as specified | the scan for the first released lot is linear |
| EDF with lookahead | EDF plus `O(T * k * Hw log k)` | at most one admission test per slot per week |
| Exact | `O((k + 1)^J)` worst case | |

### 11.9 Tests

- Appendix A, G5: the six-week example where EDF serves 2 of 5 hard lots for a value of 45 and lookahead serves 5 of 5 for 60, with the exact batch lists; the exact optimum is 60; `minSlotsForHardLots` is 2; the interval-depth example gives 3 both ways.
- Appendix A, G6: per-crop and total statistics for both algorithms on the given slot counts.
- Property, on 300 seeded random small instances (`p` in 1..3, `f` in 0..2, `T` in 5..7, 1 or 2 slots, 3 to 6 lots): every schedule is valid (no overlap on a slot, each lot at most once, every start inside its window and before `T`); the exact value is at least the EDF value and at least the lookahead value. **Do not assert that lookahead is at least EDF.** It is a heuristic, and it lost in 17 of 400 random cases when this plan was validated.
- When `f = 0`, `minSlotsForHardLots` equals `intervalDepth` of the hard lots' fixed intervals.
- Lookahead with `L = 0` never schedules fewer hard lots than plain EDF on the G5 and G6 cases.

---

## 12. Pipeline

### 12.1 Options and result

```cpp
enum class KnapsackAlgo { Greedy, Dp, BranchAndBound };
enum class PackAlgo { FirstFitDecreasing, BestFitDecreasing, Genetic };

struct PlanOptions {
    ForecastMethod forecast = ForecastMethod::Auto;
    KnapsackAlgo knapsack = KnapsackAlgo::BranchAndBound;
    std::array<bool, 5> activeResource{{true, true, true, true, true}};     // any of the five, area included, may be switched off
    long long bnbNodeLimit = 5000000; double bnbTimeLimitSeconds = 5.0;
    std::size_t dpMaxStates = 2000000;
    PackAlgo packer = PackAlgo::FirstFitDecreasing;
    GaParams ga;
    bool refill = true;
    SchedAlgo scheduler = SchedAlgo::EdfLookahead;
    int lookaheadWeeks = -1;                                                 // -1: each crop's cycle
};
PlanResult runPipeline(const Scenario&, const PlanOptions&);
```

`PlanResult` holds everything the app shows: forecasts per crop; the constraint model; the lots, `contractSlots` and `demand` per crop; the knapsack problem and result; the packing result; the final tray count per crop with its value; the schedule per crop with totals; the weekly resource-use profile; seconds per stage; warnings. Provide `to_json` for it so `farm_cli run --out` can save it.

### 12.2 Steps

1. `validateScenario`. Throw if it reports anything.
2. Forecast every crop with `H = T - 1 + Pmax`.
3. `buildConstraints`.
4. For each crop: `hardKg`, `penaltyRate`, lots (11.2), `contractSlots` (11.3), `demand`, `penaltyPerWeek`.
5. Build the knapsack problem (9.2 to 9.4) and solve it with the chosen algorithm. This is the **relaxed selection**; with branch and bound its value is an upper bound on every plan.
6. Expand the selection into trays: ids `0, 1, 2, ..`, crops in id order, each crop's trays consecutive. Pack with the chosen packer.
7. **Refill**, when enabled:
   - `remaining_r = capacity_r - (sum over crops of placedCount_c * weight_r)`;
   - list every copy `(c, k)` with `placedCount_c < k <= u_c` and sort it as the greedy does;
   - scan once. Skip crops marked full. When the copy is its crop's next one and its weights fit `remaining`, call `placeOne` on the existing `PackState` with a fresh tray id (start at 1 000 000). On success raise the count and lower `remaining`. On failure mark the crop full: the layout only fills up, so nothing of that crop can fit later.
   - The policy is the packer's own; the genetic packer refills with First-Fit.
8. `finalCount_c` is the number of placed trays of crop `c`. `planValue` is the sum of the prefix values of the final counts.
9. `validatePacking`. A violation is a bug; add it to the warnings prominently.
10. Schedule every crop that has lots, with `slots = finalCount_c`.
11. Weekly resource profile for `t = 0 .. T-1`: the number of batches with `start <= t < end`, per crop, times the per-tray weekly rates. It never exceeds the budgets, because the knapsack budgeted for every slot being busy every week.

Why not loop back from packing into the knapsack? That was tried during planning. Re-solving the knapsack with tightened caps and re-packing from scratch never settles: a crop that fitted in one round loses trays in the next because the layout shifted, and after five rounds the plan had shrunk and still had unplaced trays. Dropping what does not fit and refilling into the *existing* layout only ever adds trays, and it ends after one pass.

### 12.3 Warnings

Collect human-readable strings. At least these:

- crop X has no compatible shelf (name the failing checks);
- crop X has a contract that needs N slots but the plan gives it M;
- N trays of crop X did not fit and were dropped;
- branch and bound stopped at its limit, with the gap `(upperBound - value) / upperBound`;
- the DP grid was coarsened, so its result is approximate;
- the exact scheduler was skipped for crop X (too many lots);
- the selected crops need more colour groups than there are shelves;
- the forced forecast method was unavailable for crop X.

### 12.4 Headline metrics

| Metric | Definition |
|---|---|
| Relaxed optimum | Knapsack value, PKR per week, contract bonus included |
| Plan value | Sum of prefix values of the final counts; also as a percentage of the relaxed optimum |
| Expected weekly profit | Plan value minus the bonus terms it contains |
| Budget use | Per resource: sum of `finalCount * weight` divided by capacity |
| Space utilisation | Placed tray area divided by total shelf area; and by the area of the shelves used |
| Contract reliability | Hard lots on time divided by hard lots, across crops |
| Spot service | Soft lots served divided by soft lots |
| Idle slot-weeks | Summed over crops |
| Net over the plan | Revenue minus penalties over all scheduled lots |

---

## 13. Command-line tool

`farm_cli <command> [options]`. Exit codes: 0 success, 1 failure while running, 2 bad arguments or an invalid scenario. Output is plain ASCII, no box-drawing characters.

| Command | Options | Effect |
|---|---|---|
| `generate` | `--seed N --crops N --racks N --flat --jitter X --horizon N --out FILE` | Writes a scenario |
| `validate` | `--scenario FILE` | Prints validation errors, exit 2 if there are any |
| `run` | `--scenario FILE [--forecast auto\|naive\|ma\|ses\|hw] [--knapsack greedy\|dp\|bnb] [--packer ffd\|bfd\|ga] [--scheduler edf\|lookahead\|exact] [--lookahead N] [--no-refill] [--ga-seed N] [--out FILE]` | Runs the pipeline, prints a summary, optionally saves the result as JSON |
| `compare` | `--scenario FILE [--out FILE]` | Runs every variant of every stage on one scenario and prints one table per stage |
| `bench` | `[--quick] [--suite NAME] [--out DIR]` | Runs the benchmark suites and writes CSV files |

The `run` summary prints, in order: the scenario name and sizes; the forecast method chosen per crop; the pruning statistics; the selection table (crop, trays, weekly kg, demand, value, each resource); budget use; packing statistics; schedule statistics per crop; totals; warnings; seconds per stage.

`compare` uses a core function, `compareAll(const Scenario&, const PlanOptions&)`, that the app's Compare tab also calls. It fixes the inputs of each stage so that the rows are comparable:

- forecast: hold-out errors per method averaged over crops;
- colouring: the three algorithms on the scenario's conflict graph;
- knapsack: the three algorithms on the scenario's knapsack problem;
- packing: the three packers on the branch-and-bound selection;
- scheduling: the three schedulers on the final plan of that selection packed with FFD and refilled.

---

## 14. Desktop app

### 14.1 Structure

- `main.cpp` starts from `examples/example_glfw_opengl3/main.cpp` of the pinned ImGui. Find it under `.deps/imgui-src/` and copy its structure; do not write the bootstrap from memory. Add `ImPlot::CreateContext()` after `ImGui::CreateContext()` and `ImPlot::DestroyContext()` before `ImGui::DestroyContext()`.
- One root window covering the viewport: position `(0, 0)`, size `io.DisplaySize`, no title bar, not movable, not resizable. Inside it, a sidebar child about 340 scaled pixels wide on the left, and a tab bar filling the rest.
- Set `io.IniFilename = nullptr`. The layout is fixed, so nothing needs to persist.
- Scaling: keep the example's `main_scale`, `style.ScaleAllSizes(main_scale)` and `style.FontScaleDpi = main_scale`. Without them the app is unreadably small on a high-DPI laptop.
- Font: if `C:\Windows\Fonts\segoeui.ttf` exists, load it with `io.Fonts->AddFontFromFileTTF` and set `style.FontSizeBase = 17`; otherwise keep the built-in font.
- Theme: dark by default, `--theme light` for report figures. Apply the matching `ImGui::StyleColors*` and `ImPlot::StyleColors*`.
- Twelve fixed, distinct crop colours in one array, indexed by `cropId % 12`, used by the layout, the timeline and every chart. Never rely on colour alone: label trays and rows with the crop name or an abbreviation, and choose black or white label text by the luminance of the fill.
- Enable keyboard navigation (`ImGuiConfigFlags_NavEnableKeyboard`).
- Failures the user must see: if GLFW or the OpenGL context cannot be created, show a `MessageBoxA` on Windows and append the message to `farm_app.log` beside the executable, then exit with code 1. A GUI-subsystem program has no console, so a silent exit would look like nothing happened.

### 14.2 State and threading

```cpp
struct AppState {
    Scenario scenario; std::string scenarioPath;
    PlanOptions options;
    PlanResult result; bool hasResult = false;
    std::future<PlanResult> pending; bool running = false; bool dirty = true; bool autoRun = true;
    std::future<CompareTable> pendingCompare;
    std::string status;                         // last error or information line
    /* benchmark tables loaded from CSV, the selected crop, the selected suite */
};
```

The pipeline runs on a worker through `std::async(std::launch::async, runPipeline, scenarioCopy, optionsCopy)`. The UI thread polls with `wait_for(std::chrono::seconds(0))` once per frame and calls `get()` when ready, inside a `try`. The worker receives copies and returns a value, so there is no shared mutable state and no lock. While a run is pending the Optimize button is disabled and shows "Running...". A change made during a run sets `dirty` again and a new run starts when the current one finishes.

### 14.3 Sidebar

1. **Scenario.** A combo listing `data/*.json` (via `std::filesystem`), a Load button, the scenario's name and sizes. A "Generate" popup with seed, crops, racks, flat. A "Save as" text field with a button.
2. **Weekly budgets.** For each of the four resources: a checkbox for "active" and an `InputDouble`. After a run, a progress bar of use against budget. Total shelf area is shown read-only with its own "active" checkbox. Switching a resource off removes it from crop selection only; the packer still enforces real space, so with area off the selection simply overshoots and more trays are dropped.
3. **Planning.** Horizon in weeks, slider 4 to 26.
4. **Algorithms.** Four combos: forecast, crop selection, packing, scheduling. The app starts with automatic forecast, branch and bound, the genetic packer and EDF with lookahead, because that combination gives the best plan. (`PlanOptions` itself keeps First-Fit Decreasing as its default so that tests need no seed.) A collapsed "Advanced" header with the GA parameters, the lookahead weeks, the branch-and-bound limits, the DP state limit, and the refill switch.
5. **Run.** An Optimize button, an "auto-run on change" checkbox, and seconds per stage from the last run.
6. **Warnings** from the last run.

Any edit sets `dirty`.

### 14.4 Tabs

Before the first result, each tab shows one line telling the user to press Optimize.

1. **Overview.** A row of headline numbers (section 12.4). A table of the selection: crop, trays, area, weekly kg against demand, weekly value, labour, water, nutrient, energy. A bar chart of trays per crop with the relaxed selection beside the final plan. The relaxed optimum, the plan value, and the percentage between them.
2. **Layout.** Racks side by side, their shelves stacked, each shelf drawn to scale as a top-down rectangle with its trays as filled rectangles outlined in a darker shade, labelled when the label fits (`ImGui::CalcTextSize`). Under each shelf: name, clearance, light, humidity, percent filled. Hovering a tray shows crop, tray size as placed, and shelf. Below: the unplaced trays per crop, and a "Compare packers" button that runs the three packers on the current selection and tabulates placed trays, value, shelves used, utilisation and milliseconds.
3. **Schedule.** A timeline inside a scrolling child: one row per slot, grouped under a header row per crop; the x axis is weeks `0 .. T + Pmax`, with a vertical line at `T`. Each batch is a bar from start to end in the crop's colour; contract batches get a bright outline; the tooltip shows contract or spot, the due week and the kg. Beside each crop header: contracts on time out of total, in red when any were missed. Below: a table per delivery week of contract lots due and delivered and spot lots wanted and served; and a line chart of weekly resource use as a percentage of budget, with a line at 100%.
4. **Forecast.** A crop selector. A plot of the last 104 weeks of history, each method's hold-out prediction (toggled from the legend), the chosen method's forecast, and a horizontal line at the contract level. A table of MAE, RMSE and sMAPE per method with the chosen one highlighted, and its parameters.
5. **Constraints.** The conflict graph: crops on a circle, node colour by DSATUR class, edge colour by reason (explicit red, height orange, no common shelf grey), with a legend. A crop-by-shelf table with a tick or a cross per cell; hovering a cross names the failed checks. The pruning numbers, and the colour counts of the three algorithms.
6. **Compare.** Two parts. "This scenario": a button that runs `compareAll` on a worker and shows one table per stage. "Scaling": a suite selector that plots `time_ms` against `n` per algorithm on log-log axes and `quality` against `n`, from `bench/results/*.csv`, with the fitted exponent in the legend; and a GA convergence chart from the last genetic run.
7. **Data.** Read-only sortable tables of crops, shelves and contracts.

### 14.5 Screenshot mode

`farm_app --screenshot DIR [--scenario FILE] [--theme light|dark]`

You cannot see the window, so this is how you check your own UI work. It also produces the figures for the report and the slides.

- Forces a 1600 x 1000 window at scale 1.0.
- Loads the scenario (default `data/default_scenario.json`), runs the pipeline with the app's starting options (so the genetic packer runs and the convergence chart has data) and `compareAll`, both synchronously, and loads the benchmark tables.
- Works through a list of shots. For each: select the tab (`ImGuiTabItemFlags_SetSelected`), render four frames so that layouts and plot axes settle, read the back buffer before swapping (`glPixelStorei(GL_PACK_ALIGNMENT, 1)`, `glReadPixels(.., GL_RGB, GL_UNSIGNED_BYTE, ..)`), flip vertically (`stbi_flip_vertically_on_write(1)`), and write with `stbi_write_png`.
- Shots: `ui_overview`, `ui_layout`, `ui_schedule`, `ui_forecast`, `ui_constraints`, `ui_compare`, `ui_data`, each with the sidebar. Then, with the sidebar hidden so the figure is clean: `fig_layout`, `fig_schedule`, `fig_forecast`, `fig_constraints`, `fig_compare`, `fig_convergence`, and `fig_bench_<suite>` for each suite.
- Exits 0 when every file was written, 1 otherwise, logging to `farm_app.log`.

After each UI change run it, open the PNG files, and check them: nothing clipped or overlapping, legends present, numbers equal to what `farm_cli run` prints.

### 14.6 Self-test mode

`farm_app --selftest [--scenario FILE]`

Screenshots cannot show whether the background worker and the dirty flag behave. This mode runs the normal main loop with a short script instead of a user: wait for the first result; change a budget and wait for the automatic re-run; switch the packer and wait again; start the comparison and wait for it; visit every tab for two frames; exit. It exits 0 on success and 1, with a line in `farm_app.log`, if any step does not finish within 60 seconds or an exception escapes. About thirty lines, and it is the only automated check of section 14.2.

What neither mode can check is real mouse and keyboard use: hover tooltips, sliders, text fields, scrolling. If you have a tool that can operate the desktop, use it. If not, say so plainly at the end of M8 and ask the owner to click through the app once.

---

## 15. Benchmarks

`farm_cli bench` runs the suites and writes one CSV per suite to `bench/results/`, plus `summary.md`. All suites use one long format so a single chart function can draw any of them:

```
suite,algo,n,param,seed,time_ms,quality,aux1,aux2
```

| Suite | Instances | `n` | `param` | Algorithms | `quality` | `aux1`, `aux2` |
|---|---|---|---|---|---|---|
| `knap_1d` | One resource, integer weights 1 to 30, capacity half the total weight, groups of 10 copies with values falling 5% per copy | copies: 50, 100, 200, 400, 800, 1600 | 0 | greedy, dp, bnb | value as % of the DP optimum | nodes or states; proven 0/1 |
| `knap_dims` | 8 groups of 5 copies, integer weights 0 to 6, every capacity 24 | 40 | resources: 1, 2, 3, 4 | greedy, dp, bnb | value as % of the optimum | states or nodes; 0 |
| `knap_farm` | `generateScenario` with `racks = crops / 3` | crops: 6, 12, 18, 24 | 0 | greedy, bnb | value as % of the root bound | nodes; proven 0/1 |
| `pack_tight` | 10 tray types from a fixed list of sizes, shelves of five sizes, conflict probability 0.15, compatibility probability 0.8, tray area equal to 90% of shelf area | trays: 50, 100, 200, 400, 800 | 90 | ffd, bfd, ga | placed area as % of tray area | shelves used; placed area as % of the used shelves' area |
| `pack_loose` | The same at 60% | the same | 60 | ffd, bfd, ga | the same | the same |
| `sched` | One crop, `p = 5`, `f = 0`, `T = 26`, demand 1.3 times slot capacity, 40% of it contracts | slots: 5, 10, 20, 40, 80 | 0 | edf, lookahead | hard lots on time, % | soft served %; idle % |
| `sched_window` | The same with 20 slots | 20 | lookahead weeks: 0, 1, 2, 3, 5, 10, 26 | lookahead | hard lots on time, % | soft served %; idle % |
| `sched_exact` | `p = 2`, 2 slots, `T = 8` | lots: 8, 12, 16, 20 | 0 | edf, lookahead, exact | value as % of the exact optimum | 0; 0 |
| `forecast` | `generateScenario` with 24 crops, scoring the hold-out of every crop | 24 | relative noise %: 5, 15, 30 | naive, ma, ses, hw | MAE as % of mean demand | RMSE as % of mean demand; sMAPE |
| `colouring` | Random graphs with edge probability `param / 100` | vertices: 10, 15, 20, 50, 100, 200, 400 | 10, 30, 50 | wp, dsatur, exact (only for `n <= 20`) | colours used | exact count, or -1; proven 0/1 |

For the `forecast` suite the generator needs a noise override: add a `noiseOverride` field to `GeneratorParams` (negative means "draw sigma as usual").

For the scheduling suites, build the lots with the cumulative rounding of section 11.2 so that fractional weekly demand is reproducible. In `sched_exact`, when the exact search stops at its node limit for an instance, write `quality = -1` on that instance's rows instead of a percentage of a non-optimal value.

Rules:

- Five seeds (1 to 5) per configuration. Time each run with `std::chrono::steady_clock`: repeat it three times and keep the minimum; repeat once when a run takes over a second.
- Use the Release build. Record the CPU model, core count, RAM, compiler version and flags at the top of `summary.md` (`Get-CimInstance Win32_Processor`, `Get-CimInstance Win32_ComputerSystem`).
- Branch and bound is limited to 2 seconds per instance in the benchmarks and may report "not proven". That is a result, not a failure.
- `summary.md` holds, per suite and algorithm, the median time and median quality at each `n`, and the least-squares slope of `log(time)` against `log(n)` over points with `time_ms >= 0.05`. That slope is the measured exponent the complexity analysis compares with theory.
- `--quick` runs the smallest two sizes of each suite with one seed and must finish within 20 seconds. CTest runs it.
- Expect the full run to take a few minutes. If a configuration takes over a minute, reduce the GA's generations for that size and write the change into `summary.md`.

---

## 16. Tests

### 16.1 Harness (`tests/check.h`)

About thirty lines: a `TEST(name)` macro that registers a function in a static list, `CHECK(condition)` and `CHECK_NEAR(a, b, eps)` that print file, line and expression and increment a failure counter, and a `main` that runs every test whose name contains `argv[1]` (all when absent), prints a count, and returns 1 if anything failed.

### 16.2 What must exist

| File | Contents |
|---|---|
| `test_io.cpp` | Generate, save, load, compare field by field. The first three `Rng` values for seed 42. `generateScenario` with the flat parameters equals the committed `data/flat_scenario.json`, which guards that file against accidental edits. Every `validateScenario` rule, one broken scenario each. Unreadable and malformed files throw |
| `test_constraints.cpp` | Section 7.5 |
| `test_forecast.cpp` | Section 8.5 |
| `test_knapsack.cpp` | Section 9.8 |
| `test_packing.cpp` | Section 10.6 |
| `test_scheduling.cpp` | Section 11.9 |
| `test_pipeline.cpp` | The flat scenario end to end against Appendix A, G6. The default scenario runs under every combination of knapsack, packer and scheduler option with a valid packing, no budget exceeded, and identical results on a second run. A scenario with zero budgets and one with no contracts both run and produce an empty or sensible plan |

Compare against Appendix A with the tolerances stated at its top.

### 16.3 If a golden number does not match

1. Run `python plan/reference_model.py` and locate the first differing intermediate value. For G6 the order is: compatible shelf counts, conflict edges, the per-crop value table, greedy, branch and bound, packing, schedule.
2. Read the matching function in the reference model. Most mismatches are a tie-break, a missing `EPS`, or an off-by-one in a week range.
3. Change the test only if you can show that the reference model contradicts this plan's prose, and tell the owner.

---

## 17. Complexity analysis document

Write `docs/complexity-analysis.md`. Give each algorithm the same seven parts:

1. The problem it solves, in one paragraph.
2. Pseudo-code, 10 to 25 lines.
3. Time complexity, with the derivation and not just the result.
4. Space complexity.
5. Correctness or optimality: exact, or a heuristic, and why. Where a short proof exists, sketch it: an exchange argument for scheduling by deadline, the depth argument for interval partitioning, the validity of each branch-and-bound bound (each is a relaxation), the reason the DP visits states in descending order.
6. The measured exponent from `bench/results/summary.md` beside the theoretical one, with a sentence on any difference.
7. Where it sits in the pipeline, and what it is compared against.

Start the document with a summary table. These are the bounds for the algorithms as specified here; if your implementation differs, document what you built.

| Component | Algorithm | Paradigm | Time | Space | Optimal? |
|---|---|---|---|---|---|
| Constraints | Compatibility and conflict graph | Graph construction | `O(C^2 * S)` | `O(C*S + C^2)` | n/a |
| Constraints | Welsh-Powell | Greedy | `O(C log C + E)` | `O(C + E)` | No; uses at most max degree + 1 colours |
| Constraints | DSATUR | Greedy | `O(C * (C + E))` | `O(C + E)` | No; exact on bipartite graphs |
| Constraints | Exact colouring | Backtracking | exponential in `C` | `O(C + E)` | Yes |
| Forecast | Seasonal naive, moving average, SES | Recurrences | `O(n)` each | `O(n)` | n/a |
| Forecast | Holt-Winters with grid search | Recurrence, exhaustive search | `O(g * n)`, `g = 80` | `O(n + L)` | Least SSE on the grid |
| Selection | Greedy | Greedy | `O(N log N + N*m)` | `O(N)` | No |
| Selection | Dynamic programming | DP | `O(N * m * product of (W_r + 1))` | `O(N * product)` bits | Yes on the integer grid |
| Selection | Branch and bound | Branch and bound | exponential worst case, `O(m*N)` per node | `O(m*N)` | Yes when it finishes |
| Packing | FFD, BFD | Greedy | `O(n log n + n * (S + R))` | `O(n + S)` | No |
| Packing | Genetic algorithm | Metaheuristic | `O(G * P * n * (S + R))` | `O(P * n)` | No |
| Scheduling | Interval depth | Sweep line | `O(J log J)` | `O(J)` | Yes |
| Scheduling | EDF | Greedy | `O(J log J + T*k*J)` | `O(J + k)` | No, with release times |
| Scheduling | EDF with lookahead | Greedy with feasibility test | EDF plus `O(T * k * Hw log k)` | `O(J + k)` | No |
| Scheduling | Exact | Backtracking | `O((k + 1)^J)` | `O(J + k)` | Yes |

Also include one section on hardness: the 0/1 knapsack, bin packing, and graph colouring are NP-hard; for a fixed number of budgets `m` the knapsack DP is pseudo-polynomial, `O(N * product of W_r)`, but its table grows exponentially with `m`, which is the reason for section 9.7's behaviour; and non-preemptive scheduling with release times is NP-hard in general. State these as known results, cite them (section 18.5), and verify each statement against its source before it goes into the report.

---

## 18. Report and slides

### 18.1 Ground rules

- Every number comes from a file the program wrote: `farm_cli run --out`, `farm_cli compare --out`, `bench/results/*.csv`, `summary.md`. Insert them by script, not by typing.
- Every figure is a PNG from `farm_app --screenshot` with `--theme light`.
- The title page repeats the proposal's: course name, "Complex Computing Problems", the project title, the four names and roll numbers, the instructor. Copy them from the proposal PDF.
- Leave the "individual contributions" table empty and clearly marked for the team to fill in. Do not guess who did what.
- Describe the system that exists. If something in this plan was not built or was changed, the report says so.
- These files contain names and roll numbers. See section 20 before committing them.

### 18.2 Tooling

Use your document-generation skills if you have them. Otherwise create a throwaway environment with `uv venv .venv-docs` and `uv pip install --python .venv-docs python-docx python-pptx`. Either way, keep the generator script in `docs/tools/` so the team can rerun it after changing a number. The README must say that these scripts are documentation tooling and that the product is C++ only.

### 18.3 Report outline (`docs/report/Smart_Vertical_Farm_Report.docx`)

1. Title page
2. Abstract (about 150 words)
3. Introduction and problem description (from the proposal)
4. Why this is a Complex Computing Problem (section 18.4)
5. System overview: the pipeline diagram and a table of modules
6. Formal models: sets, parameters, decision variables, objective and constraints for crop selection, packing and scheduling
7. Algorithms: one subsection per component with the idea, pseudo-code, complexity, and the optimality discussion (from `docs/complexity-analysis.md`)
8. Implementation: language, structure, the third-party libraries and that none is algorithmic, testing, reproducibility
9. Experimental evaluation: machine and compiler; data sets; the default-farm walk-through (relaxed optimum against final plan, the three packers, EDF against lookahead); one subsection per benchmark suite with its chart and a short reading of it
10. Discussion: when each algorithm wins, the trade-offs, the limitations (the out-of-scope list in section 2, stated honestly)
11. Conclusion and future work
12. References
13. Appendix A: how to build and run. Appendix B: screenshots. Appendix C: individual contributions (empty, for the team)

### 18.4 CCP attribute mapping

The owner chose the Seoul Accord list and supplied its official text on 2026-11-13: Seoul Accord, *Section D - Graduate Attributes*, table D.4.1 "Range of Problem Solving". That table defines a Complex Computing Problem as "a computing problem having some or all of the following characteristics" and lists **nine** of them. The document says "characteristics"; the course says "CCP attributes". They are the same list. The PDF is not in the project folder, so the table below is the copy to work from.

Rules for this table in the report:

- The "Characteristic" and "Seoul Accord wording" columns are quotations. Copy them exactly: do not paraphrase, shorten, reorder or renumber. Keep the nine sentences as constants in the generator script, and make the script fail if any of them is missing from the text of the finished report.
- The last column is ours. Rewrite it against the system that was actually built. Replace each bracketed note with the measured figure from the program's output (section 18.1), and add the number of the report section that demonstrates the row.
- The definition says "some or all", so do not claim all nine at full strength. Rows 1, 2, 3, 8 and 9 carry the argument. Rows 4 to 7 are met in part, and the report states the limit given in each of those rows instead of overstating. The owner confirmed this rule on 2026-11-13.

| # | Characteristic | Seoul Accord wording | How this project shows it |
|---|---|---|---|
| 1 | Range of conflicting requirements | Involves wide-ranging or conflicting technical, computing, and other issues | Profit pulls against contract reliability, space use against crop separation, and five budgets (labour, water, nutrient, energy, floor area) pull the crop mix in different directions. The conflict is measured, not asserted: the lookahead scheduler gives up spot sales to protect contracts [contract lots on time and net revenue under both schedulers, from `compare`] |
| 2 | Depth of analysis required | Has no obvious solution, and requires conceptual thinking and innovative analysis to formulate suitable abstract models | No formula gives the plan. The abstractions had to be designed: trays as knapsack copies with falling marginal value, shelves as row-packed bins under a conflict graph, harvests as lots with due weeks on identical slots, and a relax, pack, refill scheme to join them, because the selection optimum does not survive packing [trays selected against trays placed, per packer]. Selection and packing are NP-hard (worded as in section 17), which is why exact and heuristic methods are compared |
| 3 | Depth of knowledge required | A solution requires the use of in-depth computing or domain knowledge and an analytical approach that is based on well-founded principles | Greedy methods, dynamic programming, branch and bound with relaxation bounds, backtracking, graph colouring, a genetic algorithm and exponential smoothing, each with a stated complexity bound and a measured growth rate [exponents from `summary.md`], plus domain knowledge of controlled-environment farming: growth cycles, light, humidity and clearance |
| 4 | Familiarity of issues | Involves infrequently-encountered issues | What is rarely met is the combination: humidity ranges, vertical clearance, shading between crops and delivery contracts in one model, and a best crop mix that cannot be packed as chosen. Limit to state: each sub-problem alone belongs to a familiar family (knapsack, bin packing, scheduling) |
| 5 | Level of problem | Is outside problems encompassed by standards and standard practice for professional computing | No standard, code of practice or ready-made procedure prescribes how to plan a vertical farm, so the team defined the model, the objective, the metrics and the validation. Limit to state: the individual algorithms are textbook methods; it is the problem as posed that no standard covers |
| 6 | Extent of stakeholder involvement and level of conflicting requirements | Involves diverse groups of stakeholders with widely varying needs | The model carries five parties whose needs differ: the operator (profit), contract customers (on-time delivery, enforced by penalties), spot buyers (availability), workers (the labour-hours budget) and the resource side (water and energy). Limit to state: nobody was consulted; these needs enter as parameters of generated sample data |
| 7 | Consequences | Has significant consequences in a range of contexts | Financial: missed contract deliveries cost penalties [penalty total under EDF]. Operational: a placement holds its slot for the whole plan. Environmental: water and energy use are budgeted and reported [budget use per resource]. Limit to state: these are consequences inside a simulation on generated data, not measurements from a farm |
| 8 | Interdependence | Is a high-level problem possibly including many component parts or sub-problems | Five sub-problems that feed each other: the forecast sets the value of each tray, the constraint graph limits packing, packing fixes the slots, the slots decide which deliveries can be met, and contract penalties feed back into tray values. Swapping one algorithm changes the results of the others [the same farm under two packers, from `compare`] |
| 9 | Requirement identification | Identification of a requirement or the cause of a problem is ill defined or unknown | The proposal names five components but gives no data, no objective and no formal constraints. The team had to decide what "best plan" means, what the planning unit is, how demand enters and what a missed contract costs. Causes also had to be diagnosed, not read off: why the best crop mix cannot be packed, and why earliest-deadline-first misses contracts that lookahead keeps |

The source PDF breaks three compounds across lines. They are hyphenated as printed above: `well-founded`, `infrequently-encountered`, `sub-problems`.

The same document also defines Complex Computing Activities (table D.4.2, five characteristics) and ten graduate attributes (table D.5). The owner asked for the problem mapping only. Do not add those tables unless the owner asks; their text is not reproduced here.

### 18.5 References

One source is certain, because the owner supplied it: Seoul Accord, *Section D - Graduate Attributes* (the "Seoul Accord Graduate Attributes"), table D.4.1 "Range of Problem Solving", www.seoulaccord.org. It is the source of section 18.4. The supplied pages carry no date: cite it without a year and with the date of access, and do not invent one.

The other likely references are listed below **from memory, as leads only**. Verify the authors, title, venue and year of each one before it enters the report, and drop any you cannot verify. Do not cite from this list unchecked.

- Karp (1972), NP-completeness of knapsack and related problems; Garey and Johnson (1979), *Computers and Intractability*.
- Kellerer, Pferschy and Pisinger (2004), *Knapsack Problems* (multi-dimensional knapsack).
- Coffman, Garey, Johnson and Tarjan (1980), level-oriented two-dimensional packing; Dósa (2007), the tight bound for First-Fit Decreasing.
- Jansen and Öhring (1997), and Gendreau, Laporte and Semet (2004), bin packing with conflicts.
- Welsh and Powell (1967); Brélaz (1979), DSATUR.
- Jackson (1955), earliest due date; Lenstra, Rinnooy Kan and Brucker (1977), complexity of machine scheduling; Kleinberg and Tardos, *Algorithm Design*, for interval scheduling and partitioning.
- Holt (1957) and Winters (1960), exponential smoothing with trend and seasonality.
- Falkenauer (1996), genetic algorithms for bin packing; Davis (1985), order crossover.
- Cormen, Leiserson, Rivest and Stein, *Introduction to Algorithms*, as the course text.

### 18.6 Slides (`docs/slides/Smart_Vertical_Farm_Slides.pptx`)

Twelve to fourteen slides, one idea each, figures over text:

1. Title, team, course
2. The problem: what a vertical farm operator must decide
3. Why it is a complex computing problem (the three or four strongest of the nine characteristics in section 18.4, each with its Seoul Accord sentence quoted exactly)
4. System pipeline
5. The sample farm and its constraints (layout figure)
6. Constraint graph and pruning ("48% of placements ruled out before optimising")
7. Forecasting: four methods, one chart, the selection rule
8. Crop selection: greedy against DP against branch and bound
9. Packing: FFD against BFD against the genetic algorithm (layout figure, value recovered)
10. Scheduling: EDF against lookahead (timeline figure, contracts saved)
11. Scaling results: one or two benchmark charts with measured exponents
12. Complexity summary table
13. Limitations and future work
14. Demo and questions

Add speaker notes to every slide. Each of the four students must be able to present any of them.

---

## 19. Milestones

Commit at the end of each milestone with the message `M<n>: <summary>` (git rules in section 20). Each milestone must leave `.\build.ps1 -Test` green.

### M0. Toolchain and a window (do this first; it carries the most risk)

1. The repository already exists (branch `main`, remote `origin`, `plan/` and `.gitignore` pushed). Do not run `git init` again. Check `.gitignore` against section 20. Do not push yet.
2. Install the toolchain and verify it (section 3.2).
3. Write `CMakeLists.txt` and `build.ps1` (section 4.4) with a one-function `farm_core`, a `farm_cli` that prints a version line, the test harness with one passing test, and a `farm_app` that opens a window with `ImGui::ShowDemoWindow()` and `ImPlot::ShowDemoWindow()` and supports `--screenshot`. Register only the `farm_tests` entry with CTest for now; add the two `farm_cli` entries when their commands exist (M7 and M9).
4. Acceptance:
   - `.\build.ps1 -Clean -Test` succeeds from a new PowerShell window;
   - `farm_cli.exe` and `farm_app.exe` run when PATH does not contain the MinGW folder, which proves static linking;
   - `farm_app --screenshot docs\screenshots` exits 0 and the PNG, which you open and look at, shows both demo windows with readable text.
5. If something fails, follow section 21. If M0 is not green after about an hour of focused attempts, stop and report the exact errors to the owner.

### M1. Data model, JSON, generator, CLI skeleton (sections 5, 6, 13)

- `model.h`, `rng.h`, `json_io`, `generator`, and the `generate` and `validate` commands.
- Generate and commit `data/default_scenario.json` and `data/flat_scenario.json`.
- Acceptance: `test_io.cpp` passes; `farm_cli validate` exits 0 on both files and 2 on a deliberately broken copy.

### M2. Constraint layer (section 7)

- Acceptance: `test_constraints.cpp` passes, including G1 and the constraint part of G6.

### M3. Forecasting (section 8)

- Acceptance: `test_forecast.cpp` passes, including G2.

### M4. Scheduling primitives and crop selection (sections 11.1 to 11.3, then 9)

- Lots and slot arithmetic come first, because the knapsack values need `contractSlots`.
- Acceptance: G3; the value table of G6; greedy 38 941.5; branch and bound 39 486.5, proven; the DP agrees with brute force on the random instances.

### M5. Packing (section 10)

- Acceptance: `test_packing.cpp` passes, including G4 and the FFD and BFD numbers of G6.

### M6. Scheduling (sections 11.4 to 11.9)

- Acceptance: `test_scheduling.cpp` passes, including G5 and the schedule numbers of G6.

### M7. Pipeline and CLI (sections 12, 13)

- `runPipeline`, refill, warnings, `compareAll`; the `run` and `compare` commands.
- Acceptance: `test_pipeline.cpp` passes; `farm_cli run --scenario data\flat_scenario.json` prints numbers that match Appendix A; the same command on the default scenario with `--packer ga` runs in under 5 seconds.

### M8. Desktop app (section 14)

- Build in this order and screenshot after each step: sidebar with a working Optimize button; Overview; Layout; Schedule; Forecast; Constraints; Data; Compare (scenario part).
- Acceptance: `farm_app --screenshot docs\screenshots` writes every `ui_*` and `fig_*` file; you opened each and it is legible; the numbers on Overview equal those of `farm_cli run` with the same options; `farm_app --selftest` exits 0; started with no arguments, the process is still alive after five seconds (then stop it). Tell the owner which interactions you could not exercise (section 14.6) and ask them to try the app by hand.

### M9. Benchmarks (section 15)

- `bench.cpp`, the `bench` command, the Scaling part of the Compare tab.
- Acceptance: `farm_cli bench --quick` finishes within 20 seconds; the full run writes ten CSV files and `summary.md`; the `fig_bench_*` screenshots show curves with legends.

### M10. README and complexity analysis (section 17)

- README: what the project is, a screenshot, how to build and run on Windows (and that other platforms work in principle through CMake), how to reproduce the benchmarks, the project layout, third-party libraries with their licences, and the statement that every algorithm is hand-written.
- Acceptance: every claimed bound in `docs/complexity-analysis.md` matches the code, and every measured exponent comes from `summary.md`.

### M11. Report and slides (section 18)

- Acceptance: both files open without errors; every number can be traced to an output file; the CCP table has all nine rows with the Seoul Accord wording of section 18.4 unchanged; the contributions table is empty and marked for the team.

### M12. Final check and packaging

- Clone into a fresh folder under `D:\`, run `.\build.ps1 -Test`, and confirm it passes with nothing copied over except what git tracks.
- Build `dist/SmartVerticalFarm/` with `farm_app.exe`, `farm_cli.exe`, `data/`, `bench/results/`, a short `README.txt`, and `THIRD_PARTY_NOTICES.txt` holding the licence text of ImGui, ImPlot, GLFW, nlohmann/json and stb, because their code is linked into the executables. Zip it. Do not commit the zip.
- Walk through section 22.

---

## 20. Git, GitHub and privacy

- The remote is **public**. Everything pushed is visible to anyone.
- The owner decided on 2026-11-13: `plan/` is published and already pushed; the proposal PDF is never pushed.
- Commit locally at the end of every milestone.
- **Before your first push of code, ask the owner once**, reminding them the repository is public. After they agree, push `main` at the end of each milestone without asking again. Never force-push and never rewrite pushed history.
- If `git fetch` shows commits on the remote that you do not have, stop and ask instead of merging or overwriting.
- Keep these out of git, by listing them in `.gitignore`:
  - `Smart Vertical Farm Proposal.pdf`: the owner said not to push it;
  - `docs/report/*.docx`, `docs/report/*.pdf`, `docs/slides/*.pptx`, until the owner says otherwise: they carry four students' names and roll numbers.
- Also ignore: `build/`, `.deps/`, `out/`, `dist/`, `.venv-docs/`, `*.exe`, `*.log`, `imgui.ini`, `__pycache__/`.
- The scenario files, benchmark CSV files and screenshots contain no personal data and are committed.
- Do not put the students' names, roll numbers or email addresses into source files, sample data, commit messages or the README.

---

## 21. Risks and fallbacks

Try the fallbacks in the order given, and record in the README which one you used.

| Risk | Signal | Fallback |
|---|---|---|
| winget does not honour `--location` | `g++.exe` appears under `%LOCALAPPDATA%` on C: | Accept it if C: keeps 4 GB or more free. Otherwise uninstall, download the x86_64 POSIX-SEH UCRT zip without LLVM (about 263 MB) from the `brechtsanders/winlibs_mingw` release tagged `16.2.0posix-23.1.3-14.0.0-ucrt-r3`, extract it to `D:\Tools\winlibs`, and add `D:\Tools\winlibs\mingw64\bin` to the user PATH |
| `g++` on PATH is a link that cannot find `cc1plus` | Compiling fails although `--version` works | Put the real `mingw64\bin` folder first on PATH; `build.ps1` already probes for it |
| The spaces in the project path break a tool | Errors that quote half a path | Build elsewhere: `.\build.ps1 -BuildDir D:\svf-build`, and set `FETCHCONTENT_BASE_DIR` to `D:\svf-deps` |
| GLFW 3.5.1 does not build with GCC 16 | Compile errors inside `.deps/glfw-src` | Pin GLFW 3.4 (section 3.3). If that also fails, drop GLFW and use ImGui's Win32 + OpenGL 3 backend: start from `examples/example_win32_opengl3/main.cpp` and link as its `build_mingw.bat` does (`-lopengl32 -lgdi32 -ldwmapi`). That makes the app Windows-only; say so in the README |
| ImPlot does not compile against ImGui | Errors inside `implot*.cpp` | Recheck both commit hashes against section 3.3. They were chosen as a tested pair, so a mismatch is the likely cause. If they are right, report to the owner |
| An ImGui or ImPlot call does not compile | "no matching function" in `src/app` | The signature changed this year. Read the declaration in the downloaded header and Appendix B. Do not guess |
| The OpenGL context cannot be created | The spike fails at start, or the screenshot is black | Likely a remote-desktop session or a missing GPU driver. Report it. Continue with core, CLI, tests and benchmarks, and mark the GUI as unverified |
| Branch and bound hits its limit on a generated scenario | `provenOptimal` false | Acceptable; report the gap. Do not raise the limits to hide it |
| The genetic packer is slow at 800 trays | A benchmark run over a minute | Lower the generations for that size and document it |
| A golden number does not match | A test fails | Section 16.3 |
| Disk space on C: | Under 2 GB free | Move `build/` and `.deps/` to D: as in the path-with-spaces row |
| Something not covered here blocks progress for more than an hour | | Stop, write down what you tried and the exact output, and ask the owner |

---

## 22. Definition of done

- [ ] `.\build.ps1 -Clean -Test` passes from a fresh clone on the owner's machine.
- [ ] Every golden case in Appendix A is asserted by a test and passes.
- [ ] No warnings from our own code in a Release build.
- [ ] `src/core` includes nothing but the standard library, apart from the JSON header in `json_io.cpp`.
- [ ] Two runs with the same scenario and options give byte-identical `--out` files (apart from the timing fields; write those under a separate key so the rest can be compared).
- [ ] `farm_app.exe` and `farm_cli.exe` run on a machine without MinGW.
- [ ] Every tab of the app was checked through a screenshot, and its numbers agree with the CLI. `farm_app --selftest` exits 0. The owner was told which interactions were not exercised.
- [ ] All ten benchmark CSV files and `summary.md` exist and are committed.
- [ ] `docs/complexity-analysis.md`, the report and the slides exist; every number in them traces to a program output; the CCP table has all nine rows with the Seoul Accord wording of section 18.4 unchanged; the contributions table is left for the team.
- [ ] The README explains building, running and reproducing, and lists third-party code with licences.
- [ ] Nothing containing names or roll numbers was committed without the owner's say-so, and nothing was pushed before they agreed.
- [ ] Your final message to the owner lists: what was built; anything in this plan you changed or skipped, and why; any test or check that does not pass; every fallback from section 21 that you used; and the items they must do themselves (fill in the contributions table, decide what to publish).

---

## Appendix A. Golden numbers

Produced by `python plan/reference_model.py`. Counts are exact. Doubles are printed to the precision shown: money values such as 38 941.5 are exact to that digit, so compare them with a tolerance of `1e-6`; a value printed with fewer digits, such as 278.333 or 89.62%, is rounded for display, so compare it to within half a unit of its last digit.

### G1. Graph colouring

- Crown graph on vertices 0 to 5 with edges (0,3), (0,5), (2,1), (2,5), (4,1), (4,3):
  - Welsh-Powell colours `[0, 0, 1, 1, 2, 2]`, 3 colours
  - DSATUR colours `[0, 1, 0, 1, 0, 1]`, 2 colours
  - exact: 2
- Exact chromatic numbers: the 5-cycle 3; the complete graph K4 4; the Petersen graph 3, with edges (0,1), (1,2), (2,3), (3,4), (4,0), (0,5), (1,6), (2,7), (3,8), (4,9), (5,7), (7,9), (9,6), (6,8), (8,5).

### G2. Forecasting

- Seasonal naive on `[1, 2, 3, 4, 5, 6, 7, 8]`, `L = 4`, `H = 3`: `[5, 6, 7]`.
- Moving average on `[3, 5, 4, 6, 5, 7]`: chooses `k = 4`, forecast 5.5.
- The SES recursion with `alpha = 0.5` on `[10, 20, 30]` ends at level 22.5. The SES routine with its grid chooses `alpha = 0.9` and forecasts 28.9.
- Holt-Winters fit with `alpha = 0.5, beta = 0.3, gamma = 0.2`, `L = 4`, on `[12, 18, 25, 15, 14, 21, 28, 17, 16, 23, 31, 19]`: forecast `[19.0448, 25.2662, 32.2442, 21.7855]` (compare to `1e-4`), in-sample SSE 6.7361.
- Automatic selection with `L = 4`, `H = 4` on
  `[46.6, 37.4, 35.9, 43.0, 46.8, 38.8, 37.6, 43.3, 47.4, 41.2, 38.9, 44.0, 49.0, 42.3, 40.2, 44.7, 50.9, 42.9, 41.1, 46.2, 52.3, 43.4, 42.2, 47.9]`
  (hold-out length 6):

  | Method | MAE | RMSE | sMAPE |
  |---|---|---|---|
  | Seasonal naive | 1.5833 | 1.8032 | 3.5253 |
  | Moving average | 3.2833 | 4.2963 | 7.1597 |
  | SES | 3.2833 | 3.9868 | 7.1623 |
  | Holt-Winters | 0.3302 | 0.4348 | 0.7042 |

  Chosen: Holt-Winters. Forecast `[53.1549, 44.9745, 43.4390, 48.8532]`. Compare to `1e-4`.

### G3. Knapsack

- One resource of capacity 10. Group X: weight 6, values `[7]`. Group Y: weight 5, values `[5, 5]`. Greedy returns 7 (one X). DP and branch and bound return 10 (two Y).
- Two resources with capacities `[6, 6]` and six single-copy groups given as value and weights: `9 (3,2)`, `6 (2,2)`, `7 (2,3)`, `4 (1,2)`, `8 (3,3)`, `3 (1,1)`. The optimum is 19. Two different subsets reach it, so assert the value only.

### G4. Packing

Shelves S0 of 100 x 70 and S1 of 100 x 60. Trays 0 to 3, one crop each, sizes 80 x 30, 60 x 40, 45 x 35, 60 x 40, all rotatable. No conflicts; everything compatible. Decreasing order: `[0, 1, 3, 2]`.

| Policy | Placed | Placements as tray: (shelf, x, y, w, d) | Unplaced |
|---|---|---|---|
| First-Fit | 4 of 4 | 0: (S0, 0, 0, 80, 30); 1: (S0, 0, 30, 60, 40); 3: (S1, 0, 0, 40, 60); 2: (S1, 40, 0, 35, 45) | none |
| Best-Fit | 3 of 4 | 0: (S1, 0, 0, 80, 30); 1: (S0, 0, 0, 40, 60); 3: (S0, 40, 0, 40, 60) | tray 2 |

Orientation chosen for the first row of an empty shelf:

| Shelf | Tray | Chosen (w', d') | Block count |
|---|---|---|---|
| 240 x 60 | 60 x 40 | (40, 60) | 6 |
| 240 x 60 | 50 x 25 | (25, 50) | 9 |
| 240 x 60 | 50 x 30 | (50, 30) | 8 |
| 200 x 80 | 60 x 40 | (60, 40) | 6 |
| 200 x 80 | 100 x 30 | (100, 30) | 4 |
| 180 x 50 | 50 x 25 | (25, 50) | 7 |

### G5. Scheduling

One crop with `p = 2`, `f = 0`, 2 slots, `T = 6`. For each delivery week 2 to 7: two soft lots, and from week 3 one hard lot as well. In every week create the hard lot before the soft lots. Every lot has `kg = 1`, `value = 10`; hard lots have `penalty = 5`.

| Algorithm | Hard on time | Soft served | Idle slot-weeks | Value | Batches as (slot, start, end, kind, due) |
|---|---|---|---|---|---|
| EDF | 2 of 5 | 4 of 12 | 0 | 45 | (0,0,2,S,2) (1,0,2,S,2) (0,2,4,H,4) (1,2,4,S,4) (0,4,6,H,6) (1,4,6,S,6) |
| Lookahead | 5 of 5 | 1 of 12 | 1 | 60 | (0,0,2,S,2) (1,1,3,H,3) (0,2,4,H,4) (1,3,5,H,5) (0,4,6,H,6) (1,5,7,H,7) |

Exact optimum value: 60. `minSlotsForHardLots`: 2.

Intervals `[0,3), [1,4), [2,5), [3,6), [4,7)`: `intervalDepth` is 3, and `minSlotsForHardLots` for hard lots with those fixed intervals (`p = 3`, `f = 0`) is 3.

### G6. The flat default scenario, end to end

Scenario: `data/flat_scenario.json`, which is sections 6.1 to 6.3 with every crop's history constant at its base demand. `T = 12`.

**Constraints**

- Compatible shelves per crop: Lettuce 12, Baby Spinach 12, Kale 4, Pak Choi 12, Arugula 12, Basil 7, Mint 12, Coriander 10, Microgreens 6, Pea Shoots 6, Strawberry 6, Cherry Tomato 1. Compatible crop-shelf pairs: 100 of 192.
- Conflict edges: 30, of which 22 by height and 8 explicit. Colours: Welsh-Powell 5, DSATUR 5, exact 5.

**Forecast.** Seasonal naive is chosen for every crop, and every forecast equals the base demand.

**Knapsack inputs.** Total shelf area 215 200. `N = 181` copies.

| Crop | Profit of a fully sold tray | Demand kg/week | Contract slots | Cap | Copies kept | First value | Last value |
|---|---|---|---|---|---|---|---|
| Lettuce | 124.00 | 12.600 | 10 | 72 | 35 | 340.000 | 124.000 |
| Baby Spinach | 178.50 | 7.200 | 4 | 96 | 24 | 388.500 | 178.500 |
| Kale | 216.50 | 4.800 | 0 | 20 | 11 | 216.500 | 216.500 |
| Pak Choi | 144.00 | 4.000 | 0 | 88 | 9 | 144.000 | 144.000 |
| Arugula | 344.50 | 2.400 | 0 | 124 | 8 | 344.500 | 344.500 |
| Basil | 271.00 | 3.600 | 4 | 67 | 16 | 608.500 | 271.000 |
| Mint | 101.50 | 2.000 | 0 | 148 | 10 | 101.500 | 101.500 |
| Coriander | 97.50 | 2.400 | 0 | 138 | 16 | 97.500 | 97.500 |
| Microgreens | 450.00 | 2.100 | 4 | 50 | 14 | 900.000 | 450.000 |
| Pea Shoots | 429.50 | 1.350 | 0 | 38 | 6 | 429.500 | 429.500 |
| Strawberry | 324.00 | 10.500 | 10 | 28 | 28 | 849.000 | 324.000 |
| Cherry Tomato | 278.33 | 3.500 | 0 | 4 | 4 | 278.333 | 278.333 |

**Knapsack results**, budgets 21 / 800 / 8.5 / 400 and area 215 200, all five resources active:

| Algorithm | Value | Selection |
|---|---|---|
| Greedy | 38 941.5 | Lettuce 10, Baby Spinach 24, Kale 1, Arugula 8, Basil 16, Microgreens 14, Pea Shoots 6, Strawberry 18 |
| Branch and bound | 39 486.5 | Lettuce 10, Baby Spinach 24, Kale 11, Arugula 8, Basil 16, Microgreens 14, Pea Shoots 6, Strawberry 13 |

Branch and bound: proven optimal, root bound 39 486.5, 2269 nodes in the reference model. Node counts depend on implementation details, so do not assert the exact figure, only that it stays below 100 000. The optimal selection uses labour 100.00%, water 89.62%, nutrient 83.53%, energy 82.90%, area 91.75% of budget. No other selection with this value was found by perturbing the values, so asserting the selection as well as the value is reasonable.

**Packing**, with the branch-and-bound selection (102 trays) as the explicit input:

| Packer | Placed | Value | Shelves used | Placed area | Unplaced | After refill |
|---|---|---|---|---|---|---|
| FFD | 94 of 102 | 36 858.5 | 16 | 185 290 (86.10%) | Kale 1, Arugula 7 | +0 trays; value 36 858.5 (93.34% of the relaxed optimum). Final: Lettuce 10, Baby Spinach 24, Kale 10, Arugula 1, Basil 16, Microgreens 14, Pea Shoots 6, Strawberry 13 |
| BFD | 95 of 102 | 37 203.0 | 16 | 186 640 (86.73%) | Kale 1, Arugula 6 | +1 tray, giving 96; value 37 300.5 (94.46%). Final: Lettuce 10, Baby Spinach 24, Kale 10, Arugula 2, Basil 16, Coriander 1, Microgreens 14, Pea Shoots 6, Strawberry 13 |

The genetic packer has no golden numbers because its random stream cannot match the reference. In the planning prototype, 40 generations with a population of 30 placed 97 to 98 of the 102 trays for a value near 38 500. Expect yours to beat BFD on this input; investigate if it does not.

**Scheduling**, with the FFD final plan's slot counts as the explicit input. Each cell is hard on time / hard total, soft served / soft total, idle slot-weeks.

| Crop | Slots | EDF | Lookahead |
|---|---|---|---|
| Lettuce | 10 | 12/24, 18/60, 3 | 24/24, 0/60, 20 |
| Baby Spinach | 24 | 12/12, 60/60, 36 | 12/12, 60/60, 36 |
| Kale | 10 | 0/0, 20/22, 14 | 0/0, 20/22, 14 |
| Arugula | 1 | 0/0, 4/32, 0 | 0/0, 4/32, 0 |
| Basil | 16 | 12/12, 36/36, 24 | 12/12, 36/36, 24 |
| Microgreens | 14 | 24/24, 60/60, 7 | 24/24, 60/60, 7 |
| Pea Shoots | 6 | 0/0, 36/36, 3 | 0/0, 36/36, 3 |
| Strawberry | 13 | 7/12, 12/24, 15 | 12/12, 7/24, 36 |

| Totals | Hard on time | Soft served | Idle | Revenue | Penalties | Net |
|---|---|---|---|---|---|---|
| EDF | 67 of 84 | 246 of 330 | 102 | 690 360 | 39 210 | 651 150 |
| Lookahead | 84 of 84 | 223 of 330 | 140 | 678 480 | 0 | 678 480 |

These totals cover the crops that have slots. Crops with lots but no slots add more unserved soft lots when the pipeline reports all crops, so compute both views and assert this one.

Lookahead depth on the same input, total hard lots on time out of 84: plain EDF 67; lookahead with `L = 0` 67; `L = 1` 69; `L` equal to each crop's cycle (the default) 84; unlimited `L` 84.

---

## Appendix B. Verified library facts

Checked on 2026-11-13 against the headers and examples of the pinned versions. If your memory of these libraries disagrees, the headers win.

**Dear ImGui v1.93.0**

- Bootstrap: `examples/example_glfw_opengl3/main.cpp`. It calls `ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor())`, creates the window, then `ImGui_ImplGlfw_InitForOpenGL(window, true)` and `ImGui_ImplOpenGL3_Init(glsl_version)`, where `glsl_version` may be `nullptr`. The backend needs GLFW 3.0 or later and recommends 3.4 or later.
- Fonts are dynamic since 1.92: `AddFontFromFileTTF(path)` needs no size; `style.FontSizeBase` and `style.FontScaleDpi` control size; `PushFont(ImFont*, float size)` takes two arguments.
- `ImDrawList` signatures. The parameter order of `AddRect` changed this year:
  - `AddRect(p_min, p_max, col, rounding = 0.0f, thickness = 1.0f, flags = 0)`
  - `AddRectFilled(p_min, p_max, col, rounding = 0.0f, flags = 0)`
  - `AddLine(p1, p2, col, thickness = 1.0f, flags = 0)`
  - `AddLineH(min_x, max_x, y, col, thickness, flags)` and `AddLineV(x, min_y, max_y, col, thickness, flags)`
  - `AddCircleFilled(center, radius, col, num_segments = 0)`, `AddText(pos, col, text_begin, text_end = NULL)`
- 1.93 behaviour changes: `AddLine` no longer adds half a pixel to its coordinates; closed shapes stroke inside their outline by default; rounding flags are named `ImDrawFlags_Round*` (the `RoundCorners*` names still redirect).
- Confirmed present: `BeginTable`, `TableSetupColumn`, `TableHeadersRow`, `TableSetBgColor`, `BeginTabBar`, `BeginTabItem` with `ImGuiTabItemFlags_SetSelected`, `BeginChild`, `BeginDisabled`, `BeginCombo`, `InputDouble`, `SliderInt`, `ProgressBar`, `SeparatorText`, `SetItemTooltip`, `SetTooltip`, `IsMouseHoveringRect`, `GetCursorScreenPos`, `GetContentRegionAvail`, `CalcTextSize`, `GetWindowDrawList`, `StyleColorsDark`, `StyleColorsLight`, `io.IniFilename`.

**ImPlot v1.1**

- `ImPlot::CreateContext()` and `ImPlot::DestroyContext()` bracket use; `ImPlot::StyleColorsDark()` and `ImPlot::StyleColorsLight()` exist.
- **Per-item styling changed in 1.0.** `SetNextLineStyle`, `SetNextFillStyle`, `SetNextMarkerStyle` and `SetNextErrorBarStyle` were removed. Style, flags, offset and stride now go in an `ImPlotSpec` passed as the last argument, either as a struct or inline: `ImPlot::PlotLine("a", xs, ys, n, {ImPlotProp_LineColor, colour, ImPlotProp_LineWeight, 2.0f});`
- Signatures:
  - `PlotLine(label, xs, ys, count, spec)` and `PlotLine(label, values, count, xscale = 1, xstart = 0, spec)`
  - `PlotBars(label, values, count, bar_size = 0.67, shift = 0, spec)`
  - `PlotBarGroups(label_ids, values, item_count, group_count, group_size = 0.67, shift = 0, spec)`
  - `PlotShaded(label, xs, ys1, ys2, count, spec)`
  - `PlotInfLines(label, values, count, spec)`; pass `ImPlotInfLinesFlags_Horizontal` through the spec's flags for horizontal lines
  - `PlotText(text, x, y, pix_offset, spec)`
- Axes and legend: `BeginPlot(title, size = ImVec2(-1, 0), flags)`, `SetupAxes`, `SetupAxisLimits`, `SetupAxisTicks`, `SetupAxisScale(axis, ImPlotScale_Log10)`, `SetupLegend`. Drawing inside a plot: `GetPlotDrawList`, `PlotToPixels`, `PushPlotClipRect`, `PopPlotClipRect`.
- Since 1.1 `FillAlpha` is applied once instead of twice, so shaded areas are more opaque than older examples suggest.
- `imgui_demo.cpp` and `implot_demo.cpp` are the best API references. They are compiled into `imgui_all`; a `--demo` flag that shows them is a cheap aid while building the UI.

**GLFW 3.5.1.** CMake options `GLFW_BUILD_DOCS`, `GLFW_BUILD_TESTS`, `GLFW_BUILD_EXAMPLES`, `GLFW_INSTALL` exist; turn them off. The CMake target is `glfw`.

**nlohmann/json v3.12.0.** The release asset `json.tar.xz` (about 115 KB) contains the headers and a CMake project that defines `nlohmann_json::nlohmann_json`.

**stb_image_write.h v1.16.** Define `STB_IMAGE_WRITE_IMPLEMENTATION` in exactly one source file before including it.

**Not verified.** That this exact toolchain builds these exact versions on this machine: nothing was installed while planning. Milestone M0 exists to find that out first.

---

## Appendix C. Glossary

| Term | Meaning |
|---|---|
| Shelf | One level of a rack: a rectangle with a clearance, a light level and a humidity. The "bin" of the packing problem |
| Tray | A growing container with a fixed footprint. The item that is packed |
| Slot | A tray after it has been placed: one position that grows one crop for the whole plan |
| Row | A strip along a shelf's width in which trays sit side by side |
| Copy | The k-th tray of a crop as a knapsack item |
| Relaxed selection | The knapsack's answer, which knows total area but not geometry or conflicts |
| Refill | Spending leftover budget on extra trays that are verified to fit the existing layout |
| Lot | One tray's worth of one delivery; a job for the scheduler |
| Hard lot / soft lot | A lot from a contract, with a penalty if missed / a lot from forecast spot demand |
| Batch | A lot scheduled on a slot from a start week to an end week |
| Cycle (p) | Weeks from planting to harvest |
| Harvest window (f) | How many weeks before its delivery week a harvest may happen |
| Horizon (T) | The number of weeks in which plantings are decided |
| Delivery window | The weeks `p .. T-1+p` in which a crop's plantings can deliver |
| Contract slots | The fewest slots that let a crop meet all its contract deliveries |
| Lookahead (L) | How many weeks ahead the scheduler checks for contract lots before starting a spot batch |
