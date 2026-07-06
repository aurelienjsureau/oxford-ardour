#pragma once
//
// TridentARangeEQ — modélisation comportementale de l'EQ du module canal
// Trident A-Range (schéma ED3162, 1975).
//
// Portable : aucune dépendance (std/math only) → réutilisable dans un plugin
// JUCE OU dans un fork Ardour.
//
// Topologie reproduite d'après le schéma :
//   - LF  : shelf grave, fréquence commutable, ±15 dB
//   - LMF : cloche bas-médium, fréquence commutable, ±15 dB  (Q proportionnel)
//   - HMF : cloche haut-médium, fréquence commutable, ±15 dB (Q proportionnel)
//   - HF  : shelf aigu, fréquence commutable, ±15 dB
//   - HPF : passe-haut commutable (section filtre dédiée)
//   - LPF : passe-bas commutable (section filtre dédiée)
//
// NOTE fréquences : jeu A-Range documenté (constantes ci-dessous). Les valeurs
// exactes des capas commutées du schéma pourront affiner ces points si on vise
// le bit-exact. Le caractère "A-Range" = cloches larges qui se resserrent avec
// le gain (proportional-Q) + shelfs doux.
//
#include <cmath>
#include <array>

class TridentARangeEQ
{
public:
    // ---- Tables de fréquences commutables (Hz) ----
    // Jeu Trident 80B : LF shelf 60/120, HF shelf 8k/12k, Lo-Mid 100->1.5k, Hi-Mid 1k->15k
    static constexpr std::array<double, 2> kLF  { 60.0, 120.0 };
    static constexpr std::array<double, 6> kLMF { 100.0, 200.0, 350.0, 600.0, 1000.0, 1500.0 };
    static constexpr std::array<double, 7> kHMF { 1000.0, 2000.0, 3300.0, 5000.0, 7200.0, 10000.0, 15000.0 };
    static constexpr std::array<double, 2> kHF  { 8000.0, 12000.0 };
    static constexpr std::array<double, 4> kHPF { 30.0, 50.0, 80.0, 120.0 };
    static constexpr std::array<double, 4> kLPF { 18000.0, 14000.0, 10000.0, 7000.0 };

    void prepare(double sr)
    {
        sampleRate = sr;
        for (auto* b : { &lf, &lmf, &hmf, &hf, &hp, &lp }) b->reset();
        updateAll();
    }

    // gainDb dans [-15, +15]. freqIndex = position du sélecteur.
    void setLF (int freqIndex, double gainDb) { lfFreq  = pick(kLF,  freqIndex); lfGain  = gainDb; updateLF();  }
    void setLMF(int freqIndex, double gainDb) { lmfFreq = pick(kLMF, freqIndex); lmfGain = gainDb; updateLMF(); }
    void setHMF(int freqIndex, double gainDb) { hmfFreq = pick(kHMF, freqIndex); hmfGain = gainDb; updateHMF(); }
    void setHF (int freqIndex, double gainDb) { hfFreq  = pick(kHF,  freqIndex); hfGain  = gainDb; updateHF();  }

    void setHPF(int freqIndex, bool on) { hpOn = on; hpFreq = pick(kHPF, freqIndex); updateHP(); }
    void setLPF(int freqIndex, bool on) { lpOn = on; lpFreq = pick(kLPF, freqIndex); updateLP(); }

    void setEQEnabled(bool on) { eqOn = on; }

    double processSample(double x) noexcept
    {
        if (hpOn) x = hp.process(x);
        if (lpOn) x = lp.process(x);
        if (eqOn)
        {
            x = lf.process(x);
            x = lmf.process(x);
            x = hmf.process(x);
            x = hf.process(x);
        }
        return x;
    }

private:
    // ------------------------------------------------------------------
    // Biquad RBJ (Direct Form I, double précision)
    // ------------------------------------------------------------------
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() { x1 = x2 = y1 = y2 = 0; }
        double process(double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    static double clampGain(double g) { return g < -15.0 ? -15.0 : (g > 15.0 ? 15.0 : g); }

    template <size_t N>
    static double pick(const std::array<double, N>& t, int i)
    {
        if (i < 0) i = 0; if (i >= (int)N) i = (int)N - 1;
        return t[(size_t)i];
    }

    // Proportional-Q : cloche large à faible gain, qui se resserre quand on
    // pousse — comportement musical caractéristique du A-Range.
    static double proportionalQ(double gainDb)
    {
        const double g = std::fabs(gainDb);
        return 0.5 + 0.045 * g;  // ~0.5 à plat → ~1.17 à ±15 dB (cloche LARGE, musicale)
    }

    void makePeaking(Biquad& bq, double fc, double gainDb, double Q)
    {
        const double A     = std::pow(10.0, clampGain(gainDb) / 40.0);
        const double w0    = 2.0 * M_PI_ * fc / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * Q);
        const double cw    = std::cos(w0);
        const double a0    = 1.0 + alpha / A;
        bq.b0 = (1.0 + alpha * A) / a0;
        bq.b1 = (-2.0 * cw) / a0;
        bq.b2 = (1.0 - alpha * A) / a0;
        bq.a1 = (-2.0 * cw) / a0;
        bq.a2 = (1.0 - alpha / A) / a0;
    }

    void makeShelf(Biquad& bq, double fc, double gainDb, bool high)
    {
        const double A   = std::pow(10.0, clampGain(gainDb) / 40.0);
        // Décale le coude pour que la fréquence AFFICHÉE soit dans le plateau
        // (sinon le coude RBJ ne donne que la moitié du gain à cette fréquence) :
        //   LF -> coude plus haut (plateau vers le grave) ; HF -> coude plus bas.
        double fcEff = high ? fc / 2.2 : fc * 2.2;
        const double fmax = 0.45 * sampleRate;
        if (fcEff > fmax) fcEff = fmax;
        if (fcEff < 10.0) fcEff = 10.0;
        const double w0  = 2.0 * M_PI_ * fcEff / sampleRate;
        const double cw  = std::cos(w0), sw = std::sin(w0);
        const double S   = 0.9;                       // pente douce (caractère A-Range)
        const double alpha = sw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double tsa = 2.0 * std::sqrt(A) * alpha;
        if (high)
        {
            const double a0 = (A + 1.0) - (A - 1.0) * cw + tsa;
            bq.b0 = (A * ((A + 1.0) + (A - 1.0) * cw + tsa)) / a0;
            bq.b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)) / a0;
            bq.b2 = (A * ((A + 1.0) + (A - 1.0) * cw - tsa)) / a0;
            bq.a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cw)) / a0;
            bq.a2 = ((A + 1.0) - (A - 1.0) * cw - tsa) / a0;
        }
        else
        {
            const double a0 = (A + 1.0) + (A - 1.0) * cw + tsa;
            bq.b0 = (A * ((A + 1.0) - (A - 1.0) * cw + tsa)) / a0;
            bq.b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cw)) / a0;
            bq.b2 = (A * ((A + 1.0) - (A - 1.0) * cw - tsa)) / a0;
            bq.a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cw)) / a0;
            bq.a2 = ((A + 1.0) + (A - 1.0) * cw - tsa) / a0;
        }
    }

    void makeButterworthHP(Biquad& bq, double fc)
    {
        const double w0 = 2.0 * M_PI_ * fc / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * 0.70710678);  // Q Butterworth
        const double a0 = 1.0 + alpha;
        bq.b0 = ((1.0 + cw) / 2.0) / a0;
        bq.b1 = (-(1.0 + cw)) / a0;
        bq.b2 = ((1.0 + cw) / 2.0) / a0;
        bq.a1 = (-2.0 * cw) / a0;
        bq.a2 = (1.0 - alpha) / a0;
    }

    void makeButterworthLP(Biquad& bq, double fc)
    {
        const double w0 = 2.0 * M_PI_ * fc / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        bq.b0 = ((1.0 - cw) / 2.0) / a0;
        bq.b1 = (1.0 - cw) / a0;
        bq.b2 = ((1.0 - cw) / 2.0) / a0;
        bq.a1 = (-2.0 * cw) / a0;
        bq.a2 = (1.0 - alpha) / a0;
    }

    void updateLF()  { makeShelf(lf,  lfFreq,  lfGain,  false); }
    void updateHF()  { makeShelf(hf,  hfFreq,  hfGain,  true ); }
    void updateLMF() { makePeaking(lmf, lmfFreq, lmfGain, proportionalQ(lmfGain)); }
    void updateHMF() { makePeaking(hmf, hmfFreq, hmfGain, proportionalQ(hmfGain)); }
    void updateHP()  { makeButterworthHP(hp, hpFreq); }
    void updateLP()  { makeButterworthLP(lp, lpFreq); }
    void updateAll() { updateLF(); updateLMF(); updateHMF(); updateHF(); updateHP(); updateLP(); }

    static constexpr double M_PI_ = 3.14159265358979323846;

    double sampleRate { 48000.0 };
    bool   eqOn { true }, hpOn { false }, lpOn { false };

    double lfFreq  { kLF[1]  }, lfGain  { 0.0 };
    double lmfFreq { kLMF[1] }, lmfGain { 0.0 };
    double hmfFreq { kHMF[1] }, hmfGain { 0.0 };
    double hfFreq  { kHF[1]  }, hfGain  { 0.0 };
    double hpFreq  { kHPF[1] };
    double lpFreq  { kLPF[0] };

    Biquad lf, lmf, hmf, hf, hp, lp;
};
