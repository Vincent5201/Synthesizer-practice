#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SAMPLE_RATE 44100
#define SYNTH_NODES 8
#define SYNTH_VOICES 2

typedef int32_t q31_t;
#define Q31_MAX 0x7FFFFFFF
#define Q31_MIN 0x80000000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef int8_t q7_t;

typedef struct {
    q31_t *phase_incr;
    q31_t *detune;
    q31_t (*wavegen)(q31_t input, q31_t dt); 
} synth_oscillator_t;

typedef struct {
    q31_t attack, decay, sustain, release;
} synth_envelope_t;

typedef struct {
    q31_t *input;
    int64_t low;
    int64_t band;
    q31_t factor;
    q31_t res;
} synth_filter_t;

typedef struct {
    q31_t *inputs[3];
} synth_mixer_t;

typedef enum {
    SYNTH_NODE_NONE = 0,
    SYNTH_NODE_OSCILLATOR,
    SYNTH_NODE_ENVELOPE,
    SYNTH_NODE_FILTER_LP,
    SYNTH_NODE_FILTER_HP,
    SYNTH_NODE_MIXER,
    SYNTH_NODE_END
} synth_node_type_t;

typedef struct {
    int32_t state;
    q31_t *gain;
    q31_t output;
    synth_node_type_t type;
    uint8_t param1;
    union {
        synth_oscillator_t osc;
        synth_envelope_t env;
        synth_filter_t filter;
        synth_mixer_t mixer;
    };
} synth_node_t;

typedef struct {
    uint8_t note;
    uint8_t gate : 1;
    q31_t phase_incr;
    synth_node_t nodes[SYNTH_NODES];
} synth_voice_t;

#define SYNTH_MS(ms) ((ms * SAMPLE_RATE) / 1000)
#define SYNTH_HZ_TO_PHASE(frequency) (q31_t)((frequency * (double)Q31_MAX) / SAMPLE_RATE)

synth_voice_t synth_voices[SYNTH_VOICES];

/* --- PolyBLEP 核心計算 --- */

static q31_t poly_blep(q31_t phase, q31_t dt) {
    if (dt == 0) return 0;
    // 處理 0 <= phase < dt
    if (phase < dt) {
        int64_t t = ((int64_t)phase << 31) / dt;
        q31_t t_q31 = (q31_t)t;
        q31_t t_sq = (q31_t)(((int64_t)t_q31 * t_q31) >> 31);
        return (q31_t)((int64_t)2 * t_q31 - t_sq - Q31_MAX);
    } 
    // 處理 (1-dt) <= phase < 1
    else if (phase > (Q31_MAX - dt)) {
        int64_t t = ((int64_t)(phase - Q31_MAX) << 31) / dt;
        q31_t t_q31 = (q31_t)t;
        q31_t t_sq = (q31_t)(((int64_t)t_q31 * t_q31) >> 31);
        return (q31_t)(t_sq + (int64_t)2 * t_q31 + Q31_MAX);
    }
    return 0;
}

/* --- 波形產生器 (BLEP 修正版) --- */

q31_t sawtooth_wave(q31_t input, q31_t dt) {
    q31_t naive = (q31_t)(((int64_t)input * 2) - Q31_MAX);
    return naive - poly_blep(input, dt);
}

q31_t square_wave(q31_t input, q31_t dt) {
    q31_t naive = (input < Q31_MAX / 2) ? Q31_MIN : Q31_MAX;
    naive += poly_blep(input, dt);
    q31_t phase2 = (input + 0x40000000) & 0x7FFFFFFF;
    naive -= poly_blep(phase2, dt);
    return naive;
}

q31_t sine_wave(q31_t input, q31_t dt) {
    // 正弦波本來就是帶限的，不需要 BLEP
    static const q7_t sine_lut[129] = {
        0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85, 90, 94, 98, 102, 106, 109,
        112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127, 127, 126, 126, 125, 123, 122, 120,
        117, 115, 112, 109, 106, 102, 98, 94, 90, 85, 81, 76, 71, 65, 60, 54, 49, 43, 37, 31, 25,
        19, 12, 6, 0, -6, -12, -19, -25, -31, -37, -43, -49, -54, -60, -65, -71, -76, -81, -85, -90,
        -94, -98, -102, -106, -109, -112, -115, -117, -120, -122, -123, -125, -126, -126, -127, -127,
        -127, -126, -126, -125, -123, -122, -120, -117, -115, -112, -109, -106, -102, -98, -94, -90,
        -85, -81, -76, -71, -65, -60, -54, -49, -43, -37, -31, -25, -19, -12, -6, 0
    };
    int index = (input >> 24) & 0x7F;
    q31_t factor = 16843009; 
    q31_t res = sine_lut[index] * factor;
    q31_t next = sine_lut[index + 1] * factor;
    res += (q31_t)(((int64_t)(next - res) * ((input >> 16) & 0xFF)) >> 8);
    return res;
}

static inline int64_t sat_q31(int64_t x) {
    if (x > Q31_MAX) return Q31_MAX;
    if (x < -Q31_MAX) return -Q31_MAX;
    return x;
}

static q31_t svf_cutoff(float fc) {
    float f = 2.0f * sinf(M_PI * fc / SAMPLE_RATE);
    if (f > 0.99f) f = 0.99f;
    return (q31_t)(f * Q31_MAX);
}

q31_t synth_process() {
    int64_t main_output = 0;
    for (int vi = 0; vi < SYNTH_VOICES; vi++) {
        synth_voice_t *voice = &synth_voices[vi];
        q31_t outputs[SYNTH_NODES];

        for (int i = 0; i < SYNTH_NODES && voice->nodes[i].type != SYNTH_NODE_NONE; i++) {
            synth_node_t *node = &voice->nodes[i];
            switch (node->type) {
                case SYNTH_NODE_OSCILLATOR: {
                    q31_t dt = (*node->osc.phase_incr) >> 2;
                    if (node->osc.detune) dt += *node->osc.detune;
                    outputs[i] = node->osc.wavegen(node->state & 0x7FFFFFFF, dt);
                    break;
                }
                case SYNTH_NODE_ENVELOPE:
                    outputs[i] = node->state & 0x7FFFFFFF;
                    outputs[i] = (q31_t)(((int64_t)outputs[i] * outputs[i]) >> 31);
                    if (node->env.sustain < 0) outputs[i] = -outputs[i];
                    break;
                case SYNTH_NODE_FILTER_LP:
                    outputs[i] = (q31_t)node->filter.low;
                    break;
                case SYNTH_NODE_MIXER: {
                    int64_t sum = 0;
                    for (int j = 0; j < 3; j++) if (node->mixer.inputs[j]) sum += *node->mixer.inputs[j];
                    outputs[i] = (q31_t)sum;
                    break;
                }
                default: break;
            }
            if (node->gain) outputs[i] = (q31_t)(((int64_t)outputs[i] * (*node->gain)) >> 31);
        }

        for (int i = 0; i < SYNTH_NODES && voice->nodes[i].type != SYNTH_NODE_NONE; i++) {
            synth_node_t *node = &voice->nodes[i];
            node->output = outputs[i];
            if (node->type == SYNTH_NODE_OSCILLATOR) {
                node->state = (node->state + *node->osc.phase_incr + (node->osc.detune ? *node->osc.detune : 0)) & 0x7FFFFFFF;
            } else if (node->type == SYNTH_NODE_ENVELOPE) {
                if (voice->gate) {
                    int32_t mode = node->state & 0x80000000;
                    int32_t val = node->state & 0x7FFFFFFF;
                    if (mode) {
                        val -= node->env.decay;
                        q31_t sus = node->env.sustain < 0 ? -node->env.sustain : node->env.sustain;
                        if (val < sus) val = sus;
                    } else {
                        if ((int64_t)val + node->env.attack > Q31_MAX) { val = Q31_MAX; mode = 0x80000000; }
                        else val += node->env.attack;
                    }
                    node->state = val | mode;
                } else {
                    node->state &= 0x7FFFFFFF;
                    node->state = (node->state < node->env.release) ? 0 : node->state - node->env.release;
                }
            } else if (node->type == SYNTH_NODE_FILTER_LP) {
                q31_t input = *node->filter.input;
                q31_t f = node->filter.factor;
                q31_t q = node->filter.res;
                if (f > (Q31_MAX >> 2)) f = Q31_MAX >> 2;
                if (q > (q31_t)(0.95 * Q31_MAX)) q = (q31_t)(0.95 * Q31_MAX);
                node->filter.low = sat_q31(node->filter.low + (((int64_t)f * node->filter.band) >> 31));
                int64_t high = (int64_t)input - node->filter.low - (((int64_t)q * node->filter.band) >> 31);
                node->filter.band = sat_q31(node->filter.band + (((int64_t)f * high) >> 31));
            }
        }
        main_output += voice->nodes[0].output;
    }
    return (q31_t)(((main_output * (Q31_MAX / SYNTH_VOICES)) >> 31) * 0.7);
}

/* --- 初始化與 MIDI 邏輯 --- */

static const q31_t octave_phases[12] = {
    SYNTH_HZ_TO_PHASE(4186.01), SYNTH_HZ_TO_PHASE(4434.92), SYNTH_HZ_TO_PHASE(4698.63),
    SYNTH_HZ_TO_PHASE(4978.03), SYNTH_HZ_TO_PHASE(5274.04), SYNTH_HZ_TO_PHASE(5587.65),
    SYNTH_HZ_TO_PHASE(5919.91), SYNTH_HZ_TO_PHASE(6271.93), SYNTH_HZ_TO_PHASE(6644.88),
    SYNTH_HZ_TO_PHASE(7040.00), SYNTH_HZ_TO_PHASE(7458.62), SYNTH_HZ_TO_PHASE(7902.13)
};

static q31_t midi_to_phase_incr(uint8_t note) {
    int oct = note / 12, idx = note % 12;
    return octave_phases[idx] >> (8 - oct + 1);
}

void synth_voice_note_on(synth_voice_t *v, uint8_t n) {
    v->note = n; v->gate = 1; v->phase_incr = midi_to_phase_incr(n);
    for (int i = 0; i < SYNTH_NODES; i++) v->nodes[i].state = 0;
}

void synth_voice_note_off(synth_voice_t *v) { v->gate = 0; }

void synth_init_osc_node(synth_node_t *node, q31_t *gain, q31_t *pi, q31_t *dt, q31_t (*wg)(q31_t, q31_t)) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain; node->type = SYNTH_NODE_OSCILLATOR;
    node->osc.phase_incr = pi; node->osc.detune = dt; node->osc.wavegen = wg;
}

void synth_init_envelope_node(synth_node_t *node, q31_t *gain, q31_t a, q31_t d, q31_t s, q31_t r) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain; node->type = SYNTH_NODE_ENVELOPE;
    node->env.attack = a; node->env.decay = d; node->env.sustain = s; node->env.release = r;
}

void synth_init_filter_lp_node(synth_node_t *node, q31_t *input, q31_t f, q31_t q) {
    memset(node, 0, sizeof(synth_node_t));
    node->type = SYNTH_NODE_FILTER_LP;
    node->filter.input = input;
    node->filter.factor = f;
    node->filter.res = q;
    node->filter.low = 0;
    node->filter.band = 0;
}

/* --- Main 與 檔案輸出 --- */

static int write_wav(const char *fn, const int16_t *buf, uint32_t count) {
    FILE *f = fopen(fn, "wb"); if (!f) return 1;
    uint32_t head[] = {0x46464952, count*2+36, 0x45564157, 0x20746d66, 16, 0x00010001, SAMPLE_RATE, SAMPLE_RATE*2, 0x00100002, 0x61746164, count*2};
    fwrite(head, 1, 44, f); fwrite(buf, 2, count, f); fclose(f); return 0;
}

int main() {
    q31_t lfo_inc = SYNTH_HZ_TO_PHASE(5), vib_inc = SYNTH_HZ_TO_PHASE(10);
    
    // Voice 0: Sawtooth with BLEP
    synth_init_envelope_node(&synth_voices[0].nodes[1], NULL, 500<<16, 150<<16, (q31_t)(Q31_MAX*0.8), 150<<16);
    synth_init_osc_node(&synth_voices[0].nodes[2], &vib_inc, &lfo_inc, NULL, sine_wave);
    synth_init_osc_node(&synth_voices[0].nodes[3], &synth_voices[0].nodes[1].output, &synth_voices[0].phase_incr, &synth_voices[0].nodes[2].output, sawtooth_wave);
    synth_init_filter_lp_node(&synth_voices[0].nodes[0], &synth_voices[0].nodes[3].output, svf_cutoff(2000), (q31_t)(0.5 * Q31_MAX));

    // Voice 1: Square with BLEP
    synth_init_envelope_node(&synth_voices[1].nodes[1], NULL, 100<<16, 500<<16, (q31_t)(Q31_MAX*0.6), 15<<16);
    synth_init_osc_node(&synth_voices[1].nodes[2], &synth_voices[1].nodes[1].output, &synth_voices[1].phase_incr, NULL, square_wave);
    synth_init_filter_lp_node(&synth_voices[1].nodes[0], &synth_voices[1].nodes[2].output, svf_cutoff(1000), (q31_t)(0.95 * Q31_MAX));
    
    int16_t *buf = malloc(SAMPLE_RATE * 20); uint32_t sc = 0;
    uint8_t mel[] = {60, 60, 67, 67, 69, 69, 67, 0, 65, 65, 64, 64, 62, 62, 60, 0};
    uint8_t bts[] = {4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2};
    uint32_t dur = 0, idx = 0;

    for (;;) {
        if (dur == 0) {
            dur = SYNTH_MS(2000 / bts[idx]);
            if (mel[idx]) { synth_voice_note_on(&synth_voices[0], mel[idx]); synth_voice_note_on(&synth_voices[1], mel[idx]-24); }
            if (++idx >= sizeof(mel)) break;
        } else if (dur < 500) { synth_voice_note_off(&synth_voices[0]); synth_voice_note_off(&synth_voices[1]); }
        dur--;
        int32_t dither = (rand() & 0xFFFF) - (rand() & 0xFFFF);
        buf[sc++] = (int16_t)((synth_process() + dither) >> 16);
    }
    write_wav("out.wav", buf, sc); free(buf); return 0;
}