// (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
// Licensed under the terms of the GNU General Public License v3 or later
//
// msfa_ref.cpp -- one note of a DX7 voice rendered by msfa itself (Raph Levien's DX7 engine, Google, Apache License
// 2.0, as Dexed carries it: fetch.sh), so that tools/fm_model.py can be compared with the engine it follows.
//
//   msfa_ref VOICE.bin NOTE VELOCITY HOLD_SAMPLES TOTAL_SAMPLES > out.i32
// VOICE.bin: the 155-byte unpacked DX7 voice. Writes TOTAL_SAMPLES little-endian int32 samples: the sum of the
// carriers, Q24 (msfa's output scale). The note is released after HOLD_SAMPLES (rounded up to a 64-sample block).
//
// What it runs is msfa's Env, PitchEnv, Lfo, FmCore and its tables. Dx7Note::init and Dx7Note::compute (dx7note.cc)
// are copied below without what needs Dexed's tuning and controller code: standard tuning, no pitch bend, no
// modulation wheel, no portamento, all six operators on.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "synth.h"
#include "env.h"
#include "exp2.h"
#include "fm_core.h"
#include "fm_op_kernel.h"
#include "freqlut.h"
#include "lfo.h"
#include "pitchenv.h"
#include "sin.h"

static const int32_t coarsemul[] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
    50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
    65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
    74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
    81503396, 82323963, 83117622};
static const uint8_t velocity_data[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
    205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
    233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
    252, 253, 254};
static const uint8_t exp_scale_data[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
    80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t ampmodsenstab[] = {0, 4342338, 7171437, 16777216};

void dexed_trace(const char *, const char *, ...) {}

static int32_t midinote_to_logfreq(int n) { return 50857777 + n * ((1 << 24) / 12); }

static int32_t osc_freq(int midinote, int mode, int coarse, int fine, int detune) {
    int32_t logfreq;
    if (mode == 0) {
        logfreq = midinote_to_logfreq(midinote);
        double detuneRatio = 0.0209 * exp(-0.396 * (((float)logfreq) / (1 << 24))) / 7;
        logfreq += detuneRatio * logfreq * (detune - 7);
        logfreq += coarsemul[coarse & 31];
        if (fine) logfreq += (int32_t)floor(24204406.323123 * log(1 + 0.01 * fine) + 0.5);
    } else {
        logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
        logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
    }
    return logfreq;
}

static int ScaleVelocity(int velocity, int sensitivity) {
    int clamped_vel = max(0, min(127, velocity));
    int vel_value = velocity_data[clamped_vel >> 1] - 239;
    return ((sensitivity * vel_value + 7) >> 3) << 4;
}

static int ScaleRate(int midinote, int sensitivity) {
    int x = min(31, max(0, midinote / 3 - 7));
    return (sensitivity * x) >> 3;
}

static int ScaleCurve(int group, int depth, int curve) {
    int scale;
    if (curve == 0 || curve == 3) {
        scale = (group * depth * 329) >> 12;
    } else {
        int n = sizeof(exp_scale_data);
        int raw_exp = exp_scale_data[min(group, n - 1)];
        scale = (raw_exp * depth * 329) >> 15;
    }
    if (curve < 2) scale = -scale;
    return scale;
}

static int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth, int left_curve, int right_curve) {
    int offset = midinote - break_pt - 17;
    if (offset >= 0) return ScaleCurve((offset + 1) / 3, right_depth, right_curve);
    return ScaleCurve(-(offset - 1) / 3, left_depth, left_curve);
}

struct Note {
    Env env[6];
    PitchEnv pitchenv;
    FmOpParams params[6];
    int32_t basepitch[6];
    int opMode[6];
    uint32_t ampmodsens[6];
    int32_t fb_buf[2] = {0, 0};
    int fb_shift, algorithm, pitchmoddepth, pitchmodsens, ampmoddepth;

    void init(const uint8_t *patch, int midinote, int velocity) {
        int rates[4], levels[4];
        for (int op = 0; op < 6; op++) {
            int off = op * 21;
            for (int i = 0; i < 4; i++) { rates[i] = patch[off + i]; levels[i] = patch[off + 4 + i]; }
            int outlevel = Env::scaleoutlevel(patch[off + 16]);
            outlevel += ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11],
                                   patch[off + 12]);
            outlevel = min(127, outlevel) << 5;
            outlevel += ScaleVelocity(velocity, patch[off + 15]);
            outlevel = max(0, outlevel);
            env[op].init(rates, levels, outlevel, ScaleRate(midinote, patch[off + 13]));
            opMode[op] = patch[off + 17];
            basepitch[op] = osc_freq(midinote, patch[off + 17], patch[off + 18], patch[off + 19], patch[off + 20]);
            ampmodsens[op] = ampmodsenstab[patch[off + 14] & 3];
            params[op].phase = 0;
            params[op].gain_out = 0;
        }
        for (int i = 0; i < 4; i++) { rates[i] = patch[126 + i]; levels[i] = patch[130 + i]; }
        pitchenv.set(rates, levels);
        algorithm = patch[134];
        int feedback = patch[135];
        fb_shift = feedback != 0 ? 8 - feedback : 16;
        pitchmoddepth = (patch[139] * 165) >> 6;
        pitchmodsens = pitchmodsenstab[patch[143] & 7];
        ampmoddepth = (patch[140] * 165) >> 6;
    }

    void keyup() {
        for (int op = 0; op < 6; op++) env[op].keydown(false);
        pitchenv.keydown(false);
    }

    void compute(FmCore &core, int32_t *buf, int32_t lfo_val, int32_t lfo_delay) {
        uint32_t pmd = pitchmoddepth * lfo_delay;
        int32_t senslfo = pitchmodsens * (lfo_val - (1 << 23));
        int32_t pmod_1 = (((int64_t)pmd) * (int64_t)senslfo) >> 39;
        pmod_1 = abs(pmod_1);
        int32_t pitch_mod = pitchenv.getsample() + (pmod_1 * (senslfo < 0 ? -1 : 1));
        lfo_val = (1 << 24) - lfo_val;
        uint32_t amod_1 = (uint32_t)(((int64_t)ampmoddepth * (int64_t)lfo_delay) >> 8);
        amod_1 = (uint32_t)(((int64_t)amod_1 * (int64_t)lfo_val) >> 24);
        uint32_t amd_mod = amod_1;
        for (int op = 0; op < 6; op++) {
            if (opMode[op]) params[op].freq = Freqlut::lookup(basepitch[op]);
            else params[op].freq = Freqlut::lookup(basepitch[op] + pitch_mod);
            int32_t level = env[op].getsample();
            if (ampmodsens[op] != 0) {
                uint32_t sensamp = (uint32_t)(((uint64_t)amd_mod) * ((uint64_t)ampmodsens[op]) >> 24);
                uint32_t pt = exp(((float)sensamp) / 262144 * 0.07 + 12.2);
                uint32_t ldiff = (uint32_t)(((uint64_t)level) * (((uint64_t)pt << 4)) >> 28);
                level -= ldiff;
            }
            params[op].level_in = level;
        }
        core.render(buf, params, algorithm, fb_buf, fb_shift);
    }
};

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: msfa_ref VOICE.bin NOTE VELOCITY HOLD_SAMPLES TOTAL_SAMPLES > out.i32\n");
        return 1;
    }
    uint8_t patch[156] = {0};
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(patch, 1, 155, f) != 155) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    fclose(f);
    patch[155] = 0x3f;
    int note = atoi(argv[2]), vel = atoi(argv[3]);
    long hold = atol(argv[4]), total = atol(argv[5]);
    note = max(0, min(127, note + patch[144] - 24));

    const double sr = 44100.0;
    Env::init_sr(sr);
    Freqlut::init(sr);
    Exp2::init();
    Sin::init();
    Lfo::init(sr);
    PitchEnv::init(sr);
    FmCore core;
    Lfo lfo;
    lfo.reset(patch + 137);
    Note n;
    n.init(patch, note, vel);
    lfo.keydown();
    std::vector<int32_t> out;
    bool released = false;
    for (long done = 0; done < total; done += N) {
        if (!released && done >= hold) { n.keyup(); released = true; }
        int32_t buf[N];
        memset(buf, 0, sizeof buf);
        int32_t lv = lfo.getsample(), ld = lfo.getdelay();
        n.compute(core, buf, lv, ld);
        for (int i = 0; i < N && done + i < total; i++) out.push_back(buf[i]);
    }
    fwrite(out.data(), sizeof(int32_t), out.size(), stdout);
    return 0;
}
