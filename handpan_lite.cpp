// HandpanLite.cpp - Disting NT Plugin (v0.2)
// Emulates a handpan with 4 voices and 3 modes per note.

#include <distingnt/api.h>
#include <math.h>
#include <new>
#include <cmath>
#include <cstring>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


// Fix for M_PI not being defined on some toolchains
#ifndef M_PI
#define M_PI 3.14159265358979323846f    // Pi constant
#endif

#define NUM_VOICES 4
#define MODES_PER_NOTE 3
#define SAMPLE_RATE NT_globals.sampleRate


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
    float lastTrig1 = 0.0f;
    float lastTrig2 = 0.0f;
};
enum
{
	kParamAudioInputTrigger1,
    kParamAudioInputTrigger2,
    kParamAudioInputCV1,
    kParamAudioInputCV2,
    kParamAudioOutputL,
    kParamOutputmodeL,
    kParamAudioOutputR,
    kParamOutputmodeR,
    kParamDecay,
   


};



static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Trigger 1",1,1)
    NT_PARAMETER_AUDIO_INPUT("Trigger 2",1,2)
    NT_PARAMETER_AUDIO_INPUT("CV 1",1,3)
    NT_PARAMETER_AUDIO_INPUT("CV 2",1,4)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output L",1,13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output R",1,14)
    { .name = "Decay", .min = 100, .max = 5000, .def = 1000, .unit = kNT_unitMs, .scaling = kNT_scalingNone, .enumStrings = NULL },
};


static const uint8_t Page1[] = { kParamDecay};
static const uint8_t Page2[] = { kParamAudioInputTrigger1, kParamAudioInputTrigger2, kParamAudioInputCV1, kParamAudioInputCV2, kParamAudioOutputL, kParamOutputmodeL, kParamAudioOutputR, kParamOutputmodeR };
static const _NT_parameterPage ioPageParams = { "Inputs/Outputs", ARRAY_SIZE(Page2), Page2 };
static const _NT_parameterPage decayPageParams = { "Envelope", ARRAY_SIZE(Page1), Page1 };


static const _NT_parameterPage pages[] = {
    ioPageParams,
    decayPageParams
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages
};


inline float* _NT_getAudioInput(_NT_algorithm* self, int index, float* busFrames, int numFrames) {
    int bus = self->v[index] - 1;  // Adjust for 0-based indexing
    return busFrames + bus * numFrames;
}

inline float* _NT_getAudioOutput(_NT_algorithm* self, int index, float* busFrames, int numFrames) {
    int bus = self->v[index];
    return busFrames + bus * numFrames;
}



static float cvToFreq(float cv) {
    // 1V/octave, 1 semitone = 83.333mV
    float volts = cv; // CV-Wert in Volt (angenommen 0–10V Bereich)
    float semitones = volts * 12.0f;
    return 440.0f * powf(2.0f, (semitones - 57.0f) / 12.0f); // MIDI 57 = A3 = 440 Hz
}
 _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications) {
    HandpanLite* self = new(ptrs.sram) HandpanLite;
    self->parameters = parameters;
    self->parameterPages = &parameterPages;
    self->lastTrig1 = 0.0f;
    self->lastTrig2 = 0.0f;
    for (int i = 0; i < NUM_VOICES; ++i) {
    self->voices[i].active = false;
    self->voices[i].age = 0.0f;
    for (int m = 0; m < MODES_PER_NOTE; ++m) {
        self->voices[i].modes[m].amplitude = 0.0f;
        self->voices[i].modes[m].phase = 0.0f;
        self->voices[i].modes[m].freq = 0.0f;
        self->voices[i].modes[m].decay = 1.0f;
    }
}
    return self;
}

void step(_NT_algorithm* base, float* busFrames, int numFramesBy4) {
    HandpanLite* self = static_cast<HandpanLite*>(base);
    int numFrames = numFramesBy4 * 4;

    float* trig1 = _NT_getAudioInput(self, 0, busFrames, numFrames);
    float* trig2 = _NT_getAudioInput(self, 1, busFrames, numFrames);
    float* cv1 = _NT_getAudioInput(self, 2, busFrames, numFrames);
    float* cv2 = _NT_getAudioInput(self, 3, busFrames, numFrames);
    float* outL = _NT_getAudioOutput(self, 4, busFrames, numFrames);
    float* outR = _NT_getAudioOutput(self, 5, busFrames, numFrames);
    memset(outL, 0, numFrames * sizeof(float));
    memset(outR, 0, numFrames * sizeof(float));

    const float decay = self->v[6] / 1000.0f;
    for (int f = 0; f < numFrames; ++f) {
        bool trig1Edge = (self->lastTrig1 <= 0.5f && trig1[f] > 0.5f);
        bool trig2Edge = (self->lastTrig2 <= 0.5f && trig2[f] > 0.5f);
        self->lastTrig1 = trig1[f];
        self->lastTrig2 = trig2[f];

        if (trig1Edge || trig2Edge) {
            for (int v = 0; v < NUM_VOICES; ++v) {
                if (!self->voices[v].active) {
                    float cv = (trig1[f] > 0.5f) ? cv1[f] : cv2[f];
                    float freqBase = cvToFreq(cv);

                    for (int m = 0; m < MODES_PER_NOTE; ++m) {
                        self->voices[v].modes[m].freq = freqBase * (m + 1);
                        self->voices[v].modes[m].phase = 0.0f;
                        self->voices[v].modes[m].amplitude = 1.0f;
                        self->voices[v].modes[m].decay = decay;
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
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = NULL,
    .midiRealtime = NULL,
    .midiMessage = NULL,
    .tags = 0,
    .customUi = nullptr,
    .setupUi = nullptr
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
