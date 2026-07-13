#pragma once
//
// OxfordBus — étage de bus / master de la console OXF-R3 (MONO, un objet/canal).
//
// Philosophie OXF-R3 (≠ Mixbus) : la SOMMATION est 100% PROPRE (faite par Ardour,
// AUCUNE coloration de bus injectée). Les seules colorations sont :
//   - optionnelles & conscientes : bus-compressor, Warmth ;
//   - STRUCTURELLE & toujours active : la micro-distorsion du convertisseur DAC
//     (OxfordConverter), uniquement sur la sortie master (étage Tail).
//
// Scindable pour le master (plugins de l'utilisateur au milieu) :
//   Full  = bus simple : bus-comp opt + Warmth opt + limiteur opt (PAS de converter)
//   Front = bus-comp uniquement (AVANT les plugins)
//   Tail  = master (APRÈS plugins) : Warmth opt -> limiteur opt -> CONVERTISSEUR (DAC, toujours)
//
#include "OxfordDynamics.h"
#include "OxfordWarmth.h"
#include "OxfordConverter.h"
#include <cmath>

class OxfordBus
{
public:
    enum Stage { Full, Front, Tail };
    void setStage(Stage s) { stage = s; }

    void prepare(double sr)
    {
        sampleRate = sr;
        comp.prepare(sr);
        warmth.prepare(sr);
        conv.prepare(sr);
        comp.setCompEnabled(true);          // dans OxfordBus, dyn = bus-comp pur
        comp.setLimEnabled(false);
        comp.setGateEnabled(false);
        comp.setExpEnabled(false);
        level = 0.0;
        limGain = 1.0;
        limRelC = std::exp(-1.0 / (0.050 * sampleRate));
    }

    OxfordDynamics& busComp()   { return comp;   }   // Thr/Ratio/Att/Rel/Makeup, lois de timing
    OxfordWarmth&   warmthMod() { return warmth; }

    void setCompEnabled(bool on)   { compOn = on; }
    void setWarmthEnabled(bool on) { warmthOn = on; warmth.setEnabled(on); }
    void setLimiterEnabled(bool on){ limOn = on; }
    void setLimiterCeilingDb(double dB) { limCeil = std::pow(10.0, dB / 20.0); }

    float  outLevel()        const { return (float) level; }
    double gainReductionDb() const { return compOn ? comp.compReductionDb() : 0.0; }
    double limiterReductionDb() const { return (limOn && limGain < 1.0) ? -20.0 * std::log10(limGain) : 0.0; }

    double processSample(double x) noexcept
    {
        double s = x;

        // ----- Tail (master, après plugins) : Warmth opt -> limiteur opt -> DAC -----
        if (stage == Tail)
        {
            if (warmthOn) s = warmth.processSample(s);
            if (limOn)    s = limiter(s);
            s = conv.processSample(s);     // DAC OXF-R3 : micro-distorsion TOUJOURS active
            trackLevel(s);
            return s;
        }

        // ----- Front / Full : bus-comp -----
        if (compOn) s = comp.processSample(s);

        if (stage == Full)   // bus simple : sommation propre + colorations OPTIONNELLES
        {
            if (warmthOn) s = warmth.processSample(s);
            if (limOn)    s = limiter(s);
            trackLevel(s);
        }
        return s;
    }

    // Scission autour du comp pour le stereo-link bus (Front/Full)
    double preComp(double x) noexcept { return x; }                 // (somme déjà faite par Ardour)
    bool   compEnabled() const noexcept { return compOn; }

private:
    double limiter(double s) noexcept
    {
        const double mag = std::fabs(s);
        const double need = (mag > limCeil && mag > 1e-12) ? (limCeil / mag) : 1.0;
        if (need < limGain) limGain = need;
        else                limGain = limRelC * limGain + (1.0 - limRelC) * need;
        return s * limGain;
    }
    void trackLevel(double s) noexcept
    {
        const double a = std::fabs(s);
        if (a > level) level = a; else level *= 0.9997;
    }

    Stage  stage { Full };
    double sampleRate { 48000.0 };
    bool   compOn { false }, warmthOn { false }, limOn { false };
    double limCeil { 0.985 }, limGain { 1.0 }, limRelC { 0.0 };
    double level { 0.0 };

    OxfordDynamics comp;
    OxfordWarmth   warmth;
    OxfordConverter conv;     // DAC : toujours actif sur le master (Tail)
};
