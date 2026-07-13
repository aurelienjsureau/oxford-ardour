// bwtest.cpp — validation EMPIRIQUE du "bandwidth constancy" de l'EQ Orfanidis.
//
// Protocole (demandé en review) : bell +6 dB, Q fixe (2.83), sweep du centre
// 100 Hz -> 20 kHz @ 48 kHz. Pour chaque centre on mesure PAR SINUS (pas sur
// les coefficients) le gain au centre et les deux points mi-gain (+3 dB), et
// on en déduit la largeur en octaves. Un biquad RBJ classique sert de témoin
// (c'est lui qui "crampe" près de Nyquist).
//
// Attendu : largeur Orfanidis ~constante sur toute la plage (y c. 18-20 kHz),
// largeur RBJ qui se pince quand fc approche Nyquist.
//
// Compile : PATH=/c/msys64/mingw64/bin:$PATH g++ -O2 -std=c++17 \
//             -I /d/Oxford/Ardour-9.7.0/libs/ardour/ardour/oxford \
//             bwtest.cpp -o bwtest.exe   (teste le header réellement shippé)
#include <cstdio>
#include <cmath>
#include <functional>
// chemin ABSOLU : le master local Downloads est en retard sur l'arbre (pas de
// GML/Orfanidis) et "OxfordEQ.h" le ramasserait en premier. On teste le shippé.
#include "D:/Oxford/Ardour-9.7.0/libs/ardour/ardour/oxford/OxfordEQ.h"

static constexpr double SR  = 48000.0;
static constexpr double PI2 = 2.0 * 3.14159265358979323846;

// ---- biquad RBJ peaking de référence (cookbook) ----
struct RBJ
{
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void set (double fc, double gdb, double Q)
    {
        const double A=std::pow(10.0,gdb/40.0), w0=PI2*fc/SR;
        const double al=std::sin(w0)/(2.0*Q), cw=std::cos(w0);
        const double a0=1.0+al/A;
        b0=(1.0+al*A)/a0; b1=(-2.0*cw)/a0; b2=(1.0-al*A)/a0;
        a1=(-2.0*cw)/a0;  a2=(1.0-al/A)/a0; z1=z2=0;
    }
    double proc (double x) { const double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
};

// ---- gain mesuré par sinus : 0.10 s de stabilisation, RMS sur 0.20 s ----
static double gainDbMeasured (const std::function<double(double)>& proc, double freq)
{
    const int settle=(int)(0.10*SR), meas=(int)(0.20*SR);
    const double w=PI2*freq/SR;
    for (int i=0;i<settle;i++) (void) proc (std::sin (w*i));
    double acc=0;
    for (int i=settle;i<settle+meas;i++) { const double y=proc (std::sin (w*i)); acc+=y*y; }
    return 20.0*std::log10 (std::sqrt (acc/meas) * std::sqrt (2.0));   // réf. sinus unité
}

// EQ Orfanidis : instance neuve par mesure (état + lissage propres), GML =
// constant-Q strict (pas de loi gain/Q des Types 1-4 dans le test).
static double oxGainDb (double fc, double gdb, double Q, double probe)
{
    OxfordEQ eq;
    eq.setCurveType (OxfordEQ::GML);
    eq.setBandEnabled (OxfordEQ::LF,  false);
    eq.setBandEnabled (OxfordEQ::LMF, false);
    eq.setBandEnabled (OxfordEQ::HMF, false);
    eq.setBandEnabled (OxfordEQ::HF,  false);
    eq.setBand (OxfordEQ::MF, fc, gdb, Q);
    eq.prepare (SR);
    return gainDbMeasured ([&](double x){ return eq.processSample (x); }, probe);
}

static double rbjGainDb (double fc, double gdb, double Q, double probe)
{
    RBJ f; f.set (fc, gdb, Q);
    return gainDbMeasured ([&](double x){ return f.proc (x); }, probe);
}

// bisection log du point où le gain traverse targetDb (monotone supposé sur [lo,hi])
static double crossing (const std::function<double(double)>& G, double lo, double hi,
                        double targetDb, bool descending)
{
    for (int it=0; it<24; ++it)
    {
        const double m = std::sqrt (lo*hi);
        const bool above = G (m) > targetDb;
        if (above == descending) lo = m; else hi = m;
    }
    return std::sqrt (lo*hi);
}

int main()
{
    const double gdb = 6.0, Q = 2.83, half = 3.0;
    const double centers[] = { 100, 500, 1000, 5000, 10000, 14000, 16000, 18000, 19000, 20000 };

    std::printf ("bell +6 dB, Q=%.2f (constant-Q), fs=48 kHz — points mi-gain mesures par sinus\n", Q);
    std::printf ("%8s | %26s | %26s\n", "fc (Hz)", "OXFORD (Orfanidis)", "RBJ cookbook (temoin)");
    std::printf ("%8s | %8s %8s %8s | %8s %8s %8s\n",
                 "", "g@fc dB", "f-3/f+3", "BW oct", "g@fc dB", "f-3/f+3", "BW oct");

    for (double fc : centers)
    {
        auto row = [&](const std::function<double(double)>& G)
        {
            const double gfc = G (fc);
            const double flo = crossing (G, std::max (20.0, fc/32.0), fc, half, false);
            // bord haut : peut sortir de la bande mesurable (< Nyquist)
            const double topProbe = 23900.0;
            double fhi = -1.0;
            if (G (topProbe) < half)
                fhi = crossing (G, fc, topProbe, half, true);
            char edges[32], bw[16];
            if (fhi > 0) { std::snprintf (edges,sizeof edges,"%5.0f/%5.0f", flo, fhi);
                           std::snprintf (bw,sizeof bw,"%.3f", std::log2 (fhi/flo)); }
            else         { std::snprintf (edges,sizeof edges,"%5.0f/>Nyq", flo);
                           std::snprintf (bw,sizeof bw,"  n/a"); }
            std::printf ("%8.2f %8s %8s", gfc, edges, bw);
        };

        std::printf ("%8.0f | ", fc);
        row ([&](double f){ return oxGainDb  (fc, gdb, Q, f); });
        std::printf (" | ");
        row ([&](double f){ return rbjGainDb (fc, gdb, Q, f); });
        std::printf ("\n");
    }
    return 0;
}
