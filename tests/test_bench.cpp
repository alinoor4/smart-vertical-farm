// Plan section 15: the benchmark suites, their CSV format, and the medians and exponents computed from them.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench.h"
#include "check.h"

namespace {

// One quick run of a suite, kept for the tests that read it. The rows are returned as a copy (a few dozen rows): returning a
// reference into the cache makes GCC report a possibly dangling reference at every call that passes a string literal.
std::vector<BenchRow> quickRows(const std::string& suite) {
    static std::map<std::string, std::vector<BenchRow>> cache;
    auto found = cache.find(suite);
    if (found == cache.end()) found = cache.emplace(suite, runBenchSuite(suite, true)).first;
    return found->second;
}

// The row of an algorithm at one size, parameter and seed; a row of zeros named "missing" when there is none.
BenchRow rowOf(const std::vector<BenchRow>& rows, const std::string& algo, int n, int param, int seed = 1) {
    for (const BenchRow& r : rows)
        if (r.algo == algo && r.n == n && r.param == param && r.seed == seed) return r;
    BenchRow none;
    none.algo = "missing";
    return none;
}

BenchRow synthetic(const std::string& algo, int n, int seed, double timeMs, double quality = 0, int param = 0) {
    BenchRow r;
    r.suite = "knap_1d";
    r.algo = algo;
    r.n = n;
    r.param = param;
    r.seed = seed;
    r.timeMs = timeMs;
    r.quality = quality;
    return r;
}

}  // namespace

TEST(bench_the_ten_suites_of_plan_15) {
    const std::vector<std::string> expected = {"knap_1d", "knap_dims", "knap_farm", "pack_tight", "pack_loose", "sched", "sched_window", "sched_exact", "forecast", "colouring"};
    CHECK(benchSuiteNames() == expected);
    for (const std::string& name : expected) CHECK(benchSuiteInfo(name).name == name && !benchSuiteInfo(name).what.empty() && !benchSuiteInfo(name).qualityName.empty());
    CHECK_THROWS(benchSuiteInfo("nope"));
    CHECK_THROWS(runBenchSuite("nope", true));
}

// A quick run is the two smallest sizes with one seed: these are the row counts that follows from plan 15.
TEST(bench_quick_row_counts) {
    const std::map<std::string, size_t> expected = {{"knap_1d", 6},   {"knap_dims", 6}, {"knap_farm", 4}, {"pack_tight", 6}, {"pack_loose", 6},
                                                    {"sched", 4},     {"sched_window", 2}, {"sched_exact", 6}, {"forecast", 8},   {"colouring", 18}};
    for (const std::string& name : benchSuiteNames()) {
        const std::vector<BenchRow>& rows = quickRows(name);
        CHECK(rows.size() == expected.at(name));
        for (const BenchRow& r : rows) CHECK(r.suite == name && r.seed == 1 && r.timeMs >= 0 && std::isfinite(r.quality));
    }
}

TEST(bench_knapsack_suites) {
    // knap_1d: the DP is exact on the integer grid, so it is 100% and nobody beats it; branch and bound starts from the greedy
    // answer, so it is never worse than greedy.
    const std::vector<BenchRow>& one = quickRows("knap_1d");
    for (int copies : {50, 100}) {
        const BenchRow greedy = rowOf(one, "greedy", copies, 0), dp = rowOf(one, "dp", copies, 0), bnb = rowOf(one, "bnb", copies, 0);
        CHECK_NEAR(dp.quality, 100.0, 1e-9);
        CHECK(greedy.quality <= 100.0 + 1e-9 && greedy.quality > 90.0);
        CHECK(bnb.quality >= greedy.quality - 1e-9 && bnb.quality <= 100.0 + 1e-9);
        CHECK(dp.aux1 > 0 && dp.aux2 == 1 && bnb.aux1 > 0 && bnb.aux2 == 1);
    }
    // knap_dims: the DP table has 25 states per resource, so 25^m in all. aux2 is unused.
    const std::vector<BenchRow>& dims = quickRows("knap_dims");
    for (int resources : {1, 2}) {
        const BenchRow greedy = rowOf(dims, "greedy", 40, resources), dp = rowOf(dims, "dp", 40, resources), bnb = rowOf(dims, "bnb", 40, resources);
        CHECK_NEAR(dp.aux1, std::pow(25.0, resources), 1e-9);
        CHECK_NEAR(dp.quality, 100.0, 1e-9);
        CHECK(greedy.quality <= 100.0 + 1e-9 && bnb.quality >= greedy.quality - 1e-9 && bnb.quality <= 100.0 + 1e-9);
        CHECK(greedy.aux2 == 0 && dp.aux2 == 0 && bnb.aux2 == 0);
    }
    // knap_farm: both are measured against the root bound, which nothing can exceed.
    const std::vector<BenchRow>& farm = quickRows("knap_farm");
    for (int crops : {6, 12}) {
        const BenchRow greedy = rowOf(farm, "greedy", crops, 0), bnb = rowOf(farm, "bnb", crops, 0);
        CHECK(greedy.quality > 90.0 && greedy.quality <= bnb.quality + 1e-9 && bnb.quality <= 100.0 + 1e-6);
        CHECK(bnb.aux2 == 1);   // these small farms are solved to the end
    }
}

// The genetic algorithm starts from both decreasing orders, so it never places less area than First-Fit or Best-Fit Decreasing.
TEST(bench_packing_suites) {
    for (const char* suite : {"pack_tight", "pack_loose"}) {
        const std::vector<BenchRow>& rows = quickRows(suite);
        const int fill = std::string(suite) == "pack_tight" ? 90 : 60;
        for (int trays : {50, 100}) {
            const BenchRow ffd = rowOf(rows, "ffd", trays, fill), bfd = rowOf(rows, "bfd", trays, fill), ga = rowOf(rows, "ga", trays, fill);
            CHECK(ga.quality >= std::max(ffd.quality, bfd.quality) - 1e-9);
            for (const BenchRow& r : {ffd, bfd, ga}) CHECK(r.quality > 0 && r.quality <= 100.0 + 1e-9 && r.aux1 >= 1 && r.aux2 > 0 && r.aux2 <= 100.0 + 1e-9);
            if (fill == 60) CHECK_NEAR(ga.quality, 100.0, 1e-9);   // plenty of room: everything is placed
        }
    }
}

TEST(bench_scheduling_suites) {
    for (const char* suite : {"sched", "sched_window"})
        for (const BenchRow& r : quickRows(suite))
            CHECK(r.quality >= 0 && r.quality <= 100.0 + 1e-9 && r.aux1 >= 0 && r.aux1 <= 100.0 + 1e-9 && r.aux2 >= 0 && r.aux2 <= 100.0 + 1e-9);
    CHECK(rowOf(quickRows("sched_window"), "lookahead", 20, 0).quality > 0);   // the window 0 and 1 rows exist
    CHECK(rowOf(quickRows("sched_window"), "lookahead", 20, 1).quality > 0);

    // sched_exact: the exact schedule is the optimum, so it is 100% and nothing is above it (a result below zero is possible: a
    // schedule that misses contracts can lose money). -1 would mean the exact search stopped early; not for these small instances.
    const std::vector<BenchRow>& exact = quickRows("sched_exact");
    for (int lots : {8, 12}) {
        CHECK_NEAR(rowOf(exact, "exact", lots, 0).quality, 100.0, 1e-9);
        CHECK(rowOf(exact, "edf", lots, 0).quality <= 100.0 + 1e-9 && rowOf(exact, "lookahead", lots, 0).quality <= 100.0 + 1e-9);
    }
}

// noiseOverride raises the noise of the histories, so every method's error grows with it.
TEST(bench_forecast_suite) {
    const std::vector<BenchRow>& rows = quickRows("forecast");
    for (const char* algo : {"naive", "ma", "ses", "hw"}) {
        const BenchRow quiet = rowOf(rows, algo, 24, 5), noisy = rowOf(rows, algo, 24, 15);
        CHECK(quiet.quality > 0 && quiet.aux1 >= quiet.quality - 1e-9 && quiet.aux2 > 0);   // RMSE is never below MAE
        CHECK(noisy.quality > quiet.quality);
        CHECK(noisy.timeMs >= 0);
    }
}

TEST(bench_colouring_suite) {
    const std::vector<BenchRow>& rows = quickRows("colouring");
    for (int vertices : {10, 15})
        for (int edges : {10, 30, 50}) {
            const BenchRow wp = rowOf(rows, "wp", vertices, edges), dsatur = rowOf(rows, "dsatur", vertices, edges), exact = rowOf(rows, "exact", vertices, edges);
            CHECK(exact.aux2 == 1 && exact.aux1 == exact.quality);                      // proven, and aux1 is its own count
            CHECK(wp.aux1 == exact.quality && dsatur.aux1 == exact.quality);             // the heuristics carry the exact count too
            CHECK(exact.quality >= 1 && exact.quality <= dsatur.quality && exact.quality <= wp.quality);
        }
}

// The quality, aux1 and aux2 columns do not depend on the clock, so a second run gives the same ones (the cheap suites; the
// others wait for the clock or take seconds).
TEST(bench_runs_are_repeatable) {
    for (const char* suite : {"knap_dims", "sched", "sched_window", "sched_exact", "forecast", "colouring"}) {
        const std::vector<BenchRow> again = runBenchSuite(suite, true);
        const std::vector<BenchRow>& first = quickRows(suite);
        CHECK(first.size() == again.size());
        for (size_t i = 0; i < first.size() && i < again.size(); ++i)
            CHECK(first[i].algo == again[i].algo && first[i].n == again[i].n && first[i].param == again[i].param && first[i].quality == again[i].quality &&
                  first[i].aux1 == again[i].aux1 && first[i].aux2 == again[i].aux2);
    }
}

// Values of the first runs, pinned so that a change of an instance builder (a probability, a size, the order of the random draws)
// or of the way a column is computed shows up. The knapsack, scheduling, colouring and forecast numbers were reproduced by the
// Python reference model on the full CSV files, 1,354 numbers with no difference (instances rebuilt from the plan's rules, solved
// by plan/reference_model.py). The packing numbers (First-Fit and Best-Fit were reproduced, the genetic algorithm has no
// reference) and knap_farm (the pipeline's own problem) come from this implementation.
TEST(bench_quick_values_are_pinned) {
    auto row = [](const std::string& suite, const std::string& algo, int n, int param, double quality, double aux1, double aux2) {
        const BenchRow r = rowOf(quickRows(suite), algo, n, param);
        CHECK(r.algo == algo);
        CHECK_NEAR(r.quality, quality, 6e-5);   // the pins have four decimals, so up to half a unit of the last one (5e-5) can differ
        CHECK_NEAR(r.aux1, aux1, 6e-5);
        CHECK_NEAR(r.aux2, aux2, 6e-5);
    };
    row("knap_1d", "greedy", 50, 0, 99.8439, 0, 0);
    row("knap_1d", "dp", 50, 0, 100, 231, 1);
    row("knap_1d", "bnb", 50, 0, 100, 300, 1);
    row("knap_1d", "greedy", 100, 0, 99.9965, 0, 0);
    row("knap_1d", "dp", 100, 0, 100, 686, 1);
    row("knap_1d", "bnb", 100, 0, 100, 1095, 1);
    row("knap_dims", "greedy", 40, 1, 97.8545, 0, 0);
    row("knap_dims", "bnb", 40, 1, 100, 52, 0);
    row("knap_dims", "greedy", 40, 2, 97.2267, 0, 0);
    row("knap_dims", "bnb", 40, 2, 100, 247, 0);
    row("knap_farm", "greedy", 6, 0, 99.7698, 0, 0);
    row("knap_farm", "bnb", 6, 0, 99.7698, 89, 1);
    row("knap_farm", "greedy", 12, 0, 99.0507, 0, 0);
    row("knap_farm", "bnb", 12, 0, 99.8895, 2679, 1);
    row("pack_tight", "ffd", 50, 90, 90.0340, 10, 82.7478);
    row("pack_tight", "bfd", 50, 90, 81.8267, 10, 75.2047);
    row("pack_tight", "ga", 50, 90, 97.1861, 10, 89.3211);
    row("pack_tight", "ffd", 100, 90, 94.0969, 17, 85.7542);
    row("pack_tight", "bfd", 100, 90, 96.7591, 17, 88.1804);
    row("pack_tight", "ga", 100, 90, 100, 17, 91.1340);
    row("pack_loose", "ffd", 50, 60, 100, 11, 74.6848);
    row("pack_loose", "bfd", 50, 60, 100, 12, 76.1518);
    row("pack_loose", "ga", 50, 60, 100, 11, 88.8438);
    row("sched", "edf", 5, 0, 92.3077, 25, 7.6923);
    row("sched", "lookahead", 5, 0, 100, 0, 7.6923);
    row("sched", "edf", 10, 0, 80.7692, 68.75, 5.3846);
    row("sched", "lookahead", 10, 0, 100, 0, 7.6923);
    row("sched_window", "lookahead", 20, 0, 80.7692, 73.6842, 5.5769);
    row("sched_window", "lookahead", 20, 1, 87.1795, 64.9123, 5.7692);
    row("sched_exact", "edf", 12, 0, -16.6667, 0, 0);
    row("sched_exact", "lookahead", 12, 0, 100, 0, 0);
    row("sched_exact", "exact", 12, 0, 100, 0, 0);
    row("colouring", "wp", 10, 30, 3, 2, 1);
    row("colouring", "dsatur", 10, 30, 2, 2, 1);
    row("colouring", "exact", 10, 30, 2, 2, 1);
    row("colouring", "wp", 15, 50, 5, 4, 1);       // the exact search beats both heuristics here
    row("colouring", "dsatur", 15, 50, 5, 4, 1);
    row("colouring", "exact", 15, 50, 4, 4, 1);
    row("forecast", "naive", 24, 5, 8.3955, 10.3106, 8.0738);
    row("forecast", "ses", 24, 5, 13.2668, 15.0951, 12.8172);
    row("forecast", "hw", 24, 5, 6.0584, 7.3743, 5.7711);
}

TEST(bench_csv_round_trip) {
    std::vector<BenchRow> rows = {synthetic("dp", 100, 1, 0.1234, 99.5), synthetic("bnb", 1600, 5, 2000.5, 100, 3)};
    rows[0].aux1 = 1234567;   // a count of nodes stays a whole number
    rows[1].aux2 = -1;
    const std::string text = benchCsv(rows);
    CHECK(text.substr(0, text.find('\n')) == "suite,algo,n,param,seed,time_ms,quality,aux1,aux2");
    CHECK(text.find("knap_1d,dp,100,0,1,0.1234,99.5000,1234567,0\n") != std::string::npos);
    CHECK(text.find('\r') == std::string::npos);
    std::string crlf;   // the same text as a Windows editor or git's autocrlf would leave it
    for (char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    for (const std::string& version : {text, crlf}) {
        const std::vector<BenchRow> back = parseBenchCsv(version);
        CHECK(back.size() == 2);
        if (back.size() != 2) continue;
        for (size_t i = 0; i < 2; ++i) {
            CHECK(back[i].suite == rows[i].suite && back[i].algo == rows[i].algo && back[i].n == rows[i].n && back[i].param == rows[i].param && back[i].seed == rows[i].seed);
            CHECK_NEAR(back[i].timeMs, rows[i].timeMs, 1e-9);
            CHECK_NEAR(back[i].quality, rows[i].quality, 1e-9);
            CHECK_NEAR(back[i].aux1, rows[i].aux1, 1e-9);
            CHECK_NEAR(back[i].aux2, rows[i].aux2, 1e-9);
        }
    }
    CHECK(parseBenchCsv("suite,algo,n,param,seed,time_ms,quality,aux1,aux2\n").empty());   // a header alone is a table with no rows
}

TEST(bench_csv_rejects_bad_files) {
    const std::string header = "suite,algo,n,param,seed,time_ms,quality,aux1,aux2\n";
    CHECK_THROWS(parseBenchCsv(""));                                              // no header
    CHECK_THROWS(parseBenchCsv("suite,algo,n\n"));                                // the wrong header
    CHECK_THROWS(parseBenchCsv(header + "knap_1d,dp,100,0,1,0.1,99\n"));          // too few fields
    CHECK_THROWS(parseBenchCsv(header + "knap_1d,dp,100,0,1,0.1,99,0,0,0\n"));    // too many
    CHECK_THROWS(parseBenchCsv(header + "knap_1d,dp,ten,0,1,0.1,99,0,0\n"));      // not a number
    CHECK_THROWS(parseBenchCsv(header + "knap_1d,dp,10x,0,1,0.1,99,0,0\n"));      // a number followed by text
    CHECK_THROWS(parseBenchCsv(header + "knap_1d,dp,100,0,1,0.1x,99,0,0\n"));
    CHECK_THROWS(parseBenchCsv(header + "knap_1d,dp,100,0,1,0.1,,0,0\n"));        // an empty number
    CHECK_THROWS(loadBenchCsv("this/folder/does/not/exist.csv"));
}

// Medians over the seeds, and the exponent: the slope of log(time) against log(n) over the points with time >= 0.05 ms.
TEST(bench_view_medians_and_exponent) {
    std::vector<BenchRow> rows;
    const int seeds[3] = {1, 2, 3};
    const double slow[3] = {0.8, 1.0, 1.4};   // median 1.0 at n = 100
    for (int s = 0; s < 3; ++s) {
        rows.push_back(synthetic("a", 10, seeds[s], 0.01 + 0.001 * s, 10 + s));     // below 0.05 ms: left out of the fit
        rows.push_back(synthetic("a", 100, seeds[s], slow[s], 20 + s));
        rows.push_back(synthetic("a", 1000, seeds[s], 100.0 * (1 + 0.1 * s), 30 + s));   // median 110
        rows.push_back(synthetic("b", 10, seeds[s], 1.0));                           // one point only, and another far below the limit
        rows.push_back(synthetic("b", 100, seeds[s], 0.001));
    }
    const BenchSuiteView view = benchView(rows);
    CHECK(view.xIsN && view.series.size() == 2);
    if (view.series.size() != 2) return;
    const BenchSeries& a = view.series[0];
    CHECK(a.algo == "a" && a.label == "a" && a.points.size() == 3);
    CHECK_NEAR(a.points[0].timeMs, 0.011, 1e-12);
    CHECK_NEAR(a.points[1].timeMs, 1.0, 1e-12);
    CHECK_NEAR(a.points[2].timeMs, 110.0, 1e-12);
    CHECK_NEAR(a.points[1].quality, 21.0, 1e-12);
    CHECK(a.hasExponent && a.exponentPoints == 2);
    CHECK_NEAR(a.exponent, std::log(110.0 / 1.0) / std::log(1000.0 / 100.0), 1e-12);
    CHECK(!view.series[1].hasExponent && view.series[1].exponentPoints == 1);

    // A time of exactly 0.05 ms counts ("at least 0.05"): 0.05 at n = 10 and 5 at n = 100 give a slope of exactly 2.
    CHECK_NEAR(benchView({synthetic("edge", 10, 1, 0.05), synthetic("edge", 100, 1, 5.0)}).series[0].exponent, 2.0, 1e-12);

    // Times that follow c * n^2 give exactly 2, whatever c is.
    std::vector<BenchRow> square;
    for (int n : {50, 100, 200, 400}) square.push_back(synthetic("sq", n, 1, 0.003 * n * n));
    CHECK_NEAR(benchView(square).series[0].exponent, 2.0, 1e-9);

    // An even number of seeds: the median is the mean of the middle two.
    std::vector<BenchRow> even = {synthetic("e", 10, 1, 1.0, 1), synthetic("e", 10, 2, 2.0, 2), synthetic("e", 10, 3, 3.0, 3), synthetic("e", 10, 4, 10.0, 10)};
    CHECK_NEAR(benchView(even).series[0].points[0].timeMs, 2.5, 1e-12);
    CHECK(benchView({}).series.empty());
}

// When every row has the same n the points differ in param, and that is x (there is no exponent against n).
TEST(bench_view_of_suites) {
    const BenchSuiteView dims = benchView(quickRows("knap_dims"));
    CHECK(!dims.xIsN && dims.series.size() == 3);
    for (const BenchSeries& s : dims.series) CHECK(s.points.size() == 2 && s.points[0].param == 1 && s.points[1].param == 2 && !s.hasExponent && s.label == s.algo);

    // colouring has several sizes and three densities: a series for each algorithm and density, labelled with it.
    const BenchSuiteView colouring = benchView(quickRows("colouring"));
    CHECK(colouring.xIsN && colouring.series.size() == 9);
    CHECK(colouring.series.front().label == "wp 10");
    for (const BenchSeries& s : colouring.series) CHECK(s.points.size() == 2 && s.points[0].n == 10 && s.points[1].n == 15);

    // pack_tight has one parameter value, so the labels are the algorithm names.
    const BenchSuiteView tight = benchView(quickRows("pack_tight"));
    CHECK(tight.series.size() == 3 && tight.series[0].label == "ffd" && tight.series[2].label == "ga");
}

// loadBenchResults reads <suite>.csv for the suites that have a file, in suite order, and names a suite after its file.
TEST(bench_results_folder) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "farm_bench_results_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    CHECK(loadBenchResults(dir.string()).empty());                                     // nothing there yet
    CHECK(loadBenchResults((dir / "no_such_folder").string()).empty());

    saveBenchCsv(quickRows("sched"), (dir / "sched.csv").string());                    // written in the wrong order on purpose
    saveBenchCsv(quickRows("knap_dims"), (dir / "knap_dims.csv").string());
    std::vector<BenchRow> mislabelled = quickRows("forecast");
    for (BenchRow& r : mislabelled) r.suite = "not a suite";                           // the file name decides
    saveBenchCsv(mislabelled, (dir / "forecast.csv").string());
    saveBenchCsv(quickRows("sched"), (dir / "other.csv").string());                    // a file of no suite is ignored
    const std::vector<BenchSuiteView> views = loadBenchResults(dir.string());
    CHECK(views.size() == 3);
    if (views.size() == 3) {
        CHECK(views[0].suite == "knap_dims" && views[1].suite == "sched" && views[2].suite == "forecast");   // suite order, not file order
        CHECK(!views[0].xIsN && views[1].xIsN && !views[1].series.empty() && views[2].series.size() == 4);
        CHECK(benchSummary(views, "").find("## forecast\n") != std::string::npos);     // so benchSuiteInfo knows every name in it
    }

    std::ofstream(dir / "colouring.csv", std::ios::binary) << "this is not a benchmark table\n";
    bool named = false;
    try {
        loadBenchResults(dir.string());
    } catch (const std::runtime_error& e) {
        named = std::string(e.what()).find("colouring.csv") != std::string::npos;      // the message names the file
    }
    CHECK(named);
    std::filesystem::remove_all(dir);
}

TEST(bench_summary_text) {
    const std::string text = benchSummary({benchView(quickRows("sched")), benchView(quickRows("knap_dims"))}, "a test machine");
    CHECK(text.find("# Benchmark summary") == 0);
    CHECK(text.find("- Machine: a test machine") != std::string::npos);
    CHECK(text.find("- Compiler: ") != std::string::npos && text.find("- Build flags: ") != std::string::npos);
    CHECK(text.find("## sched\n") != std::string::npos && text.find("## knap_dims\n") != std::string::npos);
    CHECK(text.find("| edf | 5 | ") != std::string::npos);               // sched: algorithm, n, time, quality
    CHECK(text.find("| dp | 2 | 40 | ") != std::string::npos);            // knap_dims: algorithm, param (resources), then n = 40
    CHECK(text.find("| algorithm | param | n |") != std::string::npos);  // only the suite that has a parameter shows the column
    CHECK(text.find("| algorithm | n | median time") != std::string::npos);
    CHECK(text.find("Measured exponents") != std::string::npos);
    CHECK(text.find("no exponent against n") != std::string::npos);
    CHECK(benchSummary({}, "").find("not recorded") != std::string::npos);
}
