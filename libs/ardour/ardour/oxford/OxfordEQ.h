#pragma once
//
// OxfordEQ — EQ paramétrique 5 bandes + filtres HP/LP à pente variable, inspiré
// du concept de l'EQ de canal de la console Sony Oxford OXF-R3 / Sonnox Oxford EQ.
//
// Implémentation DSP ORIGINALE (modèle comportemental) — aucun code tiers.
// Portable : std/math only -> réutilisable dans un fork Ardour OU un plugin JUCE.
//
// Chaîne (ordre OXF-R3) :
//   LF Filter (HP variable) -> LF (peak/shelf) -> LMF (peak) -> MF (peak)
//   -> HMF (peak) -> HF (peak/shelf) -> HF Filter (LP variable)
//
// Spécificités reproduites :
//   - 5 bandes ±20 dB, Q 0.5..16
//   - LF/HF commutables bell <-> shelf, avec "overshoot/undershoot" du shelf
//     (le Q devient une résonance de coude, comportement non-standard façon
//      EQ outboard) -> setLFShelfOvershoot / setHFShelfOvershoot (0..0.5)
//   - 4 "types" de courbe (loi de Q vs gain), d'après le guide Sonnox Oxford EQ :
//       Type1 : dépendance Gain/Q minimale, symétrique (SSL 4000 "clinique")
//       Type2 : = Type1 en boost, Q CONSTANT en cut (seul type asymétrique)
//       Type3 : dépendance modérée, symétrique (Neve / SSL G "musical")
//       Type4 : dépendance forte, aire ~constante, symétrique (mastering)
//       (le GML 8200 est une option SÉPARÉE dans le plugin, non incluse ici.)
//   - Bells "decramped" (design Orfanidis, gain prescrit à Nyquist) : les
//     cloches HF gardent leur symétrie analogique au lieu de s'écraser contre
//     Nyquist — LA signature sonore de l'EQ OXF-R3/Sonnox dans l'aigu.
//   - HP/LP à pente variable 6..36 dB/oct (cascade Butterworth + one-pole)
//   - Lissage des paramètres à débit-bloc (anti-zipper) : les fréquences/gains/Q
//     visés sont rejoints par one-pole, coeffs recalculés tous les kCoeffUpdate
//     échantillons.
//
#include <cmath>
#include <array>
#include <algorithm>

class OxfordEQ
{
public:
    enum CurveType { SSL_E = 0, Corrective, NeveG, Mastering, GML };   // GML = émulation 8200 (constant-Q symétrique, approx.)
    enum Band      { LF = 0, LMF, MF, HMF, HF };   // index des 5 bandes peak/shelf

    void prepare(double sr)
    {
        sampleRate = sr;
        for (auto& b : bands) b.reset();
        hp.reset();
        lp.reset();
        // initialise les cibles = courant (pas de glissando au démarrage)
        for (auto& b : bands) { b.curFreq = b.tgtFreq; b.curGain = b.tgtGain; b.curQ = b.tgtQ; }
        curHpF = tgtHpF; curLpF = tgtLpF;
        smoothCoef = 1.0 - std::exp(-1.0 / (0.010 * sampleRate / kCoeffUpdate)); // ~10 ms
        recomputeAll();
        ctr = 0;
        settled = false;   // un passage de convergence au 1er échantillon, puis repos
    }

    // ---- Réglages par bande (fréquence Hz, gain dB [-20,20]) ----
    // Les setters ne réveillent le recalcul de coeffs QUE si la cible change
    // réellement (ils sont re-poussés à chaque cycle par applyParams) : une fois
    // le lissage convergé, processSample ne recalcule plus rien (conso à vide).
    void setBand(Band i, double freqHz, double gainDb, double q)
    {
        auto& b = bands[(int)i];
        const double f = clampFreq(freqHz), g = clampGain(gainDb), qq = clampQ(q);
        if (f != b.tgtFreq || g != b.tgtGain || qq != b.tgtQ)
        { b.tgtFreq = f; b.tgtGain = g; b.tgtQ = qq; settled = false; }
    }
    void setBandEnabled(Band i, bool on) { bands[(int)i].on = on; }   // ne touche pas aux coeffs

    // LF/HF : bascule shelf <-> bell + montant d'overshoot (résonance de coude)
    void setLFShelf(bool shelf)           { if (bands[LF].shelf != shelf) { bands[LF].shelf = shelf; settled = false; } }
    void setHFShelf(bool shelf)           { if (bands[HF].shelf != shelf) { bands[HF].shelf = shelf; settled = false; } }
    void setLFShelfOvershoot(double o)    { o = clamp(o, 0.0, 0.5); if (o != bands[LF].overshoot) { bands[LF].overshoot = o; settled = false; } }
    void setHFShelfOvershoot(double o)    { o = clamp(o, 0.0, 0.5); if (o != bands[HF].overshoot) { bands[HF].overshoot = o; settled = false; } }

    // Type de courbe global (loi de Q vs gain)
    void setCurveType(CurveType t) { if (t != curve) { curve = t; settled = false; } }

    // Filtres HP/LP : fréquence + pente (dB/oct, arrondie au multiple de 6) + on/off
    void setHPF(double freqHz, double slopeDbOct, bool on)
    {
        const double f = clamp(freqHz, 10.0, 1000.0); const int o = slopeToOrder(slopeDbOct);
        if (on != hpOn || f != tgtHpF || o != hpOrder) { hpOn = on; tgtHpF = f; hpOrder = o; settled = false; }
    }
    void setLPF(double freqHz, double slopeDbOct, bool on)
    {
        const double f = clamp(freqHz, 1000.0, 22000.0); const int o = slopeToOrder(slopeDbOct);
        if (on != lpOn || f != tgtLpF || o != lpOrder) { lpOn = on; tgtLpF = f; lpOrder = o; settled = false; }
    }

    void setEQEnabled(bool on) { eqOn = on; }

    // ---- Réponse analytique en dB à la fréquence f (pour l'AFFICHAGE GUI) ----
    // Mêmes formules de coefficients que l'audio (Orfanidis decramp, shelf +
    // overshoot, lois de Q des 4 Types, Butterworth étagé HP/LP), calculées
    // dans des filtres LOCAUX à partir des cibles -> ne touche pas l'état
    // audio. Ce que l'écran montre = ce que le filtre fait réellement.
    double responseDb (double f)
    {
        const double wq = 2.0 * PI * clamp(f, 1.0, 0.499 * sampleRate) / sampleRate;
        double db = 0.0;
        if (eqOn) {
            if (curve == GML) {
                // parallèle réciproque : boosts = somme COMPLEXE (dry + bandes),
                // cuts = 1/(1 + k·H_bp) (miroir exact du boost)
                double re = 1.0, im = 0.0;
                for (int i = 0; i < 5; ++i) {
                    auto& b = bands[(size_t)i];
                    if (!b.on) continue;
                    Biquad bq;
                    const bool shelfBand = (i == LF || i == HF) && b.shelf;
                    const double gDb = clamp(b.tgtGain, -15.0, 15.0);
                    const double k = std::pow(10.0, std::fabs(gDb) / 20.0) - 1.0;
                    if (shelfBand) makeParaShelf (bq, b.tgtFreq, i == HF);
                    else           makeBandpass (bq, b.tgtFreq, clamp(b.tgtQ, 0.4, 4.0));
                    double hre, him;
                    biquadCplx (bq, wq, hre, him);
                    if (gDb >= 0.0) { re += k * hre; im += k * him; }
                    else {
                        db -= 10.0 * std::log10 (std::max ((1.0 + k * hre) * (1.0 + k * hre)
                                                          + (k * him) * (k * him), 1e-30));
                    }
                }
                db += 10.0 * std::log10 (std::max (re * re + im * im, 1e-30));
            } else {
                for (int i = 0; i < 5; ++i) {
                    auto& b = bands[(size_t)i];
                    if (!b.on) continue;
                    Biquad bq;
                    const bool shelfBand = (i == LF || i == HF) && b.shelf;
                    if (shelfBand) makeShelf (bq, b.tgtFreq, b.tgtGain, i == HF, b.overshoot);
                    else           makePeaking (bq, b.tgtFreq, b.tgtGain, effectiveQ (b.tgtQ, b.tgtGain));
                    db += biquadDb (bq, wq);
                }
            }
        }
        if (hpOn) { VarFilter vf; makeButterworth (vf, tgtHpF, hpOrder, true);  db += varFilterDb (vf, wq, hpOrder); }
        if (lpOn) { VarFilter vf; makeButterworth (vf, tgtLpF, lpOrder, false); db += varFilterDb (vf, wq, lpOrder); }
        return db;
    }

    double processSample(double x) noexcept
    {
        // le compteur tourne TOUJOURS (phase de lissage identique à l'original,
        // vérifié bit-à-bit) ; seul le recalcul des coeffs est sauté une fois
        // les cibles atteintes (settled) -> conso à vide quasi nulle.
        if (--ctr <= 0)
        {
            ctr = kCoeffUpdate;
            if (!settled)
            {
                smoothParams();
                if (converged()) { snapToTargets(); settled = true; }   // dernier recalcul = cibles exactes
                recomputeAll();
            }
        }

        if (hpOn) x = hp.process(x, hpOrder);
        if (eqOn) {
            if (curve == GML) {
                // GML 8200 : TOPOLOGIE PARALLÈLE RÉCIPROQUE.
                // Boost : y = x + k·BP(x)  (passe-bande sommé au dry).
                // Cut   : y = x - k·BP(y)  (MÊME passe-bande en RÉTROACTION ->
                //         la courbe de cut est le miroir exact du boost, la
                //         signature réciproque du hardware Massenburg).
                double acc = x;
                for (auto& b : bands)
                    if (b.on && b.pg > 0.0) acc += b.pg * b.bq.process(x);
                x = acc;
                for (auto& b : bands)
                    if (b.on && b.pg < 0.0) {
                        const double k = -b.pg;   // profondeur (10^{|g|/20}-1)
                        // résolution de la boucle implicite (TDF-II : BP(y)=b0·y+z1)
                        const double y = (x - k * b.bq.z1) / (1.0 + k * b.bq.b0);
                        b.bq.process (y);         // met à jour l'état avec y
                        x = y;
                    }
            } else {
                for (auto& b : bands)
                    if (b.on) x = b.bq.process(x);
            }
        }
        if (lpOn) x = lp.process(x, lpOrder);
        return x;
    }

private:
    // ------------------------------------------------------------------
    // Biquad TDF-II transposé (stable sous coeffs variables dans le temps)
    // ------------------------------------------------------------------
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        double process(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Filtre HP/LP à pente variable : jusqu'à 3 biquads Butterworth (12 dB chacun)
    // + 1 one-pole (6 dB) pour les ordres impairs. order = pente/6 (1..6).
    struct VarFilter
    {
        std::array<Biquad, 3> sec;     // sections 12 dB
        double op_b0 = 1, op_b1 = 0, op_a1 = 0, op_z = 0;  // one-pole 6 dB
        void reset() { for (auto& s : sec) s.reset(); op_z = 0; }
        double process(double x, int order) noexcept
        {
            const int nbiq = order / 2;
            for (int i = 0; i < nbiq; ++i) x = sec[(size_t)i].process(x);
            if (order & 1) { const double y = op_b0 * x + op_z; op_z = op_b1 * x - op_a1 * y; x = y; }
            return x;
        }
    };

    struct BandState
    {
        bool   on { true }, shelf { false };
        double tgtFreq { 1000.0 }, tgtGain { 0.0 }, tgtQ { 1.0 };
        double curFreq { 1000.0 }, curGain { 0.0 }, curQ { 1.0 };
        double overshoot { 0.0 };
        double pg { 0.0 };          // mode GML : gain parallèle (G-1), 0 = bande muette
        Biquad bq;
        void reset() { bq.reset(); }
    };

    static double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static double clampGain(double g) { return clamp(g, -20.0, 20.0); }
    static double clampQ(double q)    { return clamp(q, 0.5, 16.0); }   // plage Q du plugin Oxford EQ (affichée 0.5..16)
    double clampFreq(double f) const  { return clamp(f, 10.0, 0.49 * sampleRate); }
    static int    slopeToOrder(double dbOct) { int o = (int)std::lround(dbOct / 6.0); return o < 1 ? 1 : (o > 6 ? 6 : o); }

    // Loi de Q effective par TYPE de courbe — calibrée sur le plugin Sonnox Oxford EQ :
    //   Type 1/2 = Q proportionnel au gain (le bell se resserre quand on pousse),
    //              intensité croissante ; Type 3 = Q constant ; Type 4 = GML, Q constant
    //              et un peu plus large.
    double effectiveQ(double userQ, double gainDb) const
    {
        // CALIBRÉ SUR LE PLUGIN DE RÉFÉRENCE (2026-07-05) : banc de mesure VST3
        // (impulsion -> FFT, 90 points : 4 types × gains ±2..±20 × Q 0.5..16),
        // fit de chaque mesure par NOTRE cloche Orfanidis -> effQ = Q_potard × r.
        // r est exprimé dans NOTRE convention de bande passante (GB = mi-dB) ;
        // le Q du potard est multiplicatif partout (vérifié Q 0.5..16).
        const double g = std::min(std::fabs(gainDb), 20.0);
        switch (curve)
        {
            // Type 1 : Q constant dans la convention console (-3 dB sous crête) ;
            //          converti dans la nôtre : r = exp(-g/17.34) (linéaire en
            //          log sur toute la grille mesurée, résiduel ~0).
            case SSL_E:      return userQ * std::exp(-g / 17.34);
            // Type 2 : boost = Type 1 ; cut = MIROIR EXACT exp(+g/17.34) (même
            //          constante mesurée) -> cuts "notch" avec la profondeur.
            case Corrective: return gainDb >= 0.0 ? userQ * std::exp(-g / 17.34)
                                                  : userQ * std::exp( g / 17.34);
            // Type 3 : ratio CONSTANT mesuré = 0.3162 = 1/sqrt(10), stable de
            //          ±2 à ±20 dB et de Q 0.5 à 16 — le "musical" par défaut.
            case NeveG:      return userQ * 0.3162;
            // Type 4 : aire ~constante -> très large aux petits gains ; rejoint
            //          exactement le Type 3 à ±20 dB (exp(-3.46+0.115*20)=0.316).
            //          + terme affine mesuré aux petits Q (le Q effectif ne
            //          descend pas aussi bas que q*r quand le potard est < ~5).
            case Mastering:  return userQ * std::exp(-3.46 + 0.1152 * g)
                                  + 0.018 * std::max(0.0, 1.0 - (userQ - 0.5) / 5.1);
            // GML 8200 : constant-Q symétrique dans NOTRE convention (option à part,
            //            pas dans le plugin de réf. -> le mode "chirurgical" étroit).
            case GML:        return userQ;
        }
        return userQ;
    }

    // magnitude en dB d'un biquad à la pulsation normalisée w (affichage)
    static double biquadDb (const Biquad& c, double w)
    {
        const double c1 = std::cos (w), c2 = std::cos (2.0 * w);
        const double num = c.b0*c.b0 + c.b1*c.b1 + c.b2*c.b2
                         + 2.0 * (c.b0*c.b1 + c.b1*c.b2) * c1 + 2.0 * c.b0*c.b2 * c2;
        const double den = 1.0 + c.a1*c.a1 + c.a2*c.a2
                         + 2.0 * (c.a1 + c.a1*c.a2) * c1 + 2.0 * c.a2 * c2;
        return 10.0 * std::log10 (std::max (num, 1e-30) / std::max (den, 1e-30));
    }
    // réponse COMPLEXE d'un biquad à la pulsation w (mode GML : somme parallèle)
    static void biquadCplx (const Biquad& c, double w, double& re, double& im)
    {
        const double c1 = std::cos (w), s1 = std::sin (w);
        const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        // H(e^jw) = (b0 + b1 e^-jw + b2 e^-2jw) / (1 + a1 e^-jw + a2 e^-2jw)
        const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
        const double dr = 1.0  + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
        const double dd = std::max (dr * dr + di * di, 1e-30);
        re = (nr * dr + ni * di) / dd;
        im = (ni * dr - nr * di) / dd;
    }

    static double varFilterDb (const VarFilter& fl, double w, int order)
    {
        double db = 0.0;
        const int nbiq = order / 2;
        for (int i = 0; i < nbiq; ++i) db += biquadDb (fl.sec[(size_t)i], w);
        if (order & 1) {
            const double c1 = std::cos (w);
            const double num = fl.op_b0*fl.op_b0 + fl.op_b1*fl.op_b1 + 2.0*fl.op_b0*fl.op_b1*c1;
            const double den = 1.0 + fl.op_a1*fl.op_a1 + 2.0*fl.op_a1*c1;
            db += 10.0 * std::log10 (std::max (num, 1e-30) / std::max (den, 1e-30));
        }
        return db;
    }

    void smoothParams()
    {
        const double a = smoothCoef;
        for (auto& b : bands)
        {
            b.curFreq += a * (b.tgtFreq - b.curFreq);
            b.curGain += a * (b.tgtGain - b.curGain);
            b.curQ    += a * (b.tgtQ    - b.curQ);
        }
        curHpF += a * (tgtHpF - curHpF);
        curLpF += a * (tgtLpF - curLpF);
    }

    bool converged() const
    {
        for (const auto& b : bands)
        {
            if (std::fabs(b.curFreq - b.tgtFreq) > 0.01)   return false;   // Hz
            if (std::fabs(b.curGain - b.tgtGain) > 0.001)  return false;   // dB
            if (std::fabs(b.curQ    - b.tgtQ)    > 0.0005) return false;
        }
        return std::fabs(curHpF - tgtHpF) <= 0.01 && std::fabs(curLpF - tgtLpF) <= 0.01;
    }

    void snapToTargets()
    {
        for (auto& b : bands) { b.curFreq = b.tgtFreq; b.curGain = b.tgtGain; b.curQ = b.tgtQ; }
        curHpF = tgtHpF; curLpF = tgtLpF;
    }

    // ---- Calcul des coeffs ----
    // Bell "decramped" (Orfanidis : gain PRESCRIT à Nyquist, JAES 1997).
    // Un biquad bilinéaire classique (RBJ) écrase les cloches HF contre Nyquist
    // (un bell à 12 kHz @48k perd sa symétrie et sa largeur). L'EQ de l'OXF-R3
    // est précisément réputé pour NE PAS faire ça : ses bells restent conformes
    // à la réponse analogique jusqu'en haut du spectre. On force donc le gain
    // du filtre numérique à Nyquist sur celui du prototype analogique.
    void makePeaking(Biquad& bq, double fc, double gainDb, double Q)
    {
        if (std::fabs(gainDb) < 0.01)   // ~0 dB : identité (formules singulières en G=1)
        { bq.b0 = 1.0; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0; return; }

        const double G   = std::pow(10.0, gainDb / 20.0);
        const double GB  = std::pow(10.0, gainDb / 40.0);   // gain de bande passante (mi-dB)
        const double w0  = 2.0 * PI * fc / sampleRate;
        const double Dw  = w0 / Q;
        const double G2  = G * G,  GB2 = GB * GB;
        const double F   = std::max(std::fabs(G2 - GB2), 1e-12);
        const double G00 = std::max(std::fabs(G2 - 1.0),  1e-12);
        const double F00 = std::max(std::fabs(GB2 - 1.0), 1e-12);
        const double dw2 = (w0 * w0 - PI * PI) * (w0 * w0 - PI * PI);
        // gain du prototype analogique à Nyquist (cible du filtre numérique)
        const double G1  = std::sqrt((dw2 + G2 * F00 * PI * PI * Dw * Dw / F)
                                   / (dw2 +      F00 * PI * PI * Dw * Dw / F));
        const double G12 = G1 * G1;
        const double G01 = std::fabs(G2 - G1);
        const double G11 = std::max(std::fabs(G2 - G12), 1e-12);
        const double F01 = std::fabs(GB2 - G1);
        const double F11 = std::max(std::fabs(GB2 - G12), 1e-12);
        const double t0  = std::tan(w0 / 2.0);
        const double W2  = std::sqrt(G11 / G00) * t0 * t0;
        const double DW  = (1.0 + std::sqrt(F00 / F11) * W2) * std::tan(Dw / 2.0);
        const double C   = F11 * DW * DW - 2.0 * W2 * (F01 - std::sqrt(F00 * F11));
        const double D   = 2.0 * W2 * (G01 - std::sqrt(G00 * G11));
        const double A   = std::sqrt(std::max((C + D) / F, 0.0));
        const double B   = std::sqrt(std::max((G2 * C + GB2 * D) / F, 0.0));
        const double a0  = 1.0 + W2 + A;
        bq.b0 = (G1 + W2 + B)      / a0;
        bq.b1 = -2.0 * (G1 - W2)   / a0;
        bq.b2 = (G1 - B + W2)      / a0;
        bq.a1 = -2.0 * (1.0 - W2)  / a0;
        bq.a2 = (1.0 + W2 - A)     / a0;
    }

    // Shelf RBJ + "overshoot" : on injecte une résonance de coude en abaissant le S
    // effectif (pente raidie -> bosse/creux au coude, le geste 'Q' de l'OXF-R3).
    void makeShelf(Biquad& bq, double fc, double gainDb, bool high, double overshoot)
    {
        const double A  = std::pow(10.0, gainDb / 40.0);
        // Décale le coude pour que la fréquence AFFICHÉE tombe dans le PLATEAU
        // (sinon le coude RBJ ne donne que ~la moitié du gain à cette fréquence) :
        //   HF -> coude plus bas (plateau vers l'aigu) ; LF -> coude plus haut.
        double fcEff = high ? fc / 2.2 : fc * 2.2;
        fcEff = clamp(fcEff, 10.0, 0.45 * sampleRate);
        const double w0 = 2.0 * PI * fcEff / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double S  = 0.9 + 1.6 * overshoot;            // >1 -> overshoot/undershoot
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

    void makeButterworth(VarFilter& f, double fc, int order, bool high)
    {
        const double w0 = 2.0 * PI * clamp(fc, 10.0, 0.49 * sampleRate) / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const int nbiq = order / 2;
        // Q de Butterworth staggered pour une cascade d'ordre 'order'
        for (int i = 0; i < nbiq; ++i)
        {
            const double k = (2.0 * (i + 1) - 1.0);
            const double Q = 1.0 / (2.0 * std::cos(PI * k / (2.0 * order)));
            const double alpha = sw / (2.0 * Q);
            const double a0 = 1.0 + alpha;
            Biquad& bq = f.sec[(size_t)i];
            if (high)
            {
                bq.b0 = ((1.0 + cw) / 2.0) / a0;
                bq.b1 = (-(1.0 + cw)) / a0;
                bq.b2 = ((1.0 + cw) / 2.0) / a0;
            }
            else
            {
                bq.b0 = ((1.0 - cw) / 2.0) / a0;
                bq.b1 = (1.0 - cw) / a0;
                bq.b2 = ((1.0 - cw) / 2.0) / a0;
            }
            bq.a1 = (-2.0 * cw) / a0;
            bq.a2 = (1.0 - alpha) / a0;
        }
        if (order & 1)   // one-pole 6 dB
        {
            const double n = std::tan(w0 / 2.0);
            if (high) { f.op_b0 = 1.0 / (1.0 + n); f.op_b1 = -f.op_b0; f.op_a1 = (n - 1.0) / (n + 1.0); }
            else      { f.op_b0 = n / (1.0 + n);   f.op_b1 =  f.op_b0; f.op_a1 = (n - 1.0) / (n + 1.0); }
        }
    }

    void recomputeAll()
    {
        // bascule série <-> parallèle (GML) : la sémantique des biquads change,
        // on purge leurs états pour éviter un transitoire aberrant.
        if (curve != prevCurve && (curve == GML || prevCurve == GML)) {
            for (auto& b : bands) b.bq.reset();
        }
        prevCurve = curve;

        for (int i = 0; i < 5; ++i)
        {
            auto& b = bands[(size_t)i];
            const bool shelfBand = (i == LF || i == HF) && b.shelf;
            if (curve == GML)
            {
                // GML 8200 (specs hardware) : gains ±15 dB, Q 0.4..4.0,
                // bell = passe-bande parallèle, shelf = LP/HP doux parallèle.
                // pg > 0 : boost k ; pg < 0 : cut réciproque de profondeur -pg.
                const double gDb = clamp(b.curGain, -15.0, 15.0);
                const double mag = std::pow(10.0, std::fabs(gDb) / 20.0) - 1.0;
                b.pg = (gDb >= 0.0) ? mag : -mag;
                if (shelfBand) makeParaShelf(b.bq, b.curFreq, i == HF);
                else           makeBandpass(b.bq, b.curFreq, clamp(b.curQ, 0.4, 4.0));
            }
            else
            {
                b.pg = 0.0;
                if (shelfBand) makeShelf(b.bq, b.curFreq, b.curGain, i == HF, b.overshoot);
                else           makePeaking(b.bq, b.curFreq, b.curGain, effectiveQ(b.curQ, b.curGain));
            }
        }
        if (hpOn) makeButterworth(hp, curHpF, hpOrder, true);
        if (lpOn) makeButterworth(lp, curLpF, lpOrder, false);
    }

    // RBJ band-pass "constant 0 dB peak gain" — la brique du mode GML parallèle
    void makeBandpass(Biquad& bq, double fc, double Q)
    {
        const double w0 = 2.0 * PI * clamp(fc, 10.0, 0.49 * sampleRate) / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        bq.b0 = alpha / a0; bq.b1 = 0.0; bq.b2 = -alpha / a0;
        bq.a1 = (-2.0 * std::cos(w0)) / a0;
        bq.a2 = (1.0 - alpha) / a0;
    }

    // LP/HP Butterworth 2e ordre doux (Q 0.601 < 0.707 : coude "gentle" GML)
    // additionné en parallèle ×(G-1) -> shelf progressif caractéristique.
    void makeParaShelf(Biquad& bq, double fc, bool high)
    {
        const double w0 = 2.0 * PI * clamp(fc, 10.0, 0.45 * sampleRate) / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * 0.601);
        const double a0 = 1.0 + alpha;
        if (high) { bq.b0 = ((1.0 + cw) / 2.0) / a0; bq.b1 = (-(1.0 + cw)) / a0; bq.b2 = bq.b0; }
        else      { bq.b0 = ((1.0 - cw) / 2.0) / a0; bq.b1 = (1.0 - cw) / a0;    bq.b2 = bq.b0; }
        bq.a1 = (-2.0 * cw) / a0;
        bq.a2 = (1.0 - alpha) / a0;
    }

    static constexpr double PI = 3.14159265358979323846;
    static constexpr int    kCoeffUpdate = 32;   // recalcule les coeffs tous les 32 éch.

    double sampleRate { 48000.0 };
    bool   eqOn { true }, hpOn { false }, lpOn { false };
    CurveType curve { NeveG };
    CurveType prevCurve { NeveG };   // détection de bascule série<->parallèle (GML)

    std::array<BandState, 5> bands {{
        { true, true,    60.0,  0.0, 0.7 },   // LF  (shelf par défaut)
        { true, false,  250.0,  0.0, 1.0 },   // LMF
        { true, false, 1000.0,  0.0, 1.0 },   // MF
        { true, false, 4000.0,  0.0, 1.0 },   // HMF
        { true, true, 12000.0,  0.0, 0.7 },   // HF  (shelf par défaut)
    }};

    VarFilter hp, lp;
    int    hpOrder { 2 }, lpOrder { 2 };
    double tgtHpF { 80.0 }, curHpF { 80.0 };
    double tgtLpF { 18000.0 }, curLpF { 18000.0 };
    double smoothCoef { 1.0 };
    int    ctr { 0 };
    bool   settled { false };   // cibles atteintes + coeffs à jour -> processSample ne recalcule rien
};
