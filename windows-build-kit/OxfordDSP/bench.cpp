// bench.cpp — coût CPU réel des modules Oxford (headers shippés de l'arbre).
//
// Mesure du débit en ns/échantillon (bruit LCG déterministe, best-of-3,
// accumulation de sortie anti-DCE), converti en % d'un cœur par canal @48k.
// Configs : EQ 5 bandes actives + HP/LP (pire cas coeffs Orfanidis, recalcul
// tous les 32 éch. inclus), EQ à plat (bypass identité), Dynamics complet,
// tranche complète (Tape + EQ + Dyn + Warmth) = pire cas par canal.
//
// Compile : PATH=/c/msys64/mingw64/bin:$PATH g++ -O2 -std=c++17 bench.cpp -o bench.exe
#include <cstdio>
#include <chrono>
#include <functional>
#include "D:/Oxford/Ardour-9.7.0/libs/ardour/ardour/oxford/OxfordChannelStrip.h"

static constexpr double SR = 48000.0;

// bruit blanc déterministe (LCG), ~±1
static inline float noise (unsigned& s)
{
    s = s * 1664525u + 1013904223u;
    return (float) ((int) s) * (1.0f / 2147483648.0f);
}

static double bench (const char* name, const std::function<double(double)>& proc)
{
    const int N = 480000;             // 10 s @48k
    double best = 1e18;
    volatile double sink = 0.0;       // anti dead-code-elimination
    for (int rep = 0; rep < 3; ++rep)
    {
        unsigned seed = 12345;
        double acc = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i)
            acc += proc ((double) noise (seed));
        const auto t1 = std::chrono::steady_clock::now();
        sink += acc;
        const double ns = std::chrono::duration<double, std::nano> (t1 - t0).count() / N;
        if (ns < best) best = ns;
    }
    const double pctCore = best * 1e-9 * SR * 100.0;   // % d'un coeur par canal mono
    std::printf ("%-34s %7.1f ns/ech   %6.3f %% coeur/canal @48k\n", name, best, pctCore);
    return pctCore;
}

int main()
{
    // ---- EQ 5 bandes ACTIVES + HP/LP (pire cas Orfanidis, recalc/32 inclus) ----
    OxfordEQ eqFull;
    eqFull.setCurveType (OxfordEQ::NeveG);
    eqFull.setBand (OxfordEQ::LF,    80.0,  3.0, 0.9);
    eqFull.setBand (OxfordEQ::LMF,  250.0, -2.5, 2.8);
    eqFull.setBand (OxfordEQ::MF,  1000.0,  4.0, 2.8);
    eqFull.setBand (OxfordEQ::HMF, 4000.0, -3.0, 4.0);
    eqFull.setBand (OxfordEQ::HF, 12000.0,  5.0, 2.8);
    eqFull.setHPF (60.0, 24.0, true);
    eqFull.setLPF (18000.0, 12.0, true);
    eqFull.prepare (SR);

    // ---- EQ à PLAT (toutes bandes 0 dB -> coefficients identité) ----
    OxfordEQ eqFlat;
    eqFlat.prepare (SR);

    // ---- Dynamics complet (gate+exp+comp+lim) ----
    OxfordDynamics dyn;
    dyn.prepare (SR);
    dyn.setGateEnabled (true);  dyn.setGateThresholdDb (-45.0);
    dyn.setExpEnabled (true);   dyn.setExpThresholdDb (-35.0);
    dyn.setCompEnabled (true);  dyn.setCompThreshold (-18.0); dyn.setCompRatio (4.0);
    dyn.setLimEnabled (true);   dyn.setLimThresholdDb (-3.0);

    // ---- tranche COMPLÈTE : Tape3348 + EQ (5 bandes) + Dynamics + Warmth ----
    OxfordChannelStrip strip;
    strip.prepare (SR);
    strip.setTapeEnabled (true);
    strip.setWarmthEnabled (true);
    OxfordEQ& seq = strip.equaliser();
    seq.setBand (OxfordEQ::LF,    80.0,  3.0, 0.9);
    seq.setBand (OxfordEQ::MF,  1000.0,  4.0, 2.8);
    seq.setBand (OxfordEQ::HF, 12000.0,  5.0, 2.8);
    seq.setHPF (60.0, 24.0, true);
    OxfordDynamics& sd = strip.dynamics();
    sd.setCompEnabled (true); sd.setCompThreshold (-18.0); sd.setCompRatio (4.0);
    sd.setGateEnabled (true); sd.setGateThresholdDb (-45.0);

    std::printf ("=== cout CPU modules Oxford (headers shippes, g++ -O2, best-of-3) ===\n");
    bench ("EQ 5 bandes actives + HP24 + LP12", [&](double x){ return eqFull.processSample (x); });
    bench ("EQ a plat (bypass identite)",       [&](double x){ return eqFlat.processSample (x); });
    bench ("Dynamics Gate+Exp+Comp+Lim",        [&](double x){ return dyn.processSample (x); });
    const double strippct =
    bench ("TRANCHE complete (pire cas)",       [&](double x){ return strip.processSample (x); });

    std::printf ("\nprojection : piste stereo = 2 canaux -> %.2f %%/coeur ; "
                 "session 100 pistes stereo (pire cas) ~ %.1f %% d'UN coeur\n",
                 2.0*strippct, 200.0*strippct);
    return 0;
}
