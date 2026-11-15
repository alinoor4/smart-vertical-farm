// Component 2 (plan section 8): the four forecasting methods, the hold-out score and the automatic choice.
// Expected numbers come from plan Appendix A (G2), from hand calculation where a comment says so, and from
// plan/reference_model.py everywhere else.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.h"
#include "forecast.h"
#include "generator.h"
#include "model.h"
#include "rng.h"

namespace {

using Series = std::vector<double>;

// Compares two vectors element by element. A macro, so a failure reports the line of the test, not of a helper.
#define CHECK_VALUES(got, want, eps) \
    do { \
        const Series got_ = (got), want_ = (want); \
        CHECK(got_.size() == want_.size()); \
        for (size_t i_ = 0; i_ < got_.size() && i_ < want_.size(); ++i_) CHECK_NEAR(got_[i_], want_[i_], (eps)); \
    } while (0)

const ForecastMethod allMethods[] = {ForecastMethod::SeasonalNaive, ForecastMethod::MovingAverage, ForecastMethod::Ses,
                                     ForecastMethod::HoltWinters};

const ForecastErrors& errorsOf(const ForecastResult& r, ForecastMethod m) { return r.holdout[int(m) - 1]; }
const Series& predictionOf(const ForecastResult& r, ForecastMethod m) { return r.holdoutPrediction[int(m) - 1]; }

// Plan Appendix A, G2: 24 weeks of a trend, a four-week pattern and a little noise.
const Series g2Series = {46.6, 37.4, 35.9, 43.0, 46.8, 38.8, 37.6, 43.3, 47.4, 41.2, 38.9, 44.0,
                         49.0, 42.3, 40.2, 44.7, 50.9, 42.9, 41.1, 46.2, 52.3, 43.4, 42.2, 47.9};

Series clipped(Series v) {
    for (double& x : v) x = std::max(0.0, x);
    return v;
}

// A varied family of series (trend and pattern with noise, random walk, drifting pattern, changing trend, noiseless),
// so that tests meet every kind of winner. Uniform noise rather than Gaussian: no log or sqrt, so identical everywhere.
Series randomSeries(Rng& rng, int n, int seasonLength) {
    Series pattern(seasonLength);
    for (double& p : pattern) p = rng.unit() * 16 - 8;
    const int kind = rng.below(5);
    double level = 50, trend = 0;
    Series y;
    for (int t = 0; t < n; ++t) {
        const double noise = rng.unit() * 2 - 1;
        double v = 0;
        if (kind == 0) v = 50 + 0.2 * t + pattern[t % seasonLength] + 2 * noise;
        else if (kind == 1) { level += 6 * noise; v = level; }
        else if (kind == 2) { pattern[t % seasonLength] += 3 * noise; v = 50 + pattern[t % seasonLength]; }
        else if (kind == 3) { trend += 0.3 * noise; level += trend; v = level + pattern[t % seasonLength]; }
        else v = 50 + 0.3 * t + pattern[t % seasonLength];
        y.push_back(std::round(std::max(v, 0.0) * 100) / 100);
    }
    return y;
}

}  // namespace

// G2: [1..8] with a season of 4 gives [5, 6, 7]. Also: the pattern repeats when the horizon is longer than a season,
// exactly one season of history is enough, and less than one season repeats the last value.
TEST(forecast_seasonal_naive) {
    const Series eight = {1, 2, 3, 4, 5, 6, 7, 8};
    const ForecastResult g2 = forecastSeries(eight, 4, 3, ForecastMethod::SeasonalNaive);
    CHECK(g2.chosen == ForecastMethod::SeasonalNaive);
    CHECK_VALUES(g2.forecast, Series({5, 6, 7}), 0);
    CHECK(g2.parameters == "L=4");

    CHECK_VALUES(forecastSeries(eight, 4, 10, ForecastMethod::SeasonalNaive).forecast, Series({5, 6, 7, 8, 5, 6, 7, 8, 5, 6}), 0);
    CHECK_VALUES(forecastSeries({1, 2, 3, 4}, 4, 5, ForecastMethod::SeasonalNaive).forecast, Series({1, 2, 3, 4, 1}), 0);

    const ForecastResult shorter = forecastSeries({1, 2, 3}, 4, 3, ForecastMethod::SeasonalNaive);
    CHECK_VALUES(shorter.forecast, Series({3, 3, 3}), 0);
    CHECK(shorter.parameters == "last value");
    CHECK(forecastSeries(eight, 4, 0, ForecastMethod::SeasonalNaive).forecast.empty());
}

// G2: [3, 5, 4, 6, 5, 7] chooses k = 4 and forecasts 5.5. The other rows are series on which each k wins, and the
// short-series rule: k = 8 and 12 only count when they leave a week to predict, and below that k is the length.
TEST(forecast_moving_average) {
    struct Case { Series y; const char* parameters; double forecast; };
    const std::vector<Case> cases = {
        {{3, 5, 4, 6, 5, 7}, "k=4", 5.5},                                                   // G2
        {{10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}, "k=4", 23.5},    // a drift: the newest weeks matter
        {{10, 14, 9, 13, 11, 15, 8, 12, 10, 14, 9, 13, 12, 11, 10, 15}, "k=12", 140.0 / 12},   // noise around a level: average a lot
        {{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8}, "k=8", 5.375},                              // 12 weeks: k = 12 has nothing to predict
        {{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9}, "k=12", 58.0 / 12},                      // 13 weeks: k = 12 has one week
        {{5, 9, 5, 9, 5, 9, 5, 9}, "k=4", 7.0},                                             // 8 weeks: k = 8 has nothing to predict
        {{1, 2, 3, 4}, "k=4", 2.5},
        {{1, 2, 3}, "k=3", 2.0},
        {{7}, "k=1", 7.0},
    };
    for (const Case& c : cases) {
        const ForecastResult r = forecastSeries(c.y, 4, 2, ForecastMethod::MovingAverage);
        CHECK(r.chosen == ForecastMethod::MovingAverage);
        CHECK(r.parameters == c.parameters);
        CHECK_VALUES(r.forecast, Series({c.forecast, c.forecast}), 1e-12);
    }
}

// G2: the recursion with alpha = 0.5 on [10, 20, 30] ends at 22.5; the grid picks alpha = 0.9 and forecasts 28.9.
TEST(forecast_ses) {
    const SesFit half = sesFit({10, 20, 30}, 0.5);
    CHECK_NEAR(half.level, 22.5, 1e-12);
    CHECK_NEAR(half.sse, 100 + 225, 1e-9);                      // (20 - 10)^2 and (30 - 15)^2

    struct Case { Series y; const char* parameters; double forecast; };
    const std::vector<Case> cases = {
        {{10, 20, 30}, "alpha=0.9", 28.9},                                                        // G2
        {{10, 10, 10, 10, 10, 10, 20, 20, 20, 20, 20, 20}, "alpha=0.9", 19.99999},               // a step: follow it quickly
        {{10, 14, 8, 13, 9, 15, 7, 12, 10, 14, 8, 13}, "alpha=0.1", 10.807091251240005},         // noise around a level: smooth hard
        {{10, 12, 10, 12, 10, 12, 10, 12, 10, 12, 10, 12}, "alpha=0.2", 11.034756136960002},
        {{5}, "alpha=0.1", 5.0},                                                                  // one week: every alpha ties, the first stays
    };
    for (const Case& c : cases) {
        const ForecastResult r = forecastSeries(c.y, 4, 3, ForecastMethod::Ses);
        CHECK(r.chosen == ForecastMethod::Ses);
        CHECK(r.parameters == c.parameters);
        CHECK_VALUES(r.forecast, Series({c.forecast, c.forecast, c.forecast}), 1e-12);
    }
}

// G2: the fit with (0.5, 0.3, 0.2) on 12 weeks, and a noiseless trend plus pattern, which the start-up must reproduce.
TEST(forecast_holt_winters_fit) {
    const HoltWintersFit g2 = holtWintersFit({12, 18, 25, 15, 14, 21, 28, 17, 16, 23, 31, 19}, 4, 0.5, 0.3, 0.2, 4);
    CHECK_VALUES(g2.forecast, Series({19.0448, 25.2662, 32.2442, 21.7855}), 1e-4);
    CHECK_NEAR(g2.sse, 6.7361, 1e-4);

    const double pattern[4] = {5, -3, -4, 2};
    Series clean, truth;
    for (int t = 0; t < 28; ++t) (t < 24 ? clean : truth).push_back(100 + 0.5 * t + pattern[t % 4]);
    CHECK_VALUES(holtWintersFit(clean, 4, 0.5, 0.3, 0.2, 4).forecast, truth, 1e-9);
    CHECK_NEAR(holtWintersFit(clean, 4, 0.5, 0.3, 0.2, 4).sse, 0, 1e-12);

    // The raw fit may go below zero; forecastSeries clips it. A falling line 40, 38, .. 18 reaches zero in week 20.
    Series falling;
    for (int t = 0; t < 12; ++t) falling.push_back(40 - 2 * t);
    const Series raw = {16, 14, 12, 10, 8, 6, 4, 2, 0, -2, -4, -6};
    CHECK_VALUES(holtWintersFit(falling, 4, 0.5, 0.3, 0.2, 12).forecast, raw, 1e-9);
    CHECK_VALUES(forecastSeries(falling, 4, 12, ForecastMethod::HoltWinters).forecast, clipped(raw), 1e-9);
}

// G2 automatic selection: hold-out of 6 weeks, the four-row error table, Holt-Winters chosen and its forecast.
TEST(forecast_automatic_selection_g2) {
    const ForecastResult r = forecastSeries(g2Series, 4, 4);
    CHECK(r.holdoutLength == 6);
    CHECK(r.chosen == ForecastMethod::HoltWinters);
    CHECK(r.parameters == "alpha=0.1 beta=0.3 gamma=0.5");
    CHECK_VALUES(r.forecast, Series({53.1549, 44.9745, 43.4390, 48.8532}), 1e-4);

    const double table[4][3] = {{1.5833, 1.8032, 3.5253}, {3.2833, 4.2963, 7.1597}, {3.2833, 3.9868, 7.1623}, {0.3302, 0.4348, 0.7042}};
    for (int m = 0; m < 4; ++m) {
        const ForecastErrors& e = errorsOf(r, allMethods[m]);
        CHECK(e.available);
        CHECK_NEAR(e.mae, table[m][0], 1e-4);
        CHECK_NEAR(e.rmse, table[m][1], 1e-4);
        CHECK_NEAR(e.smape, table[m][2], 1e-4);
    }

    // Seasonal naive trained on the first 18 weeks repeats weeks 14..17: 40.2, 44.7, 50.9, 42.9, then 40.2, 44.7.
    CHECK_VALUES(predictionOf(r, ForecastMethod::SeasonalNaive), Series({40.2, 44.7, 50.9, 42.9, 40.2, 44.7}), 1e-12);
    // Every stored prediction reproduces its error row against the last six weeks.
    for (ForecastMethod m : allMethods) {
        const Series& p = predictionOf(r, m);
        CHECK(p.size() == 6);
        double absolute = 0;
        for (size_t i = 0; i < p.size(); ++i) absolute += std::fabs(g2Series[18 + i] - p[i]);
        CHECK_NEAR(absolute / 6, errorsOf(r, m).mae, 1e-12);
    }
}

// MAE, RMSE and sMAPE worked by hand. Weeks 1..6 then 8, 12: the last two are held out. Seasonal naive (L = 4)
// predicts 3 and 4, so the misses are 5 and 8.
TEST(forecast_error_measures) {
    const ForecastResult r = forecastSeries({1, 2, 3, 4, 5, 6, 8, 12}, 4, 2, ForecastMethod::SeasonalNaive);
    CHECK(r.holdoutLength == 2);
    const ForecastErrors& e = errorsOf(r, ForecastMethod::SeasonalNaive);
    CHECK_NEAR(e.mae, 6.5, 1e-12);                              // (5 + 8) / 2
    CHECK_NEAR(e.rmse, std::sqrt(44.5), 1e-12);                 // sqrt((25 + 64) / 2)
    CHECK_NEAR(e.smape, 50 * (10.0 / 11 + 16.0 / 16), 1e-9);    // 100 / 2 * (2 * 5 / (8 + 3) + 2 * 8 / (12 + 4))

    // Actual 0 against a forecast of 5: every term is the worst possible, 2, so sMAPE is 100 / 3 * (3 * 2) = 200.
    const ForecastResult zeroActual = forecastSeries({5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 0}, 4, 1, ForecastMethod::SeasonalNaive);
    CHECK(zeroActual.holdoutLength == 3);
    CHECK_NEAR(errorsOf(zeroActual, ForecastMethod::SeasonalNaive).mae, 5, 1e-12);
    CHECK_NEAR(errorsOf(zeroActual, ForecastMethod::SeasonalNaive).smape, 200, 1e-9);

    // Actual 0 and forecast 0: an exact forecast, which must count as 0 and not divide by zero.
    const ForecastResult zeros = forecastSeries(Series(12, 0.0), 4, 3);
    for (ForecastMethod m : allMethods) {
        CHECK(errorsOf(zeros, m).available);
        CHECK(errorsOf(zeros, m).mae == 0 && errorsOf(zeros, m).rmse == 0 && errorsOf(zeros, m).smape == 0);
    }
    CHECK_VALUES(zeros.forecast, Series({0, 0, 0}), 0);
}

// How much is held out, and what a series too short to hold anything out does.
TEST(forecast_holdout_split) {
    const int lengths[][2] = {{1, 0}, {2, 0}, {3, 0}, {4, 1}, {7, 1}, {8, 2}, {11, 2}, {12, 3}, {47, 11}, {48, 12}, {49, 12}, {156, 12}};
    for (const auto& pair : lengths) {
        const ForecastResult r = forecastSeries(Series(pair[0], 5.0), 4, 2);
        CHECK(r.holdoutLength == pair[1]);
    }

    // Fewer than four weeks: nothing to score, so seasonal naive and an empty table, and a forced method still runs.
    const ForecastResult tiny = forecastSeries({3, 9, 6}, 4, 2);
    CHECK(tiny.holdoutLength == 0);
    CHECK(tiny.chosen == ForecastMethod::SeasonalNaive);
    CHECK_VALUES(tiny.forecast, Series({6, 6}), 0);
    for (ForecastMethod m : allMethods) {
        CHECK(!errorsOf(tiny, m).available);
        CHECK(predictionOf(tiny, m).empty());
    }
    const ForecastResult tinyForced = forecastSeries({3, 9, 6}, 4, 2, ForecastMethod::MovingAverage);
    CHECK(tinyForced.chosen == ForecastMethod::MovingAverage);
    CHECK_VALUES(tinyForced.forecast, Series({6, 6}), 1e-12);
}

// Holt-Winters needs two seasons, in the hold-out (training part) and on the whole history; forcing it without
// them falls back to the automatic choice.
TEST(forecast_holt_winters_availability) {
    const Series y = {12, 18, 25, 15, 14, 21, 28, 17, 16, 23, 31, 19};
    for (int n = 4; n <= 12; ++n) {
        const Series part(y.begin(), y.begin() + n);
        const int holdout = n / 4;                             // 1, 2 or 3 weeks
        const ForecastResult automatic = forecastSeries(part, 4, 3);
        CHECK(errorsOf(automatic, ForecastMethod::HoltWinters).available == (n - holdout >= 8));
        CHECK(automatic.chosen != ForecastMethod::Auto);

        const ForecastResult forced = forecastSeries(part, 4, 3, ForecastMethod::HoltWinters);
        if (n >= 8) {
            CHECK(forced.chosen == ForecastMethod::HoltWinters);
        } else {
            CHECK(forced.chosen == automatic.chosen);          // the caller turns this into a warning
            CHECK_VALUES(forced.forecast, automatic.forecast, 0);
        }
    }
    // 9 weeks: the training part has 7 (no Holt-Winters row), the whole history has 9 (Holt-Winters runs).
    const ForecastResult nine = forecastSeries(Series(y.begin(), y.begin() + 9), 4, 3, ForecastMethod::HoltWinters);
    CHECK(nine.chosen == ForecastMethod::HoltWinters);
    CHECK(!errorsOf(nine, ForecastMethod::HoltWinters).available);
    CHECK(predictionOf(nine, ForecastMethod::HoltWinters).empty());
    CHECK_VALUES(nine.forecast, Series({23.168730039153253, 30.143151818376055, 19.66983019032385}), 1e-9);
}

// A forced method skips the choice but the error table is the same as the automatic run's.
TEST(forecast_forced_method) {
    const ForecastResult automatic = forecastSeries(g2Series, 4, 4);
    for (ForecastMethod m : allMethods) {
        const ForecastResult forced = forecastSeries(g2Series, 4, 4, m);
        CHECK(forced.chosen == m);
        CHECK(forced.holdoutLength == automatic.holdoutLength);
        for (ForecastMethod k : allMethods) {
            CHECK(errorsOf(forced, k).available == errorsOf(automatic, k).available);
            CHECK(errorsOf(forced, k).mae == errorsOf(automatic, k).mae);
            CHECK(errorsOf(forced, k).rmse == errorsOf(automatic, k).rmse);
            CHECK(errorsOf(forced, k).smape == errorsOf(automatic, k).smape);
            CHECK_VALUES(predictionOf(forced, k), predictionOf(automatic, k), 0);
        }
    }
    CHECK_VALUES(forecastSeries(g2Series, 4, 4, ForecastMethod::SeasonalNaive).forecast, Series({52.3, 43.4, 42.2, 47.9}), 1e-12);
    CHECK_VALUES(forecastSeries(g2Series, 4, 4, ForecastMethod::MovingAverage).forecast, Series(4, 45.25833333333333), 1e-9);
    CHECK_VALUES(forecastSeries(g2Series, 4, 4, ForecastMethod::Ses).forecast, Series(4, 45.34605276184841), 1e-9);
    CHECK_VALUES(forecastSeries(g2Series, 4, 4, ForecastMethod::HoltWinters).forecast,
                 Series({53.15491935296339, 44.97451195284215, 43.43896482066606, 48.85318220778718}), 1e-9);
}

// A constant series: every method forecasts the constant and seasonal naive wins the tie.
TEST(forecast_constant_series) {
    const ForecastResult r = forecastSeries(Series(30, 7.5), 4, 5);
    CHECK(r.chosen == ForecastMethod::SeasonalNaive);
    CHECK_VALUES(r.forecast, Series(5, 7.5), 0);
    for (ForecastMethod m : allMethods) {
        CHECK_VALUES(forecastSeries(Series(30, 7.5), 4, 5, m).forecast, Series(5, 7.5), 1e-12);
        CHECK_NEAR(errorsOf(r, m).mae, 0, 1e-12);
    }
}

// Demand cannot be negative: forecasts and hold-out predictions of every method are clipped at zero.
TEST(forecast_clips_at_zero) {
    for (ForecastMethod m : allMethods) {
        const ForecastResult r = forecastSeries(Series(12, -5.0), 4, 3, m);   // 3 weeks held out, 9 trained on: enough for Holt-Winters
        CHECK_VALUES(r.forecast, Series({0, 0, 0}), 0);
        CHECK_VALUES(predictionOf(r, m), Series({0, 0, 0}), 0);
        CHECK_NEAR(errorsOf(r, m).mae, 5, 1e-12);              // 0 predicted against -5
    }
}

// A later method replaces the pick only when it is better by more than EPS. Scaled down to 1e-10 the series on which
// the real winners are k = 12, alpha = 0.2, (0.1, 0.3, 0.5) and Holt-Winters, every candidate is within EPS of the
// first, so the first of each grid and seasonal naive stay. At 1e-12 the sMAPE denominators fall below EPS too.
TEST(forecast_ties_keep_the_first_candidate) {
    Series tiny, tinier;
    for (double v : g2Series) { tiny.push_back(v * 1e-10); tinier.push_back(v * 1e-12); }
    CHECK(forecastSeries(tiny, 4, 2, ForecastMethod::MovingAverage).parameters == "k=4");
    CHECK(forecastSeries(tiny, 4, 2, ForecastMethod::Ses).parameters == "alpha=0.1");
    CHECK(forecastSeries(tiny, 4, 2, ForecastMethod::HoltWinters).parameters == "alpha=0.1 beta=0.01 gamma=0.05");
    CHECK(forecastSeries(tiny, 4, 2).chosen == ForecastMethod::SeasonalNaive);

    const ForecastResult r = forecastSeries(tinier, 4, 2);
    for (ForecastMethod m : allMethods) CHECK(errorsOf(r, m).smape == 0);

    // Several grid points are within EPS of the best and the first one searched wins, with alpha outermost and gamma
    // innermost. Searched with gamma outermost, this series would give alpha=0.7 beta=0.1 gamma=0.05 (found with the
    // reference model; the winner does not change when EPS moves by 10%).
    Series close;
    for (double v : {6.7, 7.0, 5.3, 1.3, 2.8, 7.2, 5.3, 8.6, 5.0, 9.0}) close.push_back(v * 3e-5);
    CHECK(forecastSeries(close, 3, 2, ForecastMethod::HoltWinters).parameters == "alpha=0.5 beta=0.3 gamma=0.05");
}

// The Holt-Winters grid searched again here with the literal values of plan section 8.2: same winner, same forecast.
// The series are chosen so that every grid value wins somewhere, which the last three checks assert.
TEST(forecast_holt_winters_grid) {
    const double alphas[] = {0.1, 0.3, 0.5, 0.7, 0.9}, betas[] = {0.01, 0.05, 0.1, 0.3}, gammas[] = {0.05, 0.1, 0.3, 0.5};
    std::set<double> wonAlpha, wonBeta, wonGamma;
    Rng rng(2027);
    for (int round = 0; round < 200; ++round) {
        const int L = 2 + rng.below(5);
        const Series y = randomSeries(rng, 2 * L + rng.below(30), L);
        double bestSse = -1, bestA = 0, bestB = 0, bestG = 0;
        for (double a : alphas)
            for (double b : betas)
                for (double g : gammas) {
                    const double sse = holtWintersFit(y, L, a, b, g, 6).sse;
                    if (bestSse < 0 || sse < bestSse - EPS) { bestSse = sse; bestA = a; bestB = b; bestG = g; }
                }
        char text[64];
        std::snprintf(text, sizeof text, "alpha=%g beta=%g gamma=%g", bestA, bestB, bestG);
        const ForecastResult r = forecastSeries(y, L, 6, ForecastMethod::HoltWinters);
        CHECK(r.parameters == text);
        CHECK_VALUES(r.forecast, clipped(holtWintersFit(y, L, bestA, bestB, bestG, 6).forecast), 1e-12);
        wonAlpha.insert(bestA); wonBeta.insert(bestB); wonGamma.insert(bestG);
    }
    CHECK(wonAlpha.size() == 5);
    CHECK(wonBeta.size() == 4);
    CHECK(wonGamma.size() == 4);
}

// Rules that must hold for any input, on random series of every length from 1 week up, seasons from 1 week up.
TEST(forecast_random_series_invariants) {
    Rng rng(2026);
    for (int round = 0; round < 300; ++round) {
        const int L = 1 + rng.below(8), n = 1 + rng.below(60), H = rng.below(15);
        const Series y = randomSeries(rng, n, L);
        const ForecastResult r = forecastSeries(y, L, H);
        const int holdout = std::min(12, n / 4);
        CHECK(r.chosen != ForecastMethod::Auto);
        CHECK(int(r.forecast.size()) == H);
        CHECK(std::all_of(r.forecast.begin(), r.forecast.end(), [](double v) { return std::isfinite(v) && v >= 0; }));
        CHECK(r.holdoutLength == holdout);

        for (ForecastMethod m : allMethods) {
            const bool expected = holdout > 0 && (m != ForecastMethod::HoltWinters || n - holdout >= 2 * L);
            CHECK(errorsOf(r, m).available == expected);
            CHECK(int(predictionOf(r, m).size()) == (expected ? holdout : 0));
            // No method is better than the chosen one by more than EPS.
            if (expected && errorsOf(r, r.chosen).available) CHECK(errorsOf(r, m).mae >= errorsOf(r, r.chosen).mae - EPS);

            const ForecastResult forced = forecastSeries(y, L, H, m);
            const bool runs = m != ForecastMethod::HoltWinters || n >= 2 * L;
            CHECK(forced.chosen == (runs ? m : r.chosen));
            CHECK(int(forced.forecast.size()) == H);
        }
    }
}

// The three precondition checks must be what throws: without the horizon check, for example, a negative horizon
// would still end in a std::length_error from the vector, which CHECK_THROWS alone would accept.
TEST(forecast_rejects_bad_arguments) {
#define CHECK_INVALID(expr) \
    do { \
        bool invalid_ = false; \
        try { expr; } catch (const std::invalid_argument&) { invalid_ = true; } catch (...) {} \
        CHECK(invalid_); \
    } while (0)
    CHECK_INVALID(forecastSeries({}, 4, 3));
    CHECK_INVALID(forecastSeries({1, 2, 3}, 0, 3));
    CHECK_INVALID(forecastSeries({1, 2, 3}, -2, 3));
    CHECK_INVALID(forecastSeries({1, 2, 3}, 4, -1));
#undef CHECK_INVALID
    CHECK(forecastSeries({1, 2, 3}, 1, 0).forecast.empty());   // a season of one week and no horizon are fine
}

// G6: on the flat scenario every history is constant, seasonal naive wins every tie and every forecast is the base demand.
TEST(forecast_flat_scenario) {
    GeneratorParams params;
    params.flat = true;
    const Scenario s = generateScenario(params);
    int longestCycle = 0;
    for (const Crop& c : s.crops) longestCycle = std::max(longestCycle, c.cycleWeeks);
    const int horizon = s.horizonWeeks - 1 + longestCycle;
    CHECK(horizon == 23);
    // The "Base demand" column of plan section 6.1, kg per week, in crop order. (G6's "Demand" column is larger
    // for crops with a contract, because the knapsack adds the contract kilograms to the spot forecast.)
    const double baseDemand[12] = {9.0, 6.0, 4.8, 4.0, 2.4, 2.7, 2.0, 2.4, 1.5, 1.35, 7.0, 3.5};
    CHECK(s.crops.size() == 12);
    for (size_t c = 0; c < s.crops.size() && c < 12; ++c) {
        const ForecastResult r = forecastSeries(s.orderHistory[c], s.seasonLength, horizon);
        CHECK(r.chosen == ForecastMethod::SeasonalNaive);
        CHECK(int(r.forecast.size()) == horizon);
        for (double v : r.forecast) CHECK_NEAR(v, baseDemand[c], 1e-12);
    }
}

// The default scenario (156 noisy weeks, season 52): the chosen method for each of the twelve crops and the first
// and last forecast week, from the reference model. All three of seasonal naive, moving average and Holt-Winters win somewhere.
TEST(forecast_default_scenario) {
    const Scenario s = generateScenario(GeneratorParams());
    struct Expected { ForecastMethod method; const char* parameters; double first, last; };
    const ForecastMethod sn = ForecastMethod::SeasonalNaive, ma = ForecastMethod::MovingAverage, hw = ForecastMethod::HoltWinters;
    const Expected expected[12] = {
        {ma, "k=4", 10.620000000, 10.620000000},                                    // Lettuce
        {hw, "alpha=0.1 beta=0.01 gamma=0.5", 5.029994442, 6.018348901},            // Baby Spinach
        {hw, "alpha=0.1 beta=0.01 gamma=0.5", 4.733571763, 5.471348615},            // Kale
        {hw, "alpha=0.1 beta=0.01 gamma=0.5", 4.285987530, 4.260767226},            // Pak Choi
        {hw, "alpha=0.1 beta=0.01 gamma=0.5", 2.093344106, 2.455517402},            // Arugula
        {sn, "L=52", 2.670000000, 2.320000000},                                     // Basil
        {hw, "alpha=0.1 beta=0.01 gamma=0.5", 2.481720960, 1.524064734},            // Mint
        {hw, "alpha=0.1 beta=0.01 gamma=0.5", 2.418699301, 2.607575948},            // Coriander
        {sn, "L=52", 1.560000000, 1.810000000},                                     // Microgreens
        {hw, "alpha=0.1 beta=0.01 gamma=0.3", 1.682043999, 1.030057384},            // Pea Shoots
        {hw, "alpha=0.1 beta=0.01 gamma=0.3", 7.333507279, 7.564049089},            // Strawberry
        {sn, "L=52", 3.860000000, 2.770000000},                                     // Cherry Tomato
    };
    CHECK(s.crops.size() == 12);
    for (size_t c = 0; c < 12; ++c) {
        const ForecastResult r = forecastSeries(s.orderHistory[c], s.seasonLength, 23);
        CHECK(r.chosen == expected[c].method);
        CHECK(r.parameters == expected[c].parameters);
        CHECK(r.forecast.size() == 23);
        CHECK_NEAR(r.forecast.front(), expected[c].first, 1e-8);
        CHECK_NEAR(r.forecast.back(), expected[c].last, 1e-8);
    }
}
