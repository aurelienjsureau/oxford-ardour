#pragma once
//
// TridentStrip — tranche canal assemblée (MONO, un objet par canal).
// Chaîne : Préampli -> EQ A-Range -> Comp CB9146.
// (La tape n'est PAS ici : elle vit sur les bus et le master, cf. TridentBus.)
//
// Conçu pour être injecté tel quel dans la chaîne de processors d'une Route
// Ardour (un TridentStrip par canal audio). Les sous-modules sont exposés
// pour brancher les Controllables/potards.
//
#include "TridentGate.h"
#include "TridentPreamp.h"
#include "TridentARangeEQ.h"
#include "TridentCB9146.h"

class TridentStrip
{
public:
    void prepare(double sr)
    {
        gate.prepare(sr);
        pre.prepare(sr);
        eq.prepare(sr);
        comp.prepare(sr);
    }

    // Accès aux sous-modules pour régler les paramètres (potards/automation)
    TridentGate&     gateModule()  { return gate; }
    TridentPreamp&   preamp()      { return pre;  }
    TridentARangeEQ& equaliser()   { return eq;   }
    TridentCB9146&   compressor()  { return comp; }

    void setGateEnabled(bool on) { gate.setEnabled(on); }
    void setCompEnabled(bool on) { compOn = on; }
    void setEQEnabled(bool on)   { eq.setEQEnabled(on); }

    double processSample(double x) noexcept
    {
        x = gate.processSample(x);   // Gate en premier (console)
        x = pre.processSample(x);
        x = eq.processSample(x);
        if (compOn) x = comp.processSample(x);
        return x;
    }

    // --- Scission autour du comp, pour le STEREO-LINK (pistes stéréo) ---
    // preComp : gate + préampli + EQ (entrée du comp). Le comp est ensuite piloté
    // depuis l'extérieur via compressor().detect/applyOutput pour coupler L/R.
    bool   compEnabled() const noexcept { return compOn; }
    double preComp(double x) noexcept
    {
        x = gate.processSample(x);
        x = pre.processSample(x);
        x = eq.processSample(x);
        return x;
    }

private:
    TridentGate     gate;  // OFF par défaut (cf. TridentGate)
    TridentPreamp   pre;
    TridentARangeEQ eq;
    TridentCB9146   comp;
    bool compOn { false }; // comp dispo mais OFF par défaut (juste la non-linéarité)
};
