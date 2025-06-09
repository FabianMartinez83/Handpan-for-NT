// HandpanLite.cpp - Disting NT Plugin (v0.2)
// Emulates a handpan with 4 voices and 3 modes per note.

#include <distingnt/api.h>
#include <math.h>
#include <new>
#include <cmath>
#include <cstring>


// Fix for M_PI not being defined on some toolchains
#ifndef M_PI
#define M_PI 3.14159265358979323846f    // Pi constant
#endif

#define NUM_VOICES 4
#define MODES_PER_NOTE 3
#define SAMPLE_RATE NT_globals.sampleRate
#define AUDIO_IN_TRIGGER1 0
#define AUDIO_IN_TRIGGER2 1
#define CV_IN_1 2
#define CV_IN_2 3
#define AUDIO_OUT_BUS_L 13
#define AUDIO_OUT_BUS_R 14

struct Mode {
    float freq;
    float phase;
    float amplitude;
    float decay;
};

struct Voice {
    bool active;
    float age;
    Mode modes[MODES_PER_NOTE];
};

struct HandpanLite : _NT_algorithm {
    Voice voices[NUM_VOICES];
};

static const _NT_parameter parameters[] = {
    { "Decay", 100, 5000, 1000, kNT_unitMs, kNT_scalingNone, nullptr }
};

static const uint8_t pageParams[] = { 0 };
static const _NT_parameterPage pages[] = {
    { "Global", ARRAY_SIZE(pageParams), pageParams }
};
static const _NT_parameterPages parameterPages = { ARRAY_SIZE(pages), pages };

static float cvToFreq(float cv) {
    // 1V/octave, 1 semitone = 83.333mV
    float volts = cv; // CV-Wert in Volt (angenommen 0–10V Bereich)
    float semitones = volts * 12.0f;
    return 440.0f * powf(2.0f, (semitones - 57.0f) / 12.0f); // MIDI 57 = A3 = 440 Hz
}

extern "C" _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications) {
    HandpanLite* self = new(ptrs.sram) HandpanLite;
    self->parameters = parameters;
    self->parameterPages = &parameterPages;
    return self;
}

extern "C" void step(_NT_algorithm* base, float* busFrames, int numFramesBy4) {
    HandpanLite* self = static_cast<HandpanLite*>(base);
    int numFrames = numFramesBy4 * 4;
    float* trig1 = busFrames + AUDIO_IN_TRIGGER1 * numFrames;
    float* trig2 = busFrames + AUDIO_IN_TRIGGER2 * numFrames;
    float* cv1 = busFrames + CV_IN_1 * numFrames;
    float* cv2 = busFrames + CV_IN_2 * numFrames;
    float* outL = busFrames + AUDIO_OUT_BUS_L * numFrames;
    float* outR = busFrames + AUDIO_OUT_BUS_R * numFrames;
    memset(outL, 0, numFrames * sizeof(float));
    memset(outR, 0, numFrames * sizeof(float));

    const float decay = self->v[0] / 1000.0f;

    for (int f = 0; f < numFrames; ++f) {
        if (trig1[f] > 0.5f || trig2[f] > 0.5f) {
            // Suche nächste freie Stimme
            for (int v = 0; v < NUM_VOICES; ++v) {
                if (!self->voices[v].active) {
                    float cv = (trig1[f] > 0.5f) ? cv1[f] : cv2[f];
                    float freqBase = cvToFreq(cv);

                    for (int m = 0; m < MODES_PER_NOTE; ++m) {
                        self->voices[v].modes[m] = {
                            freqBase * (m + 1),
                            0.0f,
                            1.0f,
                            decay
                        };
                    }
                    self->voices[v].active = true;
                    self->voices[v].age = 0.0f;
                    break;
                }
            }
        }
    }

    for (int f = 0; f < numFrames; ++f) {
        float sample = 0.0f;
        for (int v = 0; v < NUM_VOICES; ++v) {
            if (!self->voices[v].active) continue;

            float voiceSample = 0.0f;
            bool allDead = true;
            for (int m = 0; m < MODES_PER_NOTE; ++m) {
                Mode& mode = self->voices[v].modes[m];
                float env = expf(-self->voices[v].age / mode.decay);
                float s = env * sinf(2.0f * M_PI * mode.freq * self->voices[v].age);
                voiceSample += s * 0.3f;
                if (env > 0.001f) allDead = false;
            }

            if (allDead) self->voices[v].active = false;
            sample += voiceSample;
            self->voices[v].age += 1.0f / SAMPLE_RATE;
        }
        outL[f] = sample;
        outR[f] = sample;
    }
}

extern "C" void parameterChanged(_NT_algorithm* self, int p) {}

extern "C" void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(HandpanLite);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}




static const _NT_factory factory = {
    .guid = NT_MULTICHAR('H','D','P','N'),
    .name = "HandpanLite",
    .description = "CV Triggered Modal Handpan",
    .numSpecifications = 0,
    .specifications = NULL,
    .calculateStaticRequirements = NULL,
    .initialise = NULL,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = NULL,
    .step = step,
    .draw = NULL,
    .midiRealtime = NULL,
    .midiMessage = NULL,
    .tags = 0
};

extern "C" {
uintptr_t pluginEntry(_NT_selector selector, uint32_t data)
{
    switch (selector)
    {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return 1;
    case kNT_selector_factoryInfo:
        if (data == 0)
            return (uintptr_t)&factory;
        return 0;
    }
    return 0;
}
}
