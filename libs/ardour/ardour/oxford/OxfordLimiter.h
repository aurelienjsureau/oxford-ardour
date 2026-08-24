#pragma once
//
// OxfordLimiter — limiteur de sortie du master (fin de chaîne). À ne pas
// confondre avec la section Limiter d'OxfordDynamics, qui vit dans la tranche.
// Seuil = plafond de sortie. Enhance = inflation façon Inflator.
//
#include <cmath>
#include <array>
#include <algorithm>

class OxfordLimiter
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : delay) d = 0.0;
        for (auto& q : reqDb) q = 0.0;
        writePos = 0;
        gDb = 0.0;
        holdCnt = 0;
        grMeter = 0.0;
        outPeak = 0.0;
        tpOver = 0.0;
        for (auto& z : tpZ) z = 0.0;
        infEnv = 0.0;
        acLin = 1.0;
        tpPeak = 0.0;
        for (auto& z : acZ) z = 0.0;
        build_tp_kernel ();
        recalc ();
    }

    // ---------- réglages ----------
    void setCeilingDb  (double dB) { ceilDb = clamp (dB, -20.0, 0.0); }
    void setKneeDb     (double dB) { knee   = clamp (dB, 0.0, 10.0); }   // demi-largeur
    void setAttackMs   (double ms) { attMs  = clamp (ms, 0.0, 5.0);  recalc (); }
    void setReleaseMs  (double ms) { relMs  = clamp (ms, 0.05, 3000.0); recalc (); }
    void setHoldMs     (double ms) { holdS  = (int) (clamp (ms, 0.0, 500.0) * 0.001 * sampleRate); }
    void setInputGainDb (double dB) { inGain  = std::pow (10.0, clamp (dB, -18.0, 18.0) / 20.0); }
    void setOutputTrimDb(double dB) { outTrim = std::pow (10.0, clamp (dB, -18.0,  0.0) / 20.0); }
    void setEnhance    (double pct) { enh = clamp (pct, 0.0, 125.0); }
    void setSafeMode   (bool on)    { safe = on; }
    void setAutoComp   (bool on)    { autoComp = on; if (!on) acLin = 1.0; }
    void setEnabled    (bool on)    { on_ = on; }

    bool   enabled ()          const { return on_; }
    double gainReductionDb ()  const { return grMeter; }          // >= 0
    double outputPeakDb ()     const { return 20.0 * std::log10 (outPeak > 1e-9 ? outPeak : 1e-9); }
    // Recon : dépassement des crêtes inter-échantillons au-dessus du plafond
    double reconOverDb ()      const { return tpOver; }
    // niveau de sortie en crête réelle (inter-échantillon), dBFS
    double truePeakDb ()       const { return 20.0 * std::log10 (tpPeak > 1e-9 ? tpPeak : 1e-9); }
    void   resetMeters ()            { grMeter = 0.0; outPeak = 0.0; tpOver = 0.0; }

    double processSample (double x) noexcept
    {
        if (!on_) { return x; }
        x *= inGain;

        // ---- look-ahead : on écrit l'échantillon, on lit le retardé ----
        delay[(size_t) writePos] = x;
        const double lvlDb = 20.0 * std::log10 (std::fabs (x) + 1e-12);

        // réduction requise pour CET échantillon, genou quadratique de largeur 2*knee
        const double over = lvlDb - ceilDb;
        const double W = 2.0 * knee;
        double red;                                   // >= 0
        if (W <= 0.0)              red = over > 0.0 ? over : 0.0;
        else if (over <= -W * 0.5) red = 0.0;
        else if (over >=  W * 0.5) red = over;
        else { const double t = over + W * 0.5; red = t * t / (2.0 * W); }
        reqDb[(size_t) writePos] = -red;

        // la cible est le MINIMUM des réductions requises sur la fenêtre de
        // look-ahead : c'est ce qui garantit qu'aucune crête ne passe.
        double target = 0.0;
        for (int k = 0; k <= look; ++k) {
            const int i = (writePos - k + kMaxDelay) % kMaxDelay;
            if (reqDb[(size_t) i] < target) target = reqDb[(size_t) i];
        }

        if (target < gDb) { gDb = target; holdCnt = holdS; }      // attaque : immédiate
        else if (holdCnt > 0) { --holdCnt; }
        else { gDb = target + (gDb - target) * relC; }            // release : un pôle

        const int rp = (writePos - look + kMaxDelay) % kMaxDelay;
        double y = delay[(size_t) rp] * std::pow (10.0, gDb / 20.0);
        writePos = (writePos + 1) % kMaxDelay;

        // ---- Enhance : inflation APRÈS la limitation (ordre du plugin) ----
        if (enh > 0.0 || safe) { y = inflate (y); }

        // Auto-Comp : crêtes inter-échantillons uniquement, pas les crêtes échantillon
        if (autoComp) { y = auto_comp (y); }

        y *= outTrim;

        // ---- mesures ----
        const double a = std::fabs (y);
        if (a > outPeak) outPeak = a; else outPeak *= 0.99995;
        const double g = -gDb;
        if (g > grMeter) grMeter = g; else grMeter -= 0.0004;
        if (grMeter < 0.0) grMeter = 0.0;
        trackTruePeak (y);
        return y;
    }

private:
    static double clamp (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void recalc ()
    {
        look = (int) clamp (attMs * 0.001 * sampleRate, 0.0, (double) (kMaxDelay - 1));
        // MESURÉ : la constante de temps vaut le réglage PLUS 8 ms
        infA = 1.0 - std::exp (-1.0 / (0.020 * sampleRate));   // ~20 ms
        acRel = 1.0 - std::exp (-1.0 / (0.050 * sampleRate));  // Auto-Comp : retour rapide (~50 ms)
        const double t = (relMs + 8.0) * 0.001 * sampleRate;
        relC = std::exp (-1.0 / (t < 1.0 ? 1.0 : t));
    }

    // Courbes d'inflation : kInflLo = curve -50, kInflHi = curve +50 (2u - u²).
    // « curve » interpole linéairement entre les deux.
    static double inflCurve (double u, double t)
    {
        static const double kInflLo[33] = {
            0.00000, 0.03197, 0.06531, 0.09996, 0.13569, 0.17254,
            0.21041, 0.24904, 0.28810, 0.32810, 0.36814, 0.40862,
            0.44917, 0.48955, 0.52948, 0.56941, 0.60934, 0.64641,
            0.68336, 0.72032, 0.75728, 0.78802, 0.81731, 0.84660,
            0.87588, 0.90517, 0.92472, 0.93727, 0.94981, 0.96236,
            0.97491, 0.98745, 1.00000
        };
        static const double kInflHi[33] = {
            0.00000, 0.06152, 0.12109, 0.17862, 0.23435, 0.28802,
            0.33949, 0.38902, 0.43744, 0.48235, 0.52711, 0.56784,
            0.60802, 0.64669, 0.68108, 0.71548, 0.74988, 0.77728,
            0.80440, 0.83152, 0.85864, 0.87834, 0.89630, 0.91426,
            0.93222, 0.95018, 0.96144, 0.96786, 0.97429, 0.98072,
            0.98715, 0.99357, 1.00000
        };
        const double x = clamp (u, 0.0, 1.0) * 32.0;
        const int    i = (int) x;
        const double fr = x - (double) i;
        const double lo = (i >= 32) ? kInflLo[32] : kInflLo[i] + fr * (kInflLo[i + 1] - kInflLo[i]);
        const double hi = (i >= 32) ? kInflHi[32] : kInflHi[i] + fr * (kInflHi[i + 1] - kInflHi[i]);
        return lo + (hi - lo) * clamp (t, 0.0, 1.0);
    }

    // safe OFF : courbe fixe (+50), Enhance = fondu 0..100 %.
    // safe ON  : fondu à 100 %, Enhance pilote la courbe. Identiques à 100 %.
    // Inflation normalisée sur le PLAFOND (pas 0 dBFS) : sinon elle sort au-dessus
    double inflate (double y) noexcept
    {
        const double ceilLin = std::pow (10.0, ceilDb / 20.0);
        if (ceilLin <= 1e-9) { return y; }
        const double u = std::fabs (y) / ceilLin;
        if (u <= 1e-9) { return y; }
        const double uc = u > 1.0 ? 1.0 : u;
        double shaped, mix;
        if (safe) {
            shaped = inflCurve (uc, clamp (enh / 100.0, 0.0, 1.0));
            mix = 1.0;
        } else {
            shaped = inflCurve (uc, 1.0);
            mix = clamp (enh / 100.0, 0.0, 1.0);
        }
        // inflation limitée aux transitoires : sinon elle remonte tout le programme
        double tf = transientFactor (uc);
        // 100-125 % : la courbe est au max, c'est le déclenchement qui s'élargit
        /* ouverture partielle : en grand on obtenait 20 dB au lieu de 10 */
        const double open = clamp ((enh - 100.0) / 25.0, 0.0, 1.0) * 0.25;
        tf += (1.0 - tf) * open;
        mix *= tf;
        const double v = (uc + (shaped - uc) * mix) * ceilLin;
        return (y < 0.0 ? -v : v) * (u > 1.0 ? u : 1.0);
    }

    // Auto-Comp : baisse le gain quand la crête reconstruite dépasse le plafond.
    double auto_comp (double y) noexcept
    {
        const double m   = interp_peak (y);
        /* MARGE : une interpolation cubique sous-estime systématiquement la
         * reconstruction en sinus cardinal — mesuré ~0,6 dB sur un vrai mix, et
         * densifier l'échantillonnage n'y change rien (c'est le noyau qui limite,
         * pas le nombre de points). On vise donc un peu SOUS le plafond pour que
         * le vrai pic, lui, tombe dessus. */
        /* plus de marge : le noyau sinus cardinal estime correctement, inutile
         * de sacrifier du niveau (mesuré : la marge coûtait 0,59 dB de crête). */
        const double lim = std::pow (10.0, ceilDb / 20.0);
        if (m > lim && lim > 1e-12) {
            const double need = lim / m;
            if (need < acLin) { acLin = need; }
        } else {
            acLin += (1.0 - acLin) * acRel;
            if (acLin > 1.0) { acLin = 1.0; }
        }
        /* ⚠ La crête estimée se situe entre les échantillons n-2 et n-1 : il faut
         * donc appliquer le gain À CES ÉCHANTILLONS-LÀ, pas au courant. On sort
         * donc le signal RETARDÉ de deux échantillons. Sans ça la correction
         * arrivait après coup et ne servait à rien (mesuré : 0,05 dB gagnés).
         * Coût : 2 échantillons de latence, négligeable. */
        /* la crête estimée est celle du CENTRE de la fenêtre : c'est cet
         * échantillon-là qu'il faut corriger, pas le courant. */
        return acZ[(size_t) (kTP / 2)] * acLin;
    }

    // Crête reconstruite entre deux échantillons (suréchantillonnage x4).
    double interp_peak (double y) noexcept
    {
        for (int i = kTP - 1; i > 0; --i) { acZ[(size_t) i] = acZ[(size_t) i - 1]; }
        acZ[0] = y;
        /* le point central de la fenêtre, plus les 3 phases intermédiaires */
        double best = std::fabs (acZ[(size_t) (kTP / 2)]);
        for (int p = 0; p < 3; ++p) {
            double v = 0.0;
            for (int k = 0; k < kTP; ++k) { v += acZ[(size_t) k] * tpKern[(size_t) p][(size_t) k]; }
            const double a = std::fabs (v);
            if (a > best) { best = a; }
        }
        return best;
    }

    /* Noyau de reconstruction : sinus cardinal fenêtré (Hann), 8 prises,
     * 3 phases -> suréchantillonnage x4. Une interpolation cubique sous-estime
     * de ~0,6 dB et oblige à une marge qui coûte autant de niveau. */
    void build_tp_kernel ()
    {
        for (int p = 0; p < 3; ++p) {
            const double frac = 0.25 * (double) (p + 1);
            double sum = 0.0;
            for (int k = 0; k < kTP; ++k) {
                const double t = (double) (kTP / 2) - (double) k - frac;
                double s;
                if (std::fabs (t) < 1e-9) { s = 1.0; }
                else { const double pt = PI_ * t; s = std::sin (pt) / pt; }
                const double w = 0.5 - 0.5 * std::cos (2.0 * PI_ * ((double) k + 0.5) / (double) kTP);
                tpKern[(size_t) p][(size_t) k] = s * w;
                sum += s * w;
            }
            if (sum > 1e-12) {
                for (int k = 0; k < kTP; ++k) { tpKern[(size_t) p][(size_t) k] /= sum; }
            }
        }
    }

    // Facteur de transitoire : rapport échantillon / moyenne courte.
    double transientFactor (double uc) noexcept
    {
        infEnv += (uc - infEnv) * infA;
        const double r = uc / (infEnv > 1e-6 ? infEnv : 1e-6);
        const double f = (r - 1.05) / 1.30;
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }

    // Estimateur de crête inter-échantillon : interpolation à mi-chemin sur 4
    // points (assez pour signaler un dépassement, pas pour du mastering).
    void trackTruePeak (double y) noexcept
    {
        /* MÊME estimateur 4 points que l'auto-comp : le mètre et le correcteur
         * doivent voir la même chose, sinon on corrige ce qu'on n'affiche pas. */
        tpZ[3] = tpZ[2]; tpZ[2] = tpZ[1]; tpZ[1] = tpZ[0]; tpZ[0] = y;
        const double q0 = tpZ[3], q1 = tpZ[2], q2 = tpZ[1], q3 = tpZ[0];
        double m = std::fabs (q1) > std::fabs (q2) ? std::fabs (q1) : std::fabs (q2);
        for (int k = 1; k <= 3; ++k) {
            const double t = 0.25 * (double) k;
            const double v = q1 + 0.5 * t * ((q2 - q0)
                              + t * ((2.0 * q0 - 5.0 * q1 + 4.0 * q2 - q3)
                              + t * (3.0 * (q1 - q2) + q3 - q0)));
            const double a = std::fabs (v);
            if (a > m) { m = a; }
        }
        const double lim = std::pow (10.0, ceilDb / 20.0);
        if (m > tpPeak) tpPeak = m; else tpPeak *= 0.99995;   // niveau crête réelle (mètre)
        const double ovr = (m > lim && lim > 1e-12) ? 20.0 * std::log10 (m / lim) : 0.0;
        if (ovr > tpOver) tpOver = ovr; else tpOver -= 0.0002;
        if (tpOver < 0.0) tpOver = 0.0;
    }

    static constexpr int kMaxDelay = 512;

    double sampleRate { 48000.0 };
    bool   on_ { true }, safe { false };
    double ceilDb { -0.3 }, knee { 0.0 }, attMs { 0.052 }, relMs { 7.34 }, enh { 0.0 };   /* défauts du plugin Sonnox */
    double inGain { 1.0 }, outTrim { 1.0 };
    int    look { 0 }, holdS { 0 }, holdCnt { 0 };
    double relC { 0.0 };

    std::array<double, kMaxDelay> delay {};
    std::array<double, kMaxDelay> reqDb {};
    int    writePos { 0 };
    double gDb { 0.0 };

    double infEnv { 0.0 }, infA { 0.002 };   // moyenne courte pour le facteur de transitoire
    bool   autoComp { false };
    double acLin { 1.0 }, acRel { 0.0 };
    double grMeter { 0.0 }, outPeak { 0.0 }, tpOver { 0.0 }, tpPeak { 0.0 };
    std::array<double, 4> tpZ {};
    static constexpr int kTP = 8;
    static constexpr double PI_ = 3.14159265358979323846;
    std::array<double, kTP> acZ {};
    double tpKern[3][kTP] {};
};
