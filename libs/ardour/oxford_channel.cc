/*
 * OxfordChannel — implémentation. Voir oxford_channel.h.
 */
#include "ardour/oxford_channel.h"

#include <algorithm>
#include <cmath>

#include "pbd/xml++.h"
#include "pbd/compose.h"
#include "pbd/file_utils.h"
#include "pbd/search_path.h"

#include "ardour/filesystem_paths.h"

#include "ardour/audio_buffer.h"
#include "ardour/buffer_set.h"
#include "ardour/session.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace std;

OxfordChannel::OxfordChannel (Session& s, Kind k, const std::string& name)
	: Processor (s, name, Temporal::TimeDomainProvider (Temporal::AudioTime))
	, _kind (k)
	, _sr ((double) s.sample_rate ())
{
	set_display_to_user (false);
}

bool
OxfordChannel::can_support_io_configuration (const ChanCount& in, ChanCount& out)
{
	out = in;
	return true;
}

bool
OxfordChannel::configure_io (ChanCount in, ChanCount out)
{
	if (out != in) {
		return false;
	}

	_sr = (double) _session.sample_rate ();
	const uint32_t n = in.n_audio ();

	if (_kind == Strip) {
		_strips.resize (n);
		_buses.clear ();
	} else {
		_buses.resize (n);
		/* Bus simple (stage Full) = canal RETURN OXF-R3 : EQ 3 bandes + Gate/Comp
		 * (spec "Return Channels" du manuel). Les étages master (Front/Tail/Dac)
		 * restent des bus purs. */
		if (_stage == OxfordBus::Full) {
			_strips.resize (n);
		} else {
			_strips.clear ();
		}
	}
	prepare_all ();

	return Processor::configure_io (in, out);
}

void
OxfordChannel::prepare_all ()
{
	for (auto& s : _strips) { s.prepare (_sr); }

	/* chemin du .nam (capture PCM-1630) — bundlé dans resources, résolu (PAS hardcodé).
	 * Le module NAM est le processor SÉPARÉ (stage Dac), visible/bypassable. */
	std::string namPath;
	if (_stage == OxfordBus::Dac) {
		PBD::Searchpath sp (ARDOUR::ardour_data_search_path ());
		sp.add_subdirectory_to_paths ("resources");
		std::string f;
		if (PBD::find_file (sp, "Oxford-pcm1630.nam", f)) { namPath = f; }
	}

	for (auto& b : _buses) {
		b.prepare (_sr); b.setStage (_stage);
		if (_stage == OxfordBus::Dac) { b.loadMasterTape (_sr, 8192, namPath); }
	}
	_dirty.store (true);
}

void
OxfordChannel::applyParams ()
{
	if (_kind == Strip) {
		for (auto& s : _strips) {
			/* ---- EQ ---- */
			OxfordEQ& eq = s.equaliser ();
			eq.setEQEnabled (_eqOn.load ());
			eq.setCurveType ((OxfordEQ::CurveType) _curveType.load ());
			for (int b = 0; b < 5; ++b) {
				eq.setBand ((OxfordEQ::Band) b, _bFreq[b].load (), _bGain[b].load (), _bQ[b].load ());
				eq.setBandEnabled ((OxfordEQ::Band) b, _bEn[b].load ());   // IN par bande
			}
			eq.setLFShelf (_lfShelf.load ());
			eq.setHFShelf (_hfShelf.load ());
			eq.setLFShelfOvershoot (_lfOver.load ());
			eq.setHFShelfOvershoot (_hfOver.load ());
			eq.setHPF (_hpfHz.load (), _hpfSlope.load (), _hpfOn.load ());
			eq.setLPF (_lpfHz.load (), _lpfSlope.load (), _lpfOn.load ());

			/* ---- Dynamics ---- */
			OxfordDynamics& d = s.dynamics ();
			d.setTimingLaw ((OxfordDynamics::TimingLaw) _timingLaw.load ());
			d.setLookaheadMs (_lookMs.load ());
			d.setGateEnabled (_gateOn.load ());
			d.setGateThresholdDb (_gateThr.load ());
			d.setGateRangeDb (_gateRange.load ());
			d.setGateAttackMs (_gateAtt.load ());
			d.setGateReleaseMs (_gateRel.load ());
			d.setExpEnabled (_expOn.load ());
			d.setExpThresholdDb (_expThr.load ());
			d.setExpRatio (_expRatio.load ());
			d.setExpRangeDb (_expRange.load ());
			d.setExpAttackMs (_expAtt.load ());
			d.setExpReleaseMs (_expRel.load ());
			d.setCompEnabled (_compOn.load ());
			d.setCompThreshold (_compThr.load ());
			d.setCompRatioControl (_compRatio.load ());
			d.setCompMakeupDb (_compMakeup.load ());
			d.setCompAttackMs (_compAtt.load ());
			d.setCompHoldMs (_compHold.load ());
			d.setCompReleaseMs (_compRel.load ());
			d.setCompSoftRatioDb (_compSoft.load ());
			d.setLimEnabled (_limOn.load ());
			d.setLimThresholdDb (_limThr.load ());
			d.setLimAttackMs (_limAtt.load ());
			d.setLimHoldMs (_limHold.load ());
			d.setLimReleaseMs (_limRel.load ());

			/* ---- Warmth ---- */
			s.setWarmthEnabled (_warmthOn.load ());
			s.warmthMod ().setAmount (_warmthAmt.load ());
			s.warmthMod ().setMaxTrimDb (_warmthTrim.load ());

			/* ---- Tape3348 (insert PAR PISTE : étape d'enregistrement, en tête de chaîne) ---- */
			s.setTapeEnabled (_tapeOn.load ());
			Tape3348Color& tp = s.tapeColor ();
			tp.setDrive    (_tapeDrive.load ());
			tp.setEmphasis (_tapeEmph.load ());
			tp.setGrain    (_tapeGrain.load ());
		}
	} else {
		/* ---- Canal RETURN (bus simple, stage Full) : spec OXF-R3 "Return Channels" =
		 * EQ 3 bandes LF/MF/HF (peak/shelf, PAS de LMF/HMF ni de filtres à pente)
		 * + Gate + Compresseur (PAS d'expander ni de limiteur). Tape jamais. ---- */
		for (auto& s : _strips) {
			OxfordEQ& eq = s.equaliser ();
			eq.setEQEnabled (_eqOn.load ());
			eq.setCurveType ((OxfordEQ::CurveType) _curveType.load ());
			for (int b = 0; b < 5; ++b) {
				const bool allowed = (b == 0 || b == 2 || b == 4);
				eq.setBand ((OxfordEQ::Band) b, _bFreq[b].load (), allowed ? _bGain[b].load () : 0.f, _bQ[b].load ());
				eq.setBandEnabled ((OxfordEQ::Band) b, allowed && _bEn[b].load ());
			}
			eq.setLFShelf (_lfShelf.load ());
			eq.setHFShelf (_hfShelf.load ());
			eq.setLFShelfOvershoot (_lfOver.load ());
			eq.setHFShelfOvershoot (_hfOver.load ());
			eq.setHPF (_hpfHz.load (), _hpfSlope.load (), false);
			eq.setLPF (_lpfHz.load (), _lpfSlope.load (), false);

			OxfordDynamics& d = s.dynamics ();
			d.setTimingLaw ((OxfordDynamics::TimingLaw) _timingLaw.load ());
			d.setLookaheadMs (_lookMs.load ());
			d.setGateEnabled (_gateOn.load ());
			d.setGateThresholdDb (_gateThr.load ());
			d.setGateRangeDb (_gateRange.load ());
			d.setGateAttackMs (_gateAtt.load ());
			d.setGateReleaseMs (_gateRel.load ());
			d.setExpEnabled (false);
			d.setCompEnabled (_compOn.load ());
			d.setCompThreshold (_compThr.load ());
			d.setCompRatioControl (_compRatio.load ());
			d.setCompMakeupDb (_compMakeup.load ());
			d.setCompAttackMs (_compAtt.load ());
			d.setCompHoldMs (_compHold.load ());
			d.setCompReleaseMs (_compRel.load ());
			d.setCompSoftRatioDb (_compSoft.load ());
			d.setLimEnabled (false);

			s.setWarmthEnabled (false);   // le Warmth de bus vit dans OxfordBus
			s.setTapeEnabled (false);     // un return ne passe pas par le magnéto
		}
		for (auto& b : _buses) {
			/* ---- Bus comp ---- */
			b.setCompEnabled (_bcOn.load ());
			OxfordDynamics& d = b.busComp ();
			d.setTimingLaw ((OxfordDynamics::TimingLaw) _timingLaw.load ());
			d.setCompThreshold (_bcThr.load ());
			d.setCompRatioControl (_bcRatio.load ());
			d.setCompAttackMs (_bcAtt.load ());
			d.setCompReleaseMs (_bcRel.load ());
			d.setCompMakeupDb (_bcMakeup.load ());

			/* ---- Warmth ---- */
			b.setWarmthEnabled (_warmthOn.load ());
			b.warmthMod ().setAmount (_warmthAmt.load ());
			b.warmthMod ().setMaxTrimDb (_warmthTrim.load ());

			/* OXFORD : pas de tape/coloration de bus (sommation propre, ≠ Mixbus) ;
			 * la micro-distorsion convertisseur est automatique sur le master (Tail). */

			/* ---- Limiter master ---- */
			b.setLimiterEnabled (_busLimOn.load ());
			b.setLimiterCeilingDb (_busLimCeil.load ());

			/* ---- MasterTape PCM-1630 (stage Dac) : trims In/Out du NAM ---- */
			if (_stage == OxfordBus::Dac) {
				b.masterTapeMod ().setInputGainDb ((double) _mtIn.load ());
				b.masterTapeMod ().setOutputGainDb ((double) _mtOut.load ());
			}
		}
	}
}

double
OxfordChannel::eqCurveDb (double hz)
{
	/* réponse réelle de l'EQ du 1er canal (les deux canaux ont les mêmes
	 * cibles) — utilisée par l'écran EQ du panel pour tracer la vraie courbe */
	if (_strips.empty ()) { return 0.0; }
	return _strips[0].equaliser ().responseDb (hz);
}

float
OxfordChannel::gainReductionDb ()
{
	float gr = 0.f;
	for (auto& s : _strips) { const float g = (float) s.dynamics ().compReductionDb (); if (g > gr) gr = g; }
	for (auto& b : _buses)  { const float g = (float) b.gainReductionDb ();              if (g > gr) gr = g; }
	return gr;
}

float
OxfordChannel::limiterGrDb ()
{
	float gr = 0.f;
	for (auto& s : _strips) { const float g = (float) s.dynamics ().limReductionDb (); if (g > gr) gr = g; }
	for (auto& b : _buses)  { const float g = (float) b.limiterReductionDb ();          if (g > gr) gr = g; }
	return gr;
}

float
OxfordChannel::gateGrDb ()
{
	float gr = 0.f;
	for (auto& s : _strips) { const float g = (float) s.dynamics ().gateReductionDb (); if (g > gr) gr = g; }
	return gr;
}

float
OxfordChannel::expGrDb ()
{
	float gr = 0.f;
	for (auto& s : _strips) { const float g = (float) s.dynamics ().expReductionDb (); if (g > gr) gr = g; }
	return gr;
}

float
OxfordChannel::dynLimGrDb ()
{
	float gr = 0.f;
	for (auto& s : _strips) { const float g = (float) s.dynamics ().limReductionDb (); if (g > gr) gr = g; }
	return gr;
}

float
OxfordChannel::busLevel ()
{
	float lv = 0.f;
	for (auto& b : _buses) { const float l = b.outLevel (); if (l > lv) lv = l; }
	return lv;
}

XMLNode&
OxfordChannel::state () const
{
	XMLNode& node = Processor::state ();
	node.set_property (X_("type"),  X_("oxford-channel"));
	node.set_property (X_("kind"),  _kind == Strip ? X_("strip") : X_("bus"));
	node.set_property (X_("stage"), _stage == OxfordBus::Front ? X_("front")
	                              : (_stage == OxfordBus::Tail  ? X_("tail")
	                              : (_stage == OxfordBus::Dac   ? X_("dac") : X_("full"))));

	node.set_property (X_("eq-on"),      _eqOn.load ());
	node.set_property (X_("curve-type"), _curveType.load ());
	for (int b = 0; b < 5; ++b) {
		node.set_property (string_compose ("band%1-gain", b).c_str (), _bGain[b].load ());
		node.set_property (string_compose ("band%1-freq", b).c_str (), _bFreq[b].load ());
		node.set_property (string_compose ("band%1-q",    b).c_str (), _bQ[b].load ());
		node.set_property (string_compose ("band%1-in",   b).c_str (), _bEn[b].load ());
	}
	node.set_property (X_("lf-shelf"),  _lfShelf.load ());
	node.set_property (X_("hf-shelf"),  _hfShelf.load ());
	node.set_property (X_("lf-over"),   _lfOver.load ());
	node.set_property (X_("hf-over"),   _hfOver.load ());
	node.set_property (X_("hpf-on"),    _hpfOn.load ());
	node.set_property (X_("hpf-hz"),    _hpfHz.load ());
	node.set_property (X_("hpf-slope"), _hpfSlope.load ());
	node.set_property (X_("lpf-on"),    _lpfOn.load ());
	node.set_property (X_("lpf-hz"),    _lpfHz.load ());
	node.set_property (X_("lpf-slope"), _lpfSlope.load ());

	node.set_property (X_("timing-law"),  _timingLaw.load ());
	node.set_property (X_("lookahead"),   _lookMs.load ());
	node.set_property (X_("gate-on"),     _gateOn.load ());
	node.set_property (X_("gate-thresh"), _gateThr.load ());
	node.set_property (X_("gate-range"),  _gateRange.load ());
	node.set_property (X_("gate-att"),    _gateAtt.load ());
	node.set_property (X_("gate-rel"),    _gateRel.load ());
	node.set_property (X_("exp-on"),      _expOn.load ());
	node.set_property (X_("exp-thresh"),  _expThr.load ());
	node.set_property (X_("exp-ratio"),   _expRatio.load ());
	node.set_property (X_("exp-range"),   _expRange.load ());
	node.set_property (X_("exp-att"),     _expAtt.load ());
	node.set_property (X_("exp-rel"),     _expRel.load ());
	node.set_property (X_("comp-on"),     _compOn.load ());
	node.set_property (X_("comp-thresh"), _compThr.load ());
	node.set_property (X_("comp-ratio"),  _compRatio.load ());
	node.set_property (X_("comp-makeup"), _compMakeup.load ());
	node.set_property (X_("comp-att"),    _compAtt.load ());
	node.set_property (X_("comp-hold"),   _compHold.load ());
	node.set_property (X_("comp-rel"),    _compRel.load ());
	node.set_property (X_("comp-soft"),   _compSoft.load ());
	node.set_property (X_("lim-on"),      _limOn.load ());
	node.set_property (X_("lim-thresh"),  _limThr.load ());
	node.set_property (X_("lim-att"),     _limAtt.load ());
	node.set_property (X_("lim-hold"),    _limHold.load ());
	node.set_property (X_("lim-rel"),     _limRel.load ());
	node.set_property (X_("width"),       _width.load ());

	node.set_property (X_("warmth-on"),     _warmthOn.load ());
	node.set_property (X_("warmth-amount"), _warmthAmt.load ());
	node.set_property (X_("warmth-trim"),   _warmthTrim.load ());

	node.set_property (X_("buscomp-on"),      _bcOn.load ());
	node.set_property (X_("buscomp-thresh"),  _bcThr.load ());
	node.set_property (X_("buscomp-ratio"),   _bcRatio.load ());
	node.set_property (X_("buscomp-att"),     _bcAtt.load ());
	node.set_property (X_("buscomp-rel"),     _bcRel.load ());
	node.set_property (X_("buscomp-makeup"),  _bcMakeup.load ());

	node.set_property (X_("tape-on"),    _tapeOn.load ());
	node.set_property (X_("tape-drive"), _tapeDrive.load ());
	node.set_property (X_("tape-emph"),  _tapeEmph.load ());
	node.set_property (X_("tape-grain"), _tapeGrain.load ());

	node.set_property (X_("buslim-on"),   _busLimOn.load ());
	node.set_property (X_("buslim-ceil"), _busLimCeil.load ());
	node.set_property (X_("fx-bus"),      _fxBus.load ());
	node.set_property (X_("mt-in"),       _mtIn.load ());
	node.set_property (X_("mt-out"),      _mtOut.load ());

	return node;
}

int
OxfordChannel::set_state (const XMLNode& node, int version)
{
	Processor::set_state (node, version);

	bool b; float f; int i;

	if (node.get_property (X_("eq-on"),      b)) { _eqOn.store (b); }
	if (node.get_property (X_("curve-type"), i)) { _curveType.store (i); }
	for (int k = 0; k < 5; ++k) {
		if (node.get_property (string_compose ("band%1-gain", k).c_str (), f)) { _bGain[k].store (f); }
		if (node.get_property (string_compose ("band%1-freq", k).c_str (), f)) { _bFreq[k].store (f); }
		if (node.get_property (string_compose ("band%1-q",    k).c_str (), f)) { _bQ[k].store (f); }
		if (node.get_property (string_compose ("band%1-in",   k).c_str (), b)) { _bEn[k].store (b); }
	}
	if (node.get_property (X_("lf-shelf"),  b)) { _lfShelf.store (b); }
	if (node.get_property (X_("hf-shelf"),  b)) { _hfShelf.store (b); }
	if (node.get_property (X_("lf-over"),   f)) { _lfOver.store (f); }
	if (node.get_property (X_("hf-over"),   f)) { _hfOver.store (f); }
	if (node.get_property (X_("hpf-on"),    b)) { _hpfOn.store (b); }
	if (node.get_property (X_("hpf-hz"),    f)) { _hpfHz.store (f); }
	if (node.get_property (X_("hpf-slope"), f)) { _hpfSlope.store (f); }
	if (node.get_property (X_("lpf-on"),    b)) { _lpfOn.store (b); }
	if (node.get_property (X_("lpf-hz"),    f)) { _lpfHz.store (f); }
	if (node.get_property (X_("lpf-slope"), f)) { _lpfSlope.store (f); }

	if (node.get_property (X_("timing-law"),  i)) { _timingLaw.store (i); }
	if (node.get_property (X_("lookahead"),   f)) { _lookMs.store (f); }
	if (node.get_property (X_("gate-on"),     b)) { _gateOn.store (b); }
	if (node.get_property (X_("gate-thresh"), f)) { _gateThr.store (f); }
	if (node.get_property (X_("gate-range"),  f)) { _gateRange.store (f); }
	if (node.get_property (X_("gate-att"),    f)) { _gateAtt.store (f); }
	if (node.get_property (X_("gate-rel"),    f)) { _gateRel.store (f); }
	if (node.get_property (X_("exp-on"),      b)) { _expOn.store (b); }
	if (node.get_property (X_("exp-thresh"),  f)) { _expThr.store (f); }
	if (node.get_property (X_("exp-ratio"),   f)) { _expRatio.store (f); }
	if (node.get_property (X_("exp-range"),   f)) { _expRange.store (f); }
	if (node.get_property (X_("exp-att"),     f)) { _expAtt.store (f); }
	if (node.get_property (X_("exp-rel"),     f)) { _expRel.store (f); }
	if (node.get_property (X_("comp-on"),     b)) { _compOn.store (b); }
	if (node.get_property (X_("comp-thresh"), f)) { _compThr.store (f); }
	if (node.get_property (X_("comp-ratio"),  f)) { _compRatio.store (f); }
	if (node.get_property (X_("comp-makeup"), f)) { _compMakeup.store (f); }
	if (node.get_property (X_("comp-att"),    f)) { _compAtt.store (f); }
	if (node.get_property (X_("comp-hold"),   f)) { _compHold.store (f); }
	if (node.get_property (X_("comp-rel"),    f)) { _compRel.store (f); }
	if (node.get_property (X_("comp-soft"),   f)) { _compSoft.store (f); }
	if (node.get_property (X_("lim-on"),      b)) { _limOn.store (b); }
	if (node.get_property (X_("lim-thresh"),  f)) { _limThr.store (f); }
	if (node.get_property (X_("lim-att"),     f)) { _limAtt.store (f); }
	if (node.get_property (X_("lim-hold"),    f)) { _limHold.store (f); }
	if (node.get_property (X_("lim-rel"),     f)) { _limRel.store (f); }
	if (node.get_property (X_("width"),       f)) { _width.store (f); }

	if (node.get_property (X_("warmth-on"),     b)) { _warmthOn.store (b); }
	if (node.get_property (X_("warmth-amount"), f)) { _warmthAmt.store (f); }
	if (node.get_property (X_("warmth-trim"),   f)) { _warmthTrim.store (f); }

	if (node.get_property (X_("buscomp-on"),     b)) { _bcOn.store (b); }
	if (node.get_property (X_("buscomp-thresh"), f)) { _bcThr.store (f); }
	if (node.get_property (X_("buscomp-ratio"),  f)) { _bcRatio.store (f); }
	if (node.get_property (X_("buscomp-att"),    f)) { _bcAtt.store (f); }
	if (node.get_property (X_("buscomp-rel"),    f)) { _bcRel.store (f); }
	if (node.get_property (X_("buscomp-makeup"), f)) { _bcMakeup.store (f); }

	if (node.get_property (X_("tape-on"),    b)) { _tapeOn.store (b); }
	if (node.get_property (X_("tape-drive"), f)) { _tapeDrive.store (f); }
	if (node.get_property (X_("tape-emph"),  f)) { _tapeEmph.store (f); }
	if (node.get_property (X_("tape-grain"), f)) { _tapeGrain.store (f); }

	if (node.get_property (X_("buslim-on"),   b)) { _busLimOn.store (b); }
	if (node.get_property (X_("buslim-ceil"), f)) { _busLimCeil.store (f); }
	if (node.get_property (X_("fx-bus"),      b)) { _fxBus.store (b); }
	if (node.get_property (X_("mt-in"),       f)) { _mtIn.store (f); }
	if (node.get_property (X_("mt-out"),      f)) { _mtOut.store (f); }

	_dirty.store (true);
	return 0;
}

void
OxfordChannel::run (BufferSet& bufs, samplepos_t /*start*/, samplepos_t /*end*/,
                    double /*speed*/, pframes_t nframes, bool /*result_required*/)
{
	if (!check_active ()) {
		return;
	}

	if (_dirty.exchange (false)) {
		applyParams ();
	}

	/* PISTE STÉRÉO : STEREO-LINK de la dynamique (OXF-R3) — les deux canaux
	 * partagent le même sidechain (max des niveaux post-EQ) -> réduction
	 * identique L/R, image stable. Puis LARGEUR M/S en sortie. */
	if (_kind == Strip && _strips.size () == 2) {
		BufferSet::audio_iterator it = bufs.audio_begin ();
		Sample* d0 = it->data (); ++it;
		Sample* d1 = it->data ();
		OxfordChannelStrip& s0 = _strips[0];
		OxfordChannelStrip& s1 = _strips[1];
		const double wdth = (double) _width.load ();
		/* le sidechain (log10/éch.) ne se calcule que si la dynamique travaille */
		const bool dynOn = s0.dynamics ().anyEnabled () || s1.dynamics ().anyEnabled ();
		for (pframes_t n = 0; n < nframes; ++n) {
			const double x0 = s0.stagePre ((double) d0[n]);
			const double x1 = s1.stagePre ((double) d1[n]);
			double scDb = -120.0;
			if (dynOn) {
				const double lvl = std::max (std::fabs (x0), std::fabs (x1));
				scDb = 20.0 * std::log10 (lvl + 1e-12);
			}
			const double y0 = s0.stagePost (s0.stageDyn (x0, scDb));
			const double y1 = s1.stagePost (s1.stageDyn (x1, scDb));
			const double mid  = 0.5 * (y0 + y1);
			const double side = 0.5 * (y0 - y1) * wdth;
			d0[n] = (Sample) (mid + side);
			d1[n] = (Sample) (mid - side);
		}
		return;
	}

	/* BUS STÉRÉO (Front/Full) : étage RETURN (EQ 3 bandes + Gate/Comp, stage Full
	 * seulement) puis bus-comp, chacun en STEREO-LINK (sidechain partagé). */
	if (_kind == Bus && _buses.size () == 2
	    && (_stage == OxfordBus::Front || _stage == OxfordBus::Full)) {
		BufferSet::audio_iterator it = bufs.audio_begin ();
		Sample* d0 = it->data (); ++it;
		Sample* d1 = it->data ();
		OxfordBus& b0 = _buses[0];
		OxfordBus& b1 = _buses[1];
		const bool ret = (_stage == OxfordBus::Full && _strips.size () == 2);
		const bool retDyn = ret && (_strips[0].dynamics ().anyEnabled () || _strips[1].dynamics ().anyEnabled ());
		const bool bcOn   = b0.compEnabled ();   // sidechain bus-comp inutile sinon
		for (pframes_t n = 0; n < nframes; ++n) {
			double x0 = (double) d0[n];
			double x1 = (double) d1[n];
			if (ret) {
				x0 = _strips[0].stagePre (x0);
				x1 = _strips[1].stagePre (x1);
				if (retDyn) {
					const double rl   = std::max (std::fabs (x0), std::fabs (x1));
					const double rsDb = 20.0 * std::log10 (rl + 1e-12);
					x0 = _strips[0].stageDyn (x0, rsDb);
					x1 = _strips[1].stageDyn (x1, rsDb);
				}
			}
			double scDb = -120.0;
			if (bcOn) {
				const double l = std::max (std::fabs (x0), std::fabs (x1));
				scDb = 20.0 * std::log10 (l + 1e-12);
			}
			d0[n] = (Sample) b0.processSampleSC (x0, scDb);
			d1[n] = (Sample) b1.processSampleSC (x1, scDb);
		}
		return;
	}

	uint32_t ch = 0;
	for (BufferSet::audio_iterator i = bufs.audio_begin (); i != bufs.audio_end (); ++i, ++ch) {
		Sample* d = i->data ();
		if (_kind == Strip) {
			if (ch >= _strips.size ()) { break; }
			OxfordChannelStrip& s = _strips[ch];
			for (pframes_t n = 0; n < nframes; ++n) { d[n] = (Sample) s.processSample ((double) d[n]); }
		} else {
			if (ch >= _buses.size ()) { break; }
			if (_stage == OxfordBus::Full && ch < _strips.size ()) {
				/* étage RETURN mono (EQ 3 bandes + Gate/Comp) avant le bus */
				OxfordChannelStrip& s = _strips[ch];
				for (pframes_t n = 0; n < nframes; ++n) { d[n] = (Sample) s.processSample ((double) d[n]); }
			}
			OxfordBus& b = _buses[ch];
			b.processBlock (d, (int) nframes);   // bloc (NAM PCM-1630 sur le Tail, cf. OxfordBus)
		}
	}
}
