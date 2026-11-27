// Ported from SUPER KNOBBER LEAD v1.2 (9db41af6660b1ac8).
#include "Engine.h"

namespace knobber {
using std::abs;
static double polyblep(double t, double dt) {
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1;
    }
    if (t > 1 - dt) {
        const double x = (t - 1) / dt;
        return x * x + x + x + 1;
    }
    return 0;
}
static double vosc(double phase, double dt, double wave, double pulseWidth) {
    const double tri = 1 - 4 * abs(phase - 0.5);
    const double saw = 2 * phase - 1 - polyblep(phase, dt);
    if (wave < 0.5)
        return mix(tri, saw, wave * 2);
    const double pw = clamp(pulseWidth, 0.05, 0.95);
    const double pulse =
        (phase < pw ? 1 : -1) + polyblep(phase, dt) - polyblep(wrap(phase + 1 - pw, 0, 1), dt);
    return mix(saw, pulse, (wave - 0.5) * 2);
}

Stereo Voice::process(double samplerate, const Parameters &p, const Note &note) {
    Stereo result;
    double sr = samplerate;
    double oldg = prevgate;
    double keyg = note.kgate > 0;
    double g = keyg || ((p.sustain > 0.5) && (oldg > 0));
    double newnote = note.ktrig != prevtrig;
    prevtrig = note.ktrig;
    double rising = g && (oldg == 0);
    double falling = (!g) && (oldg > 0);
    prevgate = g;
    double monoLegato = p.mono_mode > 0.5;
    double envtrig = rising || ((p.env_retrig > 0.5) && newnote && g && !monoLegato);
    double vm = velm;
    if (envtrig)
        vm = clamp(note.kvel / 127, 0, 1);
    velm = vm;
    double vscale = 1 - clamp(p.vel_amp * 0.01, 0, 1) * (1 - vm);
    double vf_oct = (vm - 0.5) * 4 * clamp(p.vel_filter * 0.01, 0, 1);
    double sc = 1 - exp(-1 / (0.004 * sr));
    double sl = 1 - exp(-1 / (0.008 * sr));
    double cs = csm + (p.cutoff - csm) * sc;
    csm = cs;
    double rs = rsm + (p.reso - rsm) * sc;
    rsm = rs;
    double ds = drvsm + (p.drive - drvsm) * sc;
    drvsm = ds;
    double l1 = l1sm + (p.o1_lvl - l1sm) * sl;
    l1sm = l1;
    double l2 = l2sm + (p.o2_lvl - l2sm) * sl;
    l2sm = l2;
    double l3 = l3sm + (p.o3_lvl - l3sm) * sl;
    l3sm = l3;
    double nzl = nzsm + (p.nz_lvl - nzsm) * sl;
    nzsm = nzl;
    double subl = subsm + (p.sub_lvl - subsm) * sl;
    subsm = subl;
    double ringl = ringsm + (p.ring_lvl - ringsm) * sl;
    ringsm = ringl;
    double fbl = fbsm + (p.fb_lvl - fbsm) * sl;
    fbsm = fbl;
    double sats = satsm + (p.sat - satsm) * sl;
    satsm = sats;
    double vols = volsm + (p.volume - volsm) * sl;
    volsm = vols;
    double cur = nn;
    if (started == 0)
        cur = note.knote;
    started = 1;
    double gtime = p.glide * 0.001;
    if ((p.porta_on > 0.5) && (gtime > 0.0005))
        cur = cur + (note.knote - cur) * (1 - exp(-1 / (gtime * sr)));
    else
        cur = note.knote;
    nn = cur;
    double pe = penv;
    double pst = pstage;
    if (envtrig) {
        if (p.penv_attack <= 0.1) {
            pe = 1;
            pst = 2;
        } else {
            pe = 0;
            pst = 1;
        }
    }
    if (pst == 1) {
        double pac = 1 - exp(-5 / (max(p.penv_attack, 0.1) * 0.001 * sr));
        pe = pe + (1 - pe) * pac;
        if (pe >= 0.995) {
            pe = 1;
            pst = 2;
        }
    } else if (pst == 2) {
        pe = pe * exp(-5 / (max(p.penv_decay, 1) * 0.001 * sr));
        if (pe < 0.00001) {
            pe = 0;
            pst = 0;
        }
    }
    penv = pe;
    pstage = pst;
    double ne = nenv;
    double nst = nstage;
    if (envtrig) {
        if (p.noise_env_attack <= 0.1) {
            ne = 1;
            nst = 2;
        } else {
            ne = 0;
            nst = 1;
        }
    }
    if (nst == 1) {
        double nac = 1 - exp(-5 / (max(p.noise_env_attack, 0.1) * 0.001 * sr));
        ne = ne + (1 - ne) * nac;
        if (ne >= 0.995) {
            ne = 1;
            nst = 2;
        }
    } else if (nst == 2) {
        ne = ne * exp(-5 / (max(p.noise_env_decay, 1) * 0.001 * sr));
        if (ne < 0.00001) {
            ne = 0;
            nst = 0;
        }
    }
    nenv = ne;
    nstage = nst;
    double fe = fenv;
    double fst = fstage;
    if (envtrig)
        fst = 1;
    else if (g == 0 && fst > 0 && fst < 4)
        fst = 4;
    double fsus = clamp(p.fs * 0.01, 0, 1);
    if (fst == 1) {
        if (p.fa <= 0.1) {
            fe = 1;
            fst = 2;
        } else {
            double fac = 1 - exp(-5 / (max(p.fa, 0.1) * 0.001 * sr));
            fe = fe + (1 - fe) * fac;
            if (fe >= 0.995) {
                fe = 1;
                fst = 2;
            }
        }
    } else if (fst == 2) {
        double fdc = 1 - exp(-5 / (max(p.fd, 1) * 0.001 * sr));
        fe = fe + (fsus - fe) * fdc;
        if (abs(fe - fsus) < 0.0005) {
            fe = fsus;
            fst = 3;
        }
    } else if (fst == 3)
        fe = fsus;
    else if (fst == 4) {
        double frc = 1 - exp(-5 / (max(p.fr, 1) * 0.001 * sr));
        fe = fe + (0 - fe) * frc;
        if (fe < 0.00001) {
            fe = 0;
            fst = 0;
        }
    }
    fenv = fe;
    fstage = fst;
    double fshape = pow(2, (50 - clamp(p.fcurve, 0, 100)) / 50);
    double feout = pow(clamp(fe, 0, 1), fshape);
    double ve = venv;
    double vst = vstage;
    if (envtrig)
        vst = 1;
    else if (g == 0 && vst > 0 && vst < 4)
        vst = 4;
    double vsus = clamp(p.vs * 0.01, 0, 1);
    if (vst == 1) {
        if (p.va <= 0.1) {
            ve = 1;
            vst = 2;
        } else {
            double vac = 1 - exp(-5 / (max(p.va, 0.1) * 0.001 * sr));
            ve = ve + (1 - ve) * vac;
            if (ve >= 0.995) {
                ve = 1;
                vst = 2;
            }
        }
    } else if (vst == 2) {
        double vdc = 1 - exp(-5 / (max(p.vd, 1) * 0.001 * sr));
        ve = ve + (vsus - ve) * vdc;
        if (abs(ve - vsus) < 0.0005) {
            ve = vsus;
            vst = 3;
        }
    } else if (vst == 3)
        ve = vsus;
    else if (vst == 4) {
        double vrc = 1 - exp(-5 / (max(p.vr, 1) * 0.001 * sr));
        ve = ve + (0 - ve) * vrc;
        if (ve < 0.00001) {
            ve = 0;
            vst = 0;
        }
    }
    venv = ve;
    vstage = vst;
    double ashape = pow(2, (50 - clamp(p.acurve, 0, 100)) / 50);
    double veout = pow(clamp(ve, 0, 1), ashape);
    if (envtrig && p.lfo_retrig > 0.5)
        lph = 0;
    double lp = lph;
    double lnext = lp + p.lfo_rate / sr;
    double sh = shv;
    if (lnext >= 1) {
        lnext = lnext - 1;
        sh = noise();
    }
    lph = lnext;
    shv = sh;
    double lfo = sh;
    if (p.lfo_wave < 0.5)
        lfo = 1 - 4 * abs(lp - 0.5);
    else if (p.lfo_wave < 1.5)
        lfo = lp < 0.5 ? 1 : -1;
    else if (p.lfo_wave < 2.5)
        lfo = 2 * lp - 1;
    double lf = lfenv;
    if (envtrig)
        lf = 0;
    if (p.lfo_fade <= 0.1)
        lf = 1;
    else
        lf = lf + (1 - lf) * (1 - exp(-5 / (max(p.lfo_fade, 1) * 0.001 * sr)));
    lfenv = lf;
    double vph = vibph;
    double vage = vibage;
    double vamp = viben;
    if (envtrig) {
        vph = 0;
        vage = 0;
        vamp = 0;
    }
    double vnext = vph + clamp(p.vib_rate, 0.1, 12) / sr;
    if (vnext >= 1)
        vnext = vnext - 1;
    vibph = vnext;
    if (g > 0)
        vage = vage + 1 / sr;
    else
        vage = 0;
    vibage = vage;
    double vdelay = max(p.vib_delay, 0) * 0.001;
    if ((g <= 0) || (vage < vdelay))
        vamp = 0;
    else if (p.vib_fade <= 0.1)
        vamp = 1;
    else
        vamp = vamp + (1 - vamp) * (1 - exp(-5 / (max(p.vib_fade, 1) * 0.001 * sr)));
    viben = vamp;
    double autovib = sin(vph * 6.28318530718) * clamp(p.vib_depth, 0, 100) * 0.01 * vamp;
    double mw = clamp(note.kwheel, 0, 1);
    double mwdep = clamp(p.mw_depth, 0, 200);
    double mwvib = lfo * mw * mwdep * 0.015;
    double mwfilter = lfo * mw * mwdep * 0.0025;
    double wheelboost = 1 + mw * mwdep * 0.006;
    double pmod = lfo * p.lfo_pitch * lf + mwvib + autovib;
    double fmv = lfo * p.lfo_filter * wheelboost * lf + mwfilter;
    double pwm1 = lfo * p.lfo_pw1 * 0.0045 * wheelboost * lf;
    double pwm2 = lfo * p.lfo_pw2 * 0.0045 * wheelboost * lf;
    double pwm3 = lfo * p.lfo_pw3 * 0.0045 * wheelboost * lf;
    double amdepth = clamp(p.lfo_amp * 0.01 * wheelboost * lf, 0, 1);
    double amod = 1 - amdepth * 0.5 * (1 - lfo);
    double kk = 0.00004;
    double nd1 = d1 + (noise() - d1) * kk;
    double nd2 = d2 + (noise() - d2) * kk;
    double nd3 = d3 + (noise() - d3) * kk;
    d1 = nd1;
    d2 = nd2;
    d3 = nd3;
    double dsemi = p.drift * 0.01;
    double base = cur + clamp(p.tune_coarse, -24, 24) + clamp(p.tune_fine, -100, 100) * 0.01 +
                  note.kbend * clamp(p.bend_range, 1, 24) + pmod + pe * p.penv_amt;
    double rst = envtrig && (p.phase_retrig > 0.5);
    double p2 = rst ? 0.33 : ph2;
    double slopsemi = clamp(p.osc_slop, 0, 30) * 0.01;
    double n2 = base + p.o2_oct * 12 + p.o2_freq + nd2 * dsemi - slopsemi * 0.31;
    double dt2 = clamp(mtof(n2) / sr, 0.0000001, 0.45);
    double osc2 = vosc(p2, dt2, clamp(p.o2_wave, 0, 1), clamp(p.o2_pw + pwm2, 0.05, 0.95));
    double p1 = rst ? 0 : ph1;
    double n1 = base + p.o1_oct * 12 + p.o1_freq + nd1 * dsemi + slopsemi * 0.17 + osc2 * p.xmod_amt;
    double dt1 = clamp(mtof(n1) / sr, 0.0000001, 0.45);
    double osc1 = vosc(p1, dt1, clamp(p.o1_wave, 0, 1), clamp(p.o1_pw + pwm1, 0.05, 0.95));
    double sswideL = 0;
    double sswideR = 0;
    double sstack = 0;
    double jpbody = 0;
    double jpedge = 0;
    if (p.saw_mix > 0.001) {
        double ssd = clamp(p.saw_detune, 0, 50) * 0.01;
        double svsel = clamp(p.saw_voices, 0, 3);
        double sscenter = vosc(p1, dt1, 0.5, 0.5);
        double ssv1 = 0;
        double ssv2 = 0;
        double ssv3 = 0;
        double ssv4 = 0;
        double ssv5 = 0;
        double ssv6 = 0;
        if (svsel >= 0.5) {
            double ssp1 = ssph1;
            double ssd1 = clamp(mtof(n1 - ssd) / sr, 0.0000001, 0.45);
            ssv1 = vosc(ssp1, ssd1, 0.5, 0.5);
            ssph1 = wrap(ssp1 + ssd1, 0, 1);
            double ssp6 = ssph6;
            double ssd6 = clamp(mtof(n1 + ssd) / sr, 0.0000001, 0.45);
            ssv6 = vosc(ssp6, ssd6, 0.5, 0.5);
            ssph6 = wrap(ssp6 + ssd6, 0, 1);
        }
        if (svsel >= 1.5) {
            double ssp2 = ssph2;
            double ssd2 = clamp(mtof(n1 - ssd * 0.62) / sr, 0.0000001, 0.45);
            ssv2 = vosc(ssp2, ssd2, 0.5, 0.5);
            ssph2 = wrap(ssp2 + ssd2, 0, 1);
            double ssp5 = ssph5;
            double ssd5 = clamp(mtof(n1 + ssd * 0.62) / sr, 0.0000001, 0.45);
            ssv5 = vosc(ssp5, ssd5, 0.5, 0.5);
            ssph5 = wrap(ssp5 + ssd5, 0, 1);
        }
        if (svsel >= 2.5) {
            double ssp3 = ssph3;
            double ssd3 = clamp(mtof(n1 - ssd * 0.28) / sr, 0.0000001, 0.45);
            ssv3 = vosc(ssp3, ssd3, 0.5, 0.5);
            ssph3 = wrap(ssp3 + ssd3, 0, 1);
            double ssp4 = ssph4;
            double ssd4 = clamp(mtof(n1 + ssd * 0.28) / sr, 0.0000001, 0.45);
            ssv4 = vosc(ssp4, ssd4, 0.5, 0.5);
            ssph4 = wrap(ssp4 + ssd4, 0, 1);
        }
        double swsum = sscenter * 1.42;
        double swden = 1.42;
        if (svsel >= 0.5) {
            swsum = swsum + (ssv1 + ssv6) * 0.86;
            swden = swden + 1.72;
        }
        if (svsel >= 1.5) {
            swsum = swsum + (ssv2 + ssv5) * 0.78;
            swden = swden + 1.56;
        }
        if (svsel >= 2.5) {
            swsum = swsum + (ssv3 + ssv4) * 0.70;
            swden = swden + 1.40;
        }
        sstack = swsum * (0.44 + 0.34 / sqrt(max(1.42, swden)));
        double sshard = clamp(p.ss_hard, 0, 100) * 0.01;
        jpbody = clamp(p.jp_body, 0, 100) * 0.01;
        jpedge = clamp(p.jp_edge, 0, 100) * 0.01;
        double ssglue = clamp(p.ss_glue, 0, 100) * 0.01;
        double ssctr = clamp(p.ss_center, 0, 100) * 0.01;
        double ssub = clamp(p.ss_sub, 0, 100) * 0.01;
        double ssdrive = 1.05 + sshard * 5.6 + jpedge * 2.2;
        double sscore = tanh(sstack * ssdrive) * (1.02 + sshard * 0.42);
        sstack = mix(sstack, sscore, clamp(0.18 + sshard * 0.82 + ssglue * 0.18, 0, 1));
        sstack = sstack * (1.02 + ssctr * 0.38) + sscenter * ssctr * 0.52;
        double ssbodyL = 0;
        double ssbodyR = 0;
        double ssairL = 0;
        double ssairR = 0;
        if (jpbody > 0.001) {
            double sbpL = ssbodyphL;
            double sbpR = ssbodyphR;
            double sbsemi = 0.025 + 0.16 * jpbody;
            double sbdL = clamp(mtof(n1 - sbsemi) / sr, 0.0000001, 0.45);
            double sbdR = clamp(mtof(n1 + sbsemi) / sr, 0.0000001, 0.45);
            ssbodyL = vosc(sbpL, sbdL, 0.5, 0.5);
            ssbodyR = vosc(sbpR, sbdR, 0.5, 0.5);
            ssbodyphL = wrap(sbpL + sbdL, 0, 1);
            ssbodyphR = wrap(sbpR + sbdR, 0, 1);
            sstack = sstack + (ssbodyL + ssbodyR) * 0.20 * jpbody;
        }
        if (jpedge > 0.001) {
            double sapL = ssairphL;
            double sapR = ssairphR;
            double sadL = clamp(mtof(n1 + 12 - 0.08) / sr, 0.0000001, 0.45);
            double sadR = clamp(mtof(n1 + 12 + 0.08) / sr, 0.0000001, 0.45);
            ssairL = vosc(sapL, sadL, 0.5, 0.5);
            ssairR = vosc(sapR, sadR, 0.5, 0.5);
            ssairphL = wrap(sapL + sadL, 0, 1);
            ssairphR = wrap(sapR + sadR, 0, 1);
            sstack = sstack + (ssairL + ssairR) * 0.035 * jpedge;
        }
        if (p.ss_sub > 0.001) {
            double ssp0 = sssubph;
            double ssdt0 = clamp(mtof(n1 - 12) / sr, 0.0000001, 0.45);
            double ssv0 = vosc(ssp0, ssdt0, 0.5, 0.5);
            sssubph = wrap(ssp0 + ssdt0, 0, 1);
            sstack = sstack + ssv0 * ssub * (0.12 + 0.30 * jpbody);
        }
        double sg = 1 + ssglue * 4.6;
        sstack = mix(sstack, tanh(sstack * sg) * (1 + ssglue * 0.26), ssglue * 0.90);
        osc1 = mix(osc1, sstack, clamp(p.saw_mix * 0.01, 0, 1));
        sswideL = (sscenter * 1.42 + ssv1 * 0.96 + ssv2 * 0.86 + ssv3 * 0.76 + ssbodyL * jpbody * 0.52 +
                   ssairL * jpedge * 0.12) *
                  0.36;
        sswideR = (sscenter * 1.42 + ssv6 * 0.96 + ssv5 * 0.86 + ssv4 * 0.76 + ssbodyR * jpbody * 0.52 +
                   ssairR * jpedge * 0.12) *
                  0.36;
        sswideL = mix(sswideL, tanh(sswideL * (1 + sshard * 4.0 + jpedge * 1.5)),
                      clamp(sshard * 0.86 + ssglue * 0.20, 0, 1));
        sswideR = mix(sswideR, tanh(sswideR * (1 + sshard * 4.0 + jpedge * 1.5)),
                      clamp(sshard * 0.86 + ssglue * 0.20, 0, 1));
    }
    double bf = mtof(base);
    double fmout = 0;
    if (p.fm_mix > 0.001) {
        double fmb = fmbodyenv;
        double fmt = fmtineenv;
        double fme = fmenv2;
        if (envtrig) {
            fmb = 1;
            fmt = 1;
            fme = 1;
        }
        double bodydec = max(p.fm_decay * 2.15, 220);
        double tinedec = max(p.fm_decay * 0.34, 48);
        fmb = fmb * exp(-1 / (bodydec * 0.001 * sr));
        fmt = fmt * exp(-1 / (tinedec * 0.001 * sr));
        fme = fme * exp(-1 / (max(p.fm_decay, 40) * 0.001 * sr));
        fmbodyenv = fmb;
        fmtineenv = fmt;
        fmenv2 = fme;
        double fmp = fmph;
        double fmcp = fmcarph;
        double fmcp2 = fmcar2ph;
        double frat = clamp(p.fm_ratio, 0.25, 8);
        double fmdt = clamp(bf * frat / sr, 0.0000001, 0.45);
        double fmcdt = clamp(bf / sr, 0.0000001, 0.45);
        double fm2dt = clamp((bf * 1.00125) / sr, 0.0000001, 0.45);
        double fbamt = clamp(p.fm_feedback, 0, 100) * 0.0065;
        double velbright = 0.26 + 1.04 * vm;
        double idx = clamp(p.fm_index, 0, 12) * velbright;
        double op6 = sin(2 * pi * wrap(fmp * (7.00 * frat), 0, 1) + fmfbh * fbamt);
        double op5 = sin(2 * pi * wrap(fmp * (3.50 * frat), 0, 1) + op6 * idx * 0.18 * fmt);
        double op4 = sin(2 * pi * wrap(fmp * (1.00 * frat), 0, 1) + op5 * idx * 0.30 * fmt);
        double op3 = sin(2 * pi * wrap(fmp * (0.50 * frat), 0, 1) + op6 * idx * 0.10 * fmb);
        fmfbh = op6;
        double body1 = sin(2 * pi * fmcp + op4 * idx * 0.25 * fmb);
        double body2 = sin(2 * pi * fmcp2 + op3 * idx * 0.18 * fmb);
        double tine1 = sin(2 * pi * wrap(fmcp * 2.000, 0, 1) + op5 * idx * 0.68 * fmt);
        double tine2 = sin(2 * pi * wrap(fmcp2 * 3.000, 0, 1) + op6 * idx * 0.34 * fmt);
        double bell1 = sin(2 * pi * wrap(fmcp * 4.010, 0, 1) + op6 * idx * 0.31 * fmt);
        double bell2 = sin(2 * pi * wrap(fmcp2 * 7.000, 0, 1) + op5 * idx * 0.16 * fmt);
        double tinebal = 0.06 + 0.50 * clamp(p.hammer, 0, 100) * 0.01;
        double bodymix = (body1 * 0.70 + body2 * 0.24);
        double tinemix = (tine1 * tinebal + tine2 * 0.10 + bell1 * 0.042 + bell2 * 0.018);
        double fmraw = (bodymix + tinemix) * 0.92;
        double fmae = clamp(p.fm_amp_env, 0, 100) * 0.01;
        double fmclean = fmraw * 0.58 * fme;
        double fmsustain = tanh(fmraw);
        fmout = mix(fmsustain, fmclean, fmae) * (0.88 + 0.22 * vm);
        fmph = wrap(fmp + fmdt, 0, 1);
        fmcarph = wrap(fmcp + fmcdt, 0, 1);
        fmcar2ph = wrap(fmcp2 + fm2dt, 0, 1);
    }
    double wtout = 0;
    if (p.wt_mix > 0.001) {
        double wp = wtph;
        double wdt = clamp(bf / sr, 0.0000001, 0.45);
        double wpos = clamp(p.wt_pos * 0.01 + lfo * p.wt_lfo * 0.005 * lf + (feout - 0.5) * p.wt_env * 0.005 +
                                (clamp(note.ktimbre, 0, 1) - 0.5) * 0.18,
                            0, 1);
        double ww = clamp(p.wt_warp, 0, 100) * 0.0012;
        double wpp = wrap(wp + sin(2 * pi * wp) * ww, 0, 1);
        double ws1 = sin(2 * pi * wpp);
        double ws2 = (sin(2 * pi * wpp) + 0.33 * sin(6 * pi * wpp) + 0.20 * sin(10 * pi * wpp)) / 1.53;
        double ws3 = (sin(2 * pi * wpp) + 0.55 * sin(4 * pi * wpp) + 0.25 * sin(8 * pi * wpp)) / 1.80;
        double ws4 = (sin(2 * pi * wpp) + 0.50 * sin(4 * pi * wpp) + 0.33 * sin(6 * pi * wpp) +
                      0.25 * sin(8 * pi * wpp) + 0.20 * sin(10 * pi * wpp)) /
                     2.28;
        wtout = ws1;
        if (wpos < 0.333333)
            wtout = mix(ws1, ws2, wpos * 3);
        else if (wpos < 0.666666)
            wtout = mix(ws2, ws3, (wpos - 0.333333) * 3);
        else
            wtout = mix(ws3, ws4, (wpos - 0.666666) * 3);
        wtph = wrap(wp + wdt, 0, 1);
    }
    double p3 = rst ? 0.66 : ph3;
    double n3 = base + p.o3_oct * 12 + p.o3_freq + nd3 * dsemi + slopsemi * 0.47;
    double dt3 = clamp(mtof(n3) / sr, 0.0000001, 0.45);
    double osc3 = vosc(p3, dt3, clamp(p.o3_wave, 0, 1), clamp(p.o3_pw + pwm3, 0.05, 0.95));
    double nph1 = p1 + dt1;
    double wrapped1 = nph1 >= 1;
    ph1 = wrap(nph1, 0, 1);
    double ph2next = wrap(p2 + dt2, 0, 1);
    if (p.sync_amt > 0.5 && wrapped1)
        ph2next = 0;
    ph2 = ph2next;
    ph3 = wrap(p3 + dt3, 0, 1);
    double ps = rst ? 0 : phsub;
    double dts = clamp(mtof(base + p.o1_oct * 12 - 12) / sr, 0.0000001, 0.45);
    double subosc = vosc(ps, dts, 0.8, 0.5);
    phsub = wrap(ps + dts, 0, 1);
    double ring = osc1 * osc2;
    double nr = noise();
    double nc = clamp(p.noise_color, -100, 100) * 0.01;
    double na = abs(nc);
    double nfc = 12000;
    if (nc < 0)
        nfc = 12000 * pow(800.0 / 12000.0, na);
    else
        nfc = 80 * pow(5000.0 / 80.0, na);
    double ncoef = 1 - exp(-2 * pi * nfc / sr);
    double nlp = noiselp + (nr - noiselp) * ncoef;
    noiselp = nlp;
    double ncol = nr;
    if (nc < 0)
        ncol = mix(nr, nlp, na);
    else
        ncol = mix(nr, nr - nlp, na);
    double nea = clamp(p.noise_env_amt * 0.01, 0, 1);
    double namp = nzl * ((1 - nea) + nea * ne);
    double mixsum = osc1 * l1 + osc2 * l2 + osc3 * l3 + ncol * namp * 0.5 + subosc * subl +
                    ring * ringl * 0.7 + fbk * fbl * 1.25 + fmout * 7 * clamp(p.fm_mix * 0.01, 0, 1) +
                    wtout * 7 * clamp(p.wt_mix * 0.01, 0, 1);
    double hpout = 0;
    double coreactive = abs(l1) + abs(l2) + abs(l3) + abs(nzl) + abs(subl) + abs(ringl) + abs(fbl) +
                        abs(p.fm_mix) + abs(p.wt_mix);
    if (coreactive > 0.0001) {
        double rawmix = mixsum * 0.10;
        double predrive = 1 + ds * 0.32;
        double xin = tanh(rawmix * predrive);
        double kt = (cur - 60) * p.kbamt * 0.01;
        double fcut =
            cs * pow(2, kt / 12 + feout * p.envamt + fmv + osc3 * p.osc3_fm + vf_oct +
                            clamp(note.kpressure, 0, 1) * 0.30 + (clamp(note.ktimbre, 0, 1) - 0.5) * 0.16);
        fcut = clamp(fcut, 15, min(20000, sr * 0.43));
        double gg = tan(pi * fcut / sr);
        double ga = gg / (1 + gg);
        double bb = 1 / (1 + gg);
        double k = clamp(rs * 0.38, 0, 3.85);
        double os1 = s1;
        double os2 = s2;
        double os3 = s3;
        double os4 = s4;
        double ss = (ga * ga * ga * os1 + ga * ga * os2 + ga * os3 + os4) * bb;
        double ga4 = ga * ga * ga * ga;
        double yest = (ga4 * xin + ss) / (1 + k * ga4);
        double u = tanh(xin - k * yest);
        double v1 = (u - os1) * ga;
        double y1 = v1 + os1;
        s1 = y1 + v1;
        double v2 = (y1 - os2) * ga;
        double y2 = v2 + os2;
        s2 = y2 + v2;
        double v3 = (y2 - os3) * ga;
        double y3 = v3 + os3;
        s3 = y3 + v3;
        double v4 = (y3 - os4) * ga;
        double y4 = v4 + os4;
        s4 = y4 + v4;
        double fout = mix(y2, y4, clamp(p.slope, 0, 1));
        fbk = fout;
        double hpc = clamp(p.hpf_cut, 10, min(4000, sr * 0.20));
        double hpa = 1 - exp(-2 * pi * hpc / sr);
        double hpl = hpstate + (fout - hpstate) * hpa;
        hpstate = hpl;
        hpout = p.hpf_cut <= 10.1 ? fout : fout - hpl;
    } else {
        fbk = 0;
    }
    double ce = clickenv;
    if (envtrig)
        ce = 1;
    else
        ce = ce * exp(-5 / (max(p.click_decay, 1) * 0.001 * sr));
    if (ce < 0.00001)
        ce = 0;
    clickenv = ce;
    double clicksig = noise() * ce * clamp(p.click_amt, 0, 100) * 0.0018;
    double kickmode =
        clamp((p.penv_amt - 3) / 28, 0, 1) * clamp((22 - p.vs) / 22, 0, 1) * clamp((900 - p.vd) / 900, 0, 1);
    double kp = kickph;
    if (envtrig && kickmode > 0.01)
        kp = 0;
    double kdrop = pe * pe;
    double drumroot = cur - 12;
    if (p.penv_amt > 18)
        drumroot = 31 + (cur - 36) * 0.06;
    double kdt = clamp(mtof(drumroot + max(p.penv_amt, 0) * kdrop) / sr, 0.0000001, 0.45);
    double kickbody = sin(2 * pi * kp);
    kickph = wrap(kp + kdt, 0, 1);
    double kickshape = tanh(kickbody * (1.30 + clamp(p.click_amt, 0, 100) * 0.010));
    double kicksig = kickshape * veout * vscale * kickmode * 1.02;
    double tp = clamp(p.transient_punch, 0, 100) * 0.01;
    double transmeter = pe * clamp(abs(p.penv_amt) / 24, 0, 1) + ne * clamp(p.noise_env_amt * 0.01, 0, 1) +
                        ce * clamp(p.click_amt * 0.01, 0, 1);
    double transboost = 1 + tp * clamp(transmeter, 0, 1) * 1.25;
    double mainpre =
        (hpout * veout * vscale * amod * (1 - kickmode * 0.94) + clicksig + kicksig) * transboost;
    double modelamp = veout * vscale;
    double sw = clamp(p.saw_mix * 0.01, 0, 1);
    double ssw = clamp(p.ss_width, 0, 100) * 0.01;
    double ssb = clamp(p.ss_bite, 0, 100) * 0.01;
    double ssh = clamp(p.ss_hard, 0, 100) * 0.01;
    double ssidegain = 0.12 + ssw * 0.50;
    double ssideL = (sswideL - sstack) * sw * ssidegain * modelamp;
    double ssideR = (sswideR - sstack) * sw * ssidegain * modelamp;
    double ssedgeL = 0;
    double ssedgeR = 0;
    if (p.saw_mix > 0.001) {
        double sslpcoef = 1 - exp(-2 * pi * (1800 + ssb * 4200 + jpedge * 2600) / sr);
        double ssl = sslpL + (sswideL - sslpL) * sslpcoef;
        sslpL = ssl;
        double ssr = sslpR + (sswideR - sslpR) * sslpcoef;
        sslpR = ssr;
        ssedgeL = (sswideL - ssl) * sw * (ssb * 0.32 + jpedge * 0.25) * modelamp;
        ssedgeR = (sswideR - ssr) * sw * (ssb * 0.32 + jpedge * 0.25) * modelamp;
    }
    double preL = leftDc(mainpre + ssideL + ssedgeL);
    double preR = rightDc(mainpre + ssideR + ssedgeR);
    double eqc1 = 1 - exp(-2 * pi * 180 / sr);
    double eqc2 = 1 - exp(-2 * pi * 900 / sr);
    double eqc3 = 1 - exp(-2 * pi * min(4200, sr * 0.20) / sr);
    double e180L = eql180L + (preL - eql180L) * eqc1;
    double e180R = eql180R + (preR - eql180R) * eqc1;
    eql180L = e180L;
    eql180R = e180R;
    double e900L = eql900L + (preL - eql900L) * eqc2;
    double e900R = eql900R + (preR - eql900R) * eqc2;
    eql900L = e900L;
    eql900R = e900R;
    double e4200L = eql4200L + (preL - eql4200L) * eqc3;
    double e4200R = eql4200R + (preR - eql4200R) * eqc3;
    eql4200L = e4200L;
    eql4200R = e4200R;
    double eloL = e180L;
    double eloR = e180R;
    double ebodyL = e900L - e180L;
    double ebodyR = e900R - e180R;
    double epresL = e4200L - e900L;
    double epresR = e4200R - e900R;
    double eairL = preL - e4200L;
    double eairR = preR - e4200R;
    double glo = dbtoa((clamp(p.toneeq_low, 0, 100) - 50) * 0.18);
    double gbody = dbtoa((clamp(p.toneeq_body, 0, 100) - 50) * 0.18);
    double gpres = dbtoa((clamp(p.toneeq_pres, 0, 100) - 50) * 0.18);
    double gair = dbtoa((clamp(p.toneeq_air, 0, 100) - 50) * 0.18);
    preL = eloL * glo + ebodyL * gbody + epresL * gpres + eairL * gair;
    preR = eloR * glo + ebodyR * gbody + epresR * gpres + eairR * gair;
    double cam = clamp(p.comp_amt, 0, 100) * 0.01;
    double dynL = preL;
    double dynR = preR;
    if (cam > 0.001) {
        double pk = max(abs(preL), abs(preR));
        double envc = compenv;
        double atms = 0.5 + clamp(p.comp_attack, 0, 100) * 0.22;
        double rlms = 35 + clamp(p.comp_release, 0, 100) * 4.4;
        double catk = 1 - exp(-1 / (atms * 0.001 * sr));
        double crel = 1 - exp(-1 / (rlms * 0.001 * sr));
        if (pk > envc)
            envc = envc + (pk - envc) * catk;
        else
            envc = envc + (pk - envc) * crel;
        compenv = envc;
        double thr = 0.42 - 0.28 * cam;
        double ratio = 1 + 9 * cam;
        double target = 1;
        if (envc > thr)
            target = pow(thr / max(envc, 0.00001), 1 - 1 / ratio);
        double cg = compgain;
        double gcoef = target < cg ? catk : crel;
        cg = cg + (target - cg) * gcoef;
        compgain = cg;
        double mup = dbtoa(clamp(p.comp_makeup, 0, 100) * 0.12);
        dynL = preL * cg * mup;
        dynR = preR * cg * mup;
    }
    double clp = clamp(p.soft_clip, 0, 100) * 0.01;
    if (clp > 0.001) {
        double cd = 1 + clp * 4.4;
        dynL = mix(dynL, tanh(dynL * cd), clp);
        dynR = mix(dynR, tanh(dynR * cd), clp);
    }
    double rangeLo = clamp(p.str_low_note, 0, 127);
    double rangeHi = clamp(p.str_high_note, 0, 127);
    double leadRangeGate = (rangeHi < rangeLo) ? 1 : ((note.knote >= rangeLo) && (note.knote <= rangeHi));
    double sdrive = 1 + sats * 0.48 + sw * ssh * 1.55 + sw * jpbody * 0.35;
    if ((sats > 0.001) || (sw > 0.001)) {
        result.left = tanh(dynL * sdrive) * dbtoa(vols) * leadRangeGate;
        result.right = tanh(dynR * sdrive) * dbtoa(vols) * leadRangeGate;
    } else {
        result.left = dynL * 0.90 * dbtoa(vols) * leadRangeGate;
        result.right = dynR * 0.90 * dbtoa(vols) * leadRangeGate;
    }
    return result;
}

Stereo Drive::process(double samplerate, const Parameters &p, Stereo input) {
    Stereo result;
    double xL = input.left;
    double xR = input.right;
    double on = clamp(p.gtr_pedal_on, 0, 1);
    double typ = clamp(p.gtr_pedal_type, 0, 3);
    double drv = clamp(p.gtr_pedal_drive, 0, 100) * 0.01;
    double tone = clamp(p.gtr_pedal_tone, 0, 100) * 0.01;
    double lev = clamp(p.gtr_pedal_level, 0, 100) * 0.01;

    double boostW = clamp(1 - typ, 0, 1);
    double fuzzW = clamp(typ - 2, 0, 1);
    double distW = clamp(typ - 1, 0, 1) - fuzzW;
    double odW = clamp(1 - boostW - distW - fuzzW, 0, 1);

    double boostL = tanh(xL * (1 + drv * 5)) * 0.96;
    double boostR = tanh(xR * (1 + drv * 5)) * 0.96;

    double bias = 0.018 + drv * 0.038;
    double odG = 1.8 + drv * 15;
    double od1L = tanh((xL + bias) * odG) - tanh(bias * odG);
    double od1R = tanh((xR + bias * 0.92) * odG) - tanh(bias * 0.92 * odG);
    double odL = tanh((od1L - bias * 0.34) * (1.5 + drv * 5.5)) + tanh(bias * 0.34 * (1.5 + drv * 5.5));
    double odR = tanh((od1R - bias * 0.30) * (1.5 + drv * 5.5)) + tanh(bias * 0.30 * (1.5 + drv * 5.5));

    double distG = 2.4 + drv * 22;
    double distL = tanh(clamp((xL + bias * 0.8) * distG, -2.5, 2.5)) * 0.88;
    double distR = tanh(clamp((xR + bias * 0.7) * distG, -2.5, 2.5)) * 0.88;

    double fuzzG = 4 + drv * 34;
    double fuzzL = tanh(tanh((xL + 0.045) * fuzzG) * 5) * 0.80;
    double fuzzR = tanh(tanh((xR + 0.041) * fuzzG) * 5) * 0.80;

    double rawL = boostL * boostW + odL * odW + distL * distW + fuzzL * fuzzW;
    double rawR = boostR * boostW + odR * odW + distR * distW + fuzzR * fuzzW;

    double fc = 1800 + tone * 9000;
    double c = 1 - exp(-2 * pi * clamp(fc, 1600, min(12000, samplerate * 0.34)) / samplerate);
    double fltL = pedalLpL + (rawL - pedalLpL) * c;
    pedalLpL = fltL;
    double fltR = pedalLpR + (rawR - pedalLpR) * c;
    pedalLpR = fltR;

    double gain = 0.45 + lev * 1.35;
    double wetL = fltL * gain;
    double wetR = fltR * gain;
    result.left = mix(xL, wetL, on);
    result.right = mix(xR, wetR, on);
    return result;
}

Stereo Effects::process(double samplerate, const Parameters &p, Stereo input) {
    Stereo result;
    double sr = samplerate;
    double xL = input.left;
    double xR = input.right;
    double wetL = 0;
    double wetR = 0;

    double cm = clamp(p.post_cho_mix, 0, 100) * 0.01;
    if (cm > 0.0001) {
        double cr = 0.06 + clamp(p.post_cho_rate, 0, 100) * 0.038;
        double cd = clamp(p.post_cho_depth, 0, 100) * 0.01;
        double cp = choPh;
        choPh = wrap(cp + cr / sr, 0, 1);
        double cb = 0.0065 * sr;
        double cdep = (0.001 + 0.010 * cd) * sr;
        double ctL = clamp(cb + cdep * (0.5 + 0.5 * sin(2 * pi * cp)), 1, 4000);
        double ctR = clamp(cb + cdep * (0.5 + 0.5 * sin(2 * pi * wrap(cp + 0.31, 0, 1))), 1, 4000);
        double cL = choL.read(ctL);
        double cR = choR.read(ctR);
        choL.write(xL);
        choR.write(xR);
        wetL = wetL + cL * cm * 0.54;
        wetR = wetR + cR * cm * 0.54;
    }

    double fm = clamp(p.post_flg_mix, 0, 100) * 0.01;
    if (fm > 0.0001) {
        double fr = 0.05 + clamp(p.post_flg_rate, 0, 100) * 0.028;
        double fd = clamp(p.post_flg_depth, 0, 100) * 0.01;
        double ffb = clamp(p.post_flg_fb, 0, 100) * 0.0078;
        double fp = flgPh;
        flgPh = wrap(fp + fr / sr, 0, 1);
        double fb = 0.0012 * sr;
        double fdep = (0.0004 + 0.0065 * fd) * sr;
        double ftL = clamp(fb + fdep * (0.5 + 0.5 * sin(2 * pi * fp)), 1, 3900);
        double ftR = clamp(fb + fdep * (0.5 + 0.5 * sin(2 * pi * wrap(fp + 0.18, 0, 1))), 1, 3900);
        double fL = flgL.read(ftL);
        double fR = flgR.read(ftR);
        flgL.write(xL + fL * ffb);
        flgR.write(xR + fR * ffb);
        wetL = wetL + (fL - xL * 0.18) * fm * 0.48;
        wetR = wetR + (fR - xR * 0.18) * fm * 0.48;
    }

    double pm = clamp(p.post_phs_mix, 0, 100) * 0.01;
    if (pm > 0.0001) {
        double pr = 0.04 + clamp(p.post_phs_rate, 0, 100) * 0.025;
        double pd = clamp(p.post_phs_depth, 0, 100) * 0.01;
        double pfb = clamp(p.post_phs_fb, 0, 100) * 0.0055;
        double pp = phsPh;
        phsPh = wrap(pp + pr / sr, 0, 1);
        double aa = clamp(0.12 + pd * 0.70 * (0.5 + 0.5 * sin(2 * pi * pp)), 0.08, 0.86);

        double piL = xL + phFbL * pfb;
        double y1 = -aa * piL + ph1L;
        ph1L = piL + aa * y1;
        double y2 = -aa * y1 + ph2L;
        ph2L = y1 + aa * y2;
        double y3 = -aa * y2 + ph3L;
        ph3L = y2 + aa * y3;
        double y4 = -aa * y3 + ph4L;
        ph4L = y3 + aa * y4;
        phFbL = y4;

        double aaR = clamp(0.12 + pd * 0.70 * (0.5 + 0.5 * sin(2 * pi * wrap(pp + 0.23, 0, 1))), 0.08, 0.86);
        double piR = xR + phFbR * pfb;
        double z1 = -aaR * piR + ph1R;
        ph1R = piR + aaR * z1;
        double z2 = -aaR * z1 + ph2R;
        ph2R = z1 + aaR * z2;
        double z3 = -aaR * z2 + ph3R;
        ph3R = z2 + aaR * z3;
        double z4 = -aaR * z3 + ph4R;
        ph4R = z3 + aaR * z4;
        phFbR = z4;

        wetL = wetL + y4 * pm * 0.46;
        wetR = wetR + z4 * pm * 0.46;
    }

    double dm = clamp(p.post_dly_mix, 0, 100) * 0.01;
    if (dm > 0.0001) {
        double dt = 0.035 + clamp(p.post_dly_time, 0, 100) * 0.0175;
        double dfb = clamp(p.post_dly_fb, 0, 100) * 0.0086;
        double ds = clamp(dt * sr, 1, 95000);
        double dL = dlyL.read(ds);
        double dR = dlyR.read(clamp(ds * 1.011, 1, 95000));
        dlyL.write(xL + dR * dfb);
        dlyR.write(xR + dL * dfb);
        wetL = wetL + dL * dm * 0.58;
        wetR = wetR + dR * dm * 0.58;
    }

    double rm = clamp(p.post_rev_mix, 0, 100) * 0.01;
    if (rm > 0.0001) {
        double rs = clamp(p.post_rev_size, 0, 100) * 0.01;
        double rd = clamp(p.post_rev_damp, 0, 100) * 0.01;
        double rfb = 0.50 + 0.40 * rs;
        double r1 = (0.045 + 0.120 * rs) * sr;
        double r2 = (0.061 + 0.153 * rs) * sr;
        double r3 = (0.079 + 0.181 * rs) * sr;
        double r4 = (0.097 + 0.211 * rs) * sr;
        double aL = rvAL.read(clamp(r1, 1, 95000));
        double aR = rvAR.read(clamp(r2, 1, 95000));
        double bL = rvBL.read(clamp(r3, 1, 95000));
        double bR = rvBR.read(clamp(r4, 1, 95000));
        double dc = 0.04 + 0.36 * (1 - rd);
        double daL = rvDampAL + (aL - rvDampAL) * dc;
        rvDampAL = daL;
        double daR = rvDampAR + (aR - rvDampAR) * dc;
        rvDampAR = daR;
        double dbL = rvDampBL + (bL - rvDampBL) * dc;
        rvDampBL = dbL;
        double dbR = rvDampBR + (bR - rvDampBR) * dc;
        rvDampBR = dbR;
        rvAL.write(xL * 0.22 + (dbR - daR) * rfb * 0.54);
        rvAR.write(xR * 0.22 + (dbL - daL) * rfb * 0.54);
        rvBL.write(xL * 0.18 + (daR + dbR) * rfb * 0.46);
        rvBR.write(xR * 0.18 + (daL + dbL) * rfb * 0.46);
        wetL = wetL + (aL + bL + aR * 0.32 + bR * 0.18) * 0.36 * rm;
        wetR = wetR + (aR + bR + aL * 0.32 + bL * 0.18) * 0.36 * rm;
    }

    result.left = wetL;
    result.right = wetR;
    choL.advance();
    choR.advance();
    flgL.advance();
    flgR.advance();
    dlyL.advance();
    dlyR.advance();
    rvAL.advance();
    rvAR.advance();
    rvBL.advance();
    rvBR.advance();
    return result;
}
} // namespace knobber
