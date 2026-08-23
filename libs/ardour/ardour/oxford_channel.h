/*
 * OxfordChannel — processor "baked-in" qui applique la couleur Oxford OXF-R3.
 *
 * Deux modes :
 *   Strip : [Tape3348] -> EQ 5 bandes (4 types) -> Dynamics (Gate/Exp/Comp/Limiter) [-> Warmth]
 *           (Tape3348 = étape d'enregistrement PCM-3348, insert PAR PISTE, en tête)
 *   Bus   : BusComp -> Warmth -> Limiter   (bus + master, scindable ; DAC sur Tail)
 *
 * Un objet DSP par canal audio. Processor baked-in du fork
 * Trident) : setters atomiques côté UI + flag _dirty + applyParams côté audio.
 */
#pragma once

#include <vector>
#include <atomic>

#include "ardour/libardour_visibility.h"
#include "ardour/processor.h"

#include "ardour/oxford/OxfordChannelStrip.h"
#include "ardour/oxford/OxfordBus.h"

namespace ARDOUR {

class LIBARDOUR_API OxfordChannel : public Processor
{
public:
	enum Kind { Strip, Bus };

	OxfordChannel (Session&, Kind, const std::string& name);

	Kind kind () const { return _kind; }

	/* Master : découpe la chaîne bus (BusComp en amont des plugins ; Warmth/Tape/
	 * Limiter en aval). Voir OxfordBus::Stage. */
	void setStage (OxfordBus::Stage s) { _stage = s; }
	OxfordBus::Stage stage () const { return _stage; }

	bool can_support_io_configuration (const ChanCount& in, ChanCount& out);
	bool configure_io (ChanCount in, ChanCount out);

	void run (BufferSet& bufs, samplepos_t start_sample, samplepos_t end_sample,
	          double speed, pframes_t nframes, bool result_required);

	/* ======================= EQ (Strip) ======================= */
	void setEQEnabled (bool on) { _eqOn.store (on); _dirty.store (true); }
	bool eqEnabled () const { return _eqOn.load (); }
	void setCurveType (int t)   { _curveType.store (t); _dirty.store (true); }  // 0..3
	int  curveType () const     { return _curveType.load (); }

	/* 5 bandes : 0=LF 1=LMF 2=MF 3=HMF 4=HF — gain dB, freq Hz, Q */
	void setBandGain (int b, float dB)  { if (b>=0&&b<5){_bGain[b].store(dB);_dirty.store(true);} }
	void setBandFreq (int b, float hz)  { if (b>=0&&b<5){_bFreq[b].store(hz);_dirty.store(true);} }
	void setBandQ    (int b, float q)   { if (b>=0&&b<5){_bQ[b].store(q);_dirty.store(true);} }
	float bandGain (int b) const { return (b>=0&&b<5)?_bGain[b].load():0.f; }
	float bandFreq (int b) const { return (b>=0&&b<5)?_bFreq[b].load():0.f; }
	float bandQ    (int b) const { return (b>=0&&b<5)?_bQ[b].load():1.f; }
	void setBandEnabled (int b, bool on) { if (b>=0&&b<5){_bEn[b].store(on);_dirty.store(true);} } // IN par bande (OXF-R3)
	bool bandEnabled (int b) const { return (b>=0&&b<5)?_bEn[b].load():true; }

	/* réponse EQ réelle en dB à f Hz (coefficients exacts, pour l'écran EQ du panel) */
	double eqCurveDb (double hz);

	void setLFShelf (bool s) { _lfShelf.store (s); _dirty.store (true); }
	void setHFShelf (bool s) { _hfShelf.store (s); _dirty.store (true); }
	void setLFOvershoot (float o) { _lfOver.store (o); _dirty.store (true); }
	void setHFOvershoot (float o) { _hfOver.store (o); _dirty.store (true); }
	bool lfShelf () const { return _lfShelf.load (); }
	bool hfShelf () const { return _hfShelf.load (); }

	/* Filtres HP/LP variables */
	void setHPF (bool on, float hz, float slope) { _hpfOn.store(on);_hpfHz.store(hz);_hpfSlope.store(slope);_dirty.store(true); }
	void setLPF (bool on, float hz, float slope) { _lpfOn.store(on);_lpfHz.store(hz);_lpfSlope.store(slope);_dirty.store(true); }
	bool  hpfOn () const { return _hpfOn.load (); }
	bool  lpfOn () const { return _lpfOn.load (); }
	float hpfHz () const { return _hpfHz.load (); }
	float lpfHz () const { return _lpfHz.load (); }
	float hpfSlope () const { return _hpfSlope.load (); }
	float lpfSlope () const { return _lpfSlope.load (); }

	/* ===================== Dynamics (Strip) ===================== */
	void setTimingLaw (int l) { _timingLaw.store (l); _dirty.store (true); }  // 0=Normal 1=Classic 2=Linear
	int  timingLaw () const   { return _timingLaw.load (); }
	void setLookaheadMs (float ms) { _lookMs.store (ms); _dirty.store (true); }
	float lookaheadMs () const { return _lookMs.load (); }

	void setGateOn (bool on) { _gateOn.store(on);_dirty.store(true);} bool gateOn() const {return _gateOn.load();}
	void setGateThreshDb (float v){_gateThr.store(v);_dirty.store(true);} float gateThreshDb() const{return _gateThr.load();}
	void setGateRangeDb  (float v){_gateRange.store(v);_dirty.store(true);} float gateRangeDb() const{return _gateRange.load();}
	void setGateAttackMs (float v){_gateAtt.store(v);_dirty.store(true);} float gateAttackMs() const{return _gateAtt.load();}
	void setGateReleaseMs(float v){_gateRel.store(v);_dirty.store(true);} float gateReleaseMs() const{return _gateRel.load();}

	void setExpOn (bool on){_expOn.store(on);_dirty.store(true);} bool expOn() const{return _expOn.load();}
	void setExpThreshDb(float v){_expThr.store(v);_dirty.store(true);} float expThreshDb() const{return _expThr.load();}
	void setExpRatio   (float v){_expRatio.store(v);_dirty.store(true);} float expRatio() const{return _expRatio.load();}
	void setExpRangeDb (float v){_expRange.store(v);_dirty.store(true);} float expRangeDb() const{return _expRange.load();}
	void setExpAttackMs(float v){_expAtt.store(v);_dirty.store(true);} float expAttackMs() const{return _expAtt.load();}
	void setExpReleaseMs(float v){_expRel.store(v);_dirty.store(true);} float expReleaseMs() const{return _expRel.load();}

	void setCompOn (bool on){_compOn.store(on);_dirty.store(true);} bool compOn() const{return _compOn.load();}
	void setCompThreshDb (float v){_compThr.store(v);_dirty.store(true);} float compThreshDb() const{return _compThr.load();}
	void setCompRatioCtrl(float v){_compRatio.store(v);_dirty.store(true);} float compRatioCtrl() const{return _compRatio.load();}
	void setCompMakeupDb (float v){_compMakeup.store(v);_dirty.store(true);} float compMakeupDb() const{return _compMakeup.load();}
	void setCompAttackMs (float v){_compAtt.store(v);_dirty.store(true);} float compAttackMs() const{return _compAtt.load();}
	void setCompHoldMs   (float v){_compHold.store(v);_dirty.store(true);} float compHoldMs() const{return _compHold.load();}
	void setCompReleaseMs(float v){_compRel.store(v);_dirty.store(true);} float compReleaseMs() const{return _compRel.load();}
	void setCompSoftDb   (float v){_compSoft.store(v);_dirty.store(true);} float compSoftDb() const{return _compSoft.load();}

	void setLimOn (bool on){_limOn.store(on);_dirty.store(true);} bool limOn() const{return _limOn.load();}
	void setLimThreshDb (float v){_limThr.store(v);_dirty.store(true);} float limThreshDb() const{return _limThr.load();}
	void setLimAttackMs (float v){_limAtt.store(v);_dirty.store(true);} float limAttackMs() const{return _limAtt.load();}
	void setLimHoldMs   (float v){_limHold.store(v);_dirty.store(true);} float limHoldMs() const{return _limHold.load();}
	void setLimReleaseMs(float v){_limRel.store(v);_dirty.store(true);} float limReleaseMs() const{return _limRel.load();}

	/* Largeur stéréo M/S (Strip stéréo) : 0=mono 1=normal 2=large */
	void  setWidth (float w) { _width.store (w < 0.f ? 0.f : w); }
	float width () const { return _width.load (); }

	/* ====================== Warmth (Strip + Bus) ====================== */
	void setWarmthOn (bool on){_warmthOn.store(on);_dirty.store(true);} bool warmthOn() const{return _warmthOn.load();}
	void setWarmthAmount(float pct){_warmthAmt.store(pct);_dirty.store(true);} float warmthAmount() const{return _warmthAmt.load();}
	void setWarmthTrimDb(float dB){_warmthTrim.store(dB);_dirty.store(true);} float warmthTrimDb() const{return _warmthTrim.load();}

	/* ======================= Bus comp (Bus) ======================= */
	void setBusCompOn (bool on){_bcOn.store(on);_dirty.store(true);} bool busCompOn() const{return _bcOn.load();}
	void setBusCompThreshDb(float v){_bcThr.store(v);_dirty.store(true);} float busCompThreshDb() const{return _bcThr.load();}
	void setBusCompRatioCtrl(float v){_bcRatio.store(v);_dirty.store(true);} float busCompRatioCtrl() const{return _bcRatio.load();}
	void setBusCompAttackMs(float v){_bcAtt.store(v);_dirty.store(true);} float busCompAttackMs() const{return _bcAtt.load();}
	void setBusCompReleaseMs(float v){_bcRel.store(v);_dirty.store(true);} float busCompReleaseMs() const{return _bcRel.load();}
	void setBusCompMakeupDb(float v){_bcMakeup.store(v);_dirty.store(true);} float busCompMakeupDb() const{return _bcMakeup.load();}

	/* ====================== Tape3348 (Strip / par piste) ====================== */
	void setTapeOn (bool on){_tapeOn.store(on);_dirty.store(true);} bool tapeOn() const{return _tapeOn.load();}
	void setTapeDrive(float v){_tapeDrive.store(v);_dirty.store(true);} float tapeDrive() const{return _tapeDrive.load();}
	void setTapeEmphasis(float v){_tapeEmph.store(v);_dirty.store(true);} float tapeEmphasis() const{return _tapeEmph.load();}
	void setTapeGrain(float v){_tapeGrain.store(v);_dirty.store(true);} float tapeGrain() const{return _tapeGrain.load();}

	/* ================ Limiter brickwall master (Bus) ================ */
	void setBusLimOn (bool on){_busLimOn.store(on);_dirty.store(true);} bool busLimOn() const{return _busLimOn.load();}
	void setBusLimCeilDb(float v){_busLimCeil.store(v);_dirty.store(true);} float busLimCeilDb() const{return _busLimCeil.load();}
	void setBusLimAttMs(float v){_busLimAtt.store(v);_dirty.store(true);} float busLimAttMs() const{return _busLimAtt.load();}
	void setBusLimRelMs(float v){_busLimRel.store(v);_dirty.store(true);} float busLimRelMs() const{return _busLimRel.load();}
	void setBusLimKneeDb(float v){_busLimKnee.store(v);_dirty.store(true);} float busLimKneeDb() const{return _busLimKnee.load();}
	void setBusLimEnhance(float v){_busLimEnh.store(v);_dirty.store(true);} float busLimEnhance() const{return _busLimEnh.load();}
	void setBusLimSafe(bool on){_busLimSafe.store(on);_dirty.store(true);} bool busLimSafe() const{return _busLimSafe.load();}
	void setBusLimAutoComp(bool on){_busLimAC.store(on);_dirty.store(true);} bool busLimAutoComp() const{return _busLimAC.load();}
	float busLimTruePeakDb ();
	/* mesures du limiteur de sortie (lecture GUI) */
	float busLimGrDb ();
	float busLimReconDb ();

	/* ====== MasterTape PCM-1630 (stage Dac) : trims autour du réseau NAM ======
	 * In = attaque du modèle (reculer pour désaturer), Out = compensation. */
	void setMtInDb (float v){_mtIn.store(v);_dirty.store(true);} float mtInDb() const{return _mtIn.load();}
	void setMtOutDb(float v){_mtOut.store(v);_dirty.store(true);} float mtOutDb() const{return _mtOut.load();}

	/* Bus FX (delay/reverb) : tape mais pas de comp */
	void setFxBus (bool on) { _fxBus.store (on); if (on) { _bcOn.store (false); } _dirty.store (true); }
	bool isFxBus () const { return _fxBus.load (); }

	/* ----------------------- Metering ----------------------- */
	float gainReductionDb ();  // comp (Strip ou Bus)
	float limiterGrDb ();      // limiteur brickwall (Bus)
	float gateGrDb ();         // réduction Gate (Strip)
	float expGrDb ();          // réduction Expander (Strip)
	float dynLimGrDb ();       // réduction Limiter de la section Dynamics (Strip)
	float busLevel ();         // niveau de sortie linéaire (Bus, VU)
	float dynInputDb ();       // niveau du sidechain Dynamics (Strip) : point mobile du graphe

	/* Persistance session */
	int set_state (const XMLNode&, int version);

protected:
	XMLNode& state () const;

private:
	Kind             _kind;
	OxfordBus::Stage _stage { OxfordBus::Full };
	double _sr;

	std::vector<OxfordChannelStrip> _strips;
	std::vector<OxfordBus>          _buses;

	std::atomic<bool>  _dirty { true };

	/* ---- EQ ---- */
	std::atomic<bool>  _eqOn { true };
	std::atomic<int>   _curveType { 2 };  // NeveG
	std::atomic<float> _bGain[5] { {0.f},{0.f},{0.f},{0.f},{0.f} };
	std::atomic<float> _bFreq[5] { {60.f},{250.f},{1000.f},{4000.f},{12000.f} };
	std::atomic<float> _bQ[5]    { {2.83f},{2.83f},{2.83f},{2.83f},{2.83f} };   // défaut Oxford : 2.83 (centre log)
	std::atomic<bool>  _bEn[5]   { {true},{true},{true},{true},{true} };  // IN par bande
	std::atomic<bool>  _lfShelf { true }, _hfShelf { true };
	std::atomic<float> _lfOver { 0.f }, _hfOver { 0.f };
	std::atomic<bool>  _hpfOn { false }, _lpfOn { false };
	std::atomic<float> _hpfHz { 80.f }, _lpfHz { 18000.f };
	std::atomic<float> _hpfSlope { 12.f }, _lpfSlope { 12.f };

	/* ---- Dynamics ---- */
	std::atomic<int>   _timingLaw { 0 };   // Normal
	std::atomic<float> _lookMs { 0.f };
	std::atomic<bool>  _gateOn { false };
	std::atomic<float> _gateThr { -80.f }, _gateRange { -80.f }, _gateAtt { 0.005f }, _gateRel { 7.8f };
	std::atomic<bool>  _expOn { false };
	std::atomic<float> _expThr { 0.f }, _expRatio { 2.f }, _expRange { -40.f }, _expAtt { 5.2f }, _expRel { 51.9f };
	std::atomic<bool>  _compOn { false };
	std::atomic<float> _compThr { 0.f }, _compRatio { 0.5f }, _compMakeup { 0.f }, _compAtt { 5.2f }, _compRel { 127.f }, _compSoft { 0.f };
	std::atomic<float> _compHold { 10.f };   // spec OXF-R3 : 10 ms .. 30 s
	std::atomic<bool>  _limOn { false };
	std::atomic<float> _limThr { -0.3f }, _limAtt { 0.3f }, _limRel { 150.f };
	std::atomic<float> _limHold { 50.f };    // spec OXF-R3 : 50 ms .. 30 s
	std::atomic<float> _width { 1.f };

	/* ---- Warmth ---- */
	std::atomic<bool>  _warmthOn { false };
	std::atomic<float> _warmthAmt { 50.f }, _warmthTrim { 0.f };

	/* ---- Bus comp ---- */
	std::atomic<bool>  _bcOn { false };
	std::atomic<float> _bcThr { -18.f }, _bcRatio { 0.3f }, _bcAtt { 30.f }, _bcRel { 200.f }, _bcMakeup { 0.f };

	/* ---- Tape3348 ---- */
	std::atomic<bool>  _tapeOn { true };   // ON par défaut (choix utilisateur : couleur 3348 d'office)
	std::atomic<float> _tapeDrive { 0.3f }, _tapeEmph { 0.f }, _tapeGrain { 0.3f };

	/* ---- Limiter master ---- */
	std::atomic<bool>  _busLimOn { false };
	std::atomic<float> _busLimCeil { -0.3f };
	std::atomic<float> _busLimAtt  { 0.052f };   /* défaut Sonnox */
	std::atomic<float> _busLimRel  { 7.34f };    /* défaut Sonnox */
	std::atomic<float> _busLimKnee { 0.0f };
	std::atomic<float> _busLimEnh  { 0.0f };
	std::atomic<bool>  _busLimSafe { false };
	std::atomic<bool>  _busLimAC   { false };
	std::atomic<bool>  _fxBus { false };

	/* ---- MasterTape PCM-1630 (stage Dac) ---- */
	std::atomic<float> _mtIn { 0.f }, _mtOut { 0.f };

	void prepare_all ();
	void applyParams ();
};

} // namespace ARDOUR
