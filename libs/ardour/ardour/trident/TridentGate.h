#pragma once
//
// TridentGate — noise gate / expander de voie (avant l'EQ, façon console).
// Portable (std/math only).
//   - détection peak avec release
//   - ouverture au-dessus du seuil, temps de HOLD, puis fermeture (release)
//   - "range" = atténuation quand fermé (pas un mute brutal)
//   - attaque/release lissés (pas de clic)
//
#include <cmath>

class TridentGate
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        env = 0.0; gain = 1.0; holdCount = 0;
        setAttackMs (attackMs); setReleaseMs (releaseMs); setHoldMs (holdMs);
        detRelease = std::exp (-1.0 / (0.050 * sampleRate)); // détecteur ~50ms
    }

    void setEnabled (bool on)     { enabled = on; }
    void setThresholdDb (double t){ thr = std::pow (10.0, t / 20.0); }
    void setRangeDb (double r)    { floorGain = std::pow (10.0, r / 20.0); } // r<0 (ex -60)
    void setAttackMs (double ms)  { attackMs = ms;  aCoef = coef (ms); }
    void setReleaseMs (double ms) { releaseMs = ms; rCoef = coef (ms); }
    void setHoldMs (double ms)    { holdMs = ms; holdSamps = (int)(ms * 0.001 * sampleRate); }

    double processSample (double x) noexcept
    {
        if (!enabled) return x;

        const double a = std::fabs (x);
        if (a > env) env = a; else env = a + (env - a) * detRelease;

        double target;
        if (env >= thr) { target = 1.0; holdCount = holdSamps; }
        else if (holdCount > 0) { target = 1.0; --holdCount; }
        else target = floorGain;

        const double c = (target > gain) ? aCoef : rCoef;  // attaque si ouvre
        gain += c * (target - gain);
        return x * gain;
    }

private:
    double coef (double ms) const
    {
        const double t = ms * 0.001 * sampleRate;
        return 1.0 - std::exp (-1.0 / (t < 1.0 ? 1.0 : t));
    }

    double sampleRate { 48000.0 };
    bool   enabled { false };
    double thr { 0.0316 };        // -30 dB
    double floorGain { 0.001 };   // -60 dB (range)
    double attackMs { 1.0 }, releaseMs { 120.0 }, holdMs { 10.0 };
    double aCoef { 0.0 }, rCoef { 0.0 }, detRelease { 0.0 };
    int    holdSamps { 0 }, holdCount { 0 };
    double env { 0.0 }, gain { 1.0 };
};
