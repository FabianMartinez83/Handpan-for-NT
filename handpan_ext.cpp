// Disting NT Plugin - Advanced Modal Percussion Synth
// Author: Fabian Martinez (extended with GPT-4o)
// Features:
// - Multiple instrument types (Handpan, Gong, Tabla, etc.)
// - Excitation signal modeling
// - Frequency & amplitude envelopes per mode
// - Inharmonic control
// - CV input for real-time modulation

#include <distingnt/api.h>
#include <cmath>
#include <cstring>
#include <new>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef EXCITATION_BUFFER_SIZE
#define EXCITATION_BUFFER_SIZE 2048
#endif

#ifndef EXCITATION_NOISETABLE_SIZE
#define EXCITATION_NOISETABLE_SIZE 2048
#endif
#define NUM_VOICES 8
#define MAX_MODES 16

#define SAMPLE_RATE NT_globals.sampleRate


float noiseTable[EXCITATION_NOISETABLE_SIZE];
bool noiseInit = false;

inline float softclip(float x) {
    return tanhf(x); // sanftes Clipping, verhindert harte Übersteuerung
}

//--------------------------------------------------------------
// Structure representing a single resonant mode
//--------------------------------------------------------------
// Struktur ModalResonator mit 6 wählbaren Resonanz-Typen
struct ModalResonator {
    float freq, gain, bandwidth;
    float env, age;
    float y1, y2;
    float a1, a2, r;

    void init(float f, float g, float bw) {
    freq = f;
    gain = g;
    bandwidth = bw;
    bw = fmaxf(bw, 5.0f);  // Mindestbandbreite 5 Hz

    env = 1.0f;
    age = 0.0f;
    y1 = 0.0001f * gain;
    y2 = 0.0001f * gain;



    r = expf(-M_PI * bandwidth / SAMPLE_RATE);
    a1 = -2.0f * r * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
    a2 = r * r;
}

float process(float x, int type = 0) {
    switch (type) {
        case 1: env *= 0.9995f; break; // Typ 1: Exponentielles Decay
        case 2: x += 0.00005f * y1; break; // Typ 2: Leichte Rückkopplung
        case 3: x = 0.5f * (x + gain * x); break; // Typ 3: Oversampled Feed
        case 4: gain *= (0.95f + 0.05f * sinf(age * 3.1415f)); break; // Typ 4: Modulierter Gain
        case 5: bandwidth *= 1.5f; // Typ 5: Breite Bänder
                r = expf(-M_PI * bandwidth / SAMPLE_RATE);
                a1 = -2.0f * r * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
                a2 = r * r;
                break;
            default: break;
        }

        float y = gain * x - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = y;
        age += 1.0f / SAMPLE_RATE;
        return y * env;
    }
};

//--------------------------------------------------------------
// Excitation signal buffer
//--------------------------------------------------------------



struct Excitation {
    float buffer[EXCITATION_BUFFER_SIZE];
    float noiseEnv[EXCITATION_BUFFER_SIZE];
    int pos;
    float mixNoise = 0.0f;
    float next() {
        float value = (pos < EXCITATION_BUFFER_SIZE ? buffer[pos++] : 0.0f);
        return softclip(value) * 0.1f; // reduziert Pegel zusätzlich
    }
    float raw() const {
    int idx = pos < EXCITATION_BUFFER_SIZE ? pos : (EXCITATION_BUFFER_SIZE - 1);
    return buffer[idx];
    }
    void generate(int type, int instrType, float inharmonicity = 0.0f, float noiseAmount = 0.0f, int a = 64, int d = 128, float s = 0.0f, int r = 256) {
        if (!noiseInit) {
            for (int i = 0; i < EXCITATION_NOISETABLE_SIZE; ++i)
                noiseTable[i] = ((rand() % 2000) / 1000.0f) - 1.0f;
            noiseInit = true;
        }

        pos = 0;
        for (int i = 0; i < EXCITATION_BUFFER_SIZE; ++i) buffer[i] = 0.0f;

            switch (type) {
            case 0: for (int i = 0; i < 8; ++i) buffer[i] = 1.0f - i * 0.1f; break;
            case 1: buffer[0] = 1.0f; buffer[1] = 0.6f; buffer[2] = 0.2f; break;
            case 2: for (int i = 0; i < 12; ++i) buffer[i] = 1.0f - (i / 12.0f); break;
            case 3: for (int i = 0; i < 24; ++i) buffer[i] = 0.7f * sinf(i * M_PI / 24.0f); break;
            case 4: buffer[0] = 1.0f; buffer[1] = 0.4f; buffer[2] = 0.0f; break;
        }

        // instrument-typischer Einschwingvorgang
        if (instrType == 3 || instrType == 4) {
            for (int i = 0; i < 16; ++i) buffer[i] += 0.05f * sinf(i * 0.4f);
        }
        mixNoise = noiseAmount;
        // spezieller Noise-Anteil für Hi-Hat (instrType == 11)
        if (instrType == 11 || noiseAmount > 0.0f) mixNoise = noiseAmount;


        // noise anteil reinmischen
        if (mixNoise > 0.0f) {
            for (int i = 0; i < EXCITATION_BUFFER_SIZE; ++i) {
                float env = 0.0f;
                if (i < a)
                env = i / (float)a;
                else if (i < a + d)
                env = 1.0f - ((i - a) / (float)d) * (1.0f - s);
                else if (i < a + d + r)
                env = s * (1.0f - (i - (a + d)) / (float)r);
                else
                env = 0.0f;

                noiseEnv[i] = env;
            }
        }

        // leichte inharmonizität über sample jitter (optional)
        if (inharmonicity > 0.0f) {
            for (int i = 0; i < EXCITATION_BUFFER_SIZE; ++i)
                buffer[i] *= 1.0f + 0.002f * inharmonicity * sinf(i * 0.1f);

        }

        // mind. 3 werte setzen zur sicherheit
        if (buffer[0] == 0.0f && buffer[1] == 0.0f && buffer[2] == 0.0f) {
            buffer[0] = 0.2f;
            buffer[1] = 0.4f;
            buffer[2] = 0.6f;
        }
    }

    

};


//--------------------------------------------------------------
// Structure representing a single voice
//--------------------------------------------------------------
struct Voice {
    bool active;
    float age;
    ModalResonator modes[MAX_MODES];
    Excitation excitation;
};

//--------------------------------------------------------------
// Main algorithm structure
//--------------------------------------------------------------
struct ModalInstrument : _NT_algorithm {
    Voice voices[NUM_VOICES];
    float lastTrigger;
    float lpState;
};

// Ergänzungen für Excitation mit mixNoise und generate(type, instrType)



//--------------------------------------------------------------
// Parameters and enums
//--------------------------------------------------------------
enum {
    kParamTrigger = 0,
    kParamNoteCV,
    kParamDecay,
    kParamBaseFreq,
    kParamInstrumentType,
    kParamExcitationType,
    kParamInharmLevel,
    kParamInharmEnable,
    kParamOutputL,
    kParamOutputModeL,
    kParamOutputR,
    kParamOutputModeR,
    kParamBaseFreqCV,
    kParamDecayCV,
    kParamExcitationCV,
    kParamResonatorType,
    kParamNoiseLevel,
    kParamNoiseAttack,
    kParamNoiseDecay,
    kParamNoiseSustain,
    kParamNoiseRelease

};

static const char* instrumentTypes[] = {
    "Handpan", "Steel Drum", "Bell", "Gong", "Triangle", "Tabla", "Conga", "Tom", "Timpani",
    "Udu", "Slit Drum", "Hi-Hat", "Cowbell", "Frame Drum"
};

static const char* excitationTypes[] = {
    "Finger Soft", "Finger Hard", "Hand", "Soft Mallet", "Hard Mallet"
};

static const char* resonatorTypes[] = {
    "Standard", "Decay", "DC Loop", "Oversample", "Wobble", "Wide BW"
};


static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Trigger", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("Note CV", 1, 2)
    { "Decay", 100, 8000, 1500, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Base Freq", 2000, 22000, 4400, kNT_unitHz, kNT_scaling100, nullptr },
    { "Instrument", 0, 13, 0, kNT_unitEnum, kNT_scalingNone, instrumentTypes },
    { "Excitation", 0, 4, 0, kNT_unitEnum, kNT_scalingNone, excitationTypes },
    { "Inharm Amt", 0, 100, 20, kNT_unitPercent, kNT_scalingNone, nullptr },
    { "Inharm On", 0, 1, 1, kNT_typeBoolean, kNT_scalingNone, nullptr },
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)
    NT_PARAMETER_AUDIO_INPUT("BaseFreq CV", 1, 3)
    NT_PARAMETER_AUDIO_INPUT("Decay CV", 1, 4)
    NT_PARAMETER_AUDIO_INPUT("Excit. CV", 1, 5)
    { "Resonator Type", 0, 5, 0, kNT_unitEnum, kNT_scalingNone, resonatorTypes },
    { "Noise Level", 0, 100, 35, kNT_unitPercent, kNT_scalingNone, nullptr },
    { "Noise A", 1, 512, 64, kNT_unitFrames, kNT_scalingNone, nullptr },
    { "Noise D", 1, 1024, 128, kNT_unitFrames, kNT_scalingNone, nullptr },
    { "Noise S", 0, 100, 30, kNT_unitPercent, kNT_scalingNone, nullptr },
    { "Noise R", 1, 2048, 256, kNT_unitFrames, kNT_scalingNone, nullptr },


};


static const uint8_t page1[] = { kParamTrigger, kParamNoteCV, kParamDecay, kParamBaseFreq };
static const uint8_t page2[] = { kParamInstrumentType, kParamExcitationType, kParamInharmLevel, kParamInharmEnable, kParamNoiseLevel, kParamNoiseAttack, kParamNoiseDecay, kParamNoiseSustain, kParamNoiseRelease };
static const uint8_t page3[] = { kParamOutputL, kParamOutputModeL, kParamOutputR, kParamOutputModeR };
static const uint8_t page4[] = { kParamBaseFreqCV, kParamDecayCV, kParamExcitationCV };
static const uint8_t page5[] = { kParamResonatorType };

// Define the parameter pages
static const _NT_parameterPage pages[] = {
    { "Modal Synth", ARRAY_SIZE(page1), page1 },
    { "Timbre & FX", ARRAY_SIZE(page2), page2 },
    { "Outputs", ARRAY_SIZE(page3), page3 },
    { "CV Inputs", ARRAY_SIZE(page4), page4 },
    { "Resonator", ARRAY_SIZE(page5), page5 }

};

static const _NT_parameterPages parameterPages = { ARRAY_SIZE(pages), pages };

//--------------------------------------------------------------
// Instrument modal definition factory
//--------------------------------------------------------------
struct ModalConfig {
    float ratios[MAX_MODES];
    float gains[MAX_MODES];
    int count;
};

ModalConfig getModalConfig(int type) {
    ModalConfig config;
    switch (type) {
        case 0: // Handpan
            config = { {1.00f,1.95f,2.76f,3.76f,4.83f,5.85f,6.93f,7.96f}, {1,0.8,0.6,0.4,0.3,0.2,0.15,0.1}, 8 }; break;
        case 1: // Steel Drum
            config = { {1.0f,2.1f,3.2f,4.3f,5.4f}, {1,0.7,0.5,0.3,0.2}, 5 }; break;
        case 2: // Bell
            config = { {1.0f,2.7f,4.3f,5.2f,6.8f}, {1,0.6,0.5,0.3,0.2}, 5 }; break;
        case 3: // Gong
            config = { {1.0f,2.01f,2.9f,4.1f,5.3f}, {1,0.6,0.4,0.3,0.2}, 5 }; break;
        case 4: // Triangle
            config = { {1.0f,2.1f,3.5f,5.6f}, {1,0.6,0.4,0.3}, 4 }; break;
        case 5: // Tabla
            config = { {1.0f,1.5f,2.4f,3.5f,4.6f}, {1,0.7,0.5,0.4,0.3}, 5 }; break;
        case 6: // Conga
            config = { {1.0f,1.6f,2.3f,3.1f}, {1,0.6,0.4,0.3}, 4 }; break;
        case 7: // Tom
            config = { {1.0f,1.9f,2.6f,3.8f}, {1,0.5,0.3,0.2}, 4 }; break;
        case 8: // Timpani
            config = { {1.0f,1.5f,2.0f,2.8f,3.6f}, {1,0.8,0.6,0.4,0.2}, 5 }; break;
        case 9: // Udu
            config = { {1.0f,1.6f,2.5f,3.3f}, {1,0.5,0.3,0.2}, 4 }; break;
        case 10: // Slit Drum
            config = { {1.0f,2.0f,3.2f,4.6f}, {1,0.7,0.5,0.3}, 4 }; break;
        case 11: // Hi-Hat
            config = { {1.0f,1.7f,2.9f,4.4f,6.1f}, {1,0.5,0.4,0.3,0.2}, 5 }; break;
        case 12: // Cowbell
            config = { {1.0f,2.1f,3.9f,5.7f}, {1,0.4,0.3,0.2}, 4 }; break;
        case 13: // Frame Drum
            config = { {1.0f,1.4f,2.3f,3.2f}, {1,0.6,0.4,0.2}, 4 }; break;
        default:
            config = { {1,2,3,4,5,6,7,8,9,10,11,12}, {1,0.8,0.7,0.6,0.5,0.4,0.3,0.2,0.15,0.1,0.08,0.06}, 12 }; break;
    }
    return config;
}



//--------------------------------------------------------------
// Algorithm construct function
//--------------------------------------------------------------
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t*) {
    ModalInstrument* self = new(ptrs.sram) ModalInstrument;
    self->parameters = parameters;
    self->parameterPages = &parameterPages;
    self->lastTrigger = 0.0f;
    self->lpState = 0.0f;
    return self;
}

//--------------------------------------------------------------
// Main processing loop
//--------------------------------------------------------------
inline float getCVOrParam(float* cv, int f, float paramValue, float scale = 1.0f, float threshold = 0.001f) {
    return (cv && fabsf(cv[f]) > threshold) ? (cv[f] * scale) : paramValue;
}
extern "C" void step(_NT_algorithm* base, float* busFrames, int numFramesBy4) {
    ModalInstrument* self = static_cast<ModalInstrument*>(base);
    int numFrames = numFramesBy4 * 4;
    int noiseA = self->v[kParamNoiseAttack];
    int noiseD = self->v[kParamNoiseDecay];
    float noiseS = self->v[kParamNoiseSustain] / 100.0f;
    int noiseR = self->v[kParamNoiseRelease];
    // Get input buffers
    float* trig = busFrames + (self->v[kParamTrigger] - 1) * numFrames;
    float* noteCV = busFrames + (self->v[kParamNoteCV] - 1) * numFrames;
    float* outL = busFrames + (self->v[kParamOutputL] - 1) * numFrames;
    float* outR = busFrames + (self->v[kParamOutputR] - 1) * numFrames;
    float* cvFreq = self->v[kParamBaseFreqCV] ? busFrames + (self->v[kParamBaseFreqCV] - 1) * numFrames : nullptr;
    float* cvDecay = self->v[kParamDecayCV] ? busFrames + (self->v[kParamDecayCV] - 1) * numFrames : nullptr;
    float* cvExcit = self->v[kParamExcitationCV] ? busFrames + (self->v[kParamExcitationCV] - 1) * numFrames : nullptr;
   
    // Clear output buffers
    memset(outL, 0, numFrames * sizeof(float));
    memset(outR, 0, numFrames * sizeof(float));

    // Get parameter values
    float baseHzParam = self->v[kParamBaseFreq] / 100.0f; // Base frequency parameter
    float decayParam = fmaxf(self->v[kParamDecay], 100) / 1000.0f; // Decay parameter
    bool inharmOn = self->v[kParamInharmEnable] > 0; // Inharmonicity toggle
    float inharmAmt = self->v[kParamInharmLevel] / 100.0f; // Inharmonicity amount
    int instrType = self->v[kParamInstrumentType]; // Instrument type
    int excTypeParam = self->v[kParamExcitationType]; // Excitation type parameter
    float noiseLevel = self->v[kParamNoiseLevel] / 100.0f;

    ModalConfig config = getModalConfig(instrType); // Get modal configuration for the selected instrument
    float gateState = self->lastTrigger; // Track the previous gate state

    for (int f = 0; f < numFrames; ++f) {
        // Combine Base Freq parameter and CV input
        float noteV = noteCV ? noteCV[f] : 0.0f;
        float noteFactor = powf(2.0f, noteV); // Convert note CV to frequency factor
        float cvBase = (cvFreq && fabsf(cvFreq[f]) > 0.001f) ? cvFreq[f] : 0.0f;
        float baseHz = ((cvBase > 0.0f) ? fmaxf(cvBase, 20.0f) : baseHzParam) * noteFactor;
        // Combine Decay parameter and CV input
        float cvD = (cvDecay && fabsf(cvDecay[f]) > 0.001f) ? cvDecay[f] : 0.0f;
        float decay = (cvD > 0.0f) ? fmaxf(cvD * 1000.0f, 100.0f) / 1000.0f : decayParam;
        // Combine Excitation parameter and CV input
        int excType = excTypeParam;
        if (cvExcit && fabsf(cvExcit[f]) > 0.01f) {
            excType = static_cast<int>(fminf(cvExcit[f] * 4.99f, 4.0f));
        }
        // Gate logic
        float currentGate = trig[f];
        bool gateOn = (currentGate >= 0.5f);

        if (!gateState && gateOn) { // Trigger on rising edge
            int voiceToUse = -1;
            float maxAge = -1.0f;
            for (int v = 0; v < NUM_VOICES; ++v) {
                if (!self->voices[v].active) {
                    voiceToUse = v;
                    break;
                } else if (self->voices[v].age > maxAge) {
                    maxAge = self->voices[v].age;
                    voiceToUse = v;
                }
            }

            Voice& voice = self->voices[voiceToUse];
            voice.excitation.generate(excType,instrType,inharmOn ? inharmAmt : 0.0f,noiseLevel,noiseA,noiseD,noiseS,noiseR);
            

            float dampingFactor = 1.0f;
            if (instrType == 3 || instrType == 4) dampingFactor = 0.7f; // Gong/Triangle damping
            else if (instrType == 8) decay *= 2.5f; // Timpani decay adjustment
            else if (instrType == 13) decay *= 2.0f; // Frame Drum decay adjustment

            // Get configuration for the selected instrument
            float* ratios = config.ratios;
            float* gains = config.gains;
            for (int m = 0; m < config.count; ++m) {
                float freq = baseHz * ratios[m];
                    if (inharmOn) {
                    static const float inharmonicOffset[MAX_MODES] = {-0.004f, +0.006f, -0.002f, +0.007f, -0.005f, +0.003f, -0.001f, +0.002f,+0.001f, -0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    freq *= (1.0f + inharmAmt * inharmonicOffset[m]);
                    }
                freq = fminf(freq, SAMPLE_RATE * 0.35f);
                float gain = gains[m];
                float bw = (1.0f / decay) * (0.4f + 0.6f * m / config.count) * dampingFactor;
                voice.modes[m].init(freq, gain, bw);
            }
            voice.active = true;
            voice.age = 0.0f;
        }

        gateState = gateOn;

        // Process voices
        float sample = 0.0f;
        for (int v = 0; v < NUM_VOICES; ++v) {
            if (!self->voices[v].active) continue;
            float exc = self->voices[v].excitation.next();
            // direktes Noise-Signal hörbar machen

            float sum = 0.0f;
            bool silent = true;
            for (int m = 0; m < config.count; ++m) {
                float s = self->voices[v].modes[m].process(exc, self->v[kParamResonatorType]);

                sum += s;
                if (fabsf(s) > 0.0005f) silent = false;
            }
            float rawExc = self->voices[v].excitation.raw();
            sample += rawExc * 0.2f * noiseLevel; 
            if (silent) self->voices[v].active = false;
            sample += sum;
            self->voices[v].age += 1.0f / SAMPLE_RATE;
        }

        // Low-pass filter
       
        float alpha = expf(-2.0f * M_PI * 3000.0f / SAMPLE_RATE);
        sample = self->lpState + alpha * (sample - self->lpState);
        self->lpState = sample;
   
        // Write to output
        outL[f] = sample * 0.1f;
        outR[f] = sample * 0.1f;
    }

    self->lastTrigger = gateState;
}



extern "C" void parameterChanged(_NT_algorithm*, int) {}
extern "C" void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(ModalInstrument);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('H','P','M','X'),
    .name = "HandpanModalXT",
    .description = "Modal Perc Synth with CV & Envelopes",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = nullptr,
    .midiRealtime = nullptr,
    .midiMessage = nullptr,
    .tags = kNT_tagInstrument
};

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version: return kNT_apiVersionCurrent;
        case kNT_selector_numFactories: return 1;
        case kNT_selector_factoryInfo: if (data == 0) return (uintptr_t)&factory; return 0;
    }
    return 0;
}
