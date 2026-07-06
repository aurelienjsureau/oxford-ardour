/*
 * MasterTapePCM1630 — implémentation (inférence NeuralAmpModelerCore, lib TIERCE MIT).
 * Compilé en C++20 dans la stlib nam_core (les en-têtes NAM exigent C++20 + Eigen).
 * NAM_SAMPLE_FLOAT est défini par les cxxflags de la stlib -> NAM_SAMPLE == float.
 */
#ifndef NAM_SAMPLE_FLOAT
#define NAM_SAMPLE_FLOAT 1
#endif

#include "ardour/oxford/MasterTapePCM1630.h"

#include "get_dsp.h"   // NeuralAmpModelerCore (include path = .../NAM)
#include "dsp.h"

#include <filesystem>
#include <vector>
#include <cmath>
#include <iostream>

struct MasterTapePCM1630::Impl
{
    std::unique_ptr<nam::DSP> model;
    double sr = 48000.0;
    int    maxBlock = 8192;
    bool   enabled = false;
    double inGain = 1.0, outGain = 1.0, normGain = 1.0;   // normGain = compensation de loudness
    std::vector<float> scratch;

    /* GEL SUR SILENCE : à l'arrêt, le master reçoit des zéros exacts mais le
     * réseau tournait à plein régime (~20 % de jauge pour rien). Après 3 s de
     * zéros, si la sortie du modèle est STRICTEMENT constante sur tout un bloc
     * et égale au dernier échantillon du bloc précédent, l'état interne est un
     * point fixe (champ réceptif WaveNet/LSTM << 3 s) : on fige et on rejoue la
     * constante. Continuer l'inférence n'aurait rien changé -> bit-exact.
     * Dégel au premier échantillon d'entrée non nul. */
    long   zeroRun = 0;
    bool   frozen = false;
    float  steadyOut = 0.f;   // sortie du modèle en régime silence (AVANT gain de sortie)
    float  lastOut = 0.f;     // dernier échantillon du bloc précédent (avant gain), pour le critère
};

MasterTapePCM1630::MasterTapePCM1630 () : _impl (new Impl()) {}
MasterTapePCM1630::~MasterTapePCM1630 () = default;
MasterTapePCM1630::MasterTapePCM1630 (MasterTapePCM1630&&) noexcept = default;
MasterTapePCM1630& MasterTapePCM1630::operator= (MasterTapePCM1630&&) noexcept = default;

void MasterTapePCM1630::prepare (double sampleRate, int maxBlockSize)
{
    _impl->sr = sampleRate;
    _impl->maxBlock = maxBlockSize > 0 ? maxBlockSize : 8192;
    _impl->scratch.assign ((size_t) _impl->maxBlock, 0.f);
    _impl->zeroRun = 0; _impl->frozen = false; _impl->lastOut = 0.f;   // reset du gel silence
    if (_impl->model) {
        try { _impl->model->Reset (sampleRate, _impl->maxBlock); } catch (...) {}
    }
}

bool MasterTapePCM1630::loadModel (const std::string& path)
{
    try {
        _impl->model = nam::get_dsp (std::filesystem::path (path));
    } catch (const std::exception& e) {
        std::cerr << "[Oxford] MasterTapePCM1630: échec chargement .nam (" << path << ") : " << e.what () << " -> bypass\n";
        _impl->model.reset ();
    } catch (...) {
        std::cerr << "[Oxford] MasterTapePCM1630: échec chargement .nam (" << path << ") -> bypass\n";
        _impl->model.reset ();
    }
    if (_impl->model) {
        try { _impl->model->Reset (_impl->sr, _impl->maxBlock); } catch (...) {}
        /* PAS de normalisation de loudness : le saut de niveau naturel du modèle est VOULU.
         * Il sert de jauge (visible sur le fader/meter master) pour doser l'attaque du
         * modèle -> on récupère le niveau à la main au fader. (Atténuer ici masquait la
         * saturation sans la réduire, et empêchait de jauger.) */
        _impl->normGain = 1.0;
        return true;
    }
    return false;
}

void MasterTapePCM1630::setEnabled (bool on)       { _impl->enabled = on; }
bool MasterTapePCM1630::enabled () const           { return _impl->enabled && _impl->model != nullptr; }
void MasterTapePCM1630::setInputGainDb (double dB) { _impl->inGain  = std::pow (10.0, dB / 20.0); }
void MasterTapePCM1630::setOutputGainDb (double dB){ _impl->outGain = std::pow (10.0, dB / 20.0); }

void MasterTapePCM1630::processBlock (float* data, int n)
{
    if (!_impl->enabled || !_impl->model || n <= 0) { return; }
    if ((int) _impl->scratch.size () < n) { _impl->scratch.assign ((size_t) n, 0.f); }

    /* gel sur silence : détection d'entrée entièrement nulle (cf. Impl) */
    bool allZero = true;
    for (int i = 0; i < n; ++i) { if (data[i] != 0.f) { allZero = false; break; } }

    const float g = (float) (_impl->outGain * _impl->normGain);   // sortie + normalisation loudness

    if (allZero) {
        _impl->zeroRun += n;
        if (_impl->frozen) {
            const float y = _impl->steadyOut * g;
            for (int i = 0; i < n; ++i) { data[i] = y; }
            return;                                    // pas d'inférence : état = point fixe
        }
    } else {
        _impl->zeroRun = 0;
        _impl->frozen = false;                         // dégel : chemin normal dès ce bloc
    }

    const float ig = (float) _impl->inGain;
    for (int i = 0; i < n; ++i) { _impl->scratch[(size_t) i] = data[i] * ig; }

    NAM_SAMPLE* in[1]  = { _impl->scratch.data () };
    NAM_SAMPLE* out[1] = { data };
    try { _impl->model->process (in, out, n); } catch (...) { return; }

    if (allZero && _impl->zeroRun > (long) (3.0 * _impl->sr)) {
        /* candidat au gel : sortie strictement constante ET continue avec le bloc précédent */
        const float first = data[0];
        bool cst = (first == _impl->lastOut);
        for (int i = 1; cst && i < n; ++i) { if (data[i] != first) { cst = false; } }
        if (cst) { _impl->frozen = true; _impl->steadyOut = first; }
    }
    _impl->lastOut = data[n - 1];

    if (g != 1.f) { for (int i = 0; i < n; ++i) { data[i] *= g; } }
}
