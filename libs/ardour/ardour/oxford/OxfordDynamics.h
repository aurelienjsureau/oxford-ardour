#pragma once
//
// OxfordDynamics — section dynamique complète inspirée de la console Sony Oxford
// OXF-R3 / Sonnox Oxford Dynamics : Gate + Expander + Compressor + Limiter.
//
// Implémentation DSP ORIGINALE (modèle comportemental). Portable (std/math).
//
// Architecture (conforme aux specs publiques) :
//   - FEED-FORWARD : le sidechain calcule le gain depuis le niveau d'ENTRÉE.
//   - Sidechain en domaine LOGARITHMIQUE (dB).
//   - Les 4 sections tournent en série ; chacune produit une réduction de gain
//     (dB <= 0) lissée par sa propre enveloppe ; la somme est appliquée au
//     signal retardé (look-ahead).
//   - 3 lois de timing (Normal / Classic / Linear) — cf. enum TimingLaw.
//   - Look-ahead commun (limiteur + pre-look expander).
//
// Plages (specs OXF-R3) :
//   Comp : Thr -60..0, loi 1/Ratio non-linéaire, Att 519µs..52ms, Hold 10ms..30s,
//          Rel 52ms..3.1s, Soft Ratio 0/-5/-10/-15/-20, Makeup +24dB
//   Limiter : Thr (réf. SORTIE), Att 100µs..500ms, Hold 50ms..30s, Rel 100ms..10s, ratio inf
//   Gate : Thr -80..0, Range 0..-80, Att 5µs..26ms, hystérésis +4dB
//   Expander : Thr -60..0, Ratio 1..16, Range 0..-80, Att 260µs..104ms, pre-look 20 éch.
//
#include <cmath>
#include <array>
#include <algorithm>

class OxfordDynamics
{
public:
    enum TimingLaw { Normal = 0, Classic, Linear };

    void prepare(double sr)
    {
        sampleRate = sr;
        for (auto& d : delay) d = 0.0;
        writePos = 0;
        gGate = gExp = gComp = gLim = 0.0;     // gains courants (dB, <=0)
        envGate = envExp = envComp = envLim = -120.0;
        gateOpen = false;
        compHoldCnt = limHoldCnt = 0;
        meterIn = -120.0;
        meterStep = 20.0 / (0.4 * sampleRate);   // afficheur : -20 dB en 400 ms
        recalcAll();
    }

    // ---------- Activations ----------
    void setGateEnabled (bool on) { gateOn = on; }
    void setExpEnabled  (bool on) { expOn  = on; }
    void setCompEnabled (bool on) { compOn = on; }
    void setLimEnabled  (bool on) { limOn  = on; }

    void setTimingLaw(TimingLaw l)
    {
        law = l;
        if (law == Classic) {                  // timings FIGÉS (mesurés sur le plugin)
            compAtt = 0.010; compRel = 0.330;  // ~10 ms / 330 ms
        }
        recalcAll();
    }

    // ---------- Compresseur ----------
    void setCompThreshold(double dB) { compThr = dB; }
    void setCompMakeupDb (double dB) { compMakeup = dB; }
    void setCompAttackMs (double ms) { compAtt = ms * 0.001; if (law != Classic) recalcComp(); }
    void setCompReleaseMs(double ms) { compRel = ms * 0.001; if (law != Classic) recalcComp(); }
    void setCompHoldMs   (double ms) { compHold = (int)(ms * 0.001 * sampleRate); }
    void setCompSoftRatioDb(double dB) { compSoft = std::fabs(dB); }   // 0/5/10/15/20 -> largeur de knee
    // Loi 1/Ratio non-linéaire : ctrl 0..1 -> 1:1@0, 2:1@0.5, 4:1@0.75, inf@1
    void setCompRatioControl(double c)
    {
        c = clamp(c, 0.0, 1.0);
        double r;
        if      (c <= 0.5)  r = 1.0 + (c / 0.5) * 1.0;          // 1 -> 2
        else if (c <= 0.75) r = 2.0 + ((c - 0.5) / 0.25) * 2.0; // 2 -> 4
        else {                                                  // 4 -> inf
            const double t = (c - 0.75) / 0.25;                 // 0..1
            r = 4.0 / std::max(1e-3, 1.0 - t);
        }
        compRatio = r;
    }
    void setCompRatio(double r) { compRatio = r < 1.0 ? 1.0 : r; }

    // ---------- Limiteur (threshold réf. SORTIE) ----------
    void setLimThresholdDb(double dB) { limThr = dB; }
    void setLimAttackMs   (double ms) { limAtt = ms * 0.001; recalcLim(); }
    void setLimReleaseMs  (double ms) { limRel = ms * 0.001; recalcLim(); }
    void setLimHoldMs     (double ms) { limHold = (int)(ms * 0.001 * sampleRate); }
    void setLookaheadMs   (double ms) { lookSamps = (int)clamp(ms * 0.001 * sampleRate, 0.0, (double)(kMaxDelay - 1)); }

    // ---------- Gate ----------
    void setGateThresholdDb(double dB) { gateThr = dB; }
    void setGateRangeDb    (double dB) { gateRange = -std::fabs(dB); }     // <=0
    void setGateAttackMs   (double ms) { gateAtt = ms * 0.001; recalcGate(); }
    void setGateReleaseMs  (double ms) { gateRel = ms * 0.001; recalcGate(); }
    void setGateHoldMs     (double ms) { gateHold = (int)(ms * 0.001 * sampleRate); }

    // ---------- Expander ----------
    void setExpThresholdDb(double dB) { expThr = dB; }
    void setExpRatio      (double r)  { expRatio = clamp(r, 1.0, 16.0); }
    void setExpRangeDb    (double dB) { expRange = -std::fabs(dB); }
    void setExpAttackMs   (double ms) { expAtt = ms * 0.001; recalcExp(); }
    void setExpReleaseMs  (double ms) { expRel = ms * 0.001; recalcExp(); }

    // ---------- Metering ----------
    double compReductionDb() const { return -gComp; }   // >=0
    double limReductionDb()  const { return -gLim;  }
    double gateReductionDb() const { return -gGate; }
    double expReductionDb()  const { return -gExp;  }
    double gateGainDb()      const { return  gGate; }
    // Niveau du sidechain (dB) avec maintien de crête + retombée lente : sert au
    // POINT MOBILE posé sur la courbe de transfert du panneau (où est le signal).
    double inputLevelDb()    const { return meterIn; }

    // true si au moins une section travaille (early-out sinon : la dynamique
    // coûte un log10 + un pow PAR ÉCHANTILLON même à vide, x N pistes)
    bool anyEnabled() const noexcept { return gateOn || expOn || compOn || limOn; }

    double processSample(double x) noexcept
    {
        if (!gateOn && !expOn && !compOn && !limOn && lookSamps == 0) {
            gGate = gExp = gComp = gLim = 0.0; compHoldCnt = limHoldCnt = 0;
            meterIn = -120.0;
            return x;   // passthrough exact (look-ahead 0 : aucun retard à préserver)
        }
        // sidechain auto (mono / non lié) : niveau du canal lui-même
        return processSample(x, 20.0 * std::log10(std::fabs(x) + 1e-12));
    }

    // levelDb = niveau sidechain FOURNI (stereo-link : max(L,R), en dB).
    // Les deux canaux d'une piste stéréo reçoivent le même levelDb -> réduction
    // identique des deux côtés, image stéréo stable (comportement console).
    double processSample(double x, double levelDb) noexcept
    {
        if (!gateOn && !expOn && !compOn && !limOn && lookSamps == 0) {
            gGate = gExp = gComp = gLim = 0.0; compHoldCnt = limHoldCnt = 0;
            meterIn = -120.0;
            return x;
        }
        // afficheur de niveau d'entrée (crête + retombée lente) — GUI seulement
        if (levelDb > meterIn) meterIn = levelDb;
        else { meterIn -= meterStep; if (meterIn < -120.0) meterIn = -120.0; }
        // --- look-ahead : on écrit x, on lit le signal retardé ---
        delay[(size_t)writePos] = x;
        const int rp = (writePos - lookSamps + kMaxDelay) % kMaxDelay;
        const double xDelayed = delay[(size_t)rp];
        writePos = (writePos + 1) % kMaxDelay;

        // --- 1) GATE (downward sous le seuil) avec hystérésis +4 dB ---
        if (gateOn)
        {
            // Mesuré sur le plugin Sonnox : la porte OUVRE à thr+4 dB et
            // FERME au seuil (fenêtre d'hystérésis de 4 dB posée AU-DESSUS du
            // seuil, pas en dessous — on ouvrait 4 dB trop tôt).
            const double openThr  = gateThr + 4.0;
            const double closeThr = gateThr;
            if (!gateOpen && levelDb >= openThr)  gateOpen = true;
            else if (gateOpen && levelDb < closeThr) gateOpen = false;
            const double target = gateOpen ? 0.0 : gateRange;
            // ouverture = "attack" (montée vers 0), fermeture = "release"
            gGate = slew(gGate, target, gateAttC, gateRelC, gateAStep, gateRStep, /*downIsRelease*/true);
        }
        else gGate = 0.0;

        // --- 2) EXPANDER (downward sous le seuil, ratio fini) ---
        if (expOn)
        {
            double target = 0.0;
            if (levelDb < expThr)
                target = std::max(expRange, (levelDb - expThr) * (expRatio - 1.0));
            gExp = slew(gExp, target, expAttC, expRelC, expAStep, expRStep, true);
        }
        else gExp = 0.0;

        // --- 3) COMPRESSEUR (downward au-dessus du seuil, loi 1/R + soft knee) ---
        if (compOn)
        {
            const double over = levelDb - compThr;
            // Soft Ratio = DEMI-largeur du genou : mesuré sur le plugin Sonnox,
            // le genou s'étend de thr-compSoft à thr+compSoft (largeur totale
            // 2·compSoft). À 0 le genou est DUR (il n'y a pas de genou doux
            // résiduel — l'ancien repli à 3 dB était une invention).
            const double knee = 2.0 * compSoft;
            const double slope = 1.0 - 1.0 / compRatio;
            double red;                                              // réduction (>=0) avant signe
            if (knee <= 0.0)              red = over > 0.0 ? over * slope : 0.0;
            else if (over <= -knee * 0.5) red = 0.0;
            else if (over >=  knee * 0.5) red = over * slope;
            else { const double t = (over + knee * 0.5); red = (t * t / (2.0 * knee)) * slope; }
            const double target = -red;                             // gain dB (<=0)
            // plus de réduction (target plus négatif) = "attack" ; HOLD : la
            // réduction est GELÉE compHold échantillons avant d'amorcer le release.
            if      (target < gComp)      { compHoldCnt = compHold;
                                            gComp = slew(gComp, target, compAttC, compRelC, compAStep, compRStep, false); }
            else if (compHoldCnt > 0)     { --compHoldCnt; }
            else                            gComp = slew(gComp, target, compAttC, compRelC, compAStep, compRStep, false);
        }
        else { gComp = 0.0; compHoldCnt = 0; }

        // --- 4) LIMITEUR (réf. SORTIE : on regarde le niveau APRÈS comp+makeup) ---
        if (limOn)
        {
            const double outDb = levelDb + gGate + gExp + gComp + (compOn ? compMakeup : 0.0);
            const double over = outDb - limThr;
            const double target = over > 0.0 ? -over : 0.0;         // ratio infini
            if      (target < gLim)       { limHoldCnt = limHold;
                                            gLim = slew(gLim, target, limAttC, limRelC, limAStep, limRStep, false); }
            else if (limHoldCnt > 0)      { --limHoldCnt; }
            else                            gLim = slew(gLim, target, limAttC, limRelC, limAStep, limRStep, false);
        }
        else { gLim = 0.0; limHoldCnt = 0; }

        // makeup UNIQUEMENT si le comp est engagé (sinon un makeup résiduel
        // s'appliquait comp éteint)
        const double totalDb = gGate + gExp + gComp + (compOn ? compMakeup : 0.0) + gLim;
        return xDelayed * std::pow(10.0, totalDb / 20.0);
    }

private:
    static double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // Coefficient RC exponentiel (Normal / Classic)
    double rc(double sec) const { const double t = sec * sampleRate; return std::exp(-1.0 / (t < 1.0 ? 1.0 : t)); }
    // Pas linéaire (Linear) : traverse kRefExc dB en 'sec' -> dB/échantillon
    double step(double sec) const { return kRefExc / ((sec < 1e-6 ? 1e-6 : sec) * sampleRate); }

    // Lissage d'un gain (dB) vers une cible selon la loi de timing.
    // downIsRelease=true : descendre (target<cur) = release (cas Gate/Expander où
    //   l'évènement "rapide" est l'ouverture = montée vers 0).
    double slew(double cur, double target, double attC, double relC,
                double attStep, double relStep, bool downIsRelease) const noexcept
    {
        const bool goingDown = target < cur;
        const bool useAttack = downIsRelease ? !goingDown : goingDown;
        if (law == Linear)
        {
            const double s = useAttack ? attStep : relStep;
            if (cur < target) return std::min(target, cur + s);
            else              return std::max(target, cur - s);
        }
        const double c = useAttack ? attC : relC;
        return target + (cur - target) * c;
    }

    void recalcComp() { compAttC = rc(compAtt); compRelC = rc(compRel); compAStep = step(compAtt); compRStep = step(compRel); }
    void recalcLim()  { limAttC  = rc(limAtt);  limRelC  = rc(limRel);  limAStep  = step(limAtt);  limRStep  = step(limRel);  }
    void recalcGate() { gateAttC = rc(gateAtt); gateRelC = rc(gateRel); gateAStep = step(gateAtt); gateRStep = step(gateRel); }
    void recalcExp()  { expAttC  = rc(expAtt);  expRelC  = rc(expRel);  expAStep  = step(expAtt);  expRStep  = step(expRel);  }
    void recalcAll()  { recalcComp(); recalcLim(); recalcGate(); recalcExp(); }

    static constexpr double kRefExc = 10.0;     // excursion de référence pour la loi Linear (dB, mesurée sur le plugin)
    static constexpr int    kMaxDelay = 4096;   // look-ahead max (~85 ms @48k)

    double sampleRate { 48000.0 };
    TimingLaw law { Normal };

    // activations
    bool gateOn { false }, expOn { false }, compOn { false }, limOn { false };

    // look-ahead
    std::array<double, kMaxDelay> delay {};
    int writePos { 0 }, lookSamps { 0 };

    // --- comp ---
    double compThr { -18.0 }, compRatio { 4.0 }, compMakeup { 0.0 }, compSoft { 0.0 };
    double compAtt { 0.010 }, compRel { 0.150 }; int compHold { 0 }, compHoldCnt { 0 };
    double compAttC { 0 }, compRelC { 0 }, compAStep { 0 }, compRStep { 0 };

    // --- limiteur ---
    double limThr { -0.3 };
    double limAtt { 0.0003 }, limRel { 0.150 }; int limHold { 0 }, limHoldCnt { 0 };
    double limAttC { 0 }, limRelC { 0 }, limAStep { 0 }, limRStep { 0 };

    // --- gate ---
    double gateThr { -45.0 }, gateRange { -80.0 };
    double gateAtt { 0.0005 }, gateRel { 0.120 }; int gateHold { 0 };
    double gateAttC { 0 }, gateRelC { 0 }, gateAStep { 0 }, gateRStep { 0 };
    bool   gateOpen { false };

    // --- expander ---
    double expThr { -40.0 }, expRatio { 2.0 }, expRange { -40.0 };
    double expAtt { 0.001 }, expRel { 0.120 };
    double expAttC { 0 }, expRelC { 0 }, expAStep { 0 }, expRStep { 0 };

    // gains courants (dB, <=0) + enveloppes
    double gGate { 0 }, gExp { 0 }, gComp { 0 }, gLim { 0 };
    double envGate { -120 }, envExp { -120 }, envComp { -120 }, envLim { -120 };

    // afficheur de niveau d'entrée (GUI) : crête + retombée linéaire en dB
    double meterIn { -120.0 }, meterStep { 0.001 };
};
