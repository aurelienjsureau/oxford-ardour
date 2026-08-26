#pragma once
//
// Tape3348Color — module de coloration "convertisseur / tape numérique" ORIGINAL,
// inspiré CONCEPTUELLEMENT (specs publiques uniquement, aucun code tiers) du
// caractère des convertisseurs AD/DA de la génération DASH (PCM-3348) et des
// convertisseurs de la console OXF-R3.
//
// Nom volontairement distinct de toute marque. Portable (std/math).
//
// Différence clé vs une saturation analogique classique :
//   - analogique : distorsion ~2e/3e harmonique, PROPORTIONNELLE au niveau ;
//   - numérique  : plancher de distorsion de quantification quasi-CONSTANT en
//     niveau absolu -> la PROPORTION de distorsion AUGMENTE quand le signal
//     descend, et le contenu se déplace vers les harmoniques d'ordre élevé.
//
// Modélisation :
//   1) Emphasis (pre) : shelf HF + boost avant l'étage non-linéaire (companding).
//   2) Drive avec AUTO-GAIN : on pousse l'entrée puis on rattrape en sortie
//      (le knob Drive ne change pas le niveau perçu).
//   3) Étage non-linéaire = mélange :
//        - saturation analogique douce (tanh, 2e/3e) proportionnelle,
//        - quantizer à pas ~constant -> "grain" numérique dont la proportion
//          monte à bas niveau (le comportement DASH caractéristique).
//   4) De-emphasis (post) : shelf inverse de l'emphasis (miroir).
//   5) Limiteur transparent bypassable en sortie (absorbe les transitoires).
//
#include <cmath>
#include <algorithm>

class Tape3348Color
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        empPre.reset(); empPost.reset();
        limGain = 1.0;
        limRelC = std::exp(-1.0 / (0.050 * sampleRate));
        updateEmphasis();
    }

    void setEnabled(bool on)   { enabled = on; }
    void setDrive(double d)    { drive = clamp(d, 0.0, 1.0);              // 0..1
                                 driveG  = 1.0 + 4.0 * drive;             // jusqu'à +14 dB d'attaque
                                 driveMk = 1.0 / driveG; }                // auto-gain (compense le drive)
    // Emphasis -1..+1 : >0 accentue les aigus avant la sat (et les atténue après),
    // <0 l'inverse. 0 = neutre.
    void setEmphasis(double e) { emphasis = clamp(e, -1.0, 1.0); updateEmphasis(); }
    void setGrain(double g)    // dosage du caractère numérique
    {
        g = clamp(g, 0.0, 1.0);
        if (g == grain) return;
        grain = g;
        const double bits = 20.0 - 8.0 * grain;          // 20 -> 12 bits
        qStep = std::pow(2.0, -(bits - 1.0));            // pré-calculé (fonction pure du knob)
    }
    void setLimiterEnabled(bool on) { limOn = on; }
    void setLimiterCeilingDb(double dB) { limCeil = std::pow(10.0, dB / 20.0); }

    double processSample(double x) noexcept
    {
        if (!enabled) return x;

        // --- Court-circuit SILENCE (bit-exact PAR CONSTRUCTION) : si l'entrée
        //     est un zéro exact ET que tout l'état interne est retombé à zéro
        //     (états des shelves == 0.0, limGain == 1.0 — ce qui arrive de
        //     soi-même après un silence, FTZ flushant les résidus), alors le
        //     chemin complet rendrait exactement 0.0 sans modifier l'état :
        //     on le rend directement. Une piste silencieuse (session à l'arrêt)
        //     ne coûte plus rien ; premier échantillon non nul -> chemin normal.
        if (x == 0.0
            && empPre.z1  == 0.0 && empPre.z2  == 0.0
            && empPost.z1 == 0.0 && empPost.z2 == 0.0
            && limGain == 1.0) {
            return 0.0;
        }

        // --- 1) emphasis (pre) ---
        double s = empPre.process(x);

        // --- 2) drive + auto-gain ---
        // g et mk ne dépendent que du knob : pré-calculés par setDrive (identiques
        // au bit près, c'est le même 1.0 + 4.0*drive), plus recalculés par échantillon.
        s *= driveG;

        // --- 3) étage non-linéaire : analogique doux + grain numérique ---
        // (a) saturation analogique douce (proportionnelle, 2e/3e harm via asymétrie)
        double ana = std::tanh(s * 0.9) + 0.03 * (s * s) * std::exp(-std::fabs(s));
        // (b) "grain" numérique : quantizer à pas CONSTANT en amplitude. C'est le
        //     cœur du caractère DASH : l'erreur de quantification a un niveau
        //     ABSOLU ~constant -> sa PROPORTION augmente quand le signal descend
        //     (à l'inverse d'une sat analogique). Le pas q est piloté par 'grain'
        //     (et non par le drive) : grain=0 -> ~20 bits (propre), grain=1 ->
        //     ~12 bits (grain marqué, queues de réverbe qui "décrochent").
        // grain == 0 : le mélange rend exactement 'ana' (0.0 * quoi que ce soit),
        // la quantification est donc du calcul jeté. On la saute — bit-exact.
        double y = ana;
        if (grain != 0.0) {
            const double q = qStep;                      // pas de quantification (pré-calculé par setGrain)
            const double quantized = std::round(ana / q) * q;
            y = ana + grain * (quantized - ana);         // mélange analogique / numérique
        }

        // --- auto-gain ---
        y *= driveMk;

        // --- 4) de-emphasis (post, miroir) ---
        y = empPost.process(y);

        // --- 5) limiteur transparent bypassable ---
        if (limOn) y = limiter(y);

        return y;
    }

private:
    static double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    struct Shelf  // high-shelf RBJ
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void reset() { z1=z2=0; }
        double process(double x) noexcept
        { const double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
    };

    void makeHighShelf(Shelf& sh, double fc, double gainDb)
    {
        const double A=std::pow(10.0,gainDb/40.0);
        const double w0=2.0*PI*fc/sampleRate;
        const double cw=std::cos(w0), sw=std::sin(w0);
        const double S=0.9;
        const double alpha=sw/2.0*std::sqrt((A+1.0/A)*(1.0/S-1.0)+2.0);
        const double tsa=2.0*std::sqrt(A)*alpha;
        const double a0=(A+1.0)-(A-1.0)*cw+tsa;
        sh.b0=(A*((A+1.0)+(A-1.0)*cw+tsa))/a0;
        sh.b1=(-2.0*A*((A-1.0)+(A+1.0)*cw))/a0;
        sh.b2=(A*((A+1.0)+(A-1.0)*cw-tsa))/a0;
        sh.a1=(2.0*((A-1.0)-(A+1.0)*cw))/a0;
        sh.a2=((A+1.0)-(A-1.0)*cw-tsa)/a0;
    }

    void updateEmphasis()
    {
        const double fc = 4000.0;
        const double dB = 8.0 * emphasis;       // +/- jusqu'à 8 dB
        makeHighShelf(empPre,  fc,  dB);
        makeHighShelf(empPost, fc, -dB);        // miroir exact
    }

    double limiter(double s) noexcept
    {
        const double mag = std::fabs(s);
        const double need = (mag > limCeil && mag > 1e-12) ? (limCeil / mag) : 1.0;
        if (need < limGain) limGain = need;                       // attaque immédiate
        else                limGain = limRelC * limGain + (1.0 - limRelC) * need;
        return s * limGain;
    }

    static constexpr double PI = 3.14159265358979323846;

    double sampleRate { 48000.0 };
    bool   enabled { false }, limOn { true };
    double drive { 0.3 }, emphasis { 0.0 }, grain { 0.5 };
    /* dérivés de 'drive', tenus à jour par setDrive — doivent rester cohérents
     * avec la valeur par défaut ci-dessus (0.3 -> g = 2.2). */
    double driveG  { 1.0 + 4.0 * 0.3 };
    double driveMk { 1.0 / (1.0 + 4.0 * 0.3) };
    double qStep { 3.0517578125e-05 };      // = 2^-15 (grain 0.5 -> 16 bits), tenu à jour par setGrain
    double limCeil { 0.985 };               // ~ -0.13 dBFS
    double limGain { 1.0 }, limRelC { 0.0 };
    Shelf  empPre, empPost;
};
