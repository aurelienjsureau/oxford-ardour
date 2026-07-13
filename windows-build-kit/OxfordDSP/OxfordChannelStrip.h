#pragma once
//
// OxfordChannelStrip — tranche canal assemblée (MONO, un objet par canal),
// inspirée de la chaîne de canal OXF-R3.
//
// Chaîne (ordre OXF-R3) :
//   [Tape3348 optionnel — ON par défaut au niveau OxfordChannel (_tapeOn{true},
//    couleur 3348 d'office sur les pistes ; forcé OFF sur les bus/Returns)]
//   ->  Filtres HP/LP + EQ 5 bandes
//   ->  Dynamics (Gate/Exp/Comp/Limiter)  ->  [Warmth optionnel, OFF]
//
//   Le Tape3348 est EN PREMIER : le Sony PCM-3348 était un magnétophone
//   numérique d'ENREGISTREMENT (pas un appareil de mix). Dans le vrai workflow,
//   chaque piste passait par ses convertisseurs AD à la PRISE, donc AVANT la
//   console OXF-R3. C'est un insert PAR PISTE (≠ OxfordConverter, qui modélise
//   le DAC de la console et reste sur le master, cf. OxfordBus).
//
// Conçu pour être injecté tel quel dans la chaîne de processors d'une Route
// Ardour (un OxfordChannelStrip par canal). Sous-modules exposés pour brancher
// les Controllables/potards. Scission autour du comp pour le stereo-link.
//
#include "OxfordEQ.h"
#include "OxfordDynamics.h"
#include "OxfordWarmth.h"
#include "Tape3348Color.h"

class OxfordChannelStrip
{
public:
    void prepare(double sr)
    {
        tape.prepare(sr);
        eq.prepare(sr);
        dyn.prepare(sr);
        warmth.prepare(sr);
    }

    OxfordEQ&       equaliser() { return eq;     }
    OxfordDynamics& dynamics()  { return dyn;    }
    OxfordWarmth&   warmthMod() { return warmth; }
    Tape3348Color&  tapeColor() { return tape;   }

    void setWarmthEnabled(bool on) { warmthOn = on; warmth.setEnabled(on); }
    void setTapeEnabled(bool on)   { tapeOn = on; tape.setEnabled(on); }

    double processSample(double x) noexcept   // voie mono (sidechain auto)
    {
        return stagePost(dyn.processSample(stagePre(x)));
    }

    // --- Scission Pre | Dyn | Post pour le STEREO-LINK (OXF-R3) : les deux
    //     canaux calculent leur Pre, le max des deux niveaux post-EQ sert de
    //     sidechain COMMUN à leurs deux blocs Dyn -> image stéréo stable.
    double stagePre (double x) noexcept
    {
        if (tapeOn) x = tape.processSample(x);   // étape d'ENREGISTREMENT (3348) en premier
        return eq.processSample(x);
    }
    double stageDyn (double x, double scLevelDb) noexcept { return dyn.processSample(x, scLevelDb); }
    double stagePost(double x) noexcept { return warmthOn ? warmth.processSample(x) : x; }

private:
    Tape3348Color  tape;            // insert PAR PISTE (étape d'enregistrement 3348) ; défaut réel géré par OxfordChannel (_tapeOn)
    OxfordEQ       eq;
    OxfordDynamics dyn;
    OxfordWarmth   warmth;
    bool warmthOn { false };
    bool tapeOn   { false };
};
