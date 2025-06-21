// Disting NT Plugin - Handpan Modal Synthesizer 
//Made by Fabian Martinez

#include <distingnt/api.h>
#include <cmath>
#include <cstring>
#include <new>

// Fix for M_PI not being defined on some toolchains
#ifndef M_PI
#define M_PI 3.14159265358979323846f    // Pi constant
#endif


#define NUM_VOICES 4
#define MODES_PER_VOICE 8
#define SAMPLE_RATE NT_globals.sampleRate

struct Resonator {
    float freq;
    float gain;
    float bandwidth;
    float y1, y2; // history

    void init(float f, float g, float bw) {
        freq = f;
        gain = g;
        bandwidth = bw;
        y1 = y2 = 0.0f;
    }

    float process(float x) {
        float r = expf(-M_PI * bandwidth / SAMPLE_RATE);
        float a1 = -2.0f * r * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
        float a2 = r * r;
        float y = gain * x - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct Voice {
    bool active = false;
    float age = 0.0f;
    Resonator modes[MODES_PER_VOICE];
};

struct HandpanModal : _NT_algorithm {
    Voice voices[NUM_VOICES];
    float lastTrigger = 0.0f;
};

enum {
    kParamTrigger = 0,
    kParamNoteCV,
    kParamDecay,
    kParamOutputL,
    kParamOutputModeL,
    kParamOutputR,
    kParamOutputModeR
};



static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Trigger", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("Note CV", 1, 2) 
    { "Decay", 100, 5000, 1500, kNT_unitMs, kNT_scalingNone, nullptr },
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)
  

};

static const uint8_t mainPageParams[] = { kParamTrigger, kParamDecay, kParamOutputL, kParamOutputModeL, kParamOutputR, kParamOutputModeR };
static const _NT_parameterPage mainPage = { "Modal Handpan", ARRAY_SIZE(mainPageParams), mainPageParams };
static const _NT_parameterPages parameterPages = { 1, &mainPage };

const float noteTable[] = {
    146.8f, // D3
    174.6f, // F3
    196.0f, // G3
    220.0f, // A3
    261.6f, // C4
    293.7f, // D4
    329.6f, // E4
    349.2f, // F4
};
const int numNotes = sizeof(noteTable) / sizeof(noteTable[0]);


_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t*) {
    HandpanModal* self = new(ptrs.sram) HandpanModal;
    self->parameters = parameters;
    self->parameterPages = &parameterPages;
    return self;
};

static float triggerThreshold = 0.5f;

extern "C" void step(_NT_algorithm* base, float* busFrames, int numFramesBy4) {
    HandpanModal* self = static_cast<HandpanModal*>(base);
    int numFrames = numFramesBy4 * 4;

    float* noteCV = busFrames + (self->v[kParamNoteCV] - 1) * numFrames;
    float* inTrig = busFrames + (self->v[kParamTrigger] - 1) * numFrames;
    float* outL = busFrames + (self->v[kParamOutputL] - 1) * numFrames;
    float* outR = busFrames + (self->v[kParamOutputR] - 1) * numFrames;
    memset(outL, 0, numFrames * sizeof(float));
    memset(outR, 0, numFrames * sizeof(float));

    float decaySec = fmaxf(self->v[kParamDecay], 100) / 1000.0f;

    for (int f = 0; f < numFrames; ++f) {
        bool trigEdge = (self->lastTrigger <= triggerThreshold && inTrig[f] > triggerThreshold);
        self->lastTrigger = inTrig[f];

        if (trigEdge) {
            float volts = noteCV[f];
            int index = static_cast<int>(floorf(volts));
            index = index < 0 ? 0 : (index >= numNotes ? numNotes - 1 : index);
            float baseFreq = noteTable[index];

            for (int v = 0; v < NUM_VOICES; ++v) {
                if (!self->voices[v].active) {
                    for (int m = 0; m < MODES_PER_VOICE; ++m) {
                        float ratio = (float)(m + 1);
                        float freq = baseFreq * ratio;
                        float gain = 1.0f / ratio;
                        float bw = 1.0f / decaySec;

                        self->voices[v].modes[m].init(freq, gain, bw);
                    }
                    self->voices[v].active = true;
                    self->voices[v].age = 0.0f;
                    break;
                }
            }
        }

        float sample = 0.0f;
        for (int v = 0; v < NUM_VOICES; ++v) {
            if (!self->voices[v].active) continue;
            float impulse = (self->voices[v].age < 1.0f / SAMPLE_RATE) ? 1.0f : 0.0f;
            float voiceSample = 0.0f;
            bool allDead = true;

            for (int m = 0; m < MODES_PER_VOICE; ++m) {
                float s = self->voices[v].modes[m].process(impulse);
                voiceSample += s;
                if (fabsf(s) > 0.0005f) allDead = false;
            }

            if (allDead) self->voices[v].active = false;
            sample += voiceSample;
            self->voices[v].age += 1.0f / SAMPLE_RATE;
        }

        outL[f] = sample * 0.02f;
        outR[f] = sample * 0.02f;
    }
}

extern "C" void parameterChanged(_NT_algorithm*, int) {}
extern "C" void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(HandpanModal);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}



static const _NT_factory factory = {
    .guid = NT_MULTICHAR('H','A','N','D'),
    .name = "Handpan",
    .description = "Handpan Synthesizer",
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
