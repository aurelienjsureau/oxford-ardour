#pragma once
//
// OxfordConverter — micro-distorsion RÉSIDUELLE de convertisseur AD/DA OXF-R3,
// TOUJOURS ACTIVE (structurelle, non bypassable) sur la sortie master (DAC).
//
// Philosophie OXF-R3 : la sommation interne est 32-bit PURE (aucune coloration
// de bus, contrairement à Mixbus). La seule distorsion "subie" est celle des
// convertisseurs, microscopique mais réelle, calée sur les specs DAC mesurées :
//   0 dBFS  : THD+N < -96 dBFS
//   -20 dBFS: contenu harmonique ~ -115 dBFS
//   -50 dBFS: < -135 dBFS  (la proportion remonte à bas niveau = grain numérique)
//
// Implémentation ORIGINALE. Portable (std/math).
//   - micro non-linéarité analogique (2e/3e harm) proportionnelle au niveau
//   - quantification DAC ~20 bits (plancher ~constant -> proportion ↑ à bas niveau)
//
#include <cmath>

class OxfordConverter
{
public:
    void prepare(double) { dcX = dcY = 0.0; }

    double processSample(double x) noexcept
    {
        // 1) micro non-linéarité analogique résiduelle (2e + 3e harmonique), infime
        double y = x + a2 * x * x + a3 * x * x * x;
        // blocage DC (le terme x^2 introduit une composante continue)
        const double o = y - dcX + 0.9995 * dcY;
        dcX = y; dcY = o;
        y = o;
        // 2) quantification DAC ~20 bits (erreur de niveau ABSOLU ~constant ->
        //    la proportion de distorsion remonte quand le signal descend)
        y = std::round(y / q) * q;
        return y;
    }

private:
    static constexpr double a2 = 3.0e-5;          // 2e harm @0dBFS ~ -96 dBFS (vraie spec)
    static constexpr double a3 = 1.5e-5;          // 3e harm
    static constexpr double q  = 1.0 / 524288.0;  // 2^-19 (DAC 20 bits)
    double dcX { 0.0 }, dcY { 0.0 };
};
