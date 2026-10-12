#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <functional>

// ===========================================================================
// FORGE · CMA-ES separable (covarianza diagonal; Ros y Hansen 2008)
//
// Minimiza f(x) en una caja [lo, hi]. Determinista: el azar sale de la semilla. Con covarianza diagonal el
// costo por generación es O(n·λ) y alcanza para los 8-20 genes de una receta. Los puntos fuera de la caja se
// recortan y se penalizan por la distancia recortada (así la búsqueda no se pega al borde).
// ===========================================================================
namespace Forge
{
    struct CmaResult { std::vector<double> best; double bestValue = 1.0e300; int evaluations = 0; int generations = 0; };

    inline CmaResult cmaesMinimize (const std::function<double (const std::vector<double>&)>& f,
                                    std::vector<double> x0, const std::vector<double>& lo, const std::vector<double>& hi,
                                    double sigma0, int budget, juce::int64 seed, const std::atomic<bool>* cancel = nullptr)
    {
        const int n = (int) x0.size();
        CmaResult res;
        if (n == 0 || budget <= 0) return res;
        juce::Random rng (seed);
        auto gauss = [&rng]
        {
            // Box-Muller (determinista)
            double u1 = juce::jmax (1.0e-12, rng.nextDouble()), u2 = rng.nextDouble();
            return std::sqrt (-2.0 * std::log (u1)) * std::cos (juce::MathConstants<double>::twoPi * u2);
        };

        const int lambda = 4 + (int) std::floor (3.0 * std::log ((double) n));
        const int mu = lambda / 2;
        std::vector<double> w ((size_t) mu);
        double wsum = 0;
        for (int i = 0; i < mu; ++i) { w[(size_t) i] = std::log (mu + 0.5) - std::log (i + 1.0); wsum += w[(size_t) i]; }
        double w2 = 0; for (auto& x : w) { x /= wsum; w2 += x * x; }
        const double mueff = 1.0 / w2;
        const double cs = (mueff + 2.0) / (n + mueff + 5.0);
        const double ds = 1.0 + 2.0 * juce::jmax (0.0, std::sqrt ((mueff - 1.0) / (n + 1.0)) - 1.0) + cs;
        const double cc = (4.0 + mueff / n) / (n + 4.0 + 2.0 * mueff / n);
        // separable: tasas de aprendizaje más altas ((n + 2) / 3, Ros y Hansen)
        const double c1 = juce::jmin (1.0, 2.0 / ((n + 1.3) * (n + 1.3) + mueff) * (n + 2.0) / 3.0);
        const double cmu = juce::jmin (1.0 - c1, 2.0 * (mueff - 2.0 + 1.0 / mueff) / ((n + 2.0) * (n + 2.0) + mueff) * (n + 2.0) / 3.0);
        const double chiN = std::sqrt ((double) n) * (1.0 - 1.0 / (4.0 * n) + 1.0 / (21.0 * n * n));

        std::vector<double> m = x0, C ((size_t) n, 1.0), pc ((size_t) n, 0.0), ps ((size_t) n, 0.0);
        double sigma = sigma0;
        auto evalClamped = [&] (const std::vector<double>& x, std::vector<double>& xc)
        {
            xc = x; double pen = 0;
            for (int i = 0; i < n; ++i)
            {
                const double c = juce::jlimit (lo[(size_t) i], hi[(size_t) i], x[(size_t) i]);
                pen += (c - x[(size_t) i]) * (c - x[(size_t) i]);
                xc[(size_t) i] = c;
            }
            ++res.evaluations;
            const double v = f (xc);
            if (v < res.bestValue) { res.bestValue = v; res.best = xc; }
            return v + 10.0 * pen;
        };
        { std::vector<double> xc; evalClamped (m, xc); }

        std::vector<std::vector<double>> z ((size_t) lambda, std::vector<double> ((size_t) n)), y = z, xs = z;
        std::vector<double> fit ((size_t) lambda);
        std::vector<int> order ((size_t) lambda);
        while (res.evaluations + lambda <= budget && (cancel == nullptr || ! cancel->load()))
        {
            for (int k = 0; k < lambda; ++k)
            {
                for (int i = 0; i < n; ++i)
                {
                    z[(size_t) k][(size_t) i] = gauss();
                    y[(size_t) k][(size_t) i] = std::sqrt (C[(size_t) i]) * z[(size_t) k][(size_t) i];
                    xs[(size_t) k][(size_t) i] = m[(size_t) i] + sigma * y[(size_t) k][(size_t) i];
                }
                std::vector<double> xc;
                fit[(size_t) k] = evalClamped (xs[(size_t) k], xc);
            }
            for (int k = 0; k < lambda; ++k) order[(size_t) k] = k;
            std::sort (order.begin(), order.end(), [&] (int a, int b) { return fit[(size_t) a] < fit[(size_t) b]; });
            std::vector<double> yw ((size_t) n, 0.0), zw ((size_t) n, 0.0);
            for (int j = 0; j < mu; ++j)
                for (int i = 0; i < n; ++i)
                {
                    yw[(size_t) i] += w[(size_t) j] * y[(size_t) order[(size_t) j]][(size_t) i];
                    zw[(size_t) i] += w[(size_t) j] * z[(size_t) order[(size_t) j]][(size_t) i];
                }
            for (int i = 0; i < n; ++i) m[(size_t) i] += sigma * yw[(size_t) i];
            double psNorm = 0;
            for (int i = 0; i < n; ++i)
            {
                ps[(size_t) i] = (1 - cs) * ps[(size_t) i] + std::sqrt (cs * (2 - cs) * mueff) * zw[(size_t) i];
                psNorm += ps[(size_t) i] * ps[(size_t) i];
            }
            psNorm = std::sqrt (psNorm);
            const double hsig = psNorm / std::sqrt (1.0 - std::pow (1.0 - cs, 2.0 * (res.generations + 1))) / chiN < 1.4 + 2.0 / (n + 1.0) ? 1.0 : 0.0;
            for (int i = 0; i < n; ++i)
            {
                pc[(size_t) i] = (1 - cc) * pc[(size_t) i] + hsig * std::sqrt (cc * (2 - cc) * mueff) * yw[(size_t) i];
                double rankMu = 0;
                for (int j = 0; j < mu; ++j) { const double yy = y[(size_t) order[(size_t) j]][(size_t) i]; rankMu += w[(size_t) j] * yy * yy; }
                C[(size_t) i] = (1 - c1 - cmu) * C[(size_t) i] + c1 * (pc[(size_t) i] * pc[(size_t) i] + (1 - hsig) * cc * (2 - cc) * C[(size_t) i]) + cmu * rankMu;
                C[(size_t) i] = juce::jlimit (1.0e-8, 4.0, C[(size_t) i]);
            }
            sigma *= std::exp ((cs / ds) * (psNorm / chiN - 1.0));
            sigma = juce::jlimit (1.0e-4, 0.5, sigma);
            ++res.generations;
        }
        return res;
    }
}
