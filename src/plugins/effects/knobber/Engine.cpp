// Derived from SUPER KNOBBER REVERB v1.0. See tools/import_knobber_reverb.py.
#include "Engine.h"
namespace knobverb {
using namespace knobber;
void Engine::reset() {
  preL.clear();
  preR.clear();
  tailPreL.clear();
  tailPreR.clear();
  erL.clear();
  erR.clear();
  d1.clear();
  d2.clear();
  d3.clear();
  d4.clear();
  d5.clear();
  d6.clear();
  d7.clear();
  d8.clear();
  d9.clear();
  d10.clear();
  d11.clear();
  d12.clear();
  d13.clear();
  d14.clear();
  d15.clear();
  d16.clear();
  diffL1.clear();
  diffL2.clear();
  diffL3.clear();
  diffL4.clear();
  diffR1.clear();
  diffR2.clear();
  diffR3.clear();
  diffR4.clear();
  sprL1.clear();
  sprL2.clear();
  sprL3.clear();
  sprR1.clear();
  sprR2.clear();
  sprR3.clear();
  echoL.clear();
  echoR.clear();
  grainL.clear();
  grainR.clear();
  resL.clear();
  resR.clear();
  preLP1L = 0;
  preLP1R = 0;
  preLP2L = 0;
  preLP2R = 0;
  preLP3L = 0;
  preLP3R = 0;
  preLP4L = 0;
  preLP4R = 0;
  hpLh = 0;
  hpRh = 0;
  outHpL = 0;
  outHpR = 0;
  outLpL = 0;
  outLpR = 0;
  postCutLpL = 0;
  postCutLpR = 0;
  postLP1L = 0;
  postLP1R = 0;
  postLP2L = 0;
  postLP2R = 0;
  postLP3L = 0;
  postLP3R = 0;
  postLP4L = 0;
  postLP4R = 0;
  fbLp1 = 0;
  fbLp2 = 0;
  fbLp3 = 0;
  fbLp4 = 0;
  fbLp5 = 0;
  fbLp6 = 0;
  fbLp7 = 0;
  fbLp8 = 0;
  fbLp9 = 0;
  fbLp10 = 0;
  fbLp11 = 0;
  fbLp12 = 0;
  fbLp13 = 0;
  fbLp14 = 0;
  fbLp15 = 0;
  fbLp16 = 0;
  fbLow1 = 0;
  fbLow2 = 0;
  fbLow3 = 0;
  fbLow4 = 0;
  fbLow5 = 0;
  fbLow6 = 0;
  fbLow7 = 0;
  fbLow8 = 0;
  fbLow9 = 0;
  fbLow10 = 0;
  fbLow11 = 0;
  fbLow12 = 0;
  fbLow13 = 0;
  fbLow14 = 0;
  fbLow15 = 0;
  fbLow16 = 0;
  ph1 = .07;
  ph2 = .19;
  ph3 = .31;
  ph4 = .43;
  ph5 = .56;
  ph6 = .68;
  ph7 = .79;
  ph8 = .91;
  ph9 = .13;
  ph10 = .24;
  ph11 = .36;
  ph12 = .48;
  ph13 = .62;
  ph14 = .73;
  ph15 = .84;
  ph16 = .96;
  envH = 0;
  gateH = 0;
  gateHoldH = 0;
  revH = 0;
  revHoldH = 0;
  revGrainH = 0;
  slowMotion = 0;
  boomDetH = 0;
  boomSlowH = 0;
  boomOnsetH = 0;
  boomGateH = 0;
  boomRefracH = 0;
  boomEnvH = 0;
  boomPitchH = 0;
  boomPhaseH = 0;
  boomAmpH = 0;
  boomHitH = 0;
  boomLowL = 0;
  boomLowR = 0;
  boomTailL = 0;
  boomTailR = 0;
  beforeLeft = beforeRight = 0;
}
Stereo Engine::process(double in1, double in2, double samplerate, int rtype,
                       const std::array<double, 108> &values) {
  const double size = values[0];
  const double decay = values[1];
  const double predelay = values[2];
  const double diffusion = values[3];
  const double density = values[4];
  const double buildup = values[5];
  const double distance = values[6];
  const double gravity = values[7];
  const double earlylevel = values[8];
  const double earlysize = values[9];
  const double earlywidth = values[10];
  const double earlydiff = values[11];
  const double taillevel = values[12];
  const double taildelay = values[13];
  const double tailwidth = values[14];
  const double taildensity = values[15];
  const double subdecay = values[16];
  const double lowdecay = values[17];
  const double middecay = values[18];
  const double highdecay = values[19];
  const double airdecay = values[20];
  const double lowxover = values[21];
  const double highxover = values[22];
  const double damping = values[23];
  const double prelowcut = values[24];
  const double prehighcut = values[25];
  const double prelow = values[26];
  const double prelowmid = values[27];
  const double premid = values[28];
  const double prehighmid = values[29];
  const double prehigh = values[30];
  const double pretilt = values[31];
  const double modrate = values[32];
  const double moddepth = values[33];
  const double modrandom = values[34];
  const double drift = values[35];
  const double spin = values[36];
  const double wander = values[37];
  const double chorus = values[38];
  const double motion = values[39];
  const double width = values[40];
  const double crossfeed = values[41];
  const double stereospread = values[42];
  const double rotation = values[43];
  const double duck = values[44];
  const double duckattack = values[45];
  const double duckrelease = values[46];
  const double gate = values[47];
  const double gatehold = values[48];
  const double gaterelease = values[49];
  const double pitchreturn = values[53];
  const double freeze = values[54];
  const double nonlinearshape = values[55];
  const double postlowcut = values[56];
  const double posthighcut = values[57];
  const double postlow = values[58];
  const double postmid = values[59];
  const double posthigh = values[60];
  const double saturation = values[61];
  const double mixamount = values[62];
  const double postlowmid = values[64];
  const double posthighmid = values[65];
  const double earlysend = values[66];
  const double erpattern = values[67];
  const double roomshape = values[68];
  const double wallabsorb = values[69];
  const double airabsorb = values[70];
  const double latecross = values[71];
  const double inputdiffuse = values[72];
  const double transientspace = values[73];
  const double platetension = values[74];
  const double platedamp = values[75];
  const double plateattack = values[76];
  const double platemetal = values[77];
  const double springtension = values[78];
  const double springdrip = values[79];
  const double springdispersion = values[80];
  const double springresonance = values[81];
  const double vintagecolor = values[82];
  const double vintagerate = values[83];
  const double reversetime = values[84];
  const double reversecurve = values[85];
  const double bloomtime = values[86];
  const double bloomshape = values[87];
  const double shimmerfeedback = values[88];
  const double shimmerdelay = values[89];
  const double detune = values[90];
  const double tremolorate = values[91];
  const double tremolodepth = values[92];
  const double echotime = values[93];
  const double echofeedback = values[94];
  const double dualblend = values[95];
  const double grainsize = values[96];
  const double graindensity = values[97];
  const double grainscatter = values[98];
  const double grainpitch = values[99];
  const double spectralblur = values[100];
  const double spectralfreeze = values[101];
  const double resonanttune = values[102];
  const double resonantamount = values[103];
  const double warpamount = values[104];
  const double twist = values[105];
  const double hybridblend = values[106];
  const double colorage = values[107];
  using std::abs;
  double ts = 0;
  double ers = 0;
  double tls = 0;
  double db = 0;
  double mb = 0;
  double spring = 0;
  double nl = 0;
  double erp = 0;
  double om = 0;
  double plateMode = 0;
  double springMode = 0;
  double vintageMode = 0;
  double grainMode = 0;
  double spectralMode = 0;
  double resonMode = 0;
  double echoMode = 0;
  double tremMode = 0;
  double dualMode = 0;
  double pt = 0;
  double pdamp = 0;
  double patk = 0;
  double pmet = 0;
  double st = 0;
  double sdp = 0;
  double srz = 0;
  double sdr = 0;
  double smulti = 0;
  double vc = 0;
  double vr = 0;
  double vdage = 0;
  double vdroom = 0;
  double sf = 0;
  double sdel = 0;
  double sdet = 0;
  double crys = 0;
  double wrp = 0;
  double tws = 0;
  double hbl = 0;
  double cag = 0;
  double r1 = 0;
  double r2 = 0;
  double r3 = 0;
  double r4 = 0;
  double r5 = 0;
  double r6 = 0;
  double r7 = 0;
  double r8 = 0;
  double erOutL = 0;
  double erOutR = 0;
  double mr = 0;
  double md = 0;
  double rn = 0;
  double dr = 0;
  double spn = 0;
  double wnd = 0;
  double mot = 0;
  double wa = 0;
  double fc = 0;
  double vdbw = 0;
  double spb = 0;
  double spf = 0;
  double gmid = 0;
  double fr = 0;
  double fzstr = 0;
  double fzfb = 0;
  double bu = 0;
  double den = 0;
  double es = 0;
  double ind = 0;
  double igin = 0;
  double toL = 0;
  double toR = 0;
  double dbl = 0;
  double hyb = 0;
  double dnearL = 0;
  double dnearR = 0;
  double daltL = 0;
  double daltR = 0;
  double dnm = 0;
  double dns = 0;
  double dfm = 0;
  double dfs = 0;
  double dga = 0;
  double dgb = 0;
  double dscale = 0;
  double altL = 0;
  double altR = 0;
  double hmix = 0;
  double stn = 0;
  double spg = 0;
  double s1L = 0;
  double s2L = 0;
  double s3L = 0;
  double s1R = 0;
  double s2R = 0;
  double s3R = 0;
  double sd1 = 0;
  double so1 = 0;
  double sd2 = 0;
  double so2 = 0;
  double sd3 = 0;
  double so3 = 0;
  double rd1 = 0;
  double ro1 = 0;
  double rd2 = 0;
  double ro2 = 0;
  double rd3 = 0;
  double ro3 = 0;
  double bodyL = 0;
  double bodyR = 0;
  double dripL = 0;
  double dripR = 0;
  double resSprL = 0;
  double resSprR = 0;
  double sprmix = 0;
  double sm = 0;
  double ss = 0;
  double wetL = 0;
  double wetR = 0;
  double pc = 0;
  double sprdrv = 0;
  double sprsatL = 0;
  double sprsatR = 0;
  double satmix = 0;
  double vds = 0;
  double vdm = 0;
  double vddrv = 0;
  double vdn = 0;
  double vdsat = 0;
  double vdmod = 0;
  double shm = 0;
  double shs = 0;
  double shdrv = 0;
  double shn = 0;
  double mmid = 0;
  double mside = 0;
  double mdrv = 0;
  double mn = 0;
  double rvtime = 0;
  double rvwins = 0;
  double rvcur = 0;
  double rvactive = 0;
  double rvholdtime = 0;
  double rtrig = 0;
  double rvup = 0;
  double rvrel = 0;
  double rvdn = 0;
  double rvgrain = 0;
  double rgp1 = 0;
  double rgp2 = 0;
  double rvbase = 0;
  double rvd1 = 0;
  double rvd2 = 0;
  double rvaL = 0;
  double rvaR = 0;
  double rvbL = 0;
  double rvbR = 0;
  double rvw1 = 0;
  double rvw2 = 0;
  double rvwn = 0;
  double rvsL = 0;
  double rvsR = 0;
  double rvm = 0;
  double rvside = 0;
  double rvenv = 0;
  double rvstart = 0;
  double rg = 0;
  double btrig = 0;
  double bc = 0;
  double bt = 0;
  double bdn = 0;
  double bsh = 0;
  double bexp = 0;
  double bg = 0;
  double boomDirectL = 0;
  double boomDirectR = 0;
  double bfa = 0;
  double bfr = 0;
  double bsa = 0;
  double bsr = 0;
  double bonset = 0;
  double bonsetGate = 0;
  double bgth = 0;
  double bgate = 0;
  double bamt = 0;
  double bimpact = 0;
  double bdrop = 0;
  double bbase = 0;
  double bdec = 0;
  double bdropT = 0;
  double benvc = 0;
  double bpitchc = 0;
  double bhitc = 0;
  double bfreq = 0;
  double bdamp = 0;
  double bfund = 0;
  double bh2 = 0;
  double bh3 = 0;
  double bsub = 0;
  double bcf = 0;
  double blpc = 0;
  double bimpL = 0;
  double bimpR = 0;
  double bimp = 0;
  double btcf = 0;
  double btlpc = 0;
  double btail = 0;
  double bsubGain = 0;
  double bbodyGain = 0;
  double bhitGain = 0;
  double bmono = 0;
  double bdirDrive = 0;
  double bdirNorm = 0;
  double durestore = 0;
  double trr = 0;
  double trd = 0;
  double tphase = 0;
  double tvL = 0;
  double tvR = 0;
  double tmin = 0;
  double et = 0;
  double efn = 0;
  double ef = 0;
  double edL = 0;
  double edR = 0;
  double emix = 0;
  double gs = 0;
  double gden = 0;
  double gsc = 0;
  double gp = 0;
  double gcloud = 0;
  double gsize = 0;
  double gscatter = 0;
  double gpofs = 0;
  double go1 = 0;
  double go2 = 0;
  double go3 = 0;
  double go4 = 0;
  double gcL = 0;
  double gcR = 0;
  double gmix = 0;
  double gside = 0;
  double sb = 0;
  double sfz = 0;
  double spwidth = 0;
  double smearL = 0;
  double smearR = 0;
  double spdrv = 0;
  double spmix = 0;
  double spmid = 0;
  double spside = 0;
  double rf = 0;
  double ra = 0;
  double rd4 = 0;
  double rr1L = 0;
  double rr1R = 0;
  double rr2L = 0;
  double rr2R = 0;
  double rr3L = 0;
  double rr3R = 0;
  double rr4L = 0;
  double rr4R = 0;
  double rlo = 0;
  double rhi = 0;
  double rfb = 0;
  double rw1 = 0;
  double rw2 = 0;
  double rw3 = 0;
  double rw4 = 0;
  double resWetL = 0;
  double resWetR = 0;
  double rdrv = 0;
  double rnrm = 0;
  double rmix = 0;
  double vg = 0;
  double twst = 0;
  double wm = 0;
  double ws = 0;
  double tdrv = 0;
  double tn = 0;
  double hm = 0;
  double hs = 0;
  double mm = 0;
  double ms = 0;
  double bmix = 0;
  double bcomp = 0;
  double bsrc = 0;
  double bamt2 = 0;
  double bimp2 = 0;
  double bdrv2 = 0;
  double bonL = 0;
  double bonR = 0;
  double bonGain = 0;
  double sr = samplerate;
  double typ = floor(clamp(rtype, 0, 48) + 0.5);
  double naturalMode = (typ >= 2 && typ <= 16);
  double hallMode = (typ >= 8 && typ <= 16);
  double xL = in1;
  double xR = in2;
  // Pre EQ: HP + five broad tone bands + LP. The five gain controls now affect
  // five distinct spectral regions.
  double lcf = clamp(prelowcut, 20, min(3000, sr * .18));
  double hcf = clamp(prehighcut, max(lcf + 160, 1800), min(22000, sr * .45));
  double hpc = 1 - exp(-2 * pi * lcf / sr);
  double lpc = 1 - exp(-2 * pi * hcf / sr);
  hpLh = hpLh + (xL - hpLh) * hpc;
  hpRh = hpRh + (xR - hpRh) * hpc;
  double hpL = xL - hpLh;
  double hpR = xR - hpRh;
  outLpL = outLpL + (hpL - outLpL) * lpc;
  outLpR = outLpR + (hpR - outLpR) * lpc;
  double pL0 = outLpL;
  double pR0 = outLpR;
  double pc1 = 1 - exp(-2 * pi * 160 / sr);
  double pc2 = 1 - exp(-2 * pi * 500 / sr);
  double pc3 = 1 - exp(-2 * pi * 2200 / sr);
  double pc4 = 1 - exp(-2 * pi * 6500 / sr);
  preLP1L = preLP1L + (pL0 - preLP1L) * pc1;
  preLP1R = preLP1R + (pR0 - preLP1R) * pc1;
  preLP2L = preLP2L + (pL0 - preLP2L) * pc2;
  preLP2R = preLP2R + (pR0 - preLP2R) * pc2;
  preLP3L = preLP3L + (pL0 - preLP3L) * pc3;
  preLP3R = preLP3R + (pR0 - preLP3R) * pc3;
  preLP4L = preLP4L + (pL0 - preLP4L) * pc4;
  preLP4R = preLP4R + (pR0 - preLP4R) * pc4;
  double pb1L = preLP1L;
  double pb1R = preLP1R;
  double pb2L = preLP2L - preLP1L;
  double pb2R = preLP2R - preLP1R;
  double pb3L = preLP3L - preLP2L;
  double pb3R = preLP3R - preLP2R;
  double pb4L = preLP4L - preLP3L;
  double pb4R = preLP4R - preLP3R;
  double pb5L = pL0 - preLP4L;
  double pb5R = pR0 - preLP4R;
  double tilt = clamp(pretilt, -100, 100) * .01;
  double pg1 = pow(10, (prelow - tilt * 4) / 20);
  double pg2 = pow(10, (prelowmid - tilt * 2) / 20);
  double pg3 = pow(10, premid / 20);
  double pg4 = pow(10, (prehighmid + tilt * 2) / 20);
  double pg5 = pow(10, (prehigh + tilt * 4) / 20);
  double fL = pb1L * pg1 + pb2L * pg2 + pb3L * pg3 + pb4L * pg4 + pb5L * pg5;
  double fR = pb1R * pg1 + pb2R * pg2 + pb3R * pg3 + pb4R * pg4 + pb5R * pg5;
  // Envelope follower for dynamics.
  double ins = max(abs(xL), abs(xR));
  double atk = clamp(duckattack, 1, 250) * .001;
  double rel = clamp(duckrelease, 20, 2000) * .001;
  double ea = 1 - exp(-1 / (atk * sr));
  double er = 1 - exp(-1 / (rel * sr));
  envH = (ins > envH) ? envH + (ins - envH) * ea : envH + (ins - envH) * er;
  // Predelay.
  double pds = clamp(predelay, 0, 300) * .001 * sr;
  double pdL = (pds < 1) ? fL : preL.read(clamp(pds, 1, 140000));
  double pdR = (pds < 1) ? fR : preR.read(clamp(pds, 1, 140000));
  preL.write(fL);
  preR.write(fR);
  // Type topology factors. v0.11 expands the family list while keeping a shared
  // high-quality FDN core.
  ts = 1;
  ers = 1;
  tls = 1;
  db = 0;
  mb = 0;
  spring = 0;
  nl = 0;
  erp = 1;
  om = 0;
  plateMode = 0;
  double plateFamily = (typ >= 17 && typ <= 20);
  springMode = 0;
  double springFamily = (typ == 21 || typ == 22);
  vintageMode = 0;
  double vintageDigitalFamily = (typ == 23 || typ == 24);
  double nonlinearFamily = (typ >= 25 && typ <= 28);
  double gatedFamily = (typ == 25);
  double nonlinearMode = (typ == 26);
  double reverseMode = (typ == 27);
  double bloomMode = (typ == 28);
  double shimmerFamily = (typ == 29 || typ == 30);
  grainMode = 0;
  spectralMode = 0;
  resonMode = 0;
  echoMode = 0;
  tremMode = 0;
  dualMode = 0;
  double dualReverbMode = (typ == 36);
  double ambienceMode = (typ == 0);
  double earlyRefMode = (typ == 1);
  double reflectionSpaceMode = (typ == 47);
  double boomMode = (typ == 48);
  double earlyFamily = (ambienceMode || earlyRefMode || reflectionSpaceMode);
  double modulatedMode = (typ == 31);
  double modEchoMode = (typ == 32);
  double tremoloVerbMode = (typ == 33);
  double modulationFamily = (modulatedMode || modEchoMode || tremoloVerbMode);
  double dynamicMode = (typ == 34);
  double duckMode = (typ == 35);
  double dynamicFamily = (dynamicMode || duckMode);
  double infiniteMode = (typ == 39);
  double freezeMode = (typ == 40);
  double sustainFamily = (infiniteMode || freezeMode);
  double voidMode = (typ == 37);
  double warpMode = (typ == 38);
  double hybridMode = (typ == 45);
  double mangledMode = (typ == 46);
  double creativeFamily = (voidMode || warpMode || hybridMode || mangledMode);
  pt = 0;
  pdamp = 0;
  patk = 0;
  pmet = 0;
  st = 0;
  sdp = 0;
  srz = 0;
  sdr = 0;
  smulti = 0;
  vc = 0;
  vr = 0;
  vdage = 0;
  vdroom = 0;
  sf = 0;
  sdel = 0;
  sdet = 0;
  crys = 0;
  wrp = 0;
  tws = 0;
  hbl = 0;
  cag = 0;
  r1 = .0297;
  r2 = .0371;
  r3 = .0411;
  r4 = .0437;
  r5 = .0313;
  r6 = .0367;
  r7 = .0423;
  r8 = .0479;
  if (typ == 0) {
    ts = .46;
    ers = 1.96;
    tls = .095;
    db = .035;
    mb = .008;
    erp = .64;
    r1 = .0109;
    r2 = .0153;
    r3 = .0201;
    r4 = .0253;
    r5 = .0127;
    r6 = .0173;
    r7 = .0221;
    r8 = .0279;
  }
  if (typ == 1) {
    ts = .40;
    ers = 2.26;
    tls = .005;
    db = .014;
    mb = .004;
    erp = .48;
    r1 = .0089;
    r2 = .0127;
    r3 = .0169;
    r4 = .0217;
    r5 = .0107;
    r6 = .0147;
    r7 = .0191;
    r8 = .0241;
  }
  if (typ == 2) {
    ts = .54;
    ers = 1.68;
    tls = .38;
    db = .075;
    mb = .015;
    erp = .72;
    r1 = .0117;
    r2 = .0157;
    r3 = .0191;
    r4 = .0233;
    r5 = .0133;
    r6 = .0173;
    r7 = .0211;
    r8 = .0257;
  }
  if (typ == 3) {
    ts = .68;
    ers = 1.42;
    tls = .60;
    db = .070;
    mb = .020;
    erp = .82;
    r1 = .0151;
    r2 = .0199;
    r3 = .0241;
    r4 = .0293;
    r5 = .0171;
    r6 = .0221;
    r7 = .0267;
    r8 = .0323;
  }
  if (typ == 4) {
    ts = .86;
    ers = 1.18;
    tls = .84;
    db = .085;
    mb = .030;
    erp = .98;
    r1 = .0197;
    r2 = .0259;
    r3 = .0311;
    r4 = .0373;
    r5 = .0221;
    r6 = .0287;
    r7 = .0343;
    r8 = .0407;
  }
  if (typ == 5) {
    ts = .62;
    ers = 1.38;
    tls = .50;
    db = .105;
    mb = .008;
    erp = .78;
    r1 = .0137;
    r2 = .0181;
    r3 = .0221;
    r4 = .0267;
    r5 = .0157;
    r6 = .0201;
    r7 = .0247;
    r8 = .0293;
  }
  if (typ == 6) {
    ts = .88;
    ers = .98;
    tls = 1.08;
    db = .205;
    mb = .055;
    erp = .94;
    om = 1;
    r1 = .0149;
    r2 = .0179;
    r3 = .0211;
    r4 = .0247;
    r5 = .0163;
    r6 = .0193;
    r7 = .0227;
    r8 = .0267;
  }
  if (typ == 7) {
    ts = .92;
    ers = .80;
    tls = 1.12;
    db = .155;
    mb = .225;
    erp = .90;
    om = 3;
    vintageMode = 1;
    r1 = .0181;
    r2 = .0239;
    r3 = .0293;
    r4 = .0353;
    r5 = .0203;
    r6 = .0261;
    r7 = .0317;
    r8 = .0381;
  }
  if (typ == 8) {
    ts = 1.04;
    ers = .72;
    tls = 1.10;
    db = .035;
    mb = .14;
    erp = 1.02;
    r1 = .0283;
    r2 = .0359;
    r3 = .0401;
    r4 = .0449;
    r5 = .0307;
    r6 = .0361;
    r7 = .0427;
    r8 = .0487;
  }
  if (typ == 9) {
    ts = 1.10;
    ers = .66;
    tls = 1.14;
    db = .045;
    mb = .16;
    erp = 1.06;
    r1 = .0297;
    r2 = .0371;
    r3 = .0411;
    r4 = .0437;
    r5 = .0313;
    r6 = .0367;
    r7 = .0423;
    r8 = .0479;
  }
  if (typ == 10) {
    ts = 1.12;
    ers = .56;
    tls = 1.16;
    db = .055;
    mb = .78;
    erp = 1.04;
    r1 = .0307;
    r2 = .0389;
    r3 = .0449;
    r4 = .0503;
    r5 = .0331;
    r6 = .0401;
    r7 = .0467;
    r8 = .0539;
  }
  if (typ == 11) {
    ts = 1.08;
    ers = .62;
    tls = 1.16;
    db = .035;
    mb = .38;
    erp = 1.08;
    r1 = .0277;
    r2 = .0347;
    r3 = .0409;
    r4 = .0467;
    r5 = .0301;
    r6 = .0367;
    r7 = .0433;
    r8 = .0503;
  }
  if (typ == 12) {
    ts = 1.30;
    ers = .48;
    tls = 1.25;
    db = .10;
    mb = .20;
    erp = 1.20;
    r1 = .0367;
    r2 = .0479;
    r3 = .0533;
    r4 = .0611;
    r5 = .0413;
    r6 = .0497;
    r7 = .0571;
    r8 = .0679;
  }
  if (typ == 13) {
    ts = 1.54;
    ers = .30;
    tls = 1.38;
    db = .16;
    mb = .18;
    erp = 1.42;
    r1 = .0479;
    r2 = .0611;
    r3 = .0713;
    r4 = .0839;
    r5 = .0533;
    r6 = .0679;
    r7 = .0791;
    r8 = .0917;
  }
  if (typ == 14) {
    ts = 1.62;
    ers = .36;
    tls = 1.35;
    db = .14;
    mb = .26;
    erp = 1.48;
    r1 = .0511;
    r2 = .0667;
    r3 = .0779;
    r4 = .0911;
    r5 = .0577;
    r6 = .0719;
    r7 = .0847;
    r8 = .0983;
  }
  if (typ == 15) {
    ts = 1.72;
    ers = .20;
    tls = 1.42;
    db = .12;
    mb = .24;
    erp = 1.52;
    r1 = .0551;
    r2 = .0721;
    r3 = .0839;
    r4 = .0997;
    r5 = .0617;
    r6 = .0781;
    r7 = .0917;
    r8 = .1087;
  }
  if (typ == 16) {
    ts = 1.52;
    ers = .14;
    tls = 1.40;
    db = .18;
    mb = .44;
    erp = 1.34;
    r1 = .0433;
    r2 = .0577;
    r3 = .0691;
    r4 = .0839;
    r5 = .0503;
    r6 = .0647;
    r7 = .0779;
    r8 = .0931;
  }
  if (typ == 17) {
    ts = .76;
    ers = .08;
    tls = 1.40;
    db = .46;
    mb = .10;
    erp = .36;
    om = 1;
    plateMode = 1;
    r1 = .0089;
    r2 = .0107;
    r3 = .0129;
    r4 = .0151;
    r5 = .0097;
    r6 = .0117;
    r7 = .0141;
    r8 = .0167;
  }
  if (typ == 18) {
    ts = .88;
    ers = .07;
    tls = 1.43;
    db = .43;
    mb = .20;
    erp = .34;
    om = 1;
    plateMode = 1;
    vintageMode = 1;
    r1 = .0101;
    r2 = .0127;
    r3 = .0157;
    r4 = .0187;
    r5 = .0113;
    r6 = .0143;
    r7 = .0173;
    r8 = .0203;
  }
  if (typ == 19) {
    ts = .70;
    ers = .055;
    tls = 1.36;
    db = .50;
    mb = .12;
    erp = .32;
    om = 1;
    plateMode = 1;
    r1 = .0079;
    r2 = .0097;
    r3 = .0119;
    r4 = .0143;
    r5 = .0087;
    r6 = .0107;
    r7 = .0131;
    r8 = .0157;
  }
  if (typ == 20) {
    ts = .96;
    ers = .045;
    tls = 1.50;
    db = .56;
    mb = .08;
    erp = .30;
    om = 1;
    plateMode = 1;
    r1 = .0113;
    r2 = .0139;
    r3 = .0169;
    r4 = .0203;
    r5 = .0121;
    r6 = .0151;
    r7 = .0181;
    r8 = .0217;
  }
  if (typ == 21) {
    ts = .56;
    ers = .16;
    tls = 1.16;
    spring = 1;
    springMode = 1;
    db = .035;
    mb = .22;
    erp = .36;
    om = 2;
    r1 = .0045;
    r2 = .0059;
    r3 = .0075;
    r4 = .0093;
    r5 = .0053;
    r6 = .0067;
    r7 = .0083;
    r8 = .0105;
  }
  if (typ == 22) {
    ts = .78;
    ers = .11;
    tls = 1.30;
    spring = 1;
    springMode = 1;
    db = .055;
    mb = .34;
    erp = .34;
    om = 2;
    r1 = .0057;
    r2 = .0075;
    r3 = .0095;
    r4 = .0119;
    r5 = .0065;
    r6 = .0083;
    r7 = .0103;
    r8 = .0127;
  }
  if (typ == 23) {
    ts = 1.08;
    ers = .27;
    tls = 1.26;
    db = .075;
    mb = .56;
    erp = .86;
    om = 3;
    vintageMode = 1;
    r1 = .0239;
    r2 = .0317;
    r3 = .0391;
    r4 = .0473;
    r5 = .0269;
    r6 = .0353;
    r7 = .0439;
    r8 = .0527;
  }
  if (typ == 24) {
    ts = .66;
    ers = .98;
    tls = .92;
    db = .055;
    mb = .38;
    erp = .72;
    om = 3;
    vintageMode = 1;
    r1 = .0139;
    r2 = .0187;
    r3 = .0241;
    r4 = .0299;
    r5 = .0161;
    r6 = .0211;
    r7 = .0269;
    r8 = .0331;
  }
  if (typ == 25) {
    ts = .64;
    ers = .18;
    tls = 1.40;
    db = .42;
    mb = .13;
    erp = .56;
    om = 1;
    r1 = .0089;
    r2 = .0107;
    r3 = .0127;
    r4 = .0151;
    r5 = .0097;
    r6 = .0117;
    r7 = .0139;
    r8 = .0163;
  }
  if (typ == 26) {
    ts = .86;
    ers = .28;
    tls = 1.30;
    db = .24;
    mb = .22;
    nl = 1;
    erp = .84;
    r1 = .0173;
    r2 = .0233;
    r3 = .0299;
    r4 = .0367;
    r5 = .0197;
    r6 = .0257;
    r7 = .0323;
    r8 = .0397;
  }
  if (typ == 27) {
    ts = 1.04;
    ers = .12;
    tls = 1.30;
    db = .08;
    mb = .36;
    erp = .96;
    r1 = .0241;
    r2 = .0329;
    r3 = .0419;
    r4 = .0511;
    r5 = .0277;
    r6 = .0367;
    r7 = .0457;
    r8 = .0563;
  }
  if (typ == 28) {
    ts = 1.34;
    ers = .07;
    tls = 1.42;
    db = .18;
    mb = .50;
    erp = 1.12;
    r1 = .0367;
    r2 = .0479;
    r3 = .0581;
    r4 = .0697;
    r5 = .0419;
    r6 = .0529;
    r7 = .0631;
    r8 = .0751;
  }
  if (typ == 29) {
    ts = 1.36;
    ers = .11;
    tls = 1.42;
    mb = .70;
    db = .14;
    erp = 1.10;
    om = 3;
    r1 = .0341;
    r2 = .0437;
    r3 = .0529;
    r4 = .0637;
    r5 = .0389;
    r6 = .0487;
    r7 = .0581;
    r8 = .0697;
  }
  if (typ == 30) {
    ts = 1.18;
    ers = .08;
    tls = 1.34;
    mb = .88;
    db = .18;
    erp = 1.04;
    om = 3;
    r1 = .0271;
    r2 = .0359;
    r3 = .0449;
    r4 = .0547;
    r5 = .0313;
    r6 = .0397;
    r7 = .0491;
    r8 = .0593;
  }
  if (typ == 31) {
    ts = 1.16;
    ers = .20;
    tls = 1.34;
    mb = 1.02;
    db = .14;
    erp = 1.02;
    om = 3;
    r1 = .0301;
    r2 = .0397;
    r3 = .0487;
    r4 = .0589;
    r5 = .0341;
    r6 = .0443;
    r7 = .0539;
    r8 = .0641;
  }
  if (typ == 32) {
    ts = 1.06;
    ers = .20;
    tls = 1.22;
    mb = .70;
    db = .11;
    erp = .96;
    echoMode = 1;
    r1 = .0263;
    r2 = .0353;
    r3 = .0449;
    r4 = .0547;
    r5 = .0301;
    r6 = .0397;
    r7 = .0491;
    r8 = .0593;
  }
  if (typ == 33) {
    ts = 1.02;
    ers = .28;
    tls = 1.22;
    mb = .36;
    db = .08;
    erp = .92;
    tremMode = 1;
    r1 = .0239;
    r2 = .0319;
    r3 = .0401;
    r4 = .0491;
    r5 = .0271;
    r6 = .0359;
    r7 = .0443;
    r8 = .0533;
  }
  if (typ == 34) {
    ts = 1.04;
    ers = .44;
    tls = 1.22;
    mb = .16;
    db = .10;
    erp = .90;
    r1 = .0233;
    r2 = .0311;
    r3 = .0397;
    r4 = .0487;
    r5 = .0269;
    r6 = .0353;
    r7 = .0443;
    r8 = .0539;
  }
  if (typ == 35) {
    ts = 1.02;
    ers = .28;
    tls = 1.30;
    mb = .20;
    db = .12;
    erp = .94;
    r1 = .0251;
    r2 = .0337;
    r3 = .0427;
    r4 = .0521;
    r5 = .0289;
    r6 = .0377;
    r7 = .0473;
    r8 = .0571;
  }
  if (typ == 36) {
    ts = 1.24;
    ers = .50;
    tls = 1.32;
    mb = .20;
    db = .14;
    erp = 1.10;
    dualMode = 1;
    r1 = .0271;
    r2 = .0361;
    r3 = .0457;
    r4 = .0563;
    r5 = .0311;
    r6 = .0407;
    r7 = .0503;
    r8 = .0611;
  }
  if (typ == 37) {
    ts = 1.68;
    ers = .035;
    tls = 1.56;
    mb = .50;
    db = .22;
    erp = 1.30;
    om = 3;
    r1 = .0473;
    r2 = .0619;
    r3 = .0751;
    r4 = .0907;
    r5 = .0547;
    r6 = .0697;
    r7 = .0839;
    r8 = .1009;
  }
  if (typ == 38) {
    ts = 1.42;
    ers = .10;
    tls = 1.42;
    mb = .98;
    db = .24;
    erp = 1.20;
    om = 3;
    r1 = .0307;
    r2 = .0419;
    r3 = .0533;
    r4 = .0661;
    r5 = .0359;
    r6 = .0473;
    r7 = .0599;
    r8 = .0731;
  }
  if (typ == 39) {
    ts = 1.52;
    ers = .05;
    tls = 1.48;
    mb = .36;
    db = .22;
    erp = 1.24;
    r1 = .0401;
    r2 = .0529;
    r3 = .0647;
    r4 = .0781;
    r5 = .0461;
    r6 = .0589;
    r7 = .0713;
    r8 = .0859;
  }
  if (typ == 40) {
    ts = 1.34;
    ers = .025;
    tls = 1.44;
    mb = .18;
    db = .26;
    erp = 1.14;
    r1 = .0371;
    r2 = .0491;
    r3 = .0601;
    r4 = .0727;
    r5 = .0427;
    r6 = .0547;
    r7 = .0661;
    r8 = .0797;
  }
  if (typ == 41) {
    ts = 1.04;
    ers = .18;
    tls = 1.20;
    mb = .92;
    db = .22;
    erp = .96;
    grainMode = 1;
  }
  if (typ == 42) {
    ts = 1.24;
    ers = .10;
    tls = 1.38;
    mb = .66;
    db = .30;
    erp = 1.08;
    grainMode = 1;
  }
  if (typ == 43) {
    ts = 1.34;
    ers = .065;
    tls = 1.44;
    db = .36;
    mb = .40;
    erp = 1.06;
    spectralMode = 1;
    r1 = .0317;
    r2 = .0421;
    r3 = .0527;
    r4 = .0641;
    r5 = .0361;
    r6 = .0473;
    r7 = .0587;
    r8 = .0713;
  }
  if (typ == 44) {
    ts = 1.02;
    ers = .16;
    tls = 1.28;
    db = .18;
    mb = .16;
    erp = .94;
    resonMode = 1;
    r1 = .0217;
    r2 = .0289;
    r3 = .0367;
    r4 = .0451;
    r5 = .0247;
    r6 = .0323;
    r7 = .0407;
    r8 = .0497;
  }
  if (typ == 45) {
    ts = 1.20;
    ers = .36;
    tls = 1.30;
    mb = .28;
    db = .15;
    erp = 1.06;
    dualMode = 1;
    r1 = .0277;
    r2 = .0367;
    r3 = .0461;
    r4 = .0569;
    r5 = .0313;
    r6 = .0413;
    r7 = .0509;
    r8 = .0623;
  }
  if (typ == 46) {
    ts = 1.06;
    ers = .18;
    tls = 1.30;
    mb = .78;
    db = .24;
    nl = 1;
    erp = .84;
    om = 3;
    echoMode = 1;
    r1 = .0199;
    r2 = .0271;
    r3 = .0359;
    r4 = .0457;
    r5 = .0233;
    r6 = .0311;
    r7 = .0401;
    r8 = .0503;
  }
  if (typ == 47) {
    ts = .90;
    ers = 1.86;
    tls = .54;
    db = .070;
    mb = .018;
    erp = .92;
    r1 = .0181;
    r2 = .0241;
    r3 = .0299;
    r4 = .0367;
    r5 = .0207;
    r6 = .0267;
    r7 = .0329;
    r8 = .0401;
  }
  if (typ == 48) {
    ts = 1.16;
    ers = .10;
    tls = 1.16;
    db = .14;
    mb = .10;
    erp = .82;
    r1 = .0203;
    r2 = .0277;
    r3 = .0359;
    r4 = .0451;
    r5 = .0239;
    r6 = .0317;
    r7 = .0407;
    r8 = .0509;
  }
  // Dedicated family controls modify topology rather than acting as cosmetic
  // macros.
  double shp = (clamp(roomshape, 0, 100) - 50) * .006;
  r1 *= 1 + shp * .35;
  r3 *= 1 + shp * .22;
  r5 *= 1 - shp * .20;
  r7 *= 1 - shp * .32;
  if (plateMode) {
    pt = clamp(platetension, 0, 100) * .01;
    pdamp = clamp(platedamp, 0, 100) * .01;
    patk = clamp(plateattack, 0, 100) * .01;
    pmet = clamp(platemetal, 0, 100) * .01;
    ts *= .78 + .38 * pt;
    db += .24 + .24 * patk;
    mb += .08 + .24 * pmet;
    tls *= 1.00 + .13 * (1 - pdamp);
    if (plateFamily) {
      r1 *= .94 + .11 * pt;
      r2 *= 1.03 - .06 * pt;
      r3 *= .97 + .08 * pmet;
      r4 *= 1.04 - .04 * pmet;
      r5 *= 1.01 - .05 * pt;
      r6 *= .96 + .09 * pt;
      r7 *= 1.03 - .04 * pmet;
      r8 *= .97 + .08 * pmet;
    }
  }
  if (springMode) {
    st = clamp(springtension, 0, 100) * .01;
    sdp = clamp(springdispersion, 0, 100) * .01;
    srz = clamp(springresonance, 0, 100) * .01;
    sdr = clamp(springdrip, 0, 100) * .01;
    smulti = (typ == 22) ? 1 : 0;
    ts *= .74 + .42 * st + .07 * smulti;
    mb += .16 + .32 * sdp + .07 * smulti;
    spring = 1 + .56 * sdr + .12 * srz;
    tls *= .92 + .13 * srz + .07 * smulti;
    db += .10 + .15 * sdp;
    if (springFamily) {
      r1 *= .93 + .15 * st;
      r2 *= 1.04 - .08 * st;
      r3 *= .93 + .18 * sdp;
      r4 *= 1.07 - .09 * sdp;
      r5 *= 1.02 - .06 * st;
      r6 *= .94 + .16 * st;
      r7 *= 1.03 - .04 * srz;
      r8 *= .97 + .09 * srz;
    }
  }
  if (vintageMode) {
    vc = clamp(vintagecolor, 0, 100) * .01;
    vr = clamp(vintagerate, 0, 100) * .01;
    mb += .45 * vr;
    tls *= .96 + .08 * vc;
    if (vintageDigitalFamily) {
      vdage = clamp(colorage, 0, 100) * .01;
      vdroom = (typ == 24) ? 1 : 0;
      mb += .22 + .36 * vr;
      tls *= .99 + .08 * (1 - vdage);
      db += .07 + .11 * vc;
      r1 *= .95 + .12 * vr;
      r2 *= 1.04 - .06 * vc;
      r3 *= .96 + .10 * vc;
      r4 *= 1.05 - .05 * vr;
      r5 *= 1.03 - .05 * vc;
      r6 *= .95 + .10 * vr;
      r7 *= 1.06 - .06 * vc;
      r8 *= .94 + .11 * vr;
    }
  }
  if (shimmerFamily) {
    sf = clamp(shimmerfeedback, 0, 100) * .01;
    sdel = clamp(shimmerdelay, 0, 500) * .002;
    sdet = clamp(detune, 0, 50) * .02;
    crys = (typ == 30) ? 1 : 0;
    mb += .30 + .34 * sdet + .14 * crys;
    tls *= 1.05 + .14 * sf + .05 * crys;
    db += .07 + .11 * sdel;
    r1 *= .96 + .10 * sdet;
    r2 *= 1.05 - .06 * sdet;
    r3 *= .95 + .09 * sf;
    r4 *= 1.06 - .05 * sf;
    r5 *= 1.04 - .05 * sdet;
    r6 *= .95 + .10 * sdet;
    r7 *= 1.05 - .04 * sf;
    r8 *= .94 + .11 * sf;
  }
  if (creativeFamily) {
    wrp = clamp(warpamount, 0, 100) * .01;
    tws = clamp(twist, -100, 100) * .01;
    hbl = clamp(hybridblend, 0, 100) * .01;
    cag = clamp(colorage, 0, 100) * .01;
  }
  if (voidMode) {
    tls *= 1.08 + .11 * cag;
    mb += .08 + .12 * cag;
    db += .11;
    erp *= .88;
    r1 *= 1.03 + .06 * cag;
    r2 *= .97 + .05 * cag;
    r3 *= 1.05 + .05 * cag;
    r4 *= .96 + .04 * cag;
    r5 *= 1.04 + .05 * cag;
    r6 *= .97 + .04 * cag;
    r7 *= 1.06 + .04 * cag;
    r8 *= .95 + .04 * cag;
  }
  if (warpMode) {
    mb += .22 + .36 * wrp;
    db += .08 + .15 * wrp;
    tls *= 1 + .10 * wrp;
    r1 *= .90 + .21 * wrp;
    r2 *= 1.07 - .09 * wrp;
    r3 *= .92 + .19 * abs(tws);
    r4 *= 1.09 - .07 * abs(tws);
    r5 *= 1.05 - .09 * wrp;
    r6 *= .91 + .20 * wrp;
    r7 *= 1.08 - .07 * abs(tws);
    r8 *= .92 + .18 * abs(tws);
  }
  if (hybridMode) {
    mb += .12 + .22 * hbl;
    db += .07 + .12 * hbl;
    tls *= .99 + .12 * hbl;
    r1 *= .97 + .06 * hbl;
    r2 *= 1.03 - .04 * hbl;
    r3 *= .96 + .06 * hbl;
    r4 *= 1.04 - .04 * hbl;
    r5 *= 1.03 - .04 * hbl;
    r6 *= .97 + .06 * hbl;
    r7 *= 1.04 - .04 * hbl;
    r8 *= .96 + .06 * hbl;
  }
  if (mangledMode) {
    wrp = max(wrp, .60);
    if (abs(tws) < .05)
      tws = .62;
    mb += .26 + .32 * wrp;
    db += .12 + .15 * abs(tws);
    tls *= 1.03 + .10 * wrp;
    r1 *= .88 + .23 * wrp;
    r2 *= 1.09 - .11 * wrp;
    r3 *= .90 + .21 * abs(tws);
    r4 *= 1.11 - .09 * abs(tws);
    r5 *= 1.07 - .11 * wrp;
    r6 *= .89 + .22 * wrp;
    r7 *= 1.10 - .09 * abs(tws);
    r8 *= .90 + .21 * abs(tws);
  }
  // HQ second delay bank: decorrelated irrational offsets chosen to reduce
  // repeated modal families.
  double r9 = r1 * 1.137 + .00137;
  double r10 = r2 * 1.089 + .00271;
  double r11 = r3 * 1.173 + .00191;
  double r12 = r4 * 1.061 + .00347;
  double r13 = r5 * 1.211 + .00113;
  double r14 = r6 * 1.097 + .00229;
  double r15 = r7 * 1.149 + .00173;
  double r16 = r8 * 1.071 + .00311;
  // Distance softens highs and shifts ER/tail balance.
  double dist = clamp(distance, 0, 100) * .01;
  double sz = clamp(size, 30, 200) * .01 * ts;
  double xf = clamp(crossfeed, 0, 100) * .005;
  double sL = mix(pdL, pdR, xf);
  double sR = mix(pdR, pdL, xf);
  // Early reflections, with independent size/diffusion/width.
  double ersz = clamp(earlysize, 25, 200) * .01;
  if (ambienceMode)
    ersz *= .86;
  if (earlyRefMode)
    ersz *= .72;
  if (reflectionSpaceMode)
    ersz *= 1.20;
  erL.write(sL);
  erR.write(sR);
  double e1L =
      erL.read(clamp((.0047 + .0060 * sz * ersz * erp) * sr, 1, 140000));
  double e2L =
      erL.read(clamp((.0109 + .0090 * sz * ersz * erp) * sr, 1, 140000));
  double e3L =
      erL.read(clamp((.0177 + .0140 * sz * ersz * erp) * sr, 1, 140000));
  double e4L =
      erL.read(clamp((.0271 + .0190 * sz * ersz * erp) * sr, 1, 140000));
  double e5L =
      erL.read(clamp((.0389 + .0247 * sz * ersz * erp) * sr, 1, 140000));
  double e6L =
      erL.read(clamp((.0527 + .0311 * sz * ersz * erp) * sr, 1, 140000));
  double e1R =
      erR.read(clamp((.0059 + .0054 * sz * ersz * erp) * sr, 1, 140000));
  double e2R =
      erR.read(clamp((.0127 + .0083 * sz * ersz * erp) * sr, 1, 140000));
  double e3R =
      erR.read(clamp((.0201 + .0133 * sz * ersz * erp) * sr, 1, 140000));
  double e4R =
      erR.read(clamp((.0313 + .0176 * sz * ersz * erp) * sr, 1, 140000));
  double e5R =
      erR.read(clamp((.0431 + .0229 * sz * ersz * erp) * sr, 1, 140000));
  double e6R =
      erR.read(clamp((.0571 + .0293 * sz * ersz * erp) * sr, 1, 140000));
  double erd = clamp(earlydiff, 0, 100) * .01;
  double epat = clamp(erpattern, -100, 100) * .01;
  if (ambienceMode)
    erd = max(erd, .50);
  if (earlyRefMode)
    erd = min(erd, .36);
  if (reflectionSpaceMode)
    erd = max(erd, .66);
  erOutL = e1L * (.35 + .06 * epat) + e2R * (.12 + .10 * erd) -
           e3L * (.10 + .10 * erd - .04 * epat) + e4R * (.15 - .04 * epat) +
           e5L * (.10 + .06 * erd) - e6R * (.07 + .03 * erd);
  erOutR = e1R * (.35 - .06 * epat) + e2L * (.12 + .10 * erd) -
           e3R * (.10 + .10 * erd + .04 * epat) + e4L * (.15 + .04 * epat) +
           e5R * (.10 + .06 * erd) - e6L * (.07 + .03 * erd);
  if (ambienceMode) {
    erOutL = erOutL * .90 + (e4R + e5L - e6R) * .070;
    erOutR = erOutR * .90 + (e4L + e5R - e6L) * .070;
  }
  if (earlyRefMode) {
    erOutL = erOutL * 1.16 + e2R * .075 - e5L * .035;
    erOutR = erOutR * 1.16 + e2L * .075 - e5R * .035;
  }
  if (reflectionSpaceMode) {
    erOutL = erOutL * .88 + e5L * .17 - e6R * .11 + e4R * .085;
    erOutR = erOutR * .88 + e5R * .17 - e6L * .11 + e4L * .085;
  }
  double erMid = (erOutL + erOutR) * .5;
  double erSide =
      (erOutL - erOutR) * .5 * clamp(earlywidth, 0, 200) * .01 *
      ((typ == 0)
           ? .82
           : ((typ == 1)
                  ? .68
                  : ((typ == 47)
                         ? .96
                         : ((typ == 2)
                                ? .66
                                : ((typ == 3)
                                       ? .76
                                       : ((typ == 4)
                                              ? .88
                                              : ((typ == 5)
                                                     ? .72
                                                     : ((typ == 6)
                                                            ? .92
                                                            : ((typ == 7)
                                                                   ? .88
                                                                   : 1)))))))));
  erOutL = erMid + erSide;
  erOutR = erMid - erSide;
  // Tail predelay.
  double tds =
      clamp(taildelay + (hallMode ? (12 + clamp(size, 20, 200) * .075) : 0) +
                ((typ == 6 || typ == 7) ? 6 : 0) +
                ((typ == 29 || typ == 30)
                     ? (35 + clamp(shimmerdelay, 0, 500) * 1.10)
                     : 0),
            0, 850) *
      .001 * sr;
  double tin0L = (tds < 1) ? sL : tailPreL.read(clamp(tds, 1, 140000));
  double tin0R = (tds < 1) ? sR : tailPreR.read(clamp(tds, 1, 140000));
  tailPreL.write(sL);
  tailPreR.write(sR);
  // HQ 16-line modulated FDN. Additional decorrelated lines greatly increase
  // late-tail density.
  double vrmod =
      1 + ((typ == 23 || typ == 24) ? clamp(vintagerate, 0, 100) * .012 : 0);
  mr = clamp(modrate, .01, 3) * (1 + mb) * vrmod;
  md = clamp(moddepth +
                 ((typ == 29 || typ == 30) ? clamp(detune, 0, 50) * .08 : 0),
             0, 16) *
       .001 * sr * (1 + mb * .7);
  rn = clamp(modrandom, 0, 100) * .01;
  dr = clamp(drift, 0, 100) * .01;
  spn = clamp(spin, 0, 100) * .01;
  wnd = clamp(wander, 0, 100) * .01;
  mot = clamp(motion, 0, 100) * .01;
  if (naturalMode) {
    rn = max(rn, .24);
    dr = max(dr, .10);
    wnd = max(wnd, .08);
    mot = max(mot, .06);
  }
  if (hallMode) {
    rn = max(rn, .38);
    dr = max(dr, .18);
    wnd = max(wnd, .17);
    mot = max(mot, .12);
    mr = max(mr, .075);
    md = max(md, .00018 * sr);
  }
  if (plateFamily) {
    rn = max(rn, .34 + .16 * pmet);
    dr = max(dr, .09 + .09 * pmet);
    wnd = max(wnd, .12 + .08 * pmet);
    mot = max(mot, .09 + .09 * pmet);
    mr = max(mr, .14 + .10 * pt);
    md = max(md, (.00028 + .00038 * pmet) * sr);
  }
  if (springFamily) {
    rn = max(rn, .08 + .13 * sdp);
    dr = max(dr, .035 + .055 * sdp);
    wnd = max(wnd, .035 + .045 * sdp);
    mot = max(mot, .03 + .055 * srz);
    mr = max(mr, .08 + .08 * st);
    md = max(md, (.00008 + .00022 * sdp) * sr);
  }
  if (vintageDigitalFamily) {
    rn = max(rn, .48 + .26 * vr);
    dr = max(dr, .18 + .18 * vc);
    wnd = max(wnd, .18 + .20 * vr);
    mot = max(mot, .13 + .16 * vc);
    mr = max(mr, .16 + .18 * vr);
    md = max(md, (.00038 + .00074 * vr) * sr);
  }
  if (nonlinearFamily) {
    rn = max(rn, gatedFamily ? .18 : .28);
    dr = max(dr, gatedFamily ? .05 : .10);
    wnd = max(wnd, bloomMode ? .12 : .08);
    mot = max(mot, reverseMode ? .10 : .06);
    mr = max(mr, .10);
    md = max(md, (gatedFamily ? .00012 : .00024) * sr);
  }
  if (spectralMode) {
    rn = max(rn, .30);
    dr = max(dr, .10);
    wnd = max(wnd, .12);
    mot = max(mot, .08);
    mr = max(mr, .085);
    md = max(md, .00018 * sr);
  }
  if (resonMode) {
    rn = max(rn, .18);
    dr = max(dr, .055);
    wnd = max(wnd, .065);
    mot = max(mot, .045);
    mr = max(mr, .065);
    md = max(md, .00010 * sr);
  }
  if (voidMode) {
    rn = max(rn, .30);
    dr = max(dr, .13);
    wnd = max(wnd, .16);
    mot = max(mot, .11);
    mr = max(mr, .085);
    md = max(md, .00024 * sr);
  }
  if (warpMode) {
    rn = max(rn, .42 + .24 * wrp);
    dr = max(dr, .16 + .16 * wrp);
    wnd = max(wnd, .20 + .15 * wrp);
    mot = max(mot, .18 + .15 * abs(tws));
    mr = max(mr, .15 + .14 * wrp);
    md = max(md, (.00032 + .00052 * wrp) * sr);
  }
  if (hybridMode) {
    rn = max(rn, .30 + .20 * hbl);
    dr = max(dr, .11 + .13 * hbl);
    wnd = max(wnd, .14 + .14 * hbl);
    mot = max(mot, .11 + .11 * hbl);
    mr = max(mr, .11 + .12 * hbl);
    md = max(md, (.00022 + .00036 * hbl) * sr);
  }
  if (mangledMode) {
    rn = max(rn, .58);
    dr = max(dr, .27);
    wnd = max(wnd, .26);
    mot = max(mot, .24);
    mr = max(mr, .20);
    md = max(md, .00070 * sr);
  }
  if (grainMode) {
    rn = max(rn, (typ == 42) ? .36 : .26);
    dr = max(dr, (typ == 42) ? .16 : .10);
    wnd = max(wnd, (typ == 42) ? .18 : .12);
    mot = max(mot, (typ == 42) ? .18 : .11);
    mr = max(mr, (typ == 42) ? .10 : .14);
    md = max(md, ((typ == 42) ? .00034 : .00024) * sr);
  }
  if (modulatedMode) {
    rn = max(rn, .64);
    dr = max(dr, .36);
    wnd = max(wnd, .40);
    mot = max(mot, .36);
    mr = max(mr, .15);
    md = max(md, .00062 * sr);
  }
  if (modEchoMode) {
    rn = max(rn, .48);
    dr = max(dr, .22);
    wnd = max(wnd, .24);
    mot = max(mot, .22);
    mr = max(mr, .14);
    md = max(md, .00040 * sr);
  }
  if (tremoloVerbMode) {
    rn = max(rn, .26);
    dr = max(dr, .10);
    wnd = max(wnd, .08);
    mot = max(mot, .08);
    mr = max(mr, .10);
    md = max(md, .00014 * sr);
  }
  if (dualReverbMode) {
    rn = max(rn, .38);
    dr = max(dr, .16);
    wnd = max(wnd, .20);
    mot = max(mot, .15);
    mr = max(mr, .11);
    md = max(md, .00024 * sr);
  }
  if (ambienceMode) {
    rn = max(rn, .08);
    dr = max(dr, .025);
    wnd = max(wnd, .025);
    mot = max(mot, .018);
  }
  if (earlyRefMode) {
    rn = max(rn, .025);
    dr = max(dr, .008);
    wnd = max(wnd, .008);
    mot = max(mot, .006);
  }
  if (reflectionSpaceMode) {
    rn = max(rn, .12);
    dr = max(dr, .035);
    wnd = max(wnd, .045);
    mot = max(mot, .028);
  }
  if (infiniteMode) {
    rn = max(rn, .34);
    dr = max(dr, .15);
    wnd = max(wnd, .18);
    mot = max(mot, .13);
    mr = max(mr, .085);
    md = max(md, .00022 * sr);
  }
  if (freezeMode) {
    rn = max(rn, .10);
    dr = max(dr, .03);
    wnd = max(wnd, .035);
    mot = max(mot, .025);
    mr = max(mr, .04);
    md = max(md, .00006 * sr);
  }
  if (shimmerFamily) {
    rn = max(rn, .48 + .24 * sdet + .06 * crys);
    dr = max(dr, .20 + .15 * sf);
    wnd = max(wnd, .22 + .18 * sdet + .05 * crys);
    mot = max(mot, .16 + .15 * sf + .04 * crys);
    mr = max(mr, .22 + .14 * sdet + .04 * crys);
    md = max(md, (.00046 + .00072 * sdet + .00010 * crys) * sr);
  }
  if (boomMode) {
    rn = max(rn, .12);
    dr = max(dr, .04);
    wnd = max(wnd, .04);
    mot = max(mot, .03);
    mr = max(mr, .06);
    md = min(md, max(.00016 * sr, md * .42));
  }
  slowMotion = slowMotion + ((sin(2 * pi * ph8) * .5 + .5) - slowMotion) *
                                (.00002 + mot * .00008);
  ph1 = wrap(ph1 + mr * (.83 + .05 * spn) / sr, 0, 1);
  ph2 = wrap(ph2 + mr * (.97 - .04 * spn) / sr, 0, 1);
  ph3 = wrap(ph3 + mr * 1.07 / sr, 0, 1);
  ph4 = wrap(ph4 + mr * 1.21 / sr, 0, 1);
  ph5 = wrap(ph5 + mr * .71 / sr, 0, 1);
  ph6 = wrap(ph6 + mr * 1.13 / sr, 0, 1);
  ph7 = wrap(ph7 + mr * .91 / sr, 0, 1);
  ph8 = wrap(ph8 + mr * 1.29 / sr, 0, 1);
  ph9 = wrap(ph9 + mr * .773 / sr, 0, 1);
  ph10 = wrap(ph10 + mr * 1.173 / sr, 0, 1);
  ph11 = wrap(ph11 + mr * .887 / sr, 0, 1);
  ph12 = wrap(ph12 + mr * 1.337 / sr, 0, 1);
  ph13 = wrap(ph13 + mr * .937 / sr, 0, 1);
  ph14 = wrap(ph14 + mr * 1.257 / sr, 0, 1);
  ph15 = wrap(ph15 + mr * .683 / sr, 0, 1);
  ph16 = wrap(ph16 + mr * 1.413 / sr, 0, 1);
  double mm1 =
      md * (sin(2 * pi * ph1) + rn * .33 * sin(2 * pi * (ph1 * .37 + .11)) +
            dr * .18 * sin(2 * pi * (ph1 * .071 + .21)) +
            wnd * .12 * (slowMotion - .5));
  double mm2 =
      md * (sin(2 * pi * ph2) + rn * .31 * sin(2 * pi * (ph2 * .41 + .27)) +
            dr * .19 * sin(2 * pi * (ph2 * .067 + .43)) -
            wnd * .10 * (slowMotion - .5));
  double mm3 =
      md * (sin(2 * pi * ph3) + rn * .35 * sin(2 * pi * (ph3 * .29 + .39)) +
            dr * .17 * sin(2 * pi * (ph3 * .083 + .61)));
  double mm4 =
      md * (sin(2 * pi * ph4) + rn * .30 * sin(2 * pi * (ph4 * .47 + .53)) +
            dr * .20 * sin(2 * pi * (ph4 * .059 + .79)));
  double mm5 =
      md * (sin(2 * pi * ph5) + rn * .34 * sin(2 * pi * (ph5 * .43 + .66)) +
            dr * .18 * sin(2 * pi * (ph5 * .073 + .91)));
  double mm6 =
      md * (sin(2 * pi * ph6) + rn * .32 * sin(2 * pi * (ph6 * .31 + .78)) +
            dr * .16 * sin(2 * pi * (ph6 * .061 + .15)));
  double mm7 =
      md * (sin(2 * pi * ph7) + rn * .36 * sin(2 * pi * (ph7 * .39 + .89)) +
            dr * .21 * sin(2 * pi * (ph7 * .079 + .35)));
  double mm8 =
      md * (sin(2 * pi * ph8) + rn * .29 * sin(2 * pi * (ph8 * .35 + .97)) +
            dr * .18 * sin(2 * pi * (ph8 * .069 + .57)));
  double mm9 =
      md * (sin(2 * pi * ph9) + rn * .31 * sin(2 * pi * (ph9 * .337 + .17)) +
            dr * .17 * sin(2 * pi * (ph9 * .053 + .31)));
  double mm10 =
      md * (sin(2 * pi * ph10) + rn * .35 * sin(2 * pi * (ph10 * .457 + .23)) +
            dr * .19 * sin(2 * pi * (ph10 * .081 + .47)));
  double mm11 =
      md * (sin(2 * pi * ph11) + rn * .28 * sin(2 * pi * (ph11 * .313 + .41)) +
            dr * .20 * sin(2 * pi * (ph11 * .063 + .59)));
  double mm12 =
      md * (sin(2 * pi * ph12) + rn * .34 * sin(2 * pi * (ph12 * .397 + .52)) +
            dr * .16 * sin(2 * pi * (ph12 * .077 + .71)));
  double mm13 =
      md * (sin(2 * pi * ph13) + rn * .32 * sin(2 * pi * (ph13 * .433 + .64)) +
            dr * .21 * sin(2 * pi * (ph13 * .057 + .83)));
  double mm14 =
      md * (sin(2 * pi * ph14) + rn * .30 * sin(2 * pi * (ph14 * .283 + .76)) +
            dr * .18 * sin(2 * pi * (ph14 * .089 + .13)));
  double mm15 =
      md * (sin(2 * pi * ph15) + rn * .36 * sin(2 * pi * (ph15 * .367 + .88)) +
            dr * .17 * sin(2 * pi * (ph15 * .071 + .37)));
  double mm16 =
      md * (sin(2 * pi * ph16) + rn * .29 * sin(2 * pi * (ph16 * .419 + .94)) +
            dr * .20 * sin(2 * pi * (ph16 * .067 + .67)));
  double ch = clamp(chorus, 0, 100) * .01;
  mm1 *= 1 + ch * .4;
  mm3 *= 1 + ch * .3;
  mm6 *= 1 + ch * .4;
  mm8 *= 1 + ch * .3;
  mm10 *= 1 + ch * .35;
  mm13 *= 1 + ch * .3;
  mm16 *= 1 + ch * .4;
  double t1 = clamp(r1 * sz * sr + mm1, 16, 140000);
  double t2 = clamp(r2 * sz * sr + mm2, 16, 140000);
  double t3 = clamp(r3 * sz * sr + mm3, 16, 140000);
  double t4 = clamp(r4 * sz * sr + mm4, 16, 140000);
  double t5 = clamp(r5 * sz * sr + mm5, 16, 140000);
  double t6 = clamp(r6 * sz * sr + mm6, 16, 140000);
  double t7 = clamp(r7 * sz * sr + mm7, 16, 140000);
  double t8 = clamp(r8 * sz * sr + mm8, 16, 140000);
  double t9 = clamp(r9 * sz * sr + mm9, 16, 140000);
  double t10 = clamp(r10 * sz * sr + mm10, 16, 140000);
  double t11 = clamp(r11 * sz * sr + mm11, 16, 140000);
  double t12 = clamp(r12 * sz * sr + mm12, 16, 140000);
  double t13 = clamp(r13 * sz * sr + mm13, 16, 140000);
  double t14 = clamp(r14 * sz * sr + mm14, 16, 140000);
  double t15 = clamp(r15 * sz * sr + mm15, 16, 140000);
  double t16 = clamp(r16 * sz * sr + mm16, 16, 140000);
  double a1 = d1.read(t1);
  double a2 = d2.read(t2);
  double a3 = d3.read(t3);
  double a4 = d4.read(t4);
  double a5 = d5.read(t5);
  double a6 = d6.read(t6);
  double a7 = d7.read(t7);
  double a8 = d8.read(t8);
  double a9 = d9.read(t9);
  double a10 = d10.read(t10);
  double a11 = d11.read(t11);
  double a12 = d12.read(t12);
  double a13 = d13.read(t13);
  double a14 = d14.read(t14);
  double a15 = d15.read(t15);
  double a16 = d16.read(t16);
  // True low/mid/high feedback damping. Unlike v0.11, MID DECAY now changes an
  // independent feedback band.
  double dn = clamp(damping, 0, 100) * .01;
  wa = clamp(wallabsorb, 0, 100) * .01;
  double aa = clamp(airabsorb, 0, 100) * .01;
  double age = clamp(colorage, 0, 100) * .01;
  double lfc = clamp(lowxover, 45, min(1400, highxover * .55));
  double lcc = 1 - exp(-2 * pi * lfc / sr);
  fc = clamp(highxover * (.35 + .65 * (1 - dn)) * (1 - .34 * wa) *
                 (1 - .26 * aa) * (1 - .22 * age),
             max(lfc * 1.8, 700), min(15000, sr * .28));
  if (vintageDigitalFamily) {
    vdbw = clamp(.94 - .26 * vc - .18 * vdage, .44, .94);
    fc = clamp(fc * vdbw, max(lfc * 1.75, 650),
               min(vdroom ? 11200 : 13200, sr * .26));
  }
  double fcc = 1 - exp(-2 * pi * fc / sr);
  fbLow1 = fbLow1 + (a1 - fbLow1) * lcc;
  fbLow2 = fbLow2 + (a2 - fbLow2) * lcc;
  fbLow3 = fbLow3 + (a3 - fbLow3) * lcc;
  fbLow4 = fbLow4 + (a4 - fbLow4) * lcc;
  fbLow5 = fbLow5 + (a5 - fbLow5) * lcc;
  fbLow6 = fbLow6 + (a6 - fbLow6) * lcc;
  fbLow7 = fbLow7 + (a7 - fbLow7) * lcc;
  fbLow8 = fbLow8 + (a8 - fbLow8) * lcc;
  fbLow9 = fbLow9 + (a9 - fbLow9) * lcc;
  fbLow10 = fbLow10 + (a10 - fbLow10) * lcc;
  fbLow11 = fbLow11 + (a11 - fbLow11) * lcc;
  fbLow12 = fbLow12 + (a12 - fbLow12) * lcc;
  fbLow13 = fbLow13 + (a13 - fbLow13) * lcc;
  fbLow14 = fbLow14 + (a14 - fbLow14) * lcc;
  fbLow15 = fbLow15 + (a15 - fbLow15) * lcc;
  fbLow16 = fbLow16 + (a16 - fbLow16) * lcc;
  fbLp1 = fbLp1 + (a1 - fbLp1) * fcc;
  fbLp2 = fbLp2 + (a2 - fbLp2) * fcc;
  fbLp3 = fbLp3 + (a3 - fbLp3) * fcc;
  fbLp4 = fbLp4 + (a4 - fbLp4) * fcc;
  fbLp5 = fbLp5 + (a5 - fbLp5) * fcc;
  fbLp6 = fbLp6 + (a6 - fbLp6) * fcc;
  fbLp7 = fbLp7 + (a7 - fbLp7) * fcc;
  fbLp8 = fbLp8 + (a8 - fbLp8) * fcc;
  fbLp9 = fbLp9 + (a9 - fbLp9) * fcc;
  fbLp10 = fbLp10 + (a10 - fbLp10) * fcc;
  fbLp11 = fbLp11 + (a11 - fbLp11) * fcc;
  fbLp12 = fbLp12 + (a12 - fbLp12) * fcc;
  fbLp13 = fbLp13 + (a13 - fbLp13) * fcc;
  fbLp14 = fbLp14 + (a14 - fbLp14) * fcc;
  fbLp15 = fbLp15 + (a15 - fbLp15) * fcc;
  fbLp16 = fbLp16 + (a16 - fbLp16) * fcc;
  double avgT = (t1 + t2 + t3 + t4 + t5 + t6 + t7 + t8 + t9 + t10 + t11 + t12 +
                 t13 + t14 + t15 + t16) /
                16;
  double dec = max(clamp(decay, .15, 40), .15);
  double baseg = clamp(pow(.001, (avgT / sr) / dec), .06, .9970);
  double grav = clamp(gravity, -100, 100) * .01;
  baseg = clamp(baseg * (1 + max(grav, 0) * .016), .06, .9972);
  double loctl = max(
      (clamp(subdecay, 25, 200) * .35 + clamp(lowdecay, 25, 200) * .65) * .01,
      .2);
  double midctl = max(clamp(middecay, 25, 200) * .01, .2);
  double hictl =
      max((clamp(highdecay, 25, 200) * .7 + clamp(airdecay, 10, 200) * .3) *
              .01 * (.72 + .28 * (1 - dn)) * (1 - .28 * aa),
          .12);
  loctl *=
      ((typ == 2)
           ? .90
           : ((typ == 3)
                  ? .93
                  : ((typ == 4)
                         ? .96
                         : ((typ == 5)
                                ? .87
                                : ((typ == 6) ? .95
                                              : ((typ == 7) ? .94 : 1))))));
  midctl *=
      ((typ == 2)
           ? .96
           : ((typ == 3)
                  ? .98
                  : ((typ == 4)
                         ? 1
                         : ((typ == 5)
                                ? .94
                                : ((typ == 6) ? 1.03
                                              : ((typ == 7) ? 1.00 : 1))))));
  hictl *=
      ((typ == 2)
           ? .90
           : ((typ == 3)
                  ? .92
                  : ((typ == 4)
                         ? .90
                         : ((typ == 5)
                                ? .78
                                : ((typ == 6) ? .98
                                              : ((typ == 7) ? .84 : 1))))));
  if (hallMode) {
    hictl *=
        ((typ == 11)
             ? 1.10
             : ((typ == 16) ? .62
                            : ((typ == 13) ? .76 : ((typ == 15) ? .82 : .92))));
    midctl *= ((typ >= 13) ? .96 : 1);
    loctl *= ((typ == 15 || typ == 13) ? 1.03 : 1);
  }
  if (plateMode)
    hictl *= 1 - .24 * clamp(platedamp, 0, 100) * .01;
  if (plateFamily) {
    hictl *= ((typ == 19) ? 1.12
                          : ((typ == 18) ? .86 : ((typ == 20) ? .96 : 1.02))) *
             (.94 + .12 * pmet);
    midctl *= ((typ == 20) ? 1.04 : 1.00) + .04 * (1 - pdamp);
    loctl *= ((typ == 20) ? 1.00 : .94) + .04 * (1 - pdamp);
  }
  if (vintageDigitalFamily) {
    hictl *= ((vdroom) ? .92 : .98) * (.78 + .14 * (1 - vc));
    midctl *= .96 + .05 * (1 - vdage);
    loctl *= 1.00 + .04 * vc;
  }
  if (spectralMode) {
    spb = clamp(spectralblur, 0, 100) * .01;
    spf = clamp(spectralfreeze, 0, 100) * .01;
    loctl *= .88 - .16 * spf;
    midctl *= 1.02 + .10 * spf + .05 * spb;
    hictl *= .92 - .10 * spf + .06 * spb;
  }
  if (shimmerFamily) {
    hictl *= crys ? 1.16 : 1.26;
    midctl *= 1.05 + .06 * sf;
    loctl *= .86 + .05 * (1 - crys);
  }
  double glo = clamp(pow(baseg, 1 / loctl), .02, .9975);
  gmid = clamp(pow(baseg, 1 / midctl), .02, .9973);
  double ghi = clamp(pow(baseg, 1 / hictl), .01, .9968);
  fr = clamp(freeze, 0, 100) * .01;
  // v0.104 FREEZE CAPTURE FIX
  // Old Freeze forced fr~1 before the network had captured the new source, and
  // input injection was simultaneously reduced almost to zero.  That could make
  // the Freeze TYPE appear dead. Freeze now opens a capture window while input
  // energy is present, then moves to near-unity feedback once the input falls.
  // The FREEZE knob remains the hold-strength control.
  double fzact = clamp(envH * 24, 0, 1);
  if (infiniteMode)
    fr = max(fr, .994);
  if (freezeMode) {
    fzstr = clamp(freeze, 0, 100) * .01;
    fzfb = mix(.970, .99962, 1 - fzact);
    fr = mix(.85, fzfb, .58 + .42 * fzstr);
  }
  if (spectralMode)
    fr = max(fr, clamp(spectralfreeze, 0, 100) * .0088);
  if (shimmerFamily)
    baseg = clamp(baseg * (1 + .027 * sf), .06, .9978);
  double flt = infiniteMode ? .99934 : (freezeMode ? .99970 : .99935);
  double fmt = infiniteMode ? .99930 : (freezeMode ? .99974 : .99915);
  double fht = infiniteMode ? .99866 : (freezeMode ? .99944 : .9986);
  glo = mix(glo, flt, fr);
  gmid = mix(gmid, fmt, fr);
  ghi = mix(ghi, fht, fr);
  double lo1 = fbLow1;
  double lo2 = fbLow2;
  double lo3 = fbLow3;
  double lo4 = fbLow4;
  double lo5 = fbLow5;
  double lo6 = fbLow6;
  double lo7 = fbLow7;
  double lo8 = fbLow8;
  double lo9 = fbLow9;
  double lo10 = fbLow10;
  double lo11 = fbLow11;
  double lo12 = fbLow12;
  double lo13 = fbLow13;
  double lo14 = fbLow14;
  double lo15 = fbLow15;
  double lo16 = fbLow16;
  double mi1 = fbLp1 - fbLow1;
  double mi2 = fbLp2 - fbLow2;
  double mi3 = fbLp3 - fbLow3;
  double mi4 = fbLp4 - fbLow4;
  double mi5 = fbLp5 - fbLow5;
  double mi6 = fbLp6 - fbLow6;
  double mi7 = fbLp7 - fbLow7;
  double mi8 = fbLp8 - fbLow8;
  double mi9 = fbLp9 - fbLow9;
  double mi10 = fbLp10 - fbLow10;
  double mi11 = fbLp11 - fbLow11;
  double mi12 = fbLp12 - fbLow12;
  double mi13 = fbLp13 - fbLow13;
  double mi14 = fbLp14 - fbLow14;
  double mi15 = fbLp15 - fbLow15;
  double mi16 = fbLp16 - fbLow16;
  double hi1 = a1 - fbLp1;
  double hi2 = a2 - fbLp2;
  double hi3 = a3 - fbLp3;
  double hi4 = a4 - fbLp4;
  double hi5 = a5 - fbLp5;
  double hi6 = a6 - fbLp6;
  double hi7 = a7 - fbLp7;
  double hi8 = a8 - fbLp8;
  double hi9 = a9 - fbLp9;
  double hi10 = a10 - fbLp10;
  double hi11 = a11 - fbLp11;
  double hi12 = a12 - fbLp12;
  double hi13 = a13 - fbLp13;
  double hi14 = a14 - fbLp14;
  double hi15 = a15 - fbLp15;
  double hi16 = a16 - fbLp16;
  double f1 = lo1 * glo + mi1 * gmid + hi1 * ghi;
  double f2 = lo2 * glo + mi2 * gmid + hi2 * ghi;
  double f3 = lo3 * glo + mi3 * gmid + hi3 * ghi;
  double f4 = lo4 * glo + mi4 * gmid + hi4 * ghi;
  double f5 = lo5 * glo + mi5 * gmid + hi5 * ghi;
  double f6 = lo6 * glo + mi6 * gmid + hi6 * ghi;
  double f7 = lo7 * glo + mi7 * gmid + hi7 * ghi;
  double f8 = lo8 * glo + mi8 * gmid + hi8 * ghi;
  double f9 = lo9 * glo + mi9 * gmid + hi9 * ghi;
  double f10 = lo10 * glo + mi10 * gmid + hi10 * ghi;
  double f11 = lo11 * glo + mi11 * gmid + hi11 * ghi;
  double f12 = lo12 * glo + mi12 * gmid + hi12 * ghi;
  double f13 = lo13 * glo + mi13 * gmid + hi13 * ghi;
  double f14 = lo14 * glo + mi14 * gmid + hi14 * ghi;
  double f15 = lo15 * glo + mi15 * gmid + hi15 * ghi;
  double f16 = lo16 * glo + mi16 * gmid + hi16 * ghi;
  // 16x16 normalized Hadamard matrix using butterfly stages.
  double u1 = f1 + f2;
  double u2 = f1 - f2;
  double u3 = f3 + f4;
  double u4 = f3 - f4;
  double u5 = f5 + f6;
  double u6 = f5 - f6;
  double u7 = f7 + f8;
  double u8 = f7 - f8;
  double u9 = f9 + f10;
  double u10 = f9 - f10;
  double u11 = f11 + f12;
  double u12 = f11 - f12;
  double u13 = f13 + f14;
  double u14 = f13 - f14;
  double u15 = f15 + f16;
  double u16 = f15 - f16;
  double v1 = u1 + u3;
  double v2 = u2 + u4;
  double v3 = u1 - u3;
  double v4 = u2 - u4;
  double v5 = u5 + u7;
  double v6 = u6 + u8;
  double v7 = u5 - u7;
  double v8 = u6 - u8;
  double v9 = u9 + u11;
  double v10 = u10 + u12;
  double v11 = u9 - u11;
  double v12 = u10 - u12;
  double v13 = u13 + u15;
  double v14 = u14 + u16;
  double v15 = u13 - u15;
  double v16 = u14 - u16;
  double w1 = v1 + v5;
  double w2 = v2 + v6;
  double w3 = v3 + v7;
  double w4 = v4 + v8;
  double w5 = v1 - v5;
  double w6 = v2 - v6;
  double w7 = v3 - v7;
  double w8 = v4 - v8;
  double w9 = v9 + v13;
  double w10 = v10 + v14;
  double w11 = v11 + v15;
  double w12 = v12 + v16;
  double w13 = v9 - v13;
  double w14 = v10 - v14;
  double w15 = v11 - v15;
  double w16 = v12 - v16;
  double h1 = (w1 + w9) * .25;
  double h2 = (w2 + w10) * .25;
  double h3 = (w3 + w11) * .25;
  double h4 = (w4 + w12) * .25;
  double h5 = (w5 + w13) * .25;
  double h6 = (w6 + w14) * .25;
  double h7 = (w7 + w15) * .25;
  double h8 = (w8 + w16) * .25;
  double h9 = (w1 - w9) * .25;
  double h10 = (w2 - w10) * .25;
  double h11 = (w3 - w11) * .25;
  double h12 = (w4 - w12) * .25;
  double h13 = (w5 - w13) * .25;
  double h14 = (w6 - w14) * .25;
  double h15 = (w7 - w15) * .25;
  double h16 = (w8 - w16) * .25;
  double df = clamp(clamp(diffusion, 0, 100) * .01 + db, 0, 1);
  double td = clamp(taildensity, 0, 100) * .01;
  if (naturalMode)
    td = max(
        td,
        (typ == 2)
            ? .70
            : ((typ == 3)
                   ? .77
                   : ((typ == 4)
                          ? .84
                          : ((typ == 5)
                                 ? .74
                                 : ((typ == 6) ? .92
                                               : ((typ == 7) ? .90 : .84))))));
  if (hallMode)
    td = max(td, .94);
  if (plateFamily)
    td = max(td, (typ == 20) ? .995 : .985);
  if (springFamily)
    td = max(td, (typ == 22) ? .92 : .72);
  if (vintageDigitalFamily)
    td = max(td, vdroom ? .90 : .965);
  if (nonlinearFamily)
    td = max(td,
             gatedFamily ? .98 : (reverseMode ? .92 : (bloomMode ? .98 : .96)));
  if (spectralMode)
    td = max(td, .985);
  if (resonMode)
    td = max(td, .91);
  if (voidMode)
    td = max(td, .975);
  if (warpMode)
    td = max(td, .945);
  if (hybridMode)
    td = max(td, .925);
  if (mangledMode)
    td = max(td, .955);
  if (modulatedMode)
    td = max(td, .975);
  if (modEchoMode)
    td = max(td, .925);
  if (tremoloVerbMode)
    td = max(td, .91);
  if (dynamicMode)
    td = max(td, .90);
  if (duckMode)
    td = max(td, .945);
  if (dualReverbMode)
    td = max(td, .955);
  if (grainMode)
    td = max(td, (typ == 42) ? .97 : .90);
  if (ambienceMode)
    td = max(td, .44);
  if (earlyRefMode)
    td = min(td, .30);
  if (reflectionSpaceMode)
    td = max(td, .64);
  if (infiniteMode)
    td = max(td, .98);
  if (freezeMode)
    td = max(td, .995);
  if (shimmerFamily)
    td = max(td, crys ? .94 : .985);
  if (boomMode)
    td = max(td, .94);
  df = clamp(df * (.62 + .38 * td), 0, 1);
  if (plateFamily)
    df = max(df, .965 + .032 * patk);
  if (springFamily)
    df = max(df, .64 + .26 * sdp + .08 * smulti);
  if (vintageDigitalFamily)
    df = max(df, (vdroom ? .86 : .93) + .045 * vc);
  if (spectralMode)
    df = max(df, .955);
  if (resonMode)
    df = max(df, .84);
  if (voidMode)
    df = max(df, .935);
  if (warpMode)
    df = max(df, .87 + .11 * wrp);
  if (hybridMode)
    df = max(df, .85 + .10 * hbl);
  if (mangledMode)
    df = max(df, .925);
  if (modulatedMode)
    df = max(df, .94);
  if (modEchoMode)
    df = max(df, .88);
  if (tremoloVerbMode)
    df = max(df, .84);
  if (dynamicMode)
    df = max(df, .82);
  if (duckMode)
    df = max(df, .885);
  if (dualReverbMode)
    df = max(df, .91);
  if (grainMode)
    df = max(df, (typ == 42) ? .94 : .86);
  if (ambienceMode)
    df = max(df, .52);
  if (earlyRefMode)
    df = min(df, .54);
  if (reflectionSpaceMode)
    df = max(df, .70);
  if (infiniteMode)
    df = max(df, .945);
  if (freezeMode)
    df = max(df, .972);
  if (shimmerFamily)
    df = max(df, (crys ? .90 : .955) + .035 * sf);
  if (boomMode)
    df = max(df, .90);
  double b1 = mix(f1, h1, df);
  double b2 = mix(f2, h2, df);
  double b3 = mix(f3, h3, df);
  double b4 = mix(f4, h4, df);
  double b5 = mix(f5, h5, df);
  double b6 = mix(f6, h6, df);
  double b7 = mix(f7, h7, df);
  double b8 = mix(f8, h8, df);
  double b9 = mix(f9, h9, df);
  double b10 = mix(f10, h10, df);
  double b11 = mix(f11, h11, df);
  double b12 = mix(f12, h12, df);
  double b13 = mix(f13, h13, df);
  double b14 = mix(f14, h14, df);
  double b15 = mix(f15, h15, df);
  double b16 = mix(f16, h16, df);
  // Multi-stage Schroeder-style input diffusion before the FDN.
  bu = clamp(buildup, 0, 100) * .01;
  den = clamp(density, 0, 100) * .01;
  es = clamp(earlysend, 0, 100) * .01;
  ind = clamp(inputdiffuse, 0, 100) * .01;
  if (naturalMode)
    ind = max(
        ind,
        (typ == 2)
            ? .34
            : ((typ == 3)
                   ? .40
                   : ((typ == 4)
                          ? .46
                          : ((typ == 5)
                                 ? .38
                                 : ((typ == 6) ? .58
                                               : ((typ == 7) ? .56 : .44))))));
  if (hallMode) {
    ind = max(ind, .60);
    den = max(den, .91);
    bu = max(bu, .34);
    es = min(es, .48);
  }
  if (plateMode)
    ind = max(ind, .88);
  if (plateFamily) {
    ind = max(ind, .94 + .05 * patk);
    bu = max(bu, .62 + .30 * patk);
    den = max(den, (typ == 20) ? .99 : .975);
    es = min(es, (typ == 19) ? .20 : .24);
  }
  if (springMode) {
    ind = max(ind, (typ == 22) ? .80 : .54);
    den = max(den, (typ == 22) ? .90 : .68);
    bu = max(bu, .22 + .30 * sdr + .10 * smulti);
    es = min(es, (typ == 22) ? .26 : .22);
  }
  if (vintageDigitalFamily) {
    ind = max(ind, vdroom ? .68 : .82);
    den = max(den, vdroom ? .89 : .96);
    bu = max(bu, vdroom ? .42 : .60);
    es = min(es, vdroom ? .56 : .34);
  }
  if (nonlinearFamily) {
    ind = max(ind, gatedFamily ? .95
                               : (reverseMode ? .74 : (bloomMode ? .86 : .88)));
    den = max(den, gatedFamily ? .99
                               : (reverseMode ? .91 : (bloomMode ? .98 : .97)));
    bu = max(bu,
             gatedFamily ? .34 : (reverseMode ? .44 : (bloomMode ? .72 : .50)));
    es = min(es,
             gatedFamily ? .24 : (reverseMode ? .20 : (bloomMode ? .10 : .40)));
  }
  if (spectralMode) {
    ind = max(ind, .88);
    den = max(den, .99);
    bu = max(bu, .68);
    es = min(es, .10);
  }
  if (resonMode) {
    ind = max(ind, .72);
    den = max(den, .90);
    bu = max(bu, .42);
    es = min(es, .22);
  }
  if (voidMode) {
    ind = max(ind, .88);
    den = max(den, .98);
    bu = max(bu, .68);
    es = min(es, .07);
  }
  if (warpMode) {
    ind = max(ind, .80);
    den = max(den, .95);
    bu = max(bu, .58);
    es = min(es, .16);
  }
  if (hybridMode) {
    ind = max(ind, .74);
    den = max(den, .92);
    bu = max(bu, .48);
    es = min(es, .24);
  }
  if (mangledMode) {
    ind = max(ind, .86);
    den = max(den, .97);
    bu = max(bu, .64);
    es = min(es, .13);
  }
  if (modulatedMode) {
    ind = max(ind, .86);
    den = max(den, .98);
    bu = max(bu, .64);
    es = min(es, .14);
  }
  if (modEchoMode) {
    ind = max(ind, .70);
    den = max(den, .92);
    bu = max(bu, .44);
    es = min(es, .26);
  }
  if (tremoloVerbMode) {
    ind = max(ind, .70);
    den = max(den, .92);
    bu = max(bu, .44);
    es = min(es, .28);
  }
  if (dynamicMode) {
    ind = max(ind, .60);
    den = max(den, .88);
    bu = max(bu, .34);
    es = min(es, .36);
  }
  if (duckMode) {
    ind = max(ind, .74);
    den = max(den, .95);
    bu = max(bu, .50);
    es = min(es, .18);
  }
  if (dualReverbMode) {
    ind = max(ind, .78);
    den = max(den, .95);
    bu = max(bu, .54);
    es = min(es, .30);
  }
  if (grainMode) {
    ind = max(ind, (typ == 42) ? .84 : .70);
    den = max(den, (typ == 42) ? .97 : .90);
    bu = max(bu, (typ == 42) ? .62 : .46);
    es = min(es, (typ == 42) ? .12 : .22);
  }
  if (ambienceMode) {
    ind = max(ind, .28);
    den = max(den, .50);
    bu = max(bu, .10);
    es = max(es, .68);
  }
  if (earlyRefMode) {
    ind = min(ind, .20);
    den = min(den, .40);
    bu = min(bu, .14);
    es = max(es, .88);
  }
  if (reflectionSpaceMode) {
    ind = max(ind, .42);
    den = max(den, .68);
    bu = max(bu, .22);
    es = max(es, .74);
  }
  if (infiniteMode) {
    ind = max(ind, .84);
    den = max(den, .98);
    bu = max(bu, .66);
    es = min(es, .10);
  }
  if (freezeMode) {
    ind = max(ind, .90);
    den = max(den, .995);
    bu = max(bu, .72);
    es = min(es, .06);
  }
  if (shimmerFamily) {
    ind = max(ind, crys ? .76 : .88);
    den = max(den, crys ? .96 : .99);
    bu = max(bu, crys ? .56 : .70);
    es = min(es, crys ? .18 : .12);
  }
  if (boomMode) {
    ind = max(ind, .76);
    den = max(den, .95);
    bu = max(bu, .52);
    es = min(es, .14);
  }
  double trsp = clamp(transientspace, 0, 100) * .01;
  double transientGain = 1 + trsp * clamp(envH * 7, 0, 1) * .30;
  if (plateFamily)
    transientGain *= .92 + .08 * (1 - patk);
  double rawTinL =
      mix(tin0L, erOutL, clamp(max(bu * .78, es * .82) + ind * .10, 0, 1)) *
      transientGain;
  double rawTinR =
      mix(tin0R, erOutR, clamp(max(bu * .78, es * .82) + ind * .10, 0, 1)) *
      transientGain;
  double apg = .18 + .57 * ind;
  double ad1L = diffL1.read(clamp((.00131 + .0012 * sz) * sr, 1, 15000));
  double ap1L = ad1L - apg * rawTinL;
  diffL1.write(rawTinL + apg * ap1L);
  double ad2L = diffL2.read(clamp((.00217 + .0015 * sz) * sr, 1, 15000));
  double ap2L = ad2L - apg * ap1L;
  diffL2.write(ap1L + apg * ap2L);
  double ad3L = diffL3.read(clamp((.00337 + .0018 * sz) * sr, 1, 15000));
  double ap3L = ad3L - apg * ap2L;
  diffL3.write(ap2L + apg * ap3L);
  double ad4L = diffL4.read(clamp((.00521 + .0022 * sz) * sr, 1, 15000));
  double ap4L = ad4L - apg * ap3L;
  diffL4.write(ap3L + apg * ap4L);
  double ad1R = diffR1.read(clamp((.00147 + .0011 * sz) * sr, 1, 15000));
  double ap1R = ad1R - apg * rawTinR;
  diffR1.write(rawTinR + apg * ap1R);
  double ad2R = diffR2.read(clamp((.00243 + .0014 * sz) * sr, 1, 15000));
  double ap2R = ad2R - apg * ap1R;
  diffR2.write(ap1R + apg * ap2R);
  double ad3R = diffR3.read(clamp((.00371 + .0017 * sz) * sr, 1, 15000));
  double ap3R = ad3R - apg * ap2R;
  diffR3.write(ap2R + apg * ap3R);
  double ad4R = diffR4.read(clamp((.00563 + .0020 * sz) * sr, 1, 15000));
  double ap4R = ad4R - apg * ap3R;
  diffR4.write(ap3R + apg * ap4R);
  double tinL = mix(rawTinL, ap4L, ind);
  double tinR = mix(rawTinR, ap4R, ind);
  igin = 1 - fr;
  if (infiniteMode)
    igin = max(igin, .012);
  // Freeze capture window: feed the current source into the tank while it is
  // sounding. After release, close the input and let the captured tank sustain.
  if (freezeMode) {
    igin = .00010 + .058 * fzact;
  }
  // Distributed injection lowers coloration while filling the 16-line network
  // quickly.
  double sgn = 1 + spring * .18;
  d1.write(b1 + tinL * .178 * igin * sgn);
  d2.write(b2 + tinR * .176 * igin);
  d3.write(b3 + (tinL + tinR) * .118 * den * igin);
  d4.write(b4 + (tinL - tinR) * .112 * den * igin);
  d5.write(b5 + (tinR + tinL * .30) * .104 * den * igin);
  d6.write(b6 + (tinL - tinR * .30) * .100 * den * igin);
  d7.write(b7 + (tinL + tinR) * .090 * den * igin);
  d8.write(b8 + (tinR - tinL) * .086 * den * igin);
  d9.write(b9 + (tinL * .72 + tinR * .28) * .080 * den * igin);
  d10.write(b10 + (tinR * .72 + tinL * .28) * .078 * den * igin);
  d11.write(b11 + (tinL + tinR) * .071 * den * igin);
  d12.write(b12 + (tinL - tinR) * .068 * den * igin);
  d13.write(b13 + (tinR + tinL * .18) * .063 * den * igin);
  d14.write(b14 + (tinL - tinR * .18) * .061 * den * igin);
  d15.write(b15 + (tinL + tinR) * .057 * den * igin);
  d16.write(b16 + (tinR - tinL) * .055 * den * igin);
  double natL = (a1 + a3 + a5 + a7 + a9 + a11 + a13 + a15) * .105 +
                (a2 - a4 + a10 - a12) * .050 + (a6 - a8 + a14 - a16) * .038;
  double natR = (a2 + a4 + a6 + a8 + a10 + a12 + a14 + a16) * .105 +
                (a1 - a3 + a9 - a11) * .050 + (a5 - a7 + a13 - a15) * .038;
  double plateL = (a1 - a2 + a3 - a4 + a5 - a6 + a7 - a8 + a9 - a10 + a11 -
                   a12 + a13 - a14 + a15 - a16) *
                      .105 +
                  (a2 + a5 - a7 + a10 + a13 - a15) * .052;
  double plateR = (a2 - a3 + a4 - a5 + a6 - a7 + a8 - a1 + a10 - a11 + a12 -
                   a13 + a14 - a15 + a16 - a9) *
                      .105 +
                  (a1 + a6 - a8 + a9 + a14 - a16) * .052;
  double springL = (a1 - a2) * .22 + (a3 - a4) * .18 + (a5 - a6) * .15 +
                   (a7 - a8) * .12 + (a9 - a10) * .10 + (a11 - a12) * .085 +
                   (a13 - a14) * .070 + (a15 - a16) * .060;
  double springR = (a2 - a1) * .22 + (a4 - a3) * .18 + (a6 - a5) * .15 +
                   (a8 - a7) * .12 + (a10 - a9) * .10 + (a12 - a11) * .085 +
                   (a14 - a13) * .070 + (a16 - a15) * .060;
  double digiL = (a1 + a4 + a6 - a3 - a8 + a9 - a11 + a14 - a16) * .150 +
                 (a2 - a5 + a10 - a13) * .065;
  double digiR = (a2 + a3 + a7 - a4 - a5 + a10 - a12 + a15 - a9) * .150 +
                 (a1 - a6 + a11 - a14) * .065;
  toL = natL;
  toR = natR;
  if (om == 1) {
    toL = plateL;
    toR = plateR;
  }
  if (om == 2) {
    toL = springL;
    toR = springR;
  }
  if (om == 3) {
    toL = digiL;
    toR = digiR;
  }
  if (dualMode) {
    dbl = clamp(dualblend, 0, 100) * .01;
    hyb = clamp(hybridblend, 0, 100) * .01;
    if (dualReverbMode) {
      // v0.139 DUAL REVERB FUSION
      // Near layer = Early Reflection + natural room body, narrower and more
      // centered. Far layer  = Plate/Digital blend, wider and more diffuse.
      // Both layers always remain present; DUAL BLEND moves the depth focus
      // between them.
      dnearL = erOutL * .30 + natL * .70;
      dnearR = erOutR * .30 + natR * .70;
      daltL = mix(plateL, digiL, .62);
      daltR = mix(plateR, digiR, .62);

      dnm = (dnearL + dnearR) * .5;
      dns = (dnearL - dnearR) * .5 * .82;
      dnearL = dnm + dns;
      dnearR = dnm - dns;

      dfm = (daltL + daltR) * .5;
      dfs = (daltL - daltR) * .5 * 1.20;
      daltL = dfm + dfs;
      daltR = dfm - dfs;

      // Equal-power-like weighting with a small permanent contribution from
      // both layers.
      dga = sqrt(.08 + .92 * (1 - dbl));
      dgb = sqrt(.08 + .92 * dbl);
      dscale = .94 / sqrt(1.08);
      toL = (dnearL * dga + daltL * dgb) * dscale;
      toR = (dnearR * dga + daltR * dgb) * dscale;
    } else {
      altL = mix(plateL, digiL, hyb);
      altR = mix(plateR, digiR, hyb);
      if (hybridMode) {
        hmix = .10 + .88 * hyb;
        toL = mix(natL, altL, hmix);
        toR = mix(natR, altR, hmix);
      } else {
        toL = mix(natL, altL, dbl);
        toR = mix(natR, altR, dbl);
      }
    }
  }
  // Dedicated spring dispersion all-pass chain: high-frequency phase dispersion
  // creates a more spring-like 'boing/drip'.
  if (springMode) {
    sdp = clamp(springdispersion, 0, 100) * .01;
    stn = clamp(springtension, 0, 100) * .01;
    srz = clamp(springresonance, 0, 100) * .01;
    sdr = clamp(springdrip, 0, 100) * .01;
    smulti = (typ == 22) ? 1 : 0;
    spg = clamp(.32 + .42 * sdp + .05 * srz, .28, .82);
    s1L = (.00052 + .00062 * stn + .00018 * sdp) * (1 + .10 * smulti);
    s2L = (.00108 + .00102 * stn + .00025 * sdp) * (1 + .16 * smulti);
    s3L = (.00182 + .00152 * stn + .00036 * sdp) * (1 + .22 * smulti);
    s1R = (.00061 + .00057 * stn + .00020 * sdp) * (1 + .12 * smulti);
    s2R = (.00119 + .00094 * stn + .00028 * sdp) * (1 + .18 * smulti);
    s3R = (.00196 + .00143 * stn + .00039 * sdp) * (1 + .24 * smulti);
    sd1 = sprL1.read(clamp(s1L * sr, 1, 11000));
    so1 = sd1 - spg * toL;
    sprL1.write(toL + spg * so1);
    sd2 = sprL2.read(clamp(s2L * sr, 1, 11000));
    so2 = sd2 - spg * so1;
    sprL2.write(so1 + spg * so2);
    sd3 = sprL3.read(clamp(s3L * sr, 1, 11000));
    so3 = sd3 - spg * so2;
    sprL3.write(so2 + spg * so3);
    rd1 = sprR1.read(clamp(s1R * sr, 1, 11000));
    ro1 = rd1 - spg * toR;
    sprR1.write(toR + spg * ro1);
    rd2 = sprR2.read(clamp(s2R * sr, 1, 11000));
    ro2 = rd2 - spg * ro1;
    sprR2.write(ro1 + spg * ro2);
    rd3 = sprR3.read(clamp(s3R * sr, 1, 11000));
    ro3 = rd3 - spg * ro2;
    sprR3.write(ro2 + spg * ro3);
    bodyL = smulti ? (so1 * .26 + so2 * .34 + so3 * .40) : so3;
    bodyR = smulti ? (ro1 * .26 + ro2 * .34 + ro3 * .40) : ro3;
    dripL = (so1 - so2) * (.11 + .38 * sdr) + (so2 - so3) * (.045 + .13 * srz);
    dripR = (ro1 - ro2) * (.11 + .38 * sdr) + (ro2 - ro3) * (.045 + .13 * srz);
    resSprL = (so1 - so3) * (.045 + .14 * srz);
    resSprR = (ro1 - ro3) * (.045 + .14 * srz);
    sprmix = clamp(.46 + .32 * sdp + .07 * srz + .08 * smulti, .42, .90);
    toL = mix(toL, bodyL, sprmix) + dripL * (.10 + .22 * sdr) + resSprL;
    toR = mix(toR, bodyR, sprmix) + dripR * (.10 + .22 * sdr) + resSprR;
    if (smulti) {
      sm = (toL + toR) * .5;
      ss = (toL - toR) * .5 * 1.12;
      toL = sm + ss;
      toR = sm - ss;
    }
  }
  double lc = clamp(latecross, 0, 100) * .005;
  if (naturalMode)
    lc = max(
        lc,
        (typ == 2)
            ? .035
            : ((typ == 3)
                   ? .045
                   : ((typ == 4)
                          ? .060
                          : ((typ == 5)
                                 ? .040
                                 : ((typ == 6) ? .082
                                               : ((typ == 7) ? .090 : .07))))));
  if (hallMode)
    lc = max(lc, .125);
  if (plateFamily)
    lc = max(lc, .15 + .035 * pmet);
  if (vintageDigitalFamily)
    lc = max(lc, vdroom ? .12 : .17);
  if (nonlinearFamily)
    lc = max(lc,
             gatedFamily ? .10 : (reverseMode ? .12 : (bloomMode ? .18 : .12)));
  if (spectralMode)
    lc = max(lc, .19);
  if (resonMode)
    lc = max(lc, .105);
  if (voidMode)
    lc = max(lc, .20);
  if (warpMode)
    lc = max(lc, .175);
  if (hybridMode)
    lc = max(lc, .135);
  if (mangledMode)
    lc = max(lc, .19);
  if (modulatedMode)
    lc = max(lc, .19);
  if (modEchoMode)
    lc = max(lc, .145);
  if (tremoloVerbMode)
    lc = max(lc, .12);
  if (dynamicMode)
    lc = max(lc, .085);
  if (duckMode)
    lc = max(lc, .145);
  if (dualReverbMode)
    lc = max(lc, .15);
  if (grainMode)
    lc = max(lc, (typ == 42) ? .18 : .14);
  if (ambienceMode)
    lc = max(lc, .035);
  if (earlyRefMode)
    lc = max(lc, .015);
  if (reflectionSpaceMode)
    lc = max(lc, .095);
  if (infiniteMode)
    lc = max(lc, .175);
  if (freezeMode)
    lc = max(lc, .125);
  if (shimmerFamily)
    lc = max(lc, crys ? .17 : .20);
  if (boomMode)
    lc = max(lc, .16);
  double cxL = mix(toL, toR, lc);
  double cxR = mix(toR, toL, lc);
  toL = cxL;
  toR = cxR;
  double tw =
      clamp(tailwidth, 0, 200) * .01 *
      ((typ == 0)
           ? .74
           : ((typ == 1)
                  ? .58
                  : ((typ == 47)
                         ? .96
                         : ((typ == 2)
                                ? .72
                                : ((typ == 3)
                                       ? .82
                                       : ((typ == 4)
                                              ? .92
                                              : ((typ == 5)
                                                     ? .78
                                                     : ((typ == 6)
                                                            ? .98
                                                            : ((typ == 7)
                                                                   ? .94
                                                                   : 1)))))))));
  double tm = (toL + toR) * .5;
  double tsd = (toL - toR) * .5 * tw;
  toL = tm + tsd;
  toR = tm - tsd;
  double eg = clamp(earlylevel, 0, 100) * .01 * ers * (1 - dist * .42);
  double tg = clamp(taillevel, 0, 100) * .01 * tls * (1 + dist * .12);
  wetL = erOutL * eg + toL * tg;
  wetR = erOutR * eg + toR * tg;
  // Family-specific coloration after the dedicated HQ topology stages.
  if (om == 1) {
    pc = tanh(1.18);
    wetL = tanh(wetL * 1.18) / pc;
    wetR = tanh(wetR * 1.18) / pc;
  }
  if (om == 2) {
    sprdrv = 1.38 + .36 * sdr + .18 * srz;
    pc = tanh(sprdrv);
    sprsatL = tanh(wetL * sprdrv) / pc;
    sprsatR = tanh(wetR * sprdrv) / pc;
    satmix = .14 + .12 * sdr + .06 * srz;
    wetL = mix(wetL, sprsatL, satmix);
    wetR = mix(wetR, sprsatR, satmix);
  }
  if (om == 3) {
    if (vintageDigitalFamily) {
      vds = (wetL - wetR) * .5 * (1.08 + .12 * vc + .03 * (1 - vdroom));
      vdm = (wetL + wetR) * .5;
      wetL = vdm + vds;
      wetR = vdm - vds;
      vddrv = 1.035 + .12 * vc + .08 * vdage;
      vdn = tanh(vddrv);
      vdsat = .03 + .08 * vc + .04 * vdage;
      wetL = mix(wetL, tanh(wetL * vddrv) / vdn, vdsat);
      wetR = mix(wetR, tanh(wetR * vddrv) / vdn, vdsat);
      vdmod = .022 + .032 * vr + .006 * vdage;
      wetL *= 1 - vdmod + vdmod * (.5 + .5 * sin(2 * pi * ph3));
      wetR *= 1 - vdmod + vdmod * (.5 + .5 * sin(2 * pi * ph6));
    } else if (shimmerFamily) {
      shm = (wetL + wetR) * .5;
      shs = (wetL - wetR) * .5 * (1.16 + .28 * sdet + .12 * crys);
      wetL = shm + shs;
      wetR = shm - shs;
      shdrv = 1.025 + .07 * crys + .06 * sf;
      shn = tanh(shdrv);
      wetL = mix(wetL, tanh(wetL * shdrv) / shn, .025 + .045 * crys);
      wetR = mix(wetR, tanh(wetR * shdrv) / shn, .025 + .045 * crys);
    } else if (modulatedMode) {
      mmid = (wetL + wetR) * .5;
      mside = (wetL - wetR) * .5 *
              (1.12 + .18 * sin(2 * pi * ph8) + .14 * (slowMotion - .5));
      wetL = mmid + mside;
      wetR = mmid - mside;
      mdrv = 1.015 + .045 * rn + .035 * mot;
      mn = max(tanh(mdrv), .001);
      wetL = mix(wetL, tanh(wetL * mdrv) / mn, .035 + .035 * mot);
      wetR = mix(wetR, tanh(wetR * mdrv) / mn, .035 + .035 * mot);
    } else {
      wetL = wetL * (.90 + .10 * sin(2 * pi * ph3));
      wetR = wetR * (.90 + .10 * sin(2 * pi * ph6));
    }
  }
  // Gate / nonlinear / reverse / bloom / dynamics.
  // v0.124 NONLINEAR SHAPE: SHAPE=0 is now genuinely neutral.
  // Negative values = immediate/front-loaded + faster drop.
  // Positive values = slower rise + longer back-loaded swell.
  double shape = clamp(nonlinearshape, -100, 100) * .01;

  double gth = gatedFamily ? .0048 : (nonlinearMode ? .0055 : .010);
  double gt = (envH > gth) ? 1 : 0;
  double ghld = clamp(gatehold, 0, 1000) * .001 *
                (nonlinearMode ? (.55 + .25 * (1 - abs(shape))) : 1);
  gateHoldH = (gt > 0) ? ghld : max(gateHoldH - 1 / sr, 0);
  double targetGate = (gt > 0 || gateHoldH > 0) ? 1 : 0;

  // NONLINEAR attack/release follows SHAPE.
  // +100 can take roughly 220 ms to open; -100 stays fast and closes much
  // faster.
  double gatk = gatedFamily
                    ? .0012
                    : (nonlinearMode ? (.008 + .220 * max(shape, 0)) : .0025);
  double grs =
      clamp(gaterelease, 10, 2500) * .001 *
      (gatedFamily ? .62
                   : (nonlinearMode
                          ? (.30 + .55 * max(shape, 0) + .20 * (1 - abs(shape)))
                          : 1));
  double gac = 1 - exp(-1 / (gatk * sr));
  double grc = 1 - exp(-1 / (grs * sr));
  gateH = (targetGate > gateH) ? gateH + (targetGate - gateH) * gac
                               : gateH + (targetGate - gateH) * grc;

  double ga = clamp(gate, 0, 100) * .01;
  if (gatedFamily)
    ga = max(ga, 1);
  double gshape = gatedFamily ? pow(clamp(gateH, 0, 1), 2.35) : gateH;
  wetL *= mix(1, gshape, ga);
  wetR *= mix(1, gshape, ga);

  // Shape curve itself is also stronger than before.
  // Negative: fast front-loaded curve. Positive: delayed/back-loaded curve.
  double nlbase = clamp(gateH, 0, 1);
  double nlexp = (shape >= 0) ? (1 + shape * 7) : (1 + (-shape) * 4.5);
  double nlg = (shape >= 0) ? pow(nlbase, nlexp) : (1 - pow(1 - nlbase, nlexp));
  double nlamt = nonlinearMode ? 1 : abs(shape);
  wetL = mix(wetL, wetL * nlg, nlamt);
  wetR = mix(wetR, wetR * nlg, nlamt);

  if (reverseMode) {
    // v0.101 REALTIME REVERSE: do not wait for the input to become silent.
    // The normal Wet texture is captured continuously, then played as
    // overlapping short backward grains.  The reverse envelope starts while the
    // source is present and a short hold keeps transient material alive long
    // enough to complete the crescendo.
    rvtime = clamp(reversetime, 80, 4000) * .001;
    rvwins = min(rvtime * sr, 188000);
    rvcur = clamp(reversecurve, -100, 100) * .01;

    echoL.write(wetL);
    echoR.write(wetR);

    // Input activity starts the reverse swell immediately.  Unlike v0.100,
    // silence is NOT the trigger.  A hold derived from REVERSE TIME also keeps
    // short drums/notes audible.
    rvactive = (envH > .0045) ? 1 : 0;
    rvholdtime = rvtime * .62;
    revHoldH =
        (rvactive > 0) ? max(revHoldH, rvholdtime) : max(revHoldH - 1 / sr, 0);
    rtrig = (rvactive > 0 || revHoldH > 0) ? 1 : 0;
    rvup = 1 - exp(-5.0 / max(rvwins, 32));
    rvrel = max(.12, rvtime * .42);
    rvdn = 1 - exp(-1 / (rvrel * sr));
    revH = (rtrig > revH) ? revH + (rtrig - revH) * rvup
                          : revH + (rtrig - revH) * rvdn;

    // Two 50%-offset reverse grains continuously read the recent Wet buffer
    // backwards. d(delay)/dt > 1 makes the source read-head move backward even
    // while new audio arrives. This gives an audible reverse texture within a
    // few milliseconds instead of after note-off.
    rvgrain = clamp(rvwins * (.16 + .035 * (rvcur + 1)), sr * .045, sr * .32);
    revGrainH = wrap(revGrainH + 1 / max(rvgrain, 32), 0, 1);
    rgp1 = revGrainH;
    rgp2 = wrap(revGrainH + .5, 0, 1);
    rvbase = clamp(sr * .008, 24, 1400);
    rvd1 = clamp(rvbase + 2 * rvgrain * rgp1, 1, 188000);
    rvd2 = clamp(rvbase + 2 * rvgrain * rgp2, 1, 188000);

    rvaL = echoL.read(rvd1);
    rvaR = echoR.read(rvd1);
    rvbL = echoL.read(rvd2);
    rvbR = echoR.read(rvd2);

    rvw1 = sin(pi * rgp1);
    rvw2 = sin(pi * rgp2);
    rvwn = max(rvw1 + rvw2, .001);
    rvsL = (rvaL * rvw1 + rvbR * rvw2) / rvwn;
    rvsR = (rvaR * rvw1 + rvbL * rvw2) / rvwn;

    // Width and curve are intentionally modest so the effect remains smooth
    // instead of metallic.
    rvm = (rvsL + rvsR) * .5;
    rvside = (rvsL - rvsR) * .5 * (1.10 + .10 * abs(rvcur));
    rvsL = rvm + rvside;
    rvsR = rvm - rvside;

    // REVERSE CURVE controls the macro crescendo.  A small activity-dependent
    // start level makes the effect clearly audible during the source itself;
    // the reverse-grain signal remains dominant.
    rvenv = pow(clamp(revH, 0, 1), pow(3, -rvcur));
    rvstart = (rvactive > 0) ? .10 : 0;
    rg = clamp(rvstart + .90 * rvenv, 0, 1);
    wetL = (rvsL * .96 + wetL * .06) * rg;
    wetR = (rvsR * .96 + wetR * .06) * rg;
  }

  if (bloomMode) {
    // v0.122 BLOOM BUILD-UP
    // Start the bloom on the INPUT RISING EDGE, then keep growing for BLOOM
    // TIME even if the source note has already ended.  This avoids the old
    // note-off bloom.
    btrig = (envH > .0045) ? 1 : 0;
    bc = (btrig > revGrainH) ? 1 : 0;
    revGrainH = btrig;
    bt = clamp(bloomtime, 80, 5000) * .001;

    // Reuse the Reverse histories because Reverse and Bloom are mutually
    // exclusive types. New onset resets the macro bloom phase; revHoldH latches
    // the build process on.
    revHoldH = max(revHoldH, bc);
    bdn = 1 / max(bt * sr, 32);
    revH = (bc > 0) ? 0 : min(revH + bdn * revHoldH, 1);

    // BLOOM SHAPE controls how early/late the expansion becomes obvious.
    bsh = clamp(bloomshape, -100, 100) * .01;
    bexp = pow(4, -bsh);
    bg = .035 + .965 * pow(clamp(revH, 0, 1), bexp);

    // Keep the beginning more centered, then widen as the bloom opens.
    rvm = (wetL + wetR) * .5;
    rvside = (wetL - wetR) * .5 * (.68 + .52 * pow(clamp(revH, 0, 1), .72));
    wetL = (rvm + rvside) * bg;
    wetR = (rvm - rvside) * bg;
  }

  boomDirectL = 0;
  boomDirectR = 0;
  if (boomMode) {
    // v0.104 BOOM DIRECT ATTACK FIX
    // The BOOM is fired by the ATTACK of the incoming source, never by
    // note-off/silence. A fast-vs-slow onset detector catches the transient
    // immediately, while hysteresis and a short refractory timer prevent
    // repeated firing during one sustained note. RES TUNE = fundamental, RES
    // AMT = amount, GRAV = pitch drop, TRANS = impact, SUBDEC + DECAY = decay
    // length, DAMP = harmonic damping.
    bfa = 1 - exp(-1 / (.00045 * sr));
    bfr = 1 - exp(-1 / (.018 * sr));
    bsa = 1 - exp(-1 / (.018 * sr));
    bsr = 1 - exp(-1 / (.120 * sr));
    boomDetH = (ins > boomDetH) ? boomDetH + (ins - boomDetH) * bfa
                                : boomDetH + (ins - boomDetH) * bfr;
    boomSlowH = (ins > boomSlowH) ? boomSlowH + (ins - boomSlowH) * bsa
                                  : boomSlowH + (ins - boomSlowH) * bsr;
    bonset = max(boomDetH - boomSlowH * .72, 0);
    bonsetGate = (bonset > .0016) ? 1 : 0;
    bgth = (boomGateH > 0) ? .00075 : .00135;
    bgate = (boomDetH > bgth) ? 1 : 0;
    btrig =
        (((bonsetGate > boomOnsetH) || (bgate > boomGateH)) && boomRefracH <= 0)
            ? 1
            : 0;
    boomOnsetH = bonsetGate;
    boomGateH = bgate;
    boomRefracH = (btrig > 0) ? .090 : max(boomRefracH - 1 / sr, 0);

    bamt = clamp(resonantamount, 0, 100) * .01;
    bimpact = clamp(transientspace, 0, 100) * .01;
    bdrop = clamp(gravity, 0, 100) * .01;
    bbase = clamp(resonanttune * .118, 31, 57);
    bdec = clamp(.068 + clamp(subdecay, 25, 200) * .00036, .078, .142);
    bdropT = .078 + .155 * bdrop;
    if (btrig) {
      boomEnvH = 1;
      boomPitchH = 1;
      boomPhaseH = 0;
      boomHitH = 1;
      boomAmpH = clamp(.30 + boomDetH * 2.7, .30, .58);
    }
    benvc = exp(-1 / (max(bdec, .05) * sr));
    bpitchc = exp(-1 / (max(bdropT, .03) * sr));
    bhitc = exp(-1 / (.105 * sr));
    if (btrig <= 0) {
      boomEnvH *= benvc;
      boomPitchH *= bpitchc;
      boomHitH *= bhitc;
    }

    // Start above the tuned fundamental and fall immediately into it: the
    // audible pitch drop begins at the same moment as the dry attack instead of
    // after the source decays.
    bfreq = bbase * (1 + (.56 + 1.44 * bdrop) * boomPitchH);
    boomPhaseH = wrap(boomPhaseH + bfreq / sr, 0, 1);
    bdamp = clamp(damping, 0, 100) * .01;
    bfund = sin(2 * pi * boomPhaseH);
    bh2 = sin(4 * pi * boomPhaseH) * (.022 * (1 - bdamp));
    bh3 = sin(6 * pi * boomPhaseH) * (.0028 * (1 - bdamp) * (1 - bdamp));
    bsub = (bfund + bh2 + bh3) * boomEnvH * boomAmpH;

    // Immediate source impact path.  v0.102 derived its low body mainly from
    // the WET tail, which could psychoacoustically read as "the boom starts
    // after the note".  v0.103 takes the low-frequency body directly from the
    // current pre-EQ source and gives it a short hit envelope.
    bcf = clamp(bbase * 3.15, 95, 305);
    blpc = 1 - exp(-2 * pi * bcf / sr);
    boomLowL = boomLowL + (fL - boomLowL) * blpc;
    boomLowR = boomLowR + (fR - boomLowR) * blpc;
    bimpL = boomLowL * boomHitH;
    bimpR = boomLowR * boomHitH;
    bimp = (bimpL + bimpR) * .5;

    // Keep a separate filtered copy of the reverb tail so the result still
    // reads as a reverb, not only as a synthesized sine/sub hit.
    btcf = clamp(bbase * 4.15, 135, 430);
    btlpc = 1 - exp(-2 * pi * btcf / sr);
    boomTailL = boomTailL + (wetL - boomTailL) * btlpc;
    boomTailR = boomTailR + (wetR - boomTailR) * btlpc;
    btail = (boomTailL + boomTailR) * .5;

    bsubGain = (.07 + .14 * bamt) * (.76 + .22 * bimpact);
    bbodyGain = .36 + .62 * bamt;
    bhitGain = .07 + .16 * bimpact;
    bmono = tanh((bsub * .20 + bimp * (.18 + .18 * bimpact) + btail * .90) *
                 1.018) /
            max(tanh(1.018), .001);
    // Main BOOM wet body.
    wetL =
        wetL * .48 + boomTailL * bbodyGain + bimp * bhitGain + bmono * bsubGain;
    wetR =
        wetR * .48 + boomTailR * bbodyGain + bimp * bhitGain + bmono * bsubGain;
    // Parallel attack lane: this is deliberately sourced from the CURRENT input
    // + triggered sub, not from the delayed reverb tail. It guarantees an
    // audible BOOM onset with the dry attack.
    bdirDrive = 1.08 + .22 * bimpact;
    bdirNorm = max(tanh(bdirDrive), .001);
    boomDirectL =
        tanh((bsub * .09 + bimp * (.30 + .15 * bimpact)) * bdirDrive) /
        bdirNorm * (.15 + .18 * bamt);
    boomDirectR =
        tanh((bsub * .09 + bimp * (.30 + .15 * bimpact)) * bdirDrive) /
        bdirNorm * (.15 + .18 * bamt);
  }

  double dua = clamp(duck, 0, 100) * .01;
  if (duckMode)
    dua = max(dua, .80);
  if (dynamicMode)
    dua = max(dua, .30);
  double duenv = clamp(envH * (duckMode ? 10.5 : 5.4), 0, 1);
  double ducurve = duckMode ? pow(duenv, .62) : pow(duenv, 1.08);
  double dudepth = duckMode ? .97 : .70;
  double dug = 1 - dua * ducurve * dudepth;
  if (dynamicMode) {
    durestore = 1 + (1 - ducurve) * dua * .11;
    wetL *= dug * durestore;
    wetR *= dug * durestore;
  } else {
    wetL *= dug;
    wetR *= dug;
  }
  // v0.104: keep BOOM onset simultaneous with the source even if any global
  // ducking is active.
  if (boomMode) {
    wetL += boomDirectL;
    wetR += boomDirectR;
  }
  // TremoloVerb.
  if (tremMode) {
    trr = clamp(tremolorate, .05, 12);
    trd = clamp(tremolodepth, 0, 100) * .01;
    tphase = ph1 * trr / max(mr, .01);
    tvL = .5 + .5 * sin(2 * pi * tphase);
    tvR = .5 + .5 * sin(2 * pi * (tphase + .18));
    tmin = 1 - .82 * trd;
    wetL *= tmin + .82 * trd * tvL;
    wetR *= tmin + .82 * trd * tvR;
  }
  // Mod-echo / mangled echo tail.
  if (echoMode) {
    et = clamp(echotime, 1, 1000) * .001 * sr;
    efn = clamp(echofeedback, 0, 95) * .01;
    ef = modEchoMode ? clamp(.10 + .66 * efn, 0, .76) : efn * .95;
    edL = echoL.read(clamp(
        et * (modEchoMode ? (1 + .010 * sin(2 * pi * ph8)) : 1), 1, 190000));
    edR = echoR.read(
        clamp(et * (modEchoMode ? (1.018 + .012 * sin(2 * pi * ph6)) : 1.037),
              1, 190000));
    echoL.write(wetL + edR * ef);
    echoR.write(wetR + edL * ef);
    emix = modEchoMode ? clamp(.24 + .38 * efn, .24, .58) : .28;
    wetL = mix(wetL, edL, emix);
    wetR = mix(wetR, edR, emix);
  }
  // Granular/cloud approximation: four moving short taps with controlled
  // scatter.
  if (grainMode) {
    // v0.136 GRAIN SPACE / SCATTER
    gs = clamp(grainsize, 5, 500) * .001 * sr;
    gden = clamp(graindensity, 0, 100) * .01;
    gsc = clamp(grainscatter, 0, 100) * .01;
    gp = clamp(grainpitch, -24, 24) / 24;
    gcloud = (typ == 42) ? 1 : 0;

    // Cloud biases toward longer grains and higher overlap.
    // Granular keeps shorter, more obvious particles.
    gsize = gs * (gcloud ? (1.22 + .28 * gden) : (.78 + .22 * gden));
    gscatter = gsc * (gcloud ? .58 : 1.12);
    gpofs = 1 + gp * (gcloud ? .16 : .28);

    grainL.write(wetL);
    grainR.write(wetR);

    go1 = grainL.read(clamp(
        gsize * (.48 + .34 * (sin(2 * pi * ph2) * .5 + .5) * gscatter) * gpofs,
        1, 140000));
    go2 = grainL.read(
        clamp(gsize * (.96 + .58 * (sin(2 * pi * ph5) * .5 + .5) * gscatter) /
                  max(gpofs, .30),
              1, 140000));
    go3 = grainR.read(clamp(
        gsize * (.63 + .43 * (sin(2 * pi * ph3) * .5 + .5) * gscatter) * gpofs,
        1, 140000));
    go4 = grainR.read(
        clamp(gsize * (1.20 + .52 * (sin(2 * pi * ph7) * .5 + .5) * gscatter) /
                  max(gpofs, .30),
              1, 140000));

    // Granular emphasizes separated particles and stereo scatter.
    // Cloud uses more cross-channel averaging for a smooth fog-like body.
    gcL = gcloud ? ((go1 + go2 + go3 + go4) * .245)
                 : ((go1 * .48 + go3 * .34 + go2 * .24 - go4 * .08) * .88);
    gcR = gcloud ? ((go1 + go2 + go3 + go4) * .245)
                 : ((go2 * .48 + go4 * .34 + go1 * .24 - go3 * .08) * .88);

    gmix = gcloud ? clamp(.42 + .52 * gden, .42, .92)
                  : clamp(.24 + .60 * gden, .24, .82);
    wetL = mix(wetL, gcL, gmix);
    wetR = mix(wetR, gcR, gmix);

    // Scatter also opens the stereo field; Cloud stays smoother and less jumpy.
    gmid = (wetL + wetR) * .5;
    gside =
        (wetL - wetR) * .5 * (gcloud ? (1.06 + .16 * gsc) : (1.10 + .34 * gsc));
    wetL = gmid + gside;
    wetR = gmid - gside;
  }
  // SAFE Spectral HQ: stronger decorrelated spectral-like smear using existing
  // wet channels. No new delay/history networks are introduced.
  if (spectralMode) {
    // v0.137 SPECTRAL SPACE / BLUR FREEZE
    sb = clamp(spectralblur, 0, 100) * .01;
    sfz = clamp(spectralfreeze, 0, 100) * .01;

    // Blur progressively collapses sharp stereo detail into a broad, stable
    // spectral cloud. Freeze retains more of the center/body than the transient
    // edge.
    sm = (wetL + wetR) * .5;
    ss = (wetL - wetR) * .5;
    spwidth = .92 - .54 * sb + .10 * (1 - sfz);
    smearL = sm + ss * spwidth;
    smearR = sm - ss * spwidth;

    // Gentle nonlinear averaging prevents narrow resonances from turning into
    // piercing peaks.
    spdrv = 1.012 + .065 * sb + .035 * sfz;
    spn = max(tanh(spdrv), .001);
    smearL = tanh(smearL * spdrv) / spn;
    smearR = tanh(smearR * spdrv) / spn;

    // Higher Blur = more dissolved texture.
    // Spectral Freeze increases persistence, but does not fully replace the
    // live tail.
    spmix = clamp(.18 + .68 * sb + .08 * sfz, 0, .90);
    wetL = mix(wetL, smearL, spmix);
    wetR = mix(wetR, smearR, spmix);

    // Very high freeze gently narrows the extreme side component for long-term
    // stability.
    spmid = (wetL + wetR) * .5;
    spside = (wetL - wetR) * .5 * (1 - .18 * sfz);
    wetL = spmid + spside;
    wetR = spmid - spside;
  }

  // v0.138 RESONANT SPACE
  // Reuse the existing resonator delay as a tuned harmonic-space generator.
  // No new Delay/History objects are introduced.
  if (resonMode) {
    rf = clamp(resonanttune, 45, min(1600, sr * .085));
    ra = clamp(resonantamount, 0, 100) * .01;

    // Fundamental + musically related upper partials.
    // Tiny L/R offsets keep the resonant field stereo without obvious detune.
    rd1 = clamp(sr / rf, 12, 2200);
    rd2 = clamp(sr / (rf * 1.5), 12, 2200);
    rd3 = clamp(sr / (rf * 2.0), 12, 2200);
    rd4 = clamp(sr / (rf * 2.5), 12, 2200);

    rr1L = resL.read(rd1);
    rr1R = resR.read(rd1 * 1.0035);
    rr2L = resL.read(rd2 * .9975);
    rr2R = resR.read(rd2 * 1.0060);
    rr3L = resL.read(rd3 * 1.0045);
    rr3R = resR.read(rd3 * .9955);
    rr4L = resL.read(rd4 * .9935);
    rr4R = resR.read(rd4 * 1.0075);

    // Feedback is strongest in the useful mid tuning range.
    // Very low settings are restrained to avoid boom; high settings are
    // restrained to avoid whistle.
    rlo = clamp((180 - rf) / 140, 0, 1);
    rhi = clamp((rf - 420) / 1180, 0, 1);
    rfb = (.20 + .48 * ra) * (1 - .20 * rlo - .18 * rhi);
    rfb = clamp(rfb, .12, .66);

    resL.write(wetL + rr1L * rfb);
    resR.write(wetR + rr1R * rfb);

    // High Tune progressively reduces upper-partial level to prevent piercing
    // resonances.
    rw1 = .42 - .10 * rhi;
    rw2 = .28 - .07 * rhi;
    rw3 = .19 - .07 * rhi;
    rw4 = .11 - .05 * rhi;
    resWetL = rr1L * rw1 + rr2L * rw2 + rr3L * rw3 + rr4L * rw4;
    resWetR = rr1R * rw1 + rr2R * rw2 + rr3R * rw3 + rr4R * rw4;

    // Gentle saturation rounds resonant peaks instead of turning them into a
    // sine-like beep.
    rdrv = 1.025 + .20 * ra;
    rnrm = max(tanh(rdrv), .001);
    resWetL = tanh(resWetL * rdrv) / rnrm;
    resWetR = tanh(resWetR * rdrv) / rnrm;

    // Amount now has a clearer musical range while retaining the underlying
    // reverb.
    rmix = clamp(ra * .66, 0, .66);
    wetL = mix(wetL, resWetL, rmix);
    wetR = mix(wetR, resWetR, rmix);
  }
  // Void/Warp/Hybrid/Mangled safe creative macro controls.
  if (voidMode) {
    vg = .34 + .66 * (1 - clamp(envH * 3.2, 0, 1));
    wetL *= vg;
    wetR *= vg;
  }
  wa = clamp(warpamount, 0, 100) * .01;
  twst = clamp(twist, -100, 100) * .01;
  if (warpMode)
    wa = max(wa, .78);
  if (mangledMode) {
    wa = max(wa, .66);
    twst = (abs(twst) < .05) ? .62 : twst;
  }
  if (hybridMode) {
    wa *= .46;
    twst *= .36;
  }
  if (wa > 0) {
    wm = (wetL + wetR) * .5;
    ws = (wetL - wetR) * .5;
    wetL = mix(wetL, wm + ws * (1 + wa * 1.72), wa * .48);
    wetR = mix(wetR, wm - ws * (1 + wa * 1.72), wa * .48);
  }
  if (abs(twst) > .001) {
    tdrv = 1 + abs(twst) * (mangledMode ? 3.35 : 2.9);
    tn = max(tanh(tdrv), .001);
    wetL = mix(wetL, tanh(wetL * tdrv) / tn, .32 + .38 * abs(twst));
    wetR = mix(wetR, tanh(wetR * tdrv) / tn, .32 + .38 * abs(twst));
  }
  if (hybridMode) {
    hm = (wetL + wetR) * .5;
    hs = (wetL - wetR) * .5 * (1.03 + .22 * hbl);
    wetL = hm + hs;
    wetR = hm - hs;
  }
  if (mangledMode) {
    mm = (wetL + wetR) * .5;
    ms = (wetL - wetR) * .5 * (1.22 + .30 * wa);
    wetL = mix(wetL, mm + ms, .32);
    wetR = mix(wetR, mm - ms, .32);
  }
  // Saturation.
  double sat = clamp(saturation, 0, 100) * .01;
  double drv = 1 + sat * 5.2;
  double nrm = max(tanh(drv), .001);
  wetL = mix(wetL, tanh(wetL * drv) / nrm, sat * .88);
  wetR = mix(wetR, tanh(wetR * drv) / nrm, sat * .88);
  // Main stereo width/spread/rotation.
  double wd = clamp(width, 0, 200) * .01;
  double spr = clamp(stereospread, 0, 100) * .01;
  double mid = (wetL + wetR) * .5;
  double side = (wetL - wetR) * .5 * wd * (.7 + .6 * spr);
  wetL = mid + side;
  wetR = mid - side;
  double rot = clamp(rotation, -100, 100) * .005;
  double tmpL = wetL * (1 - abs(rot)) + wetR * max(rot, 0);
  double tmpR = wetR * (1 - abs(rot)) + wetL * max(-rot, 0);
  wetL = tmpL;
  wetR = tmpR;
  double eqBeforeL = wetL;
  double eqBeforeR = wetR;
  // Post EQ: HP + five true tone bands + LP. This is the WET spectrum shown in
  // the green analyzer trace.
  double plc = clamp(postlowcut, 20, min(3000, sr * .18));
  double phc = clamp(posthighcut, max(plc + 160, 1800), min(22000, sr * .45));
  double pHpc = 1 - exp(-2 * pi * plc / sr);
  double pLpc = 1 - exp(-2 * pi * phc / sr);
  outHpL = outHpL + (wetL - outHpL) * pHpc;
  outHpR = outHpR + (wetR - outHpR) * pHpc;
  double pwL = wetL - outHpL;
  double pwR = wetR - outHpR;
  postCutLpL = postCutLpL + (pwL - postCutLpL) * pLpc;
  postCutLpR = postCutLpR + (pwR - postCutLpR) * pLpc;
  pwL = postCutLpL;
  pwR = postCutLpR;
  double qc1 = 1 - exp(-2 * pi * 160 / sr);
  double qc2 = 1 - exp(-2 * pi * 500 / sr);
  double qc3 = 1 - exp(-2 * pi * 2200 / sr);
  double qc4 = 1 - exp(-2 * pi * 6500 / sr);
  postLP1L = postLP1L + (pwL - postLP1L) * qc1;
  postLP1R = postLP1R + (pwR - postLP1R) * qc1;
  postLP2L = postLP2L + (pwL - postLP2L) * qc2;
  postLP2R = postLP2R + (pwR - postLP2R) * qc2;
  postLP3L = postLP3L + (pwL - postLP3L) * qc3;
  postLP3R = postLP3R + (pwR - postLP3R) * qc3;
  postLP4L = postLP4L + (pwL - postLP4L) * qc4;
  postLP4R = postLP4R + (pwR - postLP4R) * qc4;
  double qb1L = postLP1L;
  double qb1R = postLP1R;
  double qb2L = postLP2L - postLP1L;
  double qb2R = postLP2R - postLP1R;
  double qb3L = postLP3L - postLP2L;
  double qb3R = postLP3R - postLP2R;
  double qb4L = postLP4L - postLP3L;
  double qb4R = postLP4R - postLP3R;
  double qb5L = pwL - postLP4L;
  double qb5R = pwR - postLP4R;
  double qg1 = pow(10, postlow / 20);
  double qg2 = pow(10, postlowmid / 20);
  double qg3 = pow(10, postmid / 20);
  double qg4 = pow(10, posthighmid / 20);
  double qg5 = pow(10, posthigh / 20);
  wetL = qb1L * qg1 + qb2L * qg2 + qb3L * qg3 + qb4L * qg4 + qb5L * qg5;
  wetR = qb1R * qg1 + qb2R * qg2 + qb3R * qg3 + qb4R * qg4 + qb5R * qg5;
  // v0.115 BOOM MIX-AWARE ONSET COMPENSATION
  // BOOM was already triggered on note-on, but at low global MIX the dry source
  // masked it. Compensate the BOOM onset before the external wet-mix
  // multiplier.
  if (boomMode) {
    bmix = clamp(mixamount * .01, 0, 1);
    // Final gain after the top-level MIX becomes approximately sqrt(MIX), not
    // MIX. At MIX=0 the external wet multiplier still guarantees silence.
    bcomp = 1 / sqrt(max(bmix, .04));
    bcomp = min(bcomp, 1.62);
    bsrc = clamp(ins * 48, 0, 1);
    bamt2 = clamp(resonantamount, 0, 100) * .01;
    bimp2 = clamp(transientspace, 0, 100) * .01;
    bdrv2 = 1.20 + .55 * bimp2;
    bonL = tanh((boomDirectL * .38 + boomLowL * .58 * bsrc) * bdrv2) /
           max(tanh(bdrv2), .001);
    bonR = tanh((boomDirectR * .38 + boomLowR * .58 * bsrc) * bdrv2) /
           max(tanh(bdrv2), .001);
    bonGain = (.15 + .18 * bamt2) * bcomp;
    wetL = tanh((wetL + bonL * bonGain) * 1.01) / max(tanh(1.01), .001);
    wetR = tanh((wetR + bonR * bonGain) * 1.01) / max(tanh(1.01), .001);
  }

  // v0.140 TYPE LEVEL MATCH / TRANSITION SAFETY
  // Static perceptual trim only at the final Wet output.
  // Internal feedback networks and reverb character are untouched.
  double tlev = 1;

  // Early / room family
  if (typ == 0)
    tlev = 1.14;
  if (typ == 1)
    tlev = 1.12;
  if (typ == 2)
    tlev = 1.05;
  if (typ == 3)
    tlev = 1.02;
  if (typ == 4)
    tlev = .99;
  if (typ == 5)
    tlev = 1.04;
  if (typ == 6)
    tlev = .98;
  if (typ == 7)
    tlev = .96;

  // Hall family
  if (typ == 8)
    tlev = .97;
  if (typ == 9)
    tlev = .97;
  if (typ == 10)
    tlev = .94;
  if (typ == 11)
    tlev = .93;
  if (typ == 12)
    tlev = .91;
  if (typ == 13)
    tlev = .88;
  if (typ == 14)
    tlev = .88;
  if (typ == 15)
    tlev = .86;
  if (typ == 16)
    tlev = .91;

  // Plate / Spring / Vintage Digital
  if (typ == 17)
    tlev = .94;
  if (typ == 18)
    tlev = .94;
  if (typ == 19)
    tlev = .91;
  if (typ == 20)
    tlev = .94;
  if (typ == 21)
    tlev = .92;
  if (typ == 22)
    tlev = .89;
  if (typ == 23)
    tlev = .91;
  if (typ == 24)
    tlev = .96;

  // Nonlinear special family
  if (typ == 25)
    tlev = .94;
  if (typ == 26)
    tlev = .93;
  if (typ == 27)
    tlev = .92;
  if (typ == 28)
    tlev = .90;
  if (typ == 29)
    tlev = .85;
  if (typ == 30)
    tlev = .83;

  // Modulation / Dynamics / Dual
  if (typ == 31)
    tlev = .90;
  if (typ == 32)
    tlev = .92;
  if (typ == 33)
    tlev = .95;
  if (typ == 34)
    tlev = .97;
  if (typ == 35)
    tlev = .97;
  if (typ == 36)
    tlev = .90;

  // Creative / Sustain
  if (typ == 37)
    tlev = .88;
  if (typ == 38)
    tlev = .87;
  if (typ == 39)
    tlev = .83;
  if (typ == 40)
    tlev = .83;

  // Grain / Spectral / Resonant / Hybrid / Mangled
  if (typ == 41)
    tlev = .91;
  if (typ == 42)
    tlev = .89;
  if (typ == 43)
    tlev = .87;
  if (typ == 44)
    tlev = .85;
  if (typ == 45)
    tlev = .91;
  if (typ == 46)
    tlev = .84;

  // Reflection Space / Boom
  if (typ == 47)
    tlev = 1.00;
  if (typ == 48)
    tlev = .80;

  wetL *= tlev;
  wetR *= tlev;

  // Stereo-linked emergency peak guard.
  // It is transparent below the ceiling and only catches extreme peaks caused
  // by type switching, very high feedback, saturation, or unusually hot input.
  double tpk = max(abs(wetL), abs(wetR));
  double tsafe = (tpk > 1.42) ? (1.42 / max(tpk, .000001)) : 1;
  wetL *= tsafe;
  wetR *= tsafe;

  // Keep the pre-Post-EQ analyzer representative of the matched TYPE level too.
  double out1 = wetL * .78;
  double out2 = wetR * .78;
  double out3 = eqBeforeL * tlev * .78;
  double out4 = eqBeforeR * tlev * .78;
  double out5 = fL * .82;
  double out6 = fR * .82;
  preL.advance();
  preR.advance();
  tailPreL.advance();
  tailPreR.advance();
  erL.advance();
  erR.advance();
  d1.advance();
  d2.advance();
  d3.advance();
  d4.advance();
  d5.advance();
  d6.advance();
  d7.advance();
  d8.advance();
  d9.advance();
  d10.advance();
  d11.advance();
  d12.advance();
  d13.advance();
  d14.advance();
  d15.advance();
  d16.advance();
  diffL1.advance();
  diffL2.advance();
  diffL3.advance();
  diffL4.advance();
  diffR1.advance();
  diffR2.advance();
  diffR3.advance();
  diffR4.advance();
  sprL1.advance();
  sprL2.advance();
  sprL3.advance();
  sprR1.advance();
  sprR2.advance();
  sprR3.advance();
  echoL.advance();
  echoR.advance();
  grainL.advance();
  grainR.advance();
  resL.advance();
  resR.advance();
  beforeLeft = out3;
  beforeRight = out4;
  return {out1, out2};
}
} // namespace knobverb
