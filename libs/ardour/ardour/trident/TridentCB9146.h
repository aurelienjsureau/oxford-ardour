#pragma once
//
// TridentCB9146 — comp/limiteur Trident (schéma CB9146).
//
// Portable (std/math only). Modèle comportemental d'un compresseur FET en
// FEEDBACK (le 2N5458 en shunt fait la réduction de gain, le détecteur à pont
// de diodes lit la SORTIE) :
//   - détection feedback (sur la sortie précédente, au noeud de réduction) ->
//     caractère doux/musical, program-dependent (façon FET vintage)
//   - gain computer soft-knee avec pente COMPENSÉE (slope = ratio-1) : à
//     l'équilibre la boucle feedback redonne le ratio affiché (sinon trop doux)
//   - ratio variable
//   - attaque / release type RC (pots Attack 100k+2.2µF, Rel 470k+1µF)
//   - pincée d'harmonique 2 (caractère FET), trim "distortion"
//
#include <cmath>

class TridentCB9146
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        env = 0.0;        // niveau détecté (linéaire)
        grDb = 0.0;       // réduction de gain courante (dB, >=0)
        yPrev = 0.0;
        setAttackMs(attackMs);
        setReleaseMs(releaseMs);
    }

    void setThresholdDb(double t) { thresholdDb = t; }
    void setRatio(double r)       { ratio = r < 1.0 ? 1.0 : r; }
    void setKneeDb(double k)      { kneeDb = k < 0.0 ? 0.0 : k; }
    void setMakeupDb(double m)    { makeup = std::pow(10.0, m / 20.0); }
    void setDistortion(double d)  { fetDist = d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d); }

    void setAttackMs(double ms)   { attackMs = ms;  aCoeff = rcCoeff(ms);  }
    void setReleaseMs(double ms)  { releaseMs = ms; rCoeff = rcCoeff(ms);  }

    double getGainReductionDb() const { return grDb; } // pour un VU de GR

    // --- API scindée pour le STEREO-LINK (couplage détection L/R) ---
    double feedbackMag() const noexcept { return std::fabs(yPrev); }   // détecteur feedback
    void   setGrDb(double g) noexcept { grDb = g; }                    // imposer un GR lié

    // Détection : met à jour l'enveloppe RC depuis un niveau rectifié, calcule grDb.
    void detect(double rect) noexcept
    {
        const double coeff = (rect > env) ? aCoeff : rCoeff;
        env = coeff * env + (1.0 - coeff) * rect;

        const double levelDb = 20.0 * std::log10(env + 1e-9);
        const double over = levelDb - thresholdDb;
        /* pente COMPENSÉE pour le feedback : la boucle (détecteur sur la sortie)
         * adoucit le ratio effectif ; slope = ratio-1 -> à l'équilibre on retrouve
         * grDb = (1-1/ratio)*(in-thr), soit le ratio affiché. */
        const double slope = ratio - 1.0;
        double targetGrDb;
        if (over <= -kneeDb * 0.5)
            targetGrDb = 0.0;
        else if (over >= kneeDb * 0.5)
            targetGrDb = over * slope;
        else
        {
            const double t = (over + kneeDb * 0.5) / (kneeDb + 1e-9);
            targetGrDb = (over + kneeDb * 0.5) * 0.5 * t * slope;
        }
        grDb = targetGrDb;
    }

    // Applique le grDb courant (makeup + caractère FET).
    // yPrev = niveau au NOEUD de réduction (post-GR, PRÉ-makeup) : c'est ce que
    // lit le détecteur feedback (le makeup est un ampli de sortie en aval).
    double applyOutput(double x) noexcept
    {
        const double gain = std::pow(10.0, -grDb / 20.0);
        const double det  = x * gain;          // noeud FET (détecteur feedback)
        double y = det * makeup;               // ampli de sortie (makeup)
        const double h = 0.04 * fetDist * (grDb / 12.0);   // 2e harmonique FET
        y += h * (y * y - 0.5 * y * y * y);
        yPrev = det;
        return y;
    }

    double processSample(double x) noexcept
    {
        detect (std::fabs(yPrev));   // détection FEEDBACK (sortie précédente, noeud GR)
        return applyOutput (x);
    }

private:
    double rcCoeff(double ms) const
    {
        const double t = ms * 0.001 * sampleRate;
        return std::exp(-1.0 / (t < 1.0 ? 1.0 : t));
    }

    double sampleRate { 48000.0 };
    double thresholdDb { -18.0 };
    double ratio { 4.0 };
    double kneeDb { 6.0 };
    double makeup { 1.0 };
    double fetDist { 0.3 };
    double attackMs { 5.0 }, releaseMs { 150.0 };
    double aCoeff { 0.0 }, rCoeff { 0.0 };

    double env { 0.0 }, grDb { 0.0 }, yPrev { 0.0 };
};
