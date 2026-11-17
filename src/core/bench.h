#pragma once
// The benchmark suites of plan section 15. A suite builds instances of growing size from a seed, runs the algorithms of one
// component on them, and records for every algorithm, size and seed how long it took and how good its answer was. All suites
// write the same long row format, so one chart function can draw any of them.
#include <string>
#include <vector>

struct BenchRow {
    std::string suite, algo;
    int n = 0;                       // the size of the instance; benchSuiteInfo says what it counts
    int param = 0;                   // a second setting that some suites vary, 0 when the suite has none
    int seed = 1;                    // 1 to 5
    double timeMs = 0;               // the fastest of up to three runs
    double quality = 0, aux1 = 0, aux2 = 0;   // what they measure depends on the suite
};

// What a suite is made of, in words, for summary.md and the charts.
struct BenchSuiteInfo {
    std::string name, what, nName, paramName, qualityName, aux1Name, aux2Name;   // paramName is empty when param is unused
};

// The ten suites in the order of plan 15: knap_1d, knap_dims, knap_farm, pack_tight, pack_loose, sched, sched_window,
// sched_exact, forecast, colouring.
const std::vector<std::string>& benchSuiteNames();
const BenchSuiteInfo& benchSuiteInfo(const std::string& suite);   // throws std::invalid_argument for an unknown suite

// Runs one suite. Every size and parameter with seeds 1 to 5, or with quick the two smallest sizes of the suite and seed 1.
// Every answer is checked once, outside the timing (a knapsack answer within its capacities, a layout and a schedule that break
// no rule, a colouring that is proper); a wrong answer throws std::logic_error. Throws std::invalid_argument for an unknown suite.
std::vector<BenchRow> runBenchSuite(const std::string& suite, bool quick);

// The rows as CSV: the header suite,algo,n,param,seed,time_ms,quality,aux1,aux2, then one line per row, LF line ends.
std::string benchCsv(const std::vector<BenchRow>& rows);
// Reads it back (CRLF is accepted too). Throws std::runtime_error naming the line when the header or a line is wrong.
std::vector<BenchRow> parseBenchCsv(const std::string& text);
void saveBenchCsv(const std::vector<BenchRow>& rows, const std::string& path);   // throws std::runtime_error when it cannot write
std::vector<BenchRow> loadBenchCsv(const std::string& path);                     // throws std::runtime_error when it cannot read

// One point of a chart: the medians over the seeds.
struct BenchPoint {
    int n = 0, param = 0;
    double timeMs = 0, quality = 0;
};

// One line of a chart: one algorithm, and one parameter when the points differ in size.
struct BenchSeries {
    std::string algo, label;         // label is the algo, followed by the parameter when the suite has several
    int param = 0;
    std::vector<BenchPoint> points;  // ascending in x
    // The measured exponent: the least-squares slope of log(time) against log(n) over the points whose time is at least 0.05 ms
    // (plan 15). Only for a series whose points differ in n, and only when at least two such points have different n.
    bool hasExponent = false;
    double exponent = 0;
    int exponentPoints = 0;
};

// What a chart and a summary table show for one suite.
struct BenchSuiteView {
    std::string suite;
    bool xIsN = true;                // false when every row has the same n: the points then differ in param, and that is x
    std::vector<BenchSeries> series;
};

// Groups the rows of one suite into series and takes the medians. Time O(R log R) for R rows.
BenchSuiteView benchView(const std::vector<BenchRow>& rows);

// The suites whose <suite>.csv exists in dir, in suite order. Throws std::runtime_error when one cannot be read.
std::vector<BenchSuiteView> loadBenchResults(const std::string& dir);

// summary.md: the machine, the compiler and the build, then per suite a table of the medians at each size and the measured
// exponents. `machine` is free text (the CPU model, cores and RAM that the caller read from the operating system).
std::string benchSummary(const std::vector<BenchSuiteView>& views, const std::string& machine);
