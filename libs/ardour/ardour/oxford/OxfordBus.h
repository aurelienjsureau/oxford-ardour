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
#include "MasterTapePCM1630.h"
#include <cmath>
#include <string>

class OxfordBus
{
public:
    enum Stage { Full, Front, Tail, Dac };   // Dac = convertisseur seul (tout dernier, master)
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
    MasterTapePCM1630& masterTapeMod() { return masterTape; }   // trims In/Out du NAM (stage Dac)

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

        // ----- Dac : convertisseur OXF-R3 SEUL, tout dernier (après les plugins user) -----
        if (stage == Dac)
        {
            s = conv.processSample(s);     // micro-distorsion DAC, toujours active
            trackLevel(s);
            return s;
        }

        // ----- Tail (master) : Warmth opt -> limiteur opt -> CONVERTISSEUR -----
        // Le convertisseur est ICI (et non en bout absolu) pour être MESURABLE :
        // un plugin placé après le Tail voit ses harmoniques.
        if (stage == Tail)
        {
            if (warmthOn) s = warmth.processSample(s);
            if (limOn)    s = limiter(s);
            s = conv.processSample(s);     // micro-distorsion convertisseur OXF-R3
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

    // Charge le module MasterTape (NAM/PCM-1630) — uniquement utile sur le Tail (master).
    void loadMasterTape(double sr, int maxBlock, const std::string& namPath)
    {
        masterTape.prepare(sr, maxBlock);
        if (!namPath.empty() && masterTape.loadModel(namPath)) masterTape.setEnabled(true);
    }

    // Traitement par BLOC (NAM est block-based). Sur le Tail : warmth+limiteur (per-sample)
    // -> MasterTape PCM-1630 (NAM, bloc) -> CONVERTISSEUR (per-sample). Ordre historique réel.
    void processBlock(float* d, int n)
    {
        if (stage == Dac) {
            // module SÉPARÉ (processor visible/bypassable) : MasterTape PCM-1630 (NAM),
            // suivi d'un limiteur brickwall OPTIONNEL — le réseau NAM recrée les
            // crêtes du convertisseur du 1630 : ce limiteur est le seul point de
            // la chaîne qui les rattrape APRÈS le modèle.
            masterTape.processBlock(d, n);
            for (int i = 0; i < n; ++i) {
                double s = d[i];
                if (limOn) s = limiter(s);
                trackLevel(s);
                d[i] = (float) s;
            }
        } else if (stage == Tail) {
            // Tail master : warmth -> limiteur -> CONVERTISSEUR (le NAM est un module à part, en aval).
            for (int i = 0; i < n; ++i) {
                double s = d[i];
                if (warmthOn) s = warmth.processSample(s);
                if (limOn)    s = limiter(s);
                s = conv.processSample(s);
                trackLevel(s);
                d[i] = (float) s;
            }
        } else {
            for (int i = 0; i < n; ++i) d[i] = (float) processSample((double) d[i]);
        }
    }

    // Variante STEREO-LINK (Front/Full) : le bus-comp reçoit un sidechain
    // PARTAGÉ (max des deux canaux, en dB) -> pas de dérive d'image sur le
    // master quand le comp travaille (comportement console OXF-R3).
    double processSampleSC(double x, double scLevelDb) noexcept
    {
        if (stage == Tail || stage == Dac) { return processSample(x); }   // pas de comp sur ces étages
        double s = x;
        if (compOn) s = comp.processSample(s, scLevelDb);
        if (stage == Full)
        {
            if (warmthOn) s = warmth.processSample(s);
            if (limOn)    s = limiter(s);
            trackLevel(s);
        }
        return s;
    }
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
    MasterTapePCM1630 masterTape;   // module NAM PCM-1630 (Tail uniquement)
};
