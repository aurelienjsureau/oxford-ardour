// oxtest.cpp — mesures de validation du DSP Oxford (comme /tmp/eq_test.cpp Trident)
//   g++ -O2 -std=c++17 oxtest.cpp -o oxtest && ./oxtest
//
#include "OxfordEQ.h"
#include "OxfordDynamics.h"
#include "OxfordWarmth.h"
#include "Tape3348Color.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double SR = 48000.0;
static constexpr double PI = 3.14159265358979323846;

// RMS d'un sinus à freq f traité par un callback échantillon-par-échantillon.
template <class F>
double rmsAt(double f, F&& proc, int cycles = 200, int warm = 4096)
{
    const double w = 2.0 * PI * f / SR;
    double ph = 0.0, acc = 0.0; long n = 0;
    const long total = warm + (long)(cycles * SR / f);
    for (long i = 0; i < total; ++i) {
        double x = std::sin(ph); ph += w; if (ph > 2*PI) ph -= 2*PI;
        double y = proc(x);
        if (i >= warm) { acc += y * y; ++n; }
    }
    return std::sqrt(acc / (double)n);
}
static double db(double lin) { return 20.0 * std::log10(lin + 1e-12); }

// THD : énergie hors fondamentale / énergie fondamentale (sinus 1 kHz)
template <class F>
double thd(F&& proc, double f = 1000.0, double amp = 0.5)
{
    const int N = 1 << 15;
    std::vector<double> buf(N);
    const double w = 2.0 * PI * f / SR; double ph = 0;
    for (int i = 0; i < 8192; ++i) { proc(amp*std::sin(ph)); ph += w; if(ph>2*PI)ph-=2*PI; } // warm
    for (int i = 0; i < N; ++i) { buf[i] = proc(amp*std::sin(ph)); ph += w; if(ph>2*PI)ph-=2*PI; }
    // DFT aux harmoniques (1..10)
    double fund = 0, harm = 0;
    for (int k = 1; k <= 10; ++k) {
        double re=0, im=0; const double wk = 2.0*PI*(f*k)/SR;
        for (int i = 0; i < N; ++i) { re += buf[i]*std::cos(wk*i); im += buf[i]*std::sin(wk*i); }
        double mag = std::sqrt(re*re+im*im);
        if (k == 1) fund = mag; else harm += mag*mag;
    }
    return 100.0 * std::sqrt(harm) / (fund + 1e-12);
}

int main()
{
    printf("=== OxfordEQ : gain a la freq de chaque bande (+12 dB, type NeveG) ===\n");
    {
        struct Pt { OxfordEQ::Band b; double f; };
        Pt pts[] = { {OxfordEQ::LMF,250},{OxfordEQ::MF,1000},{OxfordEQ::HMF,4000} };
        for (auto& p : pts) {
            OxfordEQ eq; eq.prepare(SR); eq.setCurveType(OxfordEQ::NeveG);
            eq.setBand(p.b, p.f, 12.0, 1.0);
            double flat = rmsAt(p.f, [](double x){return x;});
            double g    = rmsAt(p.f, [&](double x){return eq.processSample(x);});
            printf("  bande %-4s @ %5.0f Hz : %+5.2f dB (consigne +12)\n",
                   (p.b==OxfordEQ::LMF?"LMF":p.b==OxfordEQ::MF?"MF":"HMF"), p.f, db(g/flat));
        }
        // shelfs
        for (int hi = 0; hi <= 1; ++hi) {
            double f = hi ? 12000 : 60;
            OxfordEQ eq; eq.prepare(SR);
            if (hi) { eq.setHFShelf(true); eq.setBand(OxfordEQ::HF, f, 12.0, 0.7); }
            else    { eq.setLFShelf(true); eq.setBand(OxfordEQ::LF, f, 12.0, 0.7); }
            double flat = rmsAt(f, [](double x){return x;});
            double g    = rmsAt(f, [&](double x){return eq.processSample(x);});
            printf("  shelf %-4s @ %5.0f Hz : %+5.2f dB\n", hi?"HF":"LF", f, db(g/flat));
        }
    }

    printf("\n=== OxfordEQ : filtre HP variable @ 100 Hz, pentes 12/24/36 dB/oct ===\n");
    for (double slope : {12.0,24.0,36.0}) {
        OxfordEQ eq; eq.prepare(SR); eq.setHPF(100.0, slope, true);
        double a50  = db(rmsAt(50,  [&](double x){return eq.processSample(x);}));
        OxfordEQ eq2; eq2.prepare(SR); eq2.setHPF(100.0, slope, true);
        double a100 = db(rmsAt(100, [&](double x){return eq2.processSample(x);}));
        printf("  %4.0f dB/oct : 50 Hz %+6.2f dB, 100 Hz %+6.2f dB\n", slope, a50, a100);
    }

    printf("\n=== OxfordDynamics : loi 1/Ratio (compRatioControl) ===\n");
    for (double c : {0.0,0.25,0.5,0.75,0.9,1.0}) {
        OxfordDynamics d; d.prepare(SR); d.setCompEnabled(true);
        d.setCompThreshold(-30); d.setCompRatioControl(c);
        d.setCompAttackMs(1); d.setCompReleaseMs(50);
        // entree -10 dBFS (20 dB au-dessus du seuil) ; mesure GR en regime
        double in = std::pow(10.0,-10.0/20.0);
        double out = rmsAt(1000,[&](double x){return d.processSample(x*in/0.7071);});
        double grDb = db(out) - db(in); // sortie vs entree -10
        printf("  ctrl %.2f : GR ~ %+6.2f dB\n", c, grDb);
    }

    printf("\n=== OxfordDynamics : ratio fixe, GR mesuree a +20 dB sur seuil ===\n");
    for (double r : {2.0,4.0,10.0,100.0}) {
        OxfordDynamics d; d.prepare(SR); d.setCompEnabled(true);
        d.setCompThreshold(-30); d.setCompRatio(r); d.setCompSoftRatioDb(0);
        d.setCompAttackMs(1); d.setCompReleaseMs(50);
        double in = std::pow(10.0,-10.0/20.0); // -10 dBFS = +20 sur seuil
        double out = rmsAt(1000,[&](double x){return d.processSample(x*in/0.7071);});
        double expected = -20.0*(1.0-1.0/r);
        printf("  ratio %6.1f : GR %+6.2f dB (theorique %+6.2f)\n", r, db(out)-db(in), expected);
    }

    printf("\n=== Lois de timing (release apres burst, temps pour revenir a -3 dB GR) ===\n");
    for (int lawi = 0; lawi < 3; ++lawi) {
        OxfordDynamics d; d.prepare(SR); d.setCompEnabled(true);
        d.setTimingLaw((OxfordDynamics::TimingLaw)lawi);
        d.setCompThreshold(-30); d.setCompRatio(10);
        d.setCompAttackMs(1); d.setCompReleaseMs(200);
        // burst fort 0.2 s puis silence ; on mesure quand GR repasse sous 3 dB
        double w = 2*PI*1000/SR, ph=0; double in = std::pow(10.0,-6.0/20.0);
        for (int i=0;i<(int)(0.2*SR);++i){ d.processSample(in*std::sin(ph)); ph+=w; }
        int rel=-1;
        for (int i=0;i<(int)(SR);++i){ d.processSample(0.0); if (d.compReductionDb()<3.0){rel=i;break;} }
        printf("  loi %-7s : retour <3dB GR en %6.1f ms\n",
               lawi==0?"Normal":lawi==1?"Classic":"Linear", rel*1000.0/SR);
    }

    printf("\n=== THD a 1 kHz (-6 dBFS) ===\n");
    { OxfordWarmth w; w.prepare(SR); w.setEnabled(true); w.setAmount(100);
      printf("  Warmth 100%%       : %.3f %%\n", thd([&](double x){return w.processSample(x);})); }
    for (double dr : {0.0,0.5,1.0}) {
      Tape3348Color t; t.prepare(SR); t.setEnabled(true); t.setDrive(dr); t.setGrain(1.0); t.setLimiterEnabled(false);
      printf("  Tape3348 drive %.1f : %.3f %%\n", dr, thd([&](double x){return t.processSample(x);})); }

    printf("\n=== Tape3348 : proportion de distorsion vs niveau (caractere numerique) ===\n");
    for (double lvDb : {-6.0,-20.0,-50.0}) {
      Tape3348Color t; t.prepare(SR); t.setEnabled(true); t.setDrive(0.5); t.setGrain(1.0); t.setLimiterEnabled(false);
      double amp = std::pow(10.0,lvDb/20.0);
      printf("  niveau %4.0f dBFS : THD %.4f %%\n", lvDb, thd([&](double x){return t.processSample(x);},1000.0,amp)); }

    printf("\nOK\n");
    return 0;
}
