#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SAMPLE_RATE 48000
#define SYNTH_NODES 8
#define SYNTH_VOICES 2

typedef int16_t q15_t;
#define Q15_MAX 0x7FFF
#define Q15_MIN 0x8000

typedef q15_t (*synth_wavegen_t)(q15_t phase, q15_t dt);

typedef struct {
    q15_t *phase_incr;
    q15_t *detune;
    synth_wavegen_t wavegen;
} synth_oscillator_t;

typedef struct {
    q15_t attack;
    q15_t decay;
    q15_t sustain;
    q15_t release;
} synth_envelope_t;

typedef struct {
    q15_t *input;
    int32_t accum;
    int32_t factor;
} synth_filter_t;

typedef struct {
    q15_t *inputs[3];
} synth_mixer_t;

typedef enum {
    SYNTH_NODE_NONE = 0,
    SYNTH_NODE_OSCILLATOR,
    SYNTH_NODE_ENVELOPE,
    SYNTH_NODE_FILTER_LP,
    SYNTH_NODE_FILTER_HP,
    SYNTH_NODE_MIXER
} synth_node_type_t;

typedef struct {
    int32_t state;
    q15_t *gain;
    q15_t output;
    synth_node_type_t type;
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
    q15_t phase_incr;
    synth_node_t nodes[SYNTH_NODES];
} synth_voice_t;


synth_voice_t synth_voices[SYNTH_VOICES];

#define BASE_OCTAVE 8
#define SYNTH_HZ_TO_PHASE(frequency) (q15_t)((frequency * (float)Q15_MAX) / SAMPLE_RATE)

static const q15_t octave_phases[12] = {
    SYNTH_HZ_TO_PHASE(4186.01f), SYNTH_HZ_TO_PHASE(4434.92f), SYNTH_HZ_TO_PHASE(4698.63f),
    SYNTH_HZ_TO_PHASE(4978.03f), SYNTH_HZ_TO_PHASE(5274.04f), SYNTH_HZ_TO_PHASE(5587.65f),
    SYNTH_HZ_TO_PHASE(5919.91f), SYNTH_HZ_TO_PHASE(6271.93f), SYNTH_HZ_TO_PHASE(6644.88f),
    SYNTH_HZ_TO_PHASE(7040.00f), SYNTH_HZ_TO_PHASE(7458.62f), SYNTH_HZ_TO_PHASE(7902.13f)
};

static const int8_t sine_lut[128 + 1] = {
    0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85, 90, 94, 98, 102, 106, 109,
    112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127, 127, 126, 126, 125, 123, 122, 120,
    117, 115, 112, 109, 106, 102, 98, 94, 90, 85, 81, 76, 71, 65, 60, 54, 49, 43, 37, 31, 25,
    19, 12, 6, 0, -6, -12, -19, -25, -31, -37, -43, -49, -54, -60, -65, -71, -76, -81, -85, -90,
    -94, -98, -102, -106, -109, -112, -115, -117, -120, -122, -123, -125, -126, -126, -127, -127,
    -127, -126, -126, -125, -123, -122, -120, -117, -115, -112, -109, -106, -102, -98, -94, -90,
    -85, -81, -76, -71, -65, -60, -54, -49, -43, -37, -31, -25, -19, -12, -6, 0
};


static float poly_blep_residual(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

q15_t sawtooth_wave_blep(q15_t phase, q15_t dt_q15) {
    float t = (float)phase / 32768.0f;
    float dt = (float)dt_q15 / 32768.0f;
    float val = 2.0f * t - 1.0f;
    val -= poly_blep_residual(t, dt);
    return (q15_t)(val * 32767.0f);
}

q15_t square_wave_blep(q15_t phase, q15_t dt_q15) {
    float t = (float)phase / 32768.0f;
    float dt = (float)dt_q15 / 32768.0f;
    float val = (t < 0.5f) ? 1.0f : -1.0f;
    val += poly_blep_residual(t, dt);
    float t2 = (t < 0.5f) ? (t + 0.5f) : (t - 0.5f);
    val -= poly_blep_residual(t2, dt);
    return (q15_t)(val * 32767.0f);
}

q15_t sine_wave(q15_t phase, q15_t dt) {
    (void)dt; 
    int index = (phase >> 8) & 0x7F;
    q15_t res = (q15_t)sine_lut[index] * 258;
    q15_t next = (q15_t)sine_lut[index + 1] * 258;
    res += (q15_t)(((int32_t)(next - res) * (phase & 0xFF)) >> 8);
    return res;
}


void synth_init_osc_node(synth_node_t *node, q15_t *gain, q15_t *phase_incr, q15_t *detune, synth_wavegen_t wavegen) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_OSCILLATOR;
    node->osc.phase_incr = phase_incr;
    node->osc.detune = detune;
    node->osc.wavegen = wavegen;
}

void synth_init_envelope_node(synth_node_t *node, q15_t *gain, q15_t attack, q15_t decay, q15_t sustain, q15_t release) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_ENVELOPE;
    node->env.attack = attack;
    node->env.decay = decay;
    node->env.sustain = sustain;
    node->env.release = release;
}

void synth_init_filter_lp_node(synth_node_t *node, q15_t *gain, q15_t *input, q15_t factor) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_FILTER_LP;
    node->filter.input = input;
    node->filter.factor = factor;
}


static q15_t midi_to_phase_incr(uint8_t note) {
    int octave = note / 12;
    int note_index = note % 12;
    q15_t phaseIncr = octave_phases[note_index];
    int shift = (BASE_OCTAVE - octave + 1);
    if (shift > 0) phaseIncr >>= shift;
    else if (shift < 0) phaseIncr <<= (-shift);
    return phaseIncr;
}

void synth_voice_note_on(synth_voice_t *voice, uint8_t note) {
    voice->note = note;
    voice->gate = 1;
    voice->phase_incr = midi_to_phase_incr(note);
    for (int i = 0; i < SYNTH_NODES; i++) voice->nodes[i].state = 0;
}

q15_t synth_process() {
    int32_t main_output = 0;
    for (int vi = 0; vi < SYNTH_VOICES; vi++) {
        synth_voice_t *voice = &synth_voices[vi];
        int32_t outputs[SYNTH_NODES] = {0};

        for (int i = 0; i < SYNTH_NODES && voice->nodes[i].type != SYNTH_NODE_NONE; i++) {
            synth_node_t *node = &voice->nodes[i];
            switch (node->type) {
                case SYNTH_NODE_OSCILLATOR: {
                    q15_t current_phase = (q15_t)(node->state & 0x7FFF);
                    q15_t total_dt = *node->osc.phase_incr;
                    if (node->osc.detune) total_dt += *node->osc.detune;
                    outputs[i] = node->osc.wavegen(current_phase, total_dt);
                } break;
                case SYNTH_NODE_ENVELOPE: {
                    outputs[i] = (node->state & 0x7FFFFF) >> 4;
                    outputs[i] = (outputs[i] * outputs[i]) >> 15;
                    if (node->env.sustain < 0) outputs[i] = -outputs[i];
                } break;
                case SYNTH_NODE_FILTER_LP:
                    outputs[i] = (q15_t)((node->filter.accum * node->filter.factor) >> 15);
                    break;
                case SYNTH_NODE_MIXER: {
                    int32_t sum = 0;
                    for (int j = 0; j < 3; j++) if (node->mixer.inputs[j]) sum += *node->mixer.inputs[j];
                    outputs[i] = sum;
                } break;
                default: break;
            }
            if (node->gain) outputs[i] = (outputs[i] * (*node->gain)) >> 15;
        }

        for (int i = 0; i < SYNTH_NODES && voice->nodes[i].type != SYNTH_NODE_NONE; i++) {
            synth_node_t *node = &voice->nodes[i];
            node->output = (q15_t)outputs[i];
            switch (node->type) {
                case SYNTH_NODE_OSCILLATOR: {
                    q15_t total_dt = *node->osc.phase_incr;
                    if (node->osc.detune) total_dt += *node->osc.detune;
                    node->state = (node->state + total_dt) & 0x7FFF;
                } break;
                case SYNTH_NODE_ENVELOPE: {
                    int mode_bit = node->state & 0x8000;
                    int32_t value = node->state & 0x7FFF;
                    if (voice->gate) {
                        if (mode_bit) {
                            q15_t sustainAbs = abs(node->env.sustain);
                            value -= node->env.decay;
                            if (value < (sustainAbs << 4)) value = sustainAbs << 4;
                        } else {
                            value += node->env.attack;
                            if (value > (Q15_MAX << 4)) { value = Q15_MAX << 4; mode_bit = 0x8000; }
                        }
                        node->state = value | mode_bit;
                    } else {
                        node->state &= 0x7FFF;
                        node->state -= node->env.release;
                        if (node->state < 0) node->state = 0;
                    }
                } break;
                case SYNTH_NODE_FILTER_LP:
                    node->filter.accum += (*node->filter.input - node->output);
                    break;
                default: break;
            }
        }
        main_output += voice->nodes[0].output;
    }
    return (q15_t)((main_output * (Q15_MAX / SYNTH_VOICES)) >> 15);
}


static int write_wav(const char *filename, const int16_t *audio_buffer, uint32_t sample_count) {
    FILE *f = fopen(filename, "wb");
    if (!f) return 1;
    uint32_t fileSize = sample_count * 2 + 36;
    uint32_t sampleRate = SAMPLE_RATE;
    uint32_t byteRate = SAMPLE_RATE * 2;
    uint16_t blockAlign = 2, bitsPerSample = 16, format = 1, channels = 1;
    uint32_t fmtSize = 16, dataSize = sample_count * 2;

    fwrite("RIFF", 1, 4, f); fwrite(&fileSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtSize, 4, 1, f); fwrite(&format, 2, 1, f);
    fwrite(&channels, 2, 1, f); fwrite(&sampleRate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f); fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f);
    fwrite(audio_buffer, 2, sample_count, f);
    fclose(f);
    return 0;
}


int main() {
    q15_t lfo_inc = SYNTH_HZ_TO_PHASE(5.0f);
    q15_t vib_depth = SYNTH_HZ_TO_PHASE(10.0f);

    // Voice 0: Sawtooth with BLEP + LFO Vibrato
    synth_init_envelope_node(&synth_voices[0].nodes[1], NULL, 500, 150, Q15_MAX * 0.8, 150);
    synth_init_osc_node(&synth_voices[0].nodes[2], &vib_depth, &lfo_inc, NULL, sine_wave);
    synth_init_osc_node(&synth_voices[0].nodes[3], &synth_voices[0].nodes[1].output, &synth_voices[0].phase_incr, &synth_voices[0].nodes[2].output, sawtooth_wave_blep);
    synth_init_filter_lp_node(&synth_voices[0].nodes[0], NULL, &synth_voices[0].nodes[3].output, 8000);

    // Voice 1: Square with BLEP
    synth_init_envelope_node(&synth_voices[1].nodes[1], NULL, 100, 500, Q15_MAX * 0.5, 15);
    synth_init_osc_node(&synth_voices[1].nodes[2], &synth_voices[1].nodes[1].output, &synth_voices[1].phase_incr, NULL, square_wave_blep);
    synth_init_filter_lp_node(&synth_voices[1].nodes[0], NULL, &synth_voices[1].nodes[2].output, 4000);

    int16_t *audio_buffer = malloc(SAMPLE_RATE * 30 * sizeof(int16_t));
    uint32_t sample_count = 0, note_duration = 0, note_index = 0;
    const uint8_t melody[] = {60, 60, 67, 67, 69, 69, 67, 0, 65, 65, 64, 64, 62, 62, 60, 0};

    for (int i = 0; i < SAMPLE_RATE * 15; i++) {
        if (note_duration == 0) {
            note_duration = SAMPLE_RATE / 2;
            if (melody[note_index]) {
                synth_voice_note_on(&synth_voices[0], melody[note_index]);
                synth_voice_note_on(&synth_voices[1], melody[note_index] - 24);
            }
            note_index = (note_index + 1) % (sizeof(melody));
        }
        if (note_duration < 500) {
            for(int v=0; v<SYNTH_VOICES; v++) synth_voices[v].gate = 0;
        }
        audio_buffer[sample_count++] = synth_process();
        note_duration--;
    }

    write_wav("out.wav", audio_buffer, sample_count);
    free(audio_buffer);
    printf("Done. Output: out.wav\n");
    return 0;
}