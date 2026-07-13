#pragma once
//
// OxfordWarmth — saturation harmonique douce "loudness" inspirée du concept
// Warmth de Sonnox Oxford (densité perçue sans monter les crêtes), orientée
// bus / canaux Return.
//
// Implémentation DSP ORIGINALE. Portable (std/math).
//
// Principe (specs publiques) :
//   - augmente la DENSITÉ des échantillons de forte valeur (loudness perçue)
//     sans augmenter les pics -> waveshaper qui remonte le ventre de l'onde
//     mais sature les crêtes (caractère tube doux + pair-harmonique).
//   - headroom interne (jusqu'à +6 dB) : le traitement absorbe les dépassements
//     transitoires sans clip dur (saturation progressive au-delà de 0 dBFS).
//   - tolérant aux signaux déjà saturés/clippés.
//   - Amount 0..100% (dosage dry/wet de la mise en forme),
//     Max Trim = calibrage fin du niveau de sortie final (indépendant).
//
#include <cmath>
#include <algorithm>

class OxfordWarmth
{
public:
    void prepare(double sr) { sampleRate = sr; }

    void setAmount(double pct)   { amount = clamp(pct, 0.0, 100.0) / 100.0; }   // 0..1
    void setMaxTrimDb(double dB) { trim = std::pow(10.0, dB / 20.0); }
    void setEnabled(bool on)     { enabled = on; }

    double processSample(double x) noexcept
    {
        if (!enabled || amount <= 0.0) return x * trim;

        // --- mise en forme "warmth" ---
        // 1) headroom interne : on travaille sur x/H (H = +6 dB) -> les dépassements
        //    > 0 dBFS rentrent dans la zone douce du shaper au lieu de clipper.
        const double H = 1.9953;                 // +6 dB
        const double xi = x / H;

        // 2) waveshaper "density + tube" :
        //    - un terme qui RELÈVE le ventre du signal (densité/loudness) :
        //      shape impair doux qui augmente le gain des petits niveaux,
        //    - une saturation paire douce (chaleur tube) sur l'ensemble,
        //    - compression de crête par tanh -> les pics ne montent pas.
        const double a   = std::fabs(xi);
        const double s   = (xi >= 0.0 ? 1.0 : -1.0);
        // densité : x^(0.75) sur l'amplitude -> remonte les niveaux moyens
        const double dens = s * std::pow(a, 0.78);
        // chaleur paire : petite asymétrie
        const double even = 0.06 * (xi * xi - a);
        double shaped = std::tanh((dens + even) * 1.0);

        // 3) restitution headroom
        shaped *= H;

        // 4) dry/wet par Amount
        double y = x + amount * (shaped - x);

        return y * trim;
    }

private:
    static double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    double sampleRate { 48000.0 };
    bool   enabled { false };
    double amount { 0.5 };     // 50 %
    double trim   { 1.0 };
};
