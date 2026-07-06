/*
 * MasterTapePCM1630 — module master "tape" basé sur une inférence NAM (Neural Amp Modeler),
 * capture du Sony PCM-1630 (Bob Ludwig). Wrapper PIMPL : ce header N'INCLUT PAS les en-têtes
 * NeuralAmpModelerCore (qui exigent C++20) — l'implémentation est dans MasterTapePCM1630.cc,
 * compilée à part en C++20 dans la stlib nam_core.
 *
 * NeuralAmpModelerCore = bibliothèque TIERCE (Steven Atkinson, licence MIT), distincte du
 * DSP "maison" du projet. Voir libs/NeuralAmpModelerCore/LICENSE.
 */
#pragma once
#include <memory>
#include <string>

class MasterTapePCM1630
{
public:
    MasterTapePCM1630();
    ~MasterTapePCM1630();
    MasterTapePCM1630 (MasterTapePCM1630&&) noexcept;
    MasterTapePCM1630& operator= (MasterTapePCM1630&&) noexcept;
    MasterTapePCM1630 (const MasterTapePCM1630&) = delete;
    MasterTapePCM1630& operator= (const MasterTapePCM1630&) = delete;

    void prepare (double sampleRate, int maxBlockSize);
    bool loadModel (const std::string& namFilePath);   // false si absent/corrompu -> bypass
    void setEnabled (bool on);
    bool enabled () const;
    void setInputGainDb (double dB);                    // attaque avant le réseau
    void setOutputGainDb (double dB);                   // compensation de sortie
    void processBlock (float* data, int numSamples);    // no-op si désactivé / non chargé

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
