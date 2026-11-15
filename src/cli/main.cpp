// farm_cli <command> [options]. Exit codes: 0 success, 1 failure while running, 2 bad arguments or invalid scenario.
#include <charconv>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "generator.h"
#include "json_io.h"
#include "version.h"

namespace {

struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };

const char* const usage =
    "usage: farm_cli <command> [options]\n"
    "  generate --out FILE [--seed N] [--crops N] [--racks N] [--flat] [--jitter X] [--horizon N]\n"
    "  validate --scenario FILE\n"
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

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty()) throw UsageError("no command given");
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (args[0] == "generate") return generate(rest);
        if (args[0] == "validate") return validate(rest);
        throw UsageError("unknown command " + args[0]);
    } catch (const UsageError& e) {
        std::fprintf(stderr, "farm_cli: %s\nfarm_cli %s\n%s", e.what(), farmVersion(), usage);
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "farm_cli: %s\n", e.what());
        return 1;
    }
}
