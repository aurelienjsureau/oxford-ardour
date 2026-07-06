#pragma once
//
// TridentBus — coloration de bus + TAPE (MONO, un objet par canal de bus).
// Pour les bus de sommation ET le master (façon Mixbus : tape sur les bus).
//
// Le SUMMING lui-même est fait par Ardour (le bus somme ses entrées) ; ce
// processor colore le signal de bus déjà sommé :
//   transfo de bus (bump grave + saturation douce) -> Tape.
//
#include "TridentTape.h"
#include "TridentCB9146.h"
#include <cmath>

class TridentBus
{
public:
    // Découpe de la chaîne pour le master :
    //   Full  = tout (ampli somme + comp + tape + limiter)        -> bus simples
    //   Front = ampli somme + comp uniquement (AVANT tes plugins) -> master
    //   Tail  = tape + limiter uniquement (APRÈS tes plugins)      -> master
    enum Stage { Full, Front, Tail };
    void setStage(Stage s) { stage = s; }

    void prepare(double sr)
    {
        sampleRate = sr;
        lfState = 0.0;
        lfCoeff = 1.0 - std::exp(-2.0 * 3.14159265358979 * 100.0 / sampleRate);
        // bande passante de l'étage d'ampli de sommation (discret, ~28 kHz)
        const double fAmp = 28000.0;
        ampCoeff = 1.0 - std::exp(-2.0 * 3.14159265358979 *
                   (fAmp < 0.45 * sampleRate ? fAmp : 0.45 * sampleRate) / sampleRate);
        ampLP = 0.0;
        dcX = dcY = 0.0;
        comp.prepare(sr);
        tape.prepare(sr);
        limGain = 1.0;
        limRel = std::exp(-1.0 / (0.050 * sampleRate)); // release ~50 ms
    }

    // 0 = propre, 1 = bien chargé (transfo de bus)
    void setBusDrive(double d) { busDrive = d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d); }
    void setTapeEnabled(bool on) { tapeOn = on; }
    void setCompEnabled(bool on) { compOn = on; }
    void setLimiterEnabled(bool on) { limOn = on; }
    // MAXIMIZER : le knob = DRIVE (dB >= 0). On pousse le signal DANS le limiteur
    // (makeup) puis brickwall à un plafond fixe -> plus de drive = plus FORT + GR.
    void setLimiterCeilingDb(double dB) { double g = std::pow(10.0, dB / 20.0); limDrive = g < 1.0 ? 1.0 : g; }

    TridentTape&    tapeModule()  { return tape; }   // drive / vitesse / wow&flutter
    TridentCB9146&  compModule()  { return comp; }   // comp glue bus/master (att/rel)

    // niveau de sortie linéaire (crête avec décroissance), pour la VU tape côté GUI
    float  outLevel()        const { return (float) level; }
    double gainReductionDb() const { return compOn ? comp.getGainReductionDb() : 0.0; }
    // réduction de gain du LIMITEUR brickwall (dB >= 0), pour un VU dédié
    double limiterReductionDb() const {
        return (limOn && limGain < 1.0) ? -20.0 * std::log10(limGain) : 0.0;
    }

    // --- Scission autour du comp, pour le STEREO-LINK (modules Front/Full) ---
    // preComp : ampli de sommation + transfo + blocage DC (entrée du comp).
    double preComp(double x) noexcept
    {
        ampLP += ampCoeff * (x - ampLP);
        double s = ampLP;
        s += 0.020 * (s * s);
        lfState += lfCoeff * (x - lfState);
        s += 0.10 * lfState;
        const double g = 1.0 + 1.4 * busDrive;
        s = std::tanh(s * g) / g;
        const double o = s - dcX + 0.9995 * dcY;
        dcX = s; dcY = o;
        return o;
    }
    // postComp : tape + limiter + suivi niveau (uniquement en mode Full).
    double postComp(double s) noexcept
    {
        if (stage == Full) {
            if (tapeOn) s = tape.processSample(s);
            if (limOn)  s = limiter(s);
            trackLevel(s);
        }
        return s;
    }

    double processSample(double x) noexcept
    {
        double s = x;

        // ===== Stage Tail (master, après les plugins) : tape -> limiter =====
        if (stage == Tail) {
            if (tapeOn) s = tape.processSample(s);
            if (limOn) { s = limiter(s); }
            trackLevel(s);                 // la VU master reflète la sortie finale
            return s;
        }

        // ===== Front / Full : ampli de sommation + transfo + comp =====
        // --- 1) Étage d'ampli de sommation Trident (discret, classe A) ---
        ampLP += ampCoeff * (x - ampLP);
        s = ampLP;
        s += 0.020 * (s * s);              // 2e harmonique (glue classe A)

        // --- 2) Transfo de bus : bump grave + saturation 3e harm ---
        lfState += lfCoeff * (x - lfState);
        s += 0.10 * lfState;
        const double g = 1.0 + 1.4 * busDrive;
        s = std::tanh(s * g) / g;          // auto-makeup -> unité

        // --- 3) Blocage DC ---
        const double o = s - dcX + 0.9995 * dcY;
        dcX = s; dcY = o;
        s = o;

        if (compOn) s = comp.processSample(s);

        // En mode Full (bus simples), la tape + le limiter suivent ici.
        if (stage == Full) {
            if (tapeOn) s = tape.processSample(s);
            if (limOn) { s = limiter(s); }
            trackLevel(s);
        }
        return s;
    }

private:
    double limiter(double s) noexcept
    {
        s *= limDrive;                                            // push INTO le limiteur (makeup)
        const double mag = std::fabs(s);
        const double need = (mag > limOut && mag > 1e-9) ? (limOut / mag) : 1.0;
        if (need < limGain) limGain = need;                       // attaque immédiate
        else                limGain = limRel * limGain + (1.0 - limRel) * need; // release
        return s * limGain;
    }
    void trackLevel(double s) noexcept
    {
        const double a = std::fabs(s);
        if (a > level) level = a; else level *= 0.9997;
    }

    Stage  stage { Full };
    double sampleRate { 48000.0 };
    double busDrive   { 0.0 };
    double lfState { 0.0 }, lfCoeff { 0.0 };
    double ampLP { 0.0 }, ampCoeff { 0.0 };   // étage d'ampli de sommation
    double dcX { 0.0 }, dcY { 0.0 };           // blocage DC
    bool   tapeOn  { true };
    bool   compOn  { false };
    bool   limOn   { false };
    double limDrive { 1.0 };          // gain d'attaque dans le limiteur (>=1)
    double limOut   { 0.97 };         // plafond de sortie fixe (~ -0.27 dBFS)
    double limGain { 1.0 }, limRel { 0.0 };
    double level   { 0.0 };
    TridentCB9146 comp;
    TridentTape   tape;
};
