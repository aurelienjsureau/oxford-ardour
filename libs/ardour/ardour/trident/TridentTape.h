#pragma once
//
// TridentTape — émulation bande pour le bus/master (style Mixbus "tape").
//
// Portable (std/math only). Chaîne comportementale :
//   input -> drive + saturation douce (compression de crête) -> perte HF
//   (dépend de la vitesse) -> bosse de tête LF (résonance grave) ->
//   wow & flutter (delay fractionnaire modulé).
//
#include <cmath>
#include <array>
#include <algorithm>

class TridentTape
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        hfState = 0.0;
        bump.reset();
        delayBuf.fill(0.0f);
        writePos = 0;
        wowPhase = fl1Phase = fl2Phase = 0.0;
        updateCoeffs();
    }

    void setDrive(double d)      { drive = clamp01(d); }          // 0..1
    void setSpeedIPS(double ips) { if (ips != speedIPS) { speedIPS = ips; updateCoeffs(); } } // 15 ou 30
    void setWowFlutter(double w) { wf = clamp01(w); }             // 0..1
    // Headroom (master tape) en dB : 0 = neutre (son d'origine). >0 = sature plus
    // tard à volume constant (atténue l'entrée du tanh + restitue le gain en sortie).
    void setHeadroomDb(double dB) { headroom = std::pow(10.0, dB / 20.0); }

    double processSample(double x) noexcept
    {
        // --- Drive + saturation bande ---
        // On POUSSE le signal dans la saturation (g) puis on compense le volume
        // au NIVEAU NOMINAL (~ -12 dBFS) : makeup = a0/tanh(g*a0). Ainsi le drive
        // densifie/sature (harmoniques + compression de crête) SANS perte de volume
        // (un /g simple faisait chuter le niveau jusqu'à -10 dB à fond -> "inaudible").
        // NB : le master tape applique en plus un 'headroom' (atténue l'entrée du tanh
        // + makeup) — cf. setHeadroomDb, neutre par défaut (0 dB) -> son d'origine.
        const double g  = 1.0 + 8.0 * drive;
        const double a0 = 0.25;                                 // niveau nominal (-12 dBFS)
        const double mk = a0 / std::tanh (g * a0 + 1e-9);       // makeup -> unité au nominal
        // headroom : /headroom RELÈVE le seuil de saturation (le tanh est attaqué
        // plus doucement), *headroom le restitue -> sature plus tard, MÊME volume.
        double s = std::tanh (x * g / headroom);               // saturation/compression
        s += 0.10 * drive * (s * s - s * s * s * s);           // 2e harmonique (chaleur), bornée
        s *= headroom * mk;                                     // restitution headroom + makeup

        // --- Perte HF (gap loss) : one-pole LP, fréquence selon vitesse ---
        hfState += hfCoeff * (s - hfState);
        s = hfState;

        // --- Bosse de tête (résonance LF) ---
        s = bump.process(s);

        // --- Wow & flutter : delay fractionnaire modulé ---
        const double depth = wf * 22.0;    // profondeur de modulation (échantillons)
        const double mod =
              0.6 * std::sin(wowPhase)     // wow lent
            + 0.3 * std::sin(fl1Phase)     // flutter
            + 0.1 * std::sin(fl2Phase);    // flutter haut
        advancePhases();

        const double readPos = (double)writePos - (baseDelay + depth * mod);
        const double y = readInterp(readPos);
        delayBuf[(size_t)writePos] = (float)s;
        writePos = (writePos + 1) % (int)delayBuf.size();
        return y;
    }

private:
    static double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    struct Biquad
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
        void reset(){ x1=x2=y1=y2=0; }
        double process(double x) noexcept
        {
            const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
            x2=x1;x1=x;y2=y1;y1=y;return y;
        }
    };

    void updateCoeffs()
    {
        // Perte HF : 30 ips -> ~20 kHz, 15 ips -> ~12 kHz
        const double fc = (speedIPS >= 30.0) ? 20000.0 : 12000.0;
        hfCoeff = 1.0 - std::exp(-2.0 * PI * std::min(fc, 0.45 * sampleRate) / sampleRate);

        // Bosse de tête : peaking subtil ~+1.2 dB. 30 ips -> ~50 Hz, 15 ips -> ~80 Hz.
        const double f0 = (speedIPS >= 30.0) ? 50.0 : 80.0;
        makePeaking(bump, f0, 1.2, 1.1);
    }

    void makePeaking(Biquad& bq, double fc, double gainDb, double Q)
    {
        const double A=std::pow(10.0,gainDb/40.0);
        const double w0=2.0*PI*fc/sampleRate;
        const double alpha=std::sin(w0)/(2.0*Q);
        const double cw=std::cos(w0);
        const double a0=1.0+alpha/A;
        bq.b0=(1.0+alpha*A)/a0; bq.b1=(-2.0*cw)/a0; bq.b2=(1.0-alpha*A)/a0;
        bq.a1=(-2.0*cw)/a0; bq.a2=(1.0-alpha/A)/a0;
    }

    void advancePhases()
    {
        wowPhase += 2.0*PI*0.7  / sampleRate;   // ~0.7 Hz
        fl1Phase += 2.0*PI*6.3  / sampleRate;   // ~6.3 Hz
        fl2Phase += 2.0*PI*11.0 / sampleRate;   // ~11 Hz
        if (wowPhase > 2.0*PI) wowPhase -= 2.0*PI;
        if (fl1Phase > 2.0*PI) fl1Phase -= 2.0*PI;
        if (fl2Phase > 2.0*PI) fl2Phase -= 2.0*PI;
    }

    double readInterp(double pos) noexcept
    {
        const int N=(int)delayBuf.size();
        while (pos < 0.0) pos += N;
        const int i0=(int)pos;
        const double f=pos-i0;
        const float a=delayBuf[(size_t)(i0 % N)];
        const float b=delayBuf[(size_t)((i0+1) % N)];
        return (double)a + f*((double)b-(double)a);
    }

    static constexpr double PI = 3.14159265358979323846;

    double sampleRate { 48000.0 };
    double drive { 0.0 }, speedIPS { 30.0 }, wf { 0.0 };
    double headroom { 1.0 };   // 1.0 = 0 dB (neutre) ; >1 = seuil de sat relevé
    double hfState { 0.0 }, hfCoeff { 0.0 };
    Biquad bump;

    std::array<float, 2048> delayBuf {};
    int    writePos { 0 };
    static constexpr double baseDelay = 64.0;  // delai de base (échantillons)
    double wowPhase { 0.0 }, fl1Phase { 0.0 }, fl2Phase { 0.0 };
};
