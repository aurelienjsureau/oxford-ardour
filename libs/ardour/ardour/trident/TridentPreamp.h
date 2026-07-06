#pragma once
//
// TridentPreamp — étage d'entrée du canal A-Range (mic/line amp + transfo).
//
// Portable (std/math only). Comportemental, pas un modèle de composants :
//   - gain de drive
//   - bump de transformateur dans le grave (low-shelf doux)
//   - saturation asymétrique type étage transistor classe A (2e harmonique
//     dominante, un peu de 3e), DC compensée
//   - léger adoucissement HF quand on pousse (saturation des aigus)
//   - trim de sortie pour rester ~à l'unité
//
#include <cmath>

class TridentPreamp
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        lfState = 0.0;
        hfState = 0.0;
        updateCoeffs();
    }

    // 0 = propre, 1 = bien poussé (~+16 dB d'attaque dans la sat)
    void setDrive(double d) { drive = d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d); }

    double processSample(double x) noexcept
    {
        // --- Bump de transfo : low-shelf doux ~+1.5 dB sous ~120 Hz ---
        // one-pole low-pass -> partie "grave", qu'on ré-injecte légèrement
        lfState += lfCoeff * (x - lfState);
        double s = x + 0.18 * lfState;     // +~1.4 dB de grave

        // --- Gain de drive vers la saturation ---
        const double g = 1.0 + 5.0 * drive;       // x1 -> x6
        s *= g;

        // --- Saturation asymétrique (classe A) : bias -> 2e harmonique ---
        const double bias = 0.06 * drive;
        s = std::tanh(s + bias) - std::tanh(bias); // DC compensé

        // --- Adoucissement HF proportionnel au drive ---
        // mélange une version passe-bas quand on pousse (les aigus saturent)
        hfState += hfCoeff * (s - hfState);
        const double hfMix = 0.35 * drive;
        s = s * (1.0 - hfMix) + hfState * hfMix;

        // --- Trim de sortie : compense EXACTEMENT le gain petit-signal ---
        // gain petit-signal de la sat = g * sech^2(bias) ; on le divise pour
        // que l'enclenchement ne change pas le niveau (la couleur, elle, reste :
        // sat/2e harm, bump de grave et adoucissement HF sont déjà appliqués).
        const double tb = std::tanh(bias);
        s /= g * (1.0 - tb * tb);
        return s;
    }

private:
    void updateCoeffs()
    {
        // one-pole LP @ ~120 Hz pour le bump transfo
        lfCoeff = 1.0 - std::exp(-2.0 * 3.14159265358979 * 120.0 / sampleRate);
        // one-pole LP @ ~6 kHz pour l'adoucissement HF
        hfCoeff = 1.0 - std::exp(-2.0 * 3.14159265358979 * 6000.0 / sampleRate);
    }

    double sampleRate { 48000.0 };
    double drive      { 0.30 }; // légère saturation par défaut (couleur Trident subtile)
    double lfState { 0.0 }, hfState { 0.0 };
    double lfCoeff { 0.0 }, hfCoeff { 0.0 };
};
