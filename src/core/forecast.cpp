#include "forecast.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "model.h"

namespace {

// What one method produces for one series. ok is false when the method cannot be fitted to it.
struct MethodRun {
    bool ok = false;
    std::vector<double> values;
    std::string parameters;
};

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", v);
    return text;
}

// Mean of y[from .. from + count - 1], added left to right.
double mean(const std::vector<double>& y, int from, int count) {
    double sum = 0;
    for (int i = from; i < from + count; ++i) sum += y[i];
    return sum / count;
}

// Method 1. One season ago, repeated; with less than one season of history, the last value.
MethodRun seasonalNaive(const std::vector<double>& y, int seasonLength, int horizon) {
    MethodRun run;
    run.ok = true;
    const int n = int(y.size());
    if (n < seasonLength) {
        run.values.assign(horizon, y.back());
        run.parameters = "last value";
        return run;
    }
    for (int h = 0; h < horizon; ++h) run.values.push_back(y[n - seasonLength + h % seasonLength]);
    run.parameters = "L=" + std::to_string(seasonLength);
    return run;
}

// Method 2. The mean of the last k weeks, with k = 4, 8 or 12 chosen by the one-step error over the whole history.
MethodRun movingAverage(const std::vector<double>& y, int horizon) {
    const int n = int(y.size());
    int bestK = 0;                                    // 0: no candidate yet
    double bestSse = 0;
    for (int k : {4, 8, 12}) {
        if (k >= n) continue;                         // needs at least one week to predict
        // Predict every week t >= k by the mean of the k weeks before it. The window sum slides one week at a time.
        double window = 0;
        for (int i = 0; i < k; ++i) window += y[i];
        double sse = 0;
        for (int t = k; t < n; ++t) {
            const double error = y[t] - window / k;
            sse += error * error;
            window += y[t] - y[t - k];
        }
        if (bestK == 0 || sse < bestSse - EPS) { bestK = k; bestSse = sse; }
    }
    if (bestK == 0) bestK = n;                        // shorter than every candidate: the mean of everything

    MethodRun run;
    run.ok = true;
    run.values.assign(horizon, mean(y, n - bestK, bestK));
    run.parameters = "k=" + std::to_string(bestK);
    return run;
}

// Method 3. The level after the last week, with alpha = 0.1 .. 0.9 chosen by the one-step error.
MethodRun simpleSmoothing(const std::vector<double>& y, int horizon) {
    SesFit best;
    double bestAlpha = 0;                             // 0: no candidate yet
    for (int tenths = 1; tenths <= 9; ++tenths) {
        const double alpha = tenths / 10.0;
        const SesFit fit = sesFit(y, alpha);
        if (bestAlpha == 0 || fit.sse < best.sse - EPS) { best = fit; bestAlpha = alpha; }
    }
    MethodRun run;
    run.ok = true;
    run.values.assign(horizon, best.level);
    run.parameters = "alpha=" + number(bestAlpha);
    return run;
}

// Method 4. Needs two full seasons. Tries 5 x 4 x 4 = 80 parameter settings, alpha outermost, gamma innermost.
MethodRun holtWinters(const std::vector<double>& y, int seasonLength, int horizon) {
    MethodRun run;
    if (static_cast<long long>(y.size()) < 2LL * seasonLength) return run;
    run.ok = true;
    double bestSse = 0;
    bool first = true;
    for (double alpha : {0.1, 0.3, 0.5, 0.7, 0.9})
        for (double beta : {0.01, 0.05, 0.1, 0.3})
            for (double gamma : {0.05, 0.1, 0.3, 0.5}) {
                const HoltWintersFit fit = holtWintersFit(y, seasonLength, alpha, beta, gamma, horizon);
                if (first || fit.sse < bestSse - EPS) {
                    first = false;
                    bestSse = fit.sse;
                    run.values = fit.forecast;
                    run.parameters = "alpha=" + number(alpha) + " beta=" + number(beta) + " gamma=" + number(gamma);
                }
            }
    return run;
}

// Runs one method (1..4 of ForecastMethod) and clips its forecast at zero: demand cannot be negative.
MethodRun runMethod(ForecastMethod method, const std::vector<double>& y, int seasonLength, int horizon) {
    MethodRun run;
    switch (method) {
        case ForecastMethod::SeasonalNaive: run = seasonalNaive(y, seasonLength, horizon); break;
        case ForecastMethod::MovingAverage: run = movingAverage(y, horizon); break;
        case ForecastMethod::Ses: run = simpleSmoothing(y, horizon); break;
        case ForecastMethod::HoltWinters: run = holtWinters(y, seasonLength, horizon); break;
        case ForecastMethod::Auto: break;             // forecastSeries resolves Auto before it gets here
    }
    for (double& v : run.values) v = std::max(0.0, v);
    return run;
}

ForecastErrors score(const std::vector<double>& actual, const std::vector<double>& predicted) {
    const int count = int(actual.size());
    double absolute = 0, squared = 0, relative = 0;
    for (int i = 0; i < count; ++i) {
        const double miss = std::fabs(actual[i] - predicted[i]);
        const double size = std::fabs(actual[i]) + std::fabs(predicted[i]);
        absolute += miss;
        squared += miss * miss;
        if (size >= EPS) relative += 2 * miss / size;  // both zero: an exact forecast, counted as 0
    }
    ForecastErrors errors;
    errors.available = true;
    errors.mae = absolute / count;
    errors.rmse = std::sqrt(squared / count);
    errors.smape = 100.0 / count * relative;
    return errors;
}

}  // namespace

SesFit sesFit(const std::vector<double>& y, double alpha) {
    SesFit fit;
    fit.level = y[0];
    for (size_t t = 1; t < y.size(); ++t) {
        const double error = y[t] - fit.level;       // what the level predicted for week t
        fit.sse += error * error;
        fit.level = alpha * y[t] + (1 - alpha) * fit.level;
    }
    return fit;
}

HoltWintersFit holtWintersFit(const std::vector<double>& y, int seasonLength, double alpha, double beta, double gamma,
                              int horizon) {
    const int n = int(y.size());
    const int L = seasonLength;
    // Start from the straight line through the first two seasons; what is left over is the season pattern.
    const double first = mean(y, 0, L), second = mean(y, L, L);
    double trend = (second - first) / L;
    double level = first + trend * (L - 1) / 2.0;     // the line's value at week L-1
    std::vector<double> season(L);
    for (int i = 0; i < L; ++i) season[i] = y[i] - (first + trend * (i - (L - 1) / 2.0));

    HoltWintersFit fit;
    for (int t = L; t < n; ++t) {
        const double old = season[t % L];
        const double error = y[t] - (level + trend + old);   // the one-step prediction for week t
        fit.sse += error * error;
        const double newLevel = alpha * (y[t] - old) + (1 - alpha) * (level + trend);
        trend = beta * (newLevel - level) + (1 - beta) * trend;
        season[t % L] = gamma * (y[t] - newLevel) + (1 - gamma) * old;
        level = newLevel;
    }
    for (int h = 0; h < horizon; ++h) fit.forecast.push_back(level + (h + 1) * trend + season[(n + h) % L]);
    return fit;
}

ForecastResult forecastSeries(const std::vector<double>& history, int seasonLength, int horizon, ForecastMethod method) {
    if (history.empty() || seasonLength < 1 || horizon < 0)
        throw std::invalid_argument("forecastSeries needs a non-empty history, seasonLength >= 1 and horizon >= 0");
    const int n = int(history.size());

    ForecastResult result;
    result.holdoutLength = std::min(12, n / 4);

    // Hold out the last weeks, train every method on the weeks before them and score the predictions.
    ForecastMethod best = ForecastMethod::SeasonalNaive;   // also the answer when nothing can be held out
    if (result.holdoutLength > 0) {
        const std::vector<double> train(history.begin(), history.end() - result.holdoutLength);
        const std::vector<double> test(history.end() - result.holdoutLength, history.end());
        double bestMae = std::numeric_limits<double>::infinity();
        for (int m = 1; m <= 4; ++m) {                      // in the order of ForecastMethod; a later method must beat the pick
            const MethodRun run = runMethod(ForecastMethod(m), train, seasonLength, result.holdoutLength);
            if (!run.ok) continue;
            result.holdoutPrediction[m - 1] = run.values;
            result.holdout[m - 1] = score(test, run.values);
            if (result.holdout[m - 1].mae < bestMae - EPS) { best = ForecastMethod(m); bestMae = result.holdout[m - 1].mae; }
        }
    }

    // Refit on the whole history: the forced method when it can be fitted, otherwise the best one.
    result.chosen = best;
    MethodRun run;
    if (method != ForecastMethod::Auto) {
        run = runMethod(method, history, seasonLength, horizon);
        if (run.ok) result.chosen = method;
    }
    if (!run.ok) run = runMethod(best, history, seasonLength, horizon);
    result.forecast = run.values;
    result.parameters = run.parameters;
    return result;
}
