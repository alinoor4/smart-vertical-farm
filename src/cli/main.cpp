// farm_cli <command> [options]. Exit codes: 0 success, 1 failure while running, 2 bad arguments or invalid scenario.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench.h"
#include "generator.h"
#include "json_io.h"
#include "pipeline.h"
#include "summary.h"
#include "version.h"

namespace {

struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };

const char* const usage =
    "usage: farm_cli <command> [options]\n"
    "  generate --out FILE [--seed N] [--crops N] [--racks N] [--flat] [--jitter X] [--horizon N]\n"
    "  validate --scenario FILE\n"
    "  run --scenario FILE [--forecast auto|naive|ma|ses|hw] [--knapsack greedy|dp|bnb] [--packer ffd|bfd|ga]\n"
    "      [--scheduler edf|lookahead|exact] [--lookahead N] [--no-refill] [--ga-seed N] [--out FILE]\n"
    "  compare --scenario FILE [--out FILE]\n"
    "  bench [--quick] [--suite NAME] [--out DIR] [--machine TEXT]\n"
    "exit codes: 0 success, 1 failure while running, 2 bad arguments or invalid scenario\n";

// The value after args[i]; leaves i on it.
const std::string& textAfter(const std::vector<std::string>& args, size_t& i) {
    if (i + 1 >= args.size()) throw UsageError("missing value for " + args[i]);
    return args[++i];
}

// std::from_chars is strict: no spaces, no trailing text, no minus sign for an unsigned type.
template <class T> T numberAfter(const std::vector<std::string>& args, size_t& i) {
    const std::string& option = args[i];
    const std::string& text = textAfter(args, i);
    T value{};
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc() || result.ptr != end) throw UsageError("bad value for " + option + ": " + text);
    return value;
}

// The value after args[i] must be one of the listed words; leaves i on it.
template <class T> T choiceAfter(const std::vector<std::string>& args, size_t& i, std::initializer_list<std::pair<const char*, T>> choices) {
    const std::string& option = args[i];
    const std::string& text = textAfter(args, i);
    std::string list;
    for (const auto& choice : choices) {
        if (text == choice.first) return choice.second;
        list += (list.empty() ? "" : "|") + std::string(choice.first);
    }
    throw UsageError("bad value for " + option + ": " + text + " (use " + list + ")");
}

void printCounts(const Scenario& s) {
    std::printf("%zu crops, %zu shelves, %zu contracts\n", s.crops.size(), s.shelves.size(), s.contracts.size());
}

int generate(const std::vector<std::string>& args) {
    GeneratorParams params;
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& option = args[i];
        if (option == "--flat") params.flat = true;
        else if (option == "--seed") params.seed = numberAfter<unsigned long long>(args, i);
        else if (option == "--crops") params.crops = numberAfter<int>(args, i);
        else if (option == "--racks") params.racks = numberAfter<int>(args, i);
        else if (option == "--jitter") params.jitter = numberAfter<double>(args, i);
        else if (option == "--horizon") params.horizonWeeks = numberAfter<int>(args, i);
        else if (option == "--out") out = textAfter(args, i);
        else throw UsageError("unknown option " + option);
    }
    if (out.empty()) throw UsageError("generate needs --out FILE");

    const Scenario scenario = generateScenario(params);
    const std::vector<std::string> problems = validateScenario(scenario);   // catches crops < 1, racks < 1, horizon out of range, jitter > 1, ...
    if (!problems.empty()) {
        std::printf("these parameters make an invalid scenario, nothing written:\n");
        for (const std::string& p : problems) std::printf("  %s\n", p.c_str());
        return 2;
    }
    saveScenario(scenario, out);
    std::printf("wrote %s: ", out.c_str());
    printCounts(scenario);
    return 0;
}

int validate(const std::vector<std::string>& args) {
    std::string path;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--scenario") path = textAfter(args, i);
        else throw UsageError("unknown option " + args[i]);
    }
    if (path.empty()) throw UsageError("validate needs --scenario FILE");

    Scenario scenario;
    try {
        scenario = loadScenario(path);
    } catch (const std::runtime_error& e) {   // unreadable, malformed or wrongly typed: not a usable scenario
        std::printf("%s\n", e.what());
        return 2;
    }
    const std::vector<std::string> problems = validateScenario(scenario);
    if (!problems.empty()) {
        std::printf("%s: %zu problem(s)\n", path.c_str(), problems.size());
        for (const std::string& p : problems) std::printf("  %s\n", p.c_str());
        return 2;
    }
    std::printf("ok: %s: ", scenario.name.c_str());
    printCounts(scenario);
    return 0;
}

// Loads and checks a scenario for run and compare. Returns false, after saying why, when it cannot be used (exit code 2).
bool loadUsable(const std::string& path, Scenario& scenario) {
    try {
        scenario = loadScenario(path);
    } catch (const std::runtime_error& e) {
        std::printf("%s\n", e.what());
        return false;
    }
    const std::vector<std::string> problems = validateScenario(scenario);
    if (problems.empty()) return true;
    std::printf("%s: %zu problem(s)\n", path.c_str(), problems.size());
    for (const std::string& p : problems) std::printf("  %s\n", p.c_str());
    return false;
}

int run(const std::vector<std::string>& args) {
    PlanOptions options;
    std::string path, out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& option = args[i];
        if (option == "--scenario") path = textAfter(args, i);
        else if (option == "--forecast")
            options.forecast = choiceAfter<ForecastMethod>(args, i, {{"auto", ForecastMethod::Auto}, {"naive", ForecastMethod::SeasonalNaive}, {"ma", ForecastMethod::MovingAverage},
                                                                     {"ses", ForecastMethod::Ses}, {"hw", ForecastMethod::HoltWinters}});
        else if (option == "--knapsack")
            options.knapsack = choiceAfter<KnapsackAlgo>(args, i, {{"greedy", KnapsackAlgo::Greedy}, {"dp", KnapsackAlgo::Dp}, {"bnb", KnapsackAlgo::BranchAndBound}});
        else if (option == "--packer")
            options.packer = choiceAfter<PackAlgo>(args, i, {{"ffd", PackAlgo::FirstFitDecreasing}, {"bfd", PackAlgo::BestFitDecreasing}, {"ga", PackAlgo::Genetic}});
        else if (option == "--scheduler")
            options.scheduler = choiceAfter<SchedAlgo>(args, i, {{"edf", SchedAlgo::Edf}, {"lookahead", SchedAlgo::EdfLookahead}, {"exact", SchedAlgo::Exact}});
        else if (option == "--lookahead") options.lookaheadWeeks = numberAfter<int>(args, i);
        else if (option == "--no-refill") options.refill = false;
        else if (option == "--ga-seed") options.ga.seed = numberAfter<unsigned long long>(args, i);
        else if (option == "--out") out = textAfter(args, i);
        else throw UsageError("unknown option " + option);
    }
    if (path.empty()) throw UsageError("run needs --scenario FILE");
    if (options.lookaheadWeeks < -1) throw UsageError("--lookahead must be -1 (each crop's cycle) or more");

    Scenario scenario;
    if (!loadUsable(path, scenario)) return 2;
    const PlanResult result = runPipeline(scenario, options);
    printRunSummary(stdout, scenario, result);
    if (!out.empty()) {
        savePlanResult(result, out);
        std::printf("\nwrote %s\n", out.c_str());
    }
    return 0;
}

int compare(const std::vector<std::string>& args) {
    std::string path, out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--scenario") path = textAfter(args, i);
        else if (args[i] == "--out") out = textAfter(args, i);
        else throw UsageError("unknown option " + args[i]);
    }
    if (path.empty()) throw UsageError("compare needs --scenario FILE");

    Scenario scenario;
    if (!loadUsable(path, scenario)) return 2;
    const CompareTable table = compareAll(scenario, PlanOptions());
    std::printf("Scenario: %s\n", scenario.name.c_str());
    printCompareTable(stdout, table);
    if (!out.empty()) {
        saveCompareTable(table, out);
        std::printf("\nwrote %s\n", out.c_str());
    }
    return 0;
}

// Runs the benchmark suites, writes one CSV per suite and summary.md (built from the CSV files in the folder, so --suite refreshes
// one suite without losing the others). --machine is the CPU, core and memory text that the shell read from the operating system.
int bench(const std::vector<std::string>& args) {
    namespace fs = std::filesystem;
    bool quick = false;
    std::string suite, out = "bench/results", machine;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& option = args[i];
        if (option == "--quick") quick = true;
        else if (option == "--suite") suite = textAfter(args, i);
        else if (option == "--out") out = textAfter(args, i);
        else if (option == "--machine") machine = textAfter(args, i);
        else throw UsageError("unknown option " + option);
    }
    const std::vector<std::string>& names = benchSuiteNames();
    if (!suite.empty() && std::find(names.begin(), names.end(), suite) == names.end()) {
        std::string list;
        for (const std::string& name : names) list += (list.empty() ? "" : "|") + name;
        throw UsageError("unknown suite " + suite + " (use " + list + ")");
    }

    std::error_code ec;
    fs::create_directories(out, ec);
    const auto begin = std::chrono::steady_clock::now();
    auto secondsSince = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count(); };
    for (const std::string& name : names) {
        if (!suite.empty() && suite != name) continue;
        const auto start = std::chrono::steady_clock::now();
        const std::vector<BenchRow> rows = runBenchSuite(name, quick);
        saveBenchCsv(rows, (fs::path(out) / (name + ".csv")).string());
        std::printf("%-13s %4zu rows %7.1f s\n", name.c_str(), rows.size(), secondsSince(start));
        std::fflush(stdout);   // the long suites are slow: show progress as it comes
    }
    const std::string summaryPath = (fs::path(out) / "summary.md").string();
    std::ofstream summary(summaryPath, std::ios::binary);
    summary << benchSummary(loadBenchResults(out), machine);
    summary.flush();
    if (!summary) throw std::runtime_error("cannot write " + summaryPath);
    std::printf("wrote the CSV files and %s in %.1f s\n", summaryPath.c_str(), secondsSince(begin));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty()) throw UsageError("no command given");
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (args[0] == "generate") return generate(rest);
        if (args[0] == "validate") return validate(rest);
        if (args[0] == "run") return run(rest);
        if (args[0] == "compare") return compare(rest);
        if (args[0] == "bench") return bench(rest);
        throw UsageError("unknown command " + args[0]);
    } catch (const UsageError& e) {
        std::fprintf(stderr, "farm_cli: %s\nfarm_cli %s\n%s", e.what(), farmVersion(), usage);
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "farm_cli: %s\n", e.what());
        return 1;
    }
}
