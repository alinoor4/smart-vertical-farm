#pragma once
// Component 2 (plan section 8): forecast one crop's weekly spot demand (kg) from its order history.
#include <array>
#include <string>
#include <vector>

enum class ForecastMethod { Auto, SeasonalNaive, MovingAverage, Ses, HoltWinters };

struct ForecastErrors { bool available = false; double mae = 0, rmse = 0, smape = 0; };

struct ForecastResult {
    ForecastMethod chosen = ForecastMethod::SeasonalNaive;   // never Auto
    std::vector<double> forecast;                            // `horizon` values >= 0; element 0 is delivery week 1
    // Hold-out scores, indexed by method - 1. A method is available when it could be fitted to the training part.
    std::array<ForecastErrors, 4> holdout;
    std::array<std::vector<double>, 4> holdoutPrediction;    // what each method predicted for the hold-out (empty when unavailable)
    int holdoutLength = 0;                                   // 0 when the history is too short to hold anything out
    std::string parameters;                                  // of the chosen method fitted to the whole history, e.g. "alpha=0.5 beta=0.1 gamma=0.3"
};

// Forecasts the `horizon` weeks after the history. Auto holds out the last min(12, n / 4) weeks, scores the four
// methods on them and picks the lowest MAE. A forced method skips the pick but the scores are still filled.
// A forced method that cannot be fitted to the whole history (only Holt-Winters, which needs 2 * seasonLength
// weeks) falls back to Auto: the caller sees chosen != method and reports it.
// Needs a non-empty history of finite values, seasonLength >= 1 and horizon >= 0, else throws std::invalid_argument.
// Time O(n + horizon) per method run; Holt-Winters tries a fixed grid of 80 settings, so its constant is 80. All four
// methods run once on the training part, then the chosen one runs again on the whole history.
ForecastResult forecastSeries(const std::vector<double>& history, int seasonLength, int horizon,
                              ForecastMethod method = ForecastMethod::Auto);

// One method on its own, as forecastSeries runs it on the training part: its forecast for the `horizon` weeks after y (clipped
// at zero) and the parameters it chose. ok is false when the method cannot be fitted to y (Holt-Winters needs two seasons).
// method must not be Auto, y must not be empty, seasonLength >= 1 and horizon >= 0, else throws std::invalid_argument. The
// benchmarks time each method with it. Time O(n + horizon), and O(80 (n + horizon)) for Holt-Winters.
struct MethodForecast {
    bool ok = false;
    std::vector<double> values;
    std::string parameters;
};
MethodForecast forecastWith(ForecastMethod method, const std::vector<double>& y, int seasonLength, int horizon);

// One parameter setting of simple exponential smoothing: the level after the last week and the one-step
// in-sample squared error. The grid search in forecastSeries tries alpha = 0.1 .. 0.9. y must not be empty.
struct SesFit { double level = 0, sse = 0; };
SesFit sesFit(const std::vector<double>& y, double alpha);

// One parameter setting of additive Holt-Winters (plan section 8.2): the forecast for the next `horizon` weeks
// (not clipped at zero) and the one-step in-sample squared error. Needs y.size() >= 2 * seasonLength.
struct HoltWintersFit { std::vector<double> forecast; double sse = 0; };
HoltWintersFit holtWintersFit(const std::vector<double>& y, int seasonLength, double alpha, double beta, double gamma,
                              int horizon);
