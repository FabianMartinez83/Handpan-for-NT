// Disting NT Plugin - Advanced Modal Percussion Synth (No Inharmonicity)
// Author: Fabian Martinez (with the help from Windsurf,ChatGPT-4o, ChatGPT-4.1 andGitHub Copilot)
// copyright: (C) 2025 Fabian Martinez
//
// https://github.com/FabianMartinez    
// This code is licensed under the MIT License.

/*

This plugin is inspired by handpans that I use at home and lead me to think about what if I create a plugin that emulates that sound (and many others)? 


It is also heavyly inspired by the work of Eyal Alon MSc by Research University of York Electronics April 2015, Analysis and Synthesis of the Handpan Sound
Link : https://pure.york.ac.uk/portal/en/publications/analysis-and-resynthesis-of-the-handpan-sound
E-Theses Link for the thesis pdf: https://etheses.whiterose.ac.uk/id/eprint/12260/1/EyalMSc.pdf

A lot of informations I found also on Sound on Sound website:

 https://www.soundonsound.com/techniques/synthesizing-percussion
 https://www.soundonsound.com/techniques/practical-percussion-synthesis-timpani






SPECIAL THANKS TO THE AI Assistants mentioned above for their contributions to this project. 
It would not have been possible to make it so fast and efficiently without the help of the AI assistants mentioned above
They helped me a lot with the code, especially with the ModalResonator class and the excitation buffer generation.
They also helped me with the noise generation and the completion of comments at the end of the code lines translating it to English
They helped creating the lists of modes and their frequencies.
They also helped creating different types of resonators and their processing methods.
They helped a lot with the checking of the code and the debugging, also they helped correcting my logic errors and bug fixing and making the code sleeker and more efficient saving CPU and RAM on the module.

SPECIAL THANKS ALSO TO:
--> ANDREW OSTLER From Expert Sleepers Ltd for: 
- his amazing work on the Disting NT API and for creating such a powerful platform for audio processing.
- his help and support, his patience with my questions and doubts, and for his guidance through the developement of this plugin. 

--> All testers and thir feedback that was very useful to improve the plugin and make it better, more stable and more efficient.


This code is a result of collaboration between human and AI, showcasing the power of modern AI tools in software development.



*/





/*
============
MIT License
============

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/





#include <distingnt/api.h>
#include <cmath>
#include <cstring>
#include <new>
#include <cstdio>

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

// NOISE
static uint32_t noiseSeed = 1;
inline float fastWhiteNoise() {
    noiseSeed = 1664525 * noiseSeed + 1013904223;
    return ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
}

// Utility function to get CV or parameter value
inline float getCVOrParam(float* cv, int f, float paramValue, float scale = 1.0f, float threshold = 0.001f) {
    return (cv && fabsf(cv[f]) > threshold) ? (cv[f] * scale) : paramValue;
}

// Noise table for excitation (not used for per-sample noise)
float noiseTable[EXCITATION_NOISETABLE_SIZE];
bool noiseInit = false;

// Soft clipping function to avoid harsh digital clipping
inline float softclip(float x) {
    return tanhf(x);
}

//--------------------------------------------------------------
// ModalResonator: represents a single resonant mode
//--------------------------------------------------------------
struct ModalResonator {
    float freq = 0.0f;          // Resonance frequency (Hz)
    float gain = 1.0f;          // Resonator gain
    float bandwidth = 0.1f;     // Bandwidth (Hz)
    float env = 1.0f;           // Envelope (for exponential decay)
    float age = 0.0f;           // Age in seconds since trigger
    float y1 = 0.0f, y2 = 0.0f; // Previous outputs (for difference equation)
    float a1 = 0.0f, a2 = 0.0f; // Filter coefficients
    float r = 0.0f;             // Pole radius (for coefficient calculation)
    

    // Initialize the resonator (call on trigger)
    void init(float f, float g, float bw, int type = 0) {
        gain = g;
        if (type == 3) bw *= 1.5f; // For "damped" type, increase bandwidth
        bandwidth = fmaxf(bw, 0.05f);
        env = 1.0f;
        age = 0.0f;
        // Randomize filter state to avoid phase artifacts
        y1 = ((rand() % 2000) / 1000.0f - 1.0f) * 0.001f;
        y2 = ((rand() % 2000) / 1000.0f - 1.0f) * 0.001f;
        freq = f;
        // Calculate filter coefficients
        r = expf(-M_PI * bandwidth / SAMPLE_RATE);
        a1 = -2.0f * r * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
        a2 = r * r;
    }

    // Process one sample for this mode
    float process(float x,int type = 0) {
       
        
        // Resonator type shapings
        switch (type) {
            case 0: break; // Standard
            case 1: env *= 0.9985f; break; // Fast Decay
            case 2: if (x > 1.0f) x = 1.0f; if (x < -1.0f) x = -1.0f; break; // Soft Clip
            case 3: gain *= (0.999f + 0.001f * env); break; // Dynamic Gain
            case 4: x *= env; break; // Envelope Damping
            case 5: gain *= (1.0f - 0.00002f * age); break; // Age Damping
            case 6: if (x > 0) x *= 1.01f; else x *= 0.99f; break; // Gentle Asymmetry
            case 7: gain *= (0.995f + 0.005f * env); break; // Env Gain
            case 8: if (x > 0.8f) x = 0.8f + 0.1f * (x - 0.8f); if (x < -0.8f) x = -0.8f + 0.1f * (x + 0.8f); break; // Limiter
            case 9: x = x - 0.01f * y1; break; // Highpass
            case 10: x += 0.0001f * (x - y1); break; // Bright
            case 11: if (x > env) x = env + 0.1f * (x - env); if (x < -env) x = -env + 0.1f * (x + env); break; // Env Clip
            case 12: y1 *= 0.9995f; y2 *= 0.9995f; break; // Out Damp
            case 13: x = -x; break; // Phase Flip
            case 14: x += 0.00005f * y1; break; // Even Harm
            case 15: if (y1 > 1.0f) y1 = 1.0f; if (y1 < -1.0f) y1 = -1.0f; break; // Out Lim
            case 16: x += 0.00005f * y2; break; // Odd Harm
            case 17: if (x > 0) x *= (1.0f + 0.005f * env); else x *= (1.0f - 0.005f * env); break; // Env Asym
            case 18: y1 -= 0.0001f * y2; break; // Out HP
            case 19: env *= (0.9998f - 0.0001f * env); break; // Dyn Decay
            default: break;
        }
        float y = gain * x - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = y;
        age += 1.0f / SAMPLE_RATE;
        return y * env;
    }
};


// Excitation: buffer for the initial impulse (not for continuous noise)

struct Excitation {
    float buffer[EXCITATION_BUFFER_SIZE];
    int pos;
    float mixNoise = 0.0f;

    // Get next sample from the excitation buffer
    float next() {
        float value = (pos < EXCITATION_BUFFER_SIZE ? buffer[pos++] : 0.0f);
        return softclip(value) * 0.1f; // Softclip and attenuate
    }

    // Generate the excitation buffer (impulse shape)
    void generate(int type, int instrType, float noiseAmount = 0.0f, int a = 64, int d = 128, float s = 0.0f, int r = 256) {
        if (!noiseInit) {
            for (int i = 0; i < EXCITATION_NOISETABLE_SIZE; ++i)
                noiseTable[i] = ((rand() % 2000) / 1000.0f) - 1.0f;
            noiseInit = true;
        }
        // Reset position 
        pos = 0;

        // Clear the buffer
        for (int i = 0; i < EXCITATION_BUFFER_SIZE; ++i) buffer[i] = 0.0f;


// Different excitation shapes
        switch (type) {
// Hard finger: loud, longer impulse
             case 0: for (int i = 0; i < 32; ++i) buffer[i] = 0.7f * expf(-0.09f * i);
                break;
// Soft finger: short, sharp impulse
            case 1: buffer[0] = 1.0f; buffer[1] = 0.5f;
                break;
// Hand: wider, round smashy impulse               
            case 2: for (int i = 0; i < 48; ++i) buffer[i] = 0.6f * expf(-0.06f * i);
                break;
// Hard mallet: Loud, slowly rising                
            case 3: for (int i = 0; i < 64; ++i) buffer[i] = 0.5f * (1.0f * expf(-0.04f * i));
                break;
// Soft mallet: very short, smooth
            case 4: buffer[0] = 1.0f; buffer[1] = -0.5f; buffer[2] = 0.2f;
                break;
// Handpan-like: longer, smoother impulse
            case 5: for (int i = 0; i < 8; ++i) buffer[i] = 1.0f - i * 0.1f;
                break;
// Steel drum-like: sharp, clear attack
            case 6: buffer[0] = 1.0f; buffer[1] = 0.6f; buffer[2] = 0.2f;
               break;
// Bell-like: longer, smoother decay
            case 7: for (int i = 0; i < 12; ++i) buffer[i] = 1.0f - (i / 2.0f);
               break;
// Chime-like: bright, ringing
            case 8: for (int i = 0; i < 4; ++i) buffer[i] = 0.5f - i *0.2f;
                break;
// Custom: user-defined shape
            case 9: buffer[0] = 1.0f; buffer[1] = 0.4f; buffer[2] = 0.0f;
                break;
// Muted slap
            case 10:  buffer[0] = 0.7f; buffer[1] = -0.3f;
                break;
// Brush
            case 11:  for (int i = 0; i < 4; ++i) buffer[i] = 0.03f * i - 0.4f;
                break;
// Double tap
            case 12: buffer[0] = 1.0f; buffer[8] = 0.7f;
                break;  
// Reverse                
            case 13: for (int i = 0; i < 16; ++i) buffer[i] =0.02f * i - 0.6f;
                break;  
// Noise burst, randomized
            case 14: for (int i = 0; i < 24; ++i) buffer[i] = fastWhiteNoise() * expf(-0.2f * i);
                break;
// Triangle pulse
            case 15:   buffer[0] = 0.8f; buffer[1] = 0.4f;
                break;  
// Sine burst
            case 16:   buffer[0] = 0.2f; buffer[1] = 0.6f;
                break;    
// Fallback: single impulse
            default:  buffer[0] = 1.0f; break;
        

            
        }
        

        // Add a little "strike" for some instruments
        if (instrType == 3 || instrType == 4) {
            for (int i = 0; i < 16; ++i) buffer[i] += 0.05f * sinf(i * 0.4f);
        }

        // Excitation smoothing (simple lowpass)
        float prev = 0.0f;
        for (int i = 0; i < EXCITATION_BUFFER_SIZE; ++i) {
            buffer[i] = 0.7f * buffer[i] + 0.3f * prev;
            prev = buffer[i];
        }

        // Ensure at least 3 nonzero values
        if (fabsf(buffer[0]) < 0.001f && fabsf(buffer[1]) < 0.001f && fabsf(buffer[2]) < 0.001f) {
            buffer[0] = 0.1f;
            buffer[1] = 0.2f;
            buffer[2] = 0.3f;
        }
    }
};

// AR envelope for excitation
struct ExcitationAR {
    int stage = 0;            // 0=idle, 1=attack, 2=release
    int pos = 0;              // Sample position in current stage
    int attackSamples = 8;    // Default: 8 Samples (ca. 0.17 ms @48kHz)
    int releaseSamples = 32;  // Default: 32 Samples (ca. 0.67 ms @48kHz)
    float env = 0.0f;         // Current envelope value



// Trigger the envelope with Attack and Release times
    void trigger(int a, int r) {  
        stage = 1;
        pos = 0;
        attackSamples = a;
        releaseSamples = r;
        env = 0.0f;
    }
// Reset the envelope to idle state
    float next() {
        if (stage == 1) { // Attack
            env = (attackSamples > 0) ? (pos / (float)attackSamples) : 1.0f;
            pos++;
            if (pos >= attackSamples) { stage = 2; pos = 0; }
        } else if (stage == 2) { // Release
            env = 1.0f - (pos / (float)releaseSamples);
            pos++;
            if (pos >= releaseSamples) { stage = 0; env = 0.0f; }
        }
        return env;
        }
};

// Envelope: generic ADSR envelope
struct Envelope {
    float env = 0.0f;                     // Current envelope value
    int stage = 0;                        // 0=off, 1=attack, 2=decay, 3=sustain, 4=release
    int pos = 0;                          // Sample counter for current stage
    float releaseStart = 0.0f;            // Start value for release stage   
};

// Voice: one polyphonic voice
struct Voice {
    bool active;                        // Is this voice active?
    float age;                          // How long has this voice been active?
    ModalResonator modes[MAX_MODES];    // Modal resonators
    Excitation excitation;              // Excitation buffer
    Envelope ampEnv;                    // Amplitude envelope (not used here)
    ExcitationAR excitationAR;          // AR envelope for excitation
};

// Main algorithm structure
struct ModalInstrument : _NT_algorithm {
    Voice voices[NUM_VOICES];    // All voices
    float lastTrigger1;          // Last trigger state
    float lastTrigger2;          // Last trigger state
    float lpState;               // Lowpass filter state for output
    Envelope noiseEnv;           // global Noise-ADSR
    bool noiseGate;              // global Gate-Flag for Noise          
};

// Parameters and enums
enum {
    kParamTrigger1 = 0,
    kParamTrigger2,
    kParamNoteCV1,
    kParamNoteCV2,
    kParamDecay,
    kParamBaseFreq,
    kParamInstrumentType,
    kParamExcitationType,
    kParamOutputL,
    kParamOutputModeL,
    kParamOutputR,
    kParamOutputModeR,
    kParamBaseFreqCV,
    kParamDecayCV,
    kParamExcitationCV,
    kParamResonatorType,
    kParamNoiseType,
    kParamNoiseLevel,
    kParamNoiseAttack,
    kParamNoiseDecay,
    kParamNoiseSustain,
    kParamNoiseRelease,
    kParamExcitationAttack,
    kParamExcitationRelease
};

static const char* instrumentTypes[] = {
    "Handpan", "Steel Drum", "Bell", "Gong", "Triangle", "Tabla", "Conga", "Tom", "Timpani", "Udu",
    "Slit Drum", "Organ Pipe", "Cowbell", "Frame Drum", "Kalimba", "Woodblock", "Glass Bowl", "Metal Pipe",
    "Broken Bell", "Bottle", "Deep Gong", "Ceramic Pot", "Plate", "Agogo Bell", "Water Drop", "Anvil", "Marimba",
    "Vibraphone", "Glass Harmonica", "Oil Drum", "Synth Tom", "Spring Drum", "Brake Drum", "Wind Chime",
    "Tibetan Bowl", "Plastic Tube", "Gamelan Gong", "Sheet Metal", "Toy Piano", "Metal Rod", "Waterphone",
    "Steel Plate", "Large Bell", "Cowbell 2", "Trash Can", "Sheet Glass", "Pipe Organ", "Alien Metal",
    "Broken Cymbal", "Submarine Hull", "Random Metal"
};

static const char* excitationTypes[] = {
    "Finger Hard", "Finger Soft", "Hand Smash", "Hard Mallet", "SoftMallet",
    "Handpan", "Hard Steel", "Ding", "Chime", "Custom",
    "Muted Slap", "Brush", "Double Tap", "Reverse", "Noise Burst", "Triangle Pulse", "Sine Burst"
};

static const char* resonatorTypes[] = {
    "Standard", "Fast Decay", "Soft Clip", "Dyn Gain", "Env Damp", "Age Damp", "Asymmetry", "Env Gain",
    "Limiter", "Highpass", "Bright", "Env Clip", "Out Damp", "Phase Flip", "Even Harm", "Out Lim",
    "Odd Harm", "Env Asym", "Out HP", "Dyn Decay"
};

static const char* noiseTypes[] = {
    "White", "Pink", "Blue", "HP Fast", "HP Slow", "LP Fast", "LP Slow",
    "Bitcrush8", "Bitcrush4", "Bitcrush2",
    "S&H Fast", "S&H Med", "S&H Slow",
    "Dust Rare", "Dust Med", "Dust Freq",
    "Chopper Slow", "Chopper Med", "Chopper Fast",
    "Metallic", "AM Slow", "AM Fast",
    "Ringmod Slow", "Ringmod Fast",
    "EnvFollow Slow", "EnvFollow Fast",
    "Blue+Pink", "HP+LP", "S&H+Bitcrush", "White+Metallic"
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Trigger 1", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("Trigger 2", 1, 2)
    NT_PARAMETER_CV_INPUT("Note CV 1", 1, 3)
    NT_PARAMETER_CV_INPUT("Note CV 2", 1, 4)
    { "Decay", 100, 8000, 600, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Base Freq", 40, 4000, 110, kNT_unitHz, kNT_scalingNone, nullptr },
    { "Instrument", 0, 50, 0, kNT_unitEnum, kNT_scalingNone, instrumentTypes },
    { "Excitation", 0, 16, 0, kNT_unitEnum, kNT_scalingNone, excitationTypes },
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out R", 1, 14)
    NT_PARAMETER_CV_INPUT("BaseFreq CV", 1, 5)
    NT_PARAMETER_CV_INPUT("Decay CV", 1, 6)
    NT_PARAMETER_CV_INPUT("Excit. CV", 1, 7)
    { "Resonator Type", 0, 19, 0, kNT_unitEnum, kNT_scalingNone, resonatorTypes },
    { "Noise Type", 0, 29, 0, kNT_unitEnum, kNT_scalingNone, noiseTypes },
    { "Noise Level", 0, 100, 0, kNT_unitPercent, kNT_scalingNone, nullptr },
    { "Noise A", 1, 4000, 10, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Noise D", 1, 4000, 50, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Noise S", 0, 100, 30, kNT_unitPercent, kNT_scalingNone, nullptr },
    { "Noise R", 1, 4000, 100, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Exciter Attack", 1, 128, 16, kNT_unitFrames, kNT_scalingNone, nullptr },
    { "Exciter Release", 1, 256, 32, kNT_unitFrames, kNT_scalingNone, nullptr },
};

static const uint8_t page1[] = { kParamTrigger1, kParamTrigger2, kParamNoteCV1, kParamNoteCV2, kParamBaseFreqCV, kParamDecayCV, kParamExcitationCV };
static const uint8_t page2[] = { kParamOutputL, kParamOutputModeL, kParamOutputR, kParamOutputModeR };
static const uint8_t page3[] = { kParamInstrumentType, kParamExcitationType, kParamExcitationAttack, kParamExcitationRelease, kParamDecay, kParamBaseFreq };
static const uint8_t page4[] = { kParamResonatorType };
static const uint8_t page5[] = { kParamNoiseType, kParamNoiseLevel, kParamNoiseAttack, kParamNoiseDecay, kParamNoiseSustain, kParamNoiseRelease };

static const _NT_parameterPage pages[] = {
    { "CV Inputs", ARRAY_SIZE(page1), page1 },
    { "Outputs", ARRAY_SIZE(page2), page2 },
    { "Modal Synth", ARRAY_SIZE(page3), page3 },
    { "Resonator", ARRAY_SIZE(page4), page4 },
    { "Noise", ARRAY_SIZE(page5), page5 }
};

static const _NT_parameterPages parameterPages = { ARRAY_SIZE(pages), pages };

// ModalConfig: defines the modal structure for each instrument
struct ModalConfig {
    float ratios[MAX_MODES];
    float gains[MAX_MODES];
    int count;
};

// Returns the modal configuration for the selected instrument type
ModalConfig getModalConfig(int type) {
    ModalConfig config;
    switch (type) {
        case 0: config = { {1.00f,1.95f,2.76f,3.76f,4.83f,5.85f,6.93f,7.96f}, {1,0.8,0.6,0.4,0.3,0.2,0.15,0.1}, 8 }; break;
        case 1: config = { {1.0f,2.1f,3.2f,4.3f,5.4f}, {1,0.7,0.5,0.3,0.2}, 5 }; break;
        case 2: config = { {1.0f,2.7f,4.3f,5.2f,6.8f}, {1,0.6,0.5,0.3,0.2}, 5 }; break;
        case 3: config = { {1.0f,2.01f,2.9f,4.1f,5.3f}, {1,0.6,0.4,0.3,0.2}, 5 }; break;
        case 4: config = { {1.0f,2.1f,3.5f,5.6f}, {1,0.6,0.4,0.3}, 4 }; break;
        case 5: config = { {1.0f,1.5f,2.4f,3.5f,4.6f}, {1,0.7,0.5,0.4,0.3}, 5 }; break;
        case 6: config = { {1.0f,1.6f,2.3f,3.1f}, {1,0.6,0.4,0.3}, 4 }; break;
        case 7: config = { {1.0f,1.9f,2.6f,3.8f}, {1,0.5,0.3,0.2}, 4 }; break;
        case 8: config = { {1.0f,1.5f,2.0f,2.8f,3.6f}, {1,0.8,0.6,0.4,0.2}, 5 }; break;
        case 9: config = { {1.0f,1.6f,2.5f,3.3f}, {1,0.5,0.3,0.2}, 4 }; break;
        case 10: config = { {1.0f,2.0f,3.2f,4.6f}, {1,0.7,0.5,0.3}, 4 }; break;
        case 11: config = { {1.0f,1.7f,2.9f,4.4f,6.1f}, {1,0.5,0.4,0.3,0.2}, 5 }; break;
        case 12: config = { {1.0f,2.1f,3.9f,5.7f}, {1,0.4,0.3,0.2}, 4 }; break;
        case 13: config = { {1.0f,1.4f,2.3f,3.2f}, {1,0.6,0.4,0.2}, 4 }; break;
        case 14: config = { {1.0f, 2.2f, 3.5f, 5.0f}, {1, 0.5, 0.3, 0.15}, 4 }; break;
        case 15: config = { {1.0f, 2.8f, 4.1f}, {1, 0.4, 0.2}, 3 }; break;
        case 16: config = { {1.0f, 2.5f, 4.8f, 6.9f}, {1, 0.7, 0.4, 0.2}, 4 }; break;
        case 17: config = { {1.0f, 1.6f, 2.3f, 3.1f, 4.0f}, {1, 0.8, 0.5, 0.3, 0.15}, 5 }; break;
        case 18: config = { {1.0f, 1.5f, 2.2f, 3.3f, 4.7f}, {1, 0.6, 0.4, 0.2, 0.1}, 5 }; break;
        case 19: config = { {1.0f, 2.0f, 3.7f, 5.5f}, {1, 0.5, 0.3, 0.1}, 4 }; break;
        case 20: config = { {1.0f, 1.8f, 2.7f, 3.9f, 5.6f}, {1, 0.7, 0.5, 0.3, 0.15}, 5 }; break;
        case 21: config = { {1.0f, 1.7f, 2.9f, 4.2f}, {1, 0.6, 0.3, 0.15}, 4 }; break;
        case 22: config = { {1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f}, {1, 0.7, 0.5, 0.3, 0.2, 0.1}, 6 }; break;
        case 23: config = { {1.0f, 2.3f, 3.7f, 5.1f}, {1, 0.5, 0.3, 0.15}, 4 }; break;
        case 24: config = { {1.0f, 2.5f, 4.7f}, {1, 0.4, 0.2}, 3 }; break;
        case 25: config = { {1.0f, 1.4f, 2.2f, 3.6f, 5.0f}, {1, 0.8, 0.5, 0.3, 0.1}, 5 }; break;
        case 26: config = { {1.0f, 3.9f, 9.0f}, {1, 0.4, 0.2}, 3 }; break;
        case 27: config = { {1.0f, 2.8f, 5.6f, 8.9f}, {1, 0.5, 0.3, 0.1}, 4 }; break;
        case 28: config = { {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}, {1, 0.7, 0.5, 0.3, 0.2}, 5 }; break;
        case 29: config = { {1.0f, 1.8f, 2.7f, 3.5f, 4.2f}, {1, 0.6, 0.4, 0.2, 0.1}, 5 }; break;
        case 30: config = { {1.0f, 1.5f, 2.2f}, {1, 0.5, 0.2}, 3 }; break;
        case 31: config = { {1.0f, 1.3f, 1.7f, 2.2f, 2.8f}, {1, 0.7, 0.5, 0.3, 0.15}, 5 }; break;
        case 32: config = { {1.0f, 2.2f, 3.5f, 5.1f}, {1, 0.6, 0.4, 0.2}, 4 }; break;
        case 33: config = { {1.0f, 2.5f, 4.1f, 6.2f}, {1, 0.5, 0.3, 0.15}, 4 }; break;
        case 34: config = { {1.0f, 2.3f, 3.8f, 5.7f}, {1, 0.7, 0.4, 0.2}, 4 }; break;
        case 35: config = { {1.0f, 1.6f, 2.3f, 3.0f}, {1, 0.6, 0.3, 0.1}, 4 }; break;
        case 36: config = { {1.0f, 1.8f, 2.6f, 3.7f, 5.2f}, {1, 0.8, 0.5, 0.3, 0.1}, 5 }; break;
        case 37: config = { {1.0f, 1.41f, 2.24f, 2.83f, 3.16f}, {1, 0.7, 0.5, 0.3, 0.15}, 5 }; break;
        case 38: config = { {1.0f, 2.9f, 5.5f, 8.2f}, {1, 0.5, 0.3, 0.1}, 4 }; break;
        case 39: config = { {1.0f, 2.76f, 5.40f, 8.93f}, {1, 0.6, 0.3, 0.1}, 4 }; break;
        case 40: config = { {1.0f, 1.3f, 2.1f, 3.4f, 5.7f}, {1, 0.7, 0.5, 0.3, 0.15}, 5 }; break;
        case 41: config = { {1.0f, 1.58f, 2.24f, 2.87f, 3.46f, 4.0f}, {1, 0.7, 0.5, 0.3, 0.2, 0.1}, 6 }; break;
        case 42: config = { {1.0f, 2.1f, 2.9f, 4.0f, 5.2f, 6.8f}, {1, 0.8, 0.6, 0.4, 0.2, 0.1}, 6 }; break;
        case 43: config = { {1.0f, 1.7f, 2.5f, 3.3f}, {1, 0.6, 0.4, 0.2}, 4 }; break;
        case 44: config = { {1.0f, 1.9f, 2.8f, 4.2f, 5.7f}, {1, 0.5, 0.3, 0.15, 0.08}, 5 }; break;
        case 45: config = { {1.0f, 1.41f, 2.0f, 2.24f, 2.83f}, {1, 0.7, 0.5, 0.3, 0.15}, 5 }; break;
        case 46: config = { {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}, {1, 0.6, 0.3, 0.15, 0.08}, 5 }; break;
        case 47: config = { {1.0f, 1.13f, 1.47f, 2.03f, 2.89f, 4.17f}, {1, 0.9, 0.7, 0.5, 0.3, 0.1}, 6 }; break;
        case 48: config = { {1.0f, 1.3f, 1.7f, 2.2f, 2.9f, 3.7f}, {1, 0.8, 0.5, 0.3, 0.2, 0.1}, 6 }; break;
        case 49: config = { {1.0f, 1.2f, 1.5f, 2.0f, 2.7f, 3.5f}, {1, 0.7, 0.5, 0.3, 0.2, 0.1}, 6 }; break;
        case 50: config = { {1.0f, 1.33f, 2.17f, 2.98f, 4.11f, 5.29f}, {1, 0.6, 0.4, 0.2, 0.1, 0.05}, 6 }; break;
        default: config = { {1,2,3,4,5,6,7,8,9,10,11,12}, {1,0.8,0.7,0.6,0.5,0.4,0.3,0.2,0.15,0.1,0.08,0.06}, 12 }; break;
    }
    return config;
}

// Algorithm construct function
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t*) {
    ModalInstrument* self = new(ptrs.sram) ModalInstrument;
    self->parameters = parameters;
    self->parameterPages = &parameterPages;
    self->lastTrigger1 = 0.0f;
    self->lastTrigger2 = 0.0f;
    self->lpState = 0.0f;
    return self;
}

// Compute a generic ADSR envelope (used for noise)
float computeADSR(Envelope& env, int attack, int decay, float sustain, int release, bool gate) {
    switch (env.stage) {
        case 0: // Idle
            if (gate) { env.stage = 1; env.pos = 0; }
            env.env = 0.0f;
            break;
        case 1: // Attack
            env.env = (attack > 0) ? (env.pos / (float)attack) : 1.0f;
            env.pos++;
            if (env.pos >= attack) { env.stage = 2; env.pos = 0; }
            break;
        case 2: // Decay
            env.env = 1.0f - (1.0f - sustain) * (env.pos / (float)decay);
            env.pos++;
            if (env.pos >= decay) { env.stage = 3; env.pos = 0; }
            break;
        case 3: // Sustain
            env.env = sustain;
            if (!gate) { env.stage = 4; env.pos = 0; env.releaseStart = env.env; }
            break;
        case 4: // Release
            env.env = env.releaseStart * (1.0f - (env.pos / (float)release));
            env.pos++;
            if (env.pos >= release) { env.stage = 0; env.env = 0.0f; }
            break;
    }
    return env.env;
}

// Main audio processing loop
extern "C" void step(_NT_algorithm* base, float* busFrames, int numFramesBy4) {
    ModalInstrument* self = static_cast<ModalInstrument*>(base);
    int numFrames = numFramesBy4 * 4;

 
    // Noise state variables (declare static at file or function scope)
    static uint32_t noiseSeed = 1;
    static float pink = 0.0f;
    static float blueLast = 0.0f;
    static float hp1 = 0.0f, hp2 = 0.0f;
    static float lp1 = 0.0f, lp2 = 0.0f;
    static int sAndHcnt1 = 0, sAndHcnt2 = 0, sAndHcnt3 = 0;
    static float sAndH1 = 0.0f, sAndH2 = 0.0f, sAndH3 = 0.0f;
    static float chopperPhase1 = 0.0f, chopperPhase2 = 0.0f, chopperPhase3 = 0.0f;
    static float amPhase1 = 0.0f, amPhase2 = 0.0f;
    static float ringPhase1 = 0.0f, ringPhase2 = 0.0f;
    static float envPhase1 = 0.0f, envPhase2 = 0.0f;
    // Input and output buffers
    float* trig1   = busFrames + (self->v[kParamTrigger1] - 1) * numFrames;
    float* trig2   = busFrames + (self->v[kParamTrigger2] - 1) * numFrames;
    float* noteCV1 = (self->v[kParamNoteCV1] ? busFrames + (self->v[kParamNoteCV1] - 1) * numFrames : nullptr);
    float* noteCV2 = (self->v[kParamNoteCV2] ? busFrames + (self->v[kParamNoteCV2] - 1) * numFrames : nullptr);
    float* cvFreq  = (self->v[kParamBaseFreqCV] ? busFrames + (self->v[kParamBaseFreqCV] - 1) * numFrames : nullptr);
    float* cvDecay = (self->v[kParamDecayCV]    ? busFrames + (self->v[kParamDecayCV]    - 1) * numFrames : nullptr);
    float* cvExcit = (self->v[kParamExcitationCV] ? busFrames + (self->v[kParamExcitationCV] - 1) * numFrames : nullptr);
    float* outL = busFrames + (self->v[kParamOutputL] - 1) * numFrames;
    float* outR = busFrames + (self->v[kParamOutputR] - 1) * numFrames;

    // UI parameters
    float baseHzParam  = self->v[kParamBaseFreq];
    float decayParam   = self->v[kParamDecay];
    int instrType      = self->v[kParamInstrumentType];
    int excTypeParam   = self->v[kParamExcitationType];
    float noiseLevel   = self->v[kParamNoiseLevel] / 100.0f;
    float noiseA       = (int)(self->v[kParamNoiseAttack] * SAMPLE_RATE / 1000.0f);
    float noiseD       = (int)(self->v[kParamNoiseDecay]  * SAMPLE_RATE / 1000.0f);
    float noiseR       = (int)(self->v[kParamNoiseRelease] * SAMPLE_RATE / 1000.0f);
    float noiseS       = self->v[kParamNoiseSustain] / 100.0f;
    int excitAttack    = self->v[kParamExcitationAttack];
    int excitRelease   = self->v[kParamExcitationRelease];
    int noiseType      = self->v[kParamNoiseType];

    //Modal 
    ModalConfig config = getModalConfig(instrType);

//memset reset out buffers
    memset(outL, 0, numFrames * sizeof(float));
    memset(outR, 0, numFrames * sizeof(float));

// Reset noise envelope
    float gateState1 = self->lastTrigger1;
    float gateState2 = self->lastTrigger2;

    for (int f = 0; f < numFrames; ++f) {
        // --- GATE-Handling ---
        float currentGate1 = trig1[f];
        float currentGate2 = trig2[f];
        bool gateOn1 = (currentGate1 >= 0.5f);
        bool gateOn2 = (currentGate2 >= 0.5f);


        // --- HAND 1: Calculate base frequency ---
        float baseHz1 = baseHzParam;
        if (cvFreq && fabsf(cvFreq[f]) > 0.01f) {
            baseHz1 = baseHzParam * powf(2.0f, cvFreq[f]);
            baseHz1 = fmaxf(baseHz1, 40.0f);
        }
        float noteFactor1 = 1.0f;
        if (noteCV1 && fabsf(noteCV1[f]) < 6.0f) {
            float noteV = noteCV1[f];
            noteFactor1 = powf(2.0f, noteV);
        }
        baseHz1 *= noteFactor1;
        baseHz1 = fmaxf(baseHz1, 40.0f);

        // --- HAND 2: Calculate base frequency ---
        float baseHz2 = baseHzParam;
        if (cvFreq && fabsf(cvFreq[f]) > 0.01f) {
            baseHz2 = baseHzParam * powf(2.0f, cvFreq[f]); 
            baseHz2 = fmaxf(baseHz2, 40.0f);
        }
        float noteFactor2 = 1.0f;
        if (noteCV2 && fabsf(noteCV2[f]) < 6.0f) {
            float noteV = noteCV2[f];
            noteFactor2 = powf(2.0f, noteV);
        }
        baseHz2 *= noteFactor2;
        baseHz2 = fmaxf(baseHz2, 40.0f);

        // --- Calculate decay ---
        float decayCV = (cvDecay ? cvDecay[f] : 0.0f);
        float decayMs = decayParam + decayCV * 8000.0f;
        decayMs = fmaxf(decayMs, 100.0f);
        float decay = decayMs / 1000.0f;

        // --- Calculate excitation type ---
        int excType = excTypeParam;
        if (cvExcit && fabsf(cvExcit[f]) > 0.01f) {
            excType = static_cast<int>(fminf(cvExcit[f] * 4.99f, 4.0f));
        }

        // --- GATE LOGIC FOR HAND 1 ---
        if (!gateState1 && gateOn1) {
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
            voice.excitation.generate(excType, instrType, noiseLevel, noiseA, noiseD, noiseS, noiseR);
            voice.excitationAR.trigger(excitAttack, excitRelease);
            

            float dampingFactor = 1.0f;
            if (instrType == 3 || instrType == 4) dampingFactor = 0.7f;
            else if (instrType == 8) decay *= 2.5f;
            else if (instrType == 13) decay *= 2.0f;

            
           

            // Initialize modal resonators for this voice
            for (int m = 0; m < config.count; ++m) {
                float freq = baseHz1 * config.ratios[m];
                freq = fminf(freq, SAMPLE_RATE * 0.35f);
                float gain = config.gains[m];
                float bw = (1.0f / decay) * (0.4f + 0.6f * m / config.count) * dampingFactor;
                voice.modes[m].init(freq, gain, bw, self->v[kParamResonatorType]);
            }
            voice.active = true;
            voice.age = 0.0f;
        }
        gateState1 = gateOn1;


        // --- GATE LOGIC FOR HAND 2 ---
        if (!gateState2 && gateOn2) {
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
            voice.excitation.generate(excType, instrType, noiseLevel, noiseA, noiseD, noiseS, noiseR);
            voice.excitationAR.trigger(excitAttack, excitRelease);
            

            float dampingFactor = 1.0f;
            if (instrType == 3 || instrType == 4) dampingFactor = 0.7f;
            else if (instrType == 8) decay *= 2.5f;
            else if (instrType == 13) decay *= 2.0f;

            

            for (int m = 0; m < config.count; ++m) {
                float freq = baseHz2 * config.ratios[m];
                freq = fminf(freq, SAMPLE_RATE * 0.35f);
                float gain = config.gains[m];
                float bw = (1.0f / decay) * (0.4f + 0.6f * m / config.count) * dampingFactor;
                voice.modes[m].init(freq, gain, bw, self->v[kParamResonatorType]);
            }
            voice.active = true;
            voice.age = 0.0f;
        }
        gateState2 = gateOn2;

        // --- NOISE-ADSR retrigger: at each Gate-On from Trigger 1 or 2 ---
        bool retriggerNoise = (!self->noiseGate && (gateOn1 || gateOn2));
        self->noiseGate = (gateOn1 || gateOn2);
        if (retriggerNoise) {
            self->noiseEnv.stage = 1; // Attack
            self->noiseEnv.pos = 0;
            self->noiseEnv.env = 0.0f;
        }

        

        // === Process all voices and sum output ===
       float sample = 0.0f;
        for (int v = 0; v < NUM_VOICES; ++v) {
            if (!self->voices[v].active) continue;
            float exc = self->voices[v].excitation.next() * self->voices[v].excitationAR.next(); // Get excitation signal for this voice
            float sum = 0.0f;
            bool silent = true;
            for (int m = 0; m < config.count; ++m) {
                float s = self->voices[v].modes[m].process(exc, self->v[kParamResonatorType]);
                sum += s;
                if (fabsf(s) > 0.0005f) silent = false;
            }
          
            //Noise processing
            float noiseVal = 0.0f;

            // Noise types
            switch (noiseType) {
                case 0: // White Noise
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    noiseVal = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    break;
                case 1: // Pink Noise
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    pink = 0.98f * pink + 0.02f * (((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f);
                    noiseVal = pink;
                    break;
                case 2: // Blue Noise
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        noiseVal = white - blueLast;
                        blueLast = white;
                    }
                    break;
                case 3: // Highpass Noise (fast)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        hp1 = 0.8f * hp1 + white - (0.8f * hp1);
                        noiseVal = hp1;
                    }
                    break;
                case 4: // Highpass Noise (slow)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        hp2 = 0.95f * hp2 + white - (0.95f * hp2);
                        noiseVal = hp2;
                    }
                    break;
                case 5: // Lowpass Noise (fast)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        lp1 = 0.85f * lp1 + 0.15f * white;
                        noiseVal = lp1;
                    }
                    break;
                case 6: // Lowpass Noise (slow)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        lp2 = 0.98f * lp2 + 0.02f * white;
                        noiseVal = lp2;
                    }
                    break;
                case 7: // Bitcrushed Noise (8 levels)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        noiseVal = floorf(white * 8.0f) / 8.0f;
                    }
                    break;
                case 8: // Bitcrushed Noise (4 levels)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        noiseVal = floorf(white * 4.0f) / 4.0f;
                    }
                    break;
                case 9: // Bitcrushed Noise (2 levels)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        noiseVal = (white > 0.0f) ? 1.0f : -1.0f;
                    }
                    break;
                case 10: // Sample & Hold (fast)
                    if (++sAndHcnt1 > 10) {
                        noiseSeed = 1664525 * noiseSeed + 1013904223;
                        sAndH1 = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        sAndHcnt1 = 0;
                    }
                    noiseVal = sAndH1;
                    break;
                case 11: // Sample & Hold (medium)
                    if (++sAndHcnt2 > 40) {
                        noiseSeed = 1664525 * noiseSeed + 1013904223;
                        sAndH2 = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        sAndHcnt2 = 0;
                    }
                    noiseVal = sAndH2;
                    break;
                case 12: // Sample & Hold (slow)
                    if (++sAndHcnt3 > 200) {
                        noiseSeed = 1664525 * noiseSeed + 1013904223;
                        sAndH3 = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        sAndHcnt3 = 0;
                    }
                    noiseVal = sAndH3;
                    break;
                case 13: // Dust (rare)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f;
                        noiseVal = (white > 0.995f) ? (white * 2.0f - 1.0f) : 0.0f;
                    }
                    break;
                case 14: // Dust (medium)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f;
                        noiseVal = (white > 0.98f) ? (white * 2.0f - 1.0f) : 0.0f;
                    }
                    break;
                case 15: // Dust (frequent)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f;
                        noiseVal = (white > 0.90f) ? (white * 2.0f - 1.0f) : 0.0f;
                    }
                    break;
                case 16: // Chopper (slow)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        chopperPhase1 += 0.005f;
                        if (chopperPhase1 > 2.0f * M_PI) chopperPhase1 -= 2.0f * M_PI;
                        noiseVal = white * (sinf(chopperPhase1) > 0.0f ? 1.0f : 0.0f);
                    }
                    break;
                case 17: // Chopper (medium)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        chopperPhase2 += 0.02f;
                        if (chopperPhase2 > 2.0f * M_PI) chopperPhase2 -= 2.0f * M_PI;
                        noiseVal = white * (sinf(chopperPhase2) > 0.0f ? 1.0f : 0.0f);
                    }
                    break;
                case 18: // Chopper (fast)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        chopperPhase3 += 0.08f;
                        if (chopperPhase3 > 2.0f * M_PI) chopperPhase3 -= 2.0f * M_PI;
                        noiseVal = white * (sinf(chopperPhase3) > 0.0f ? 1.0f : 0.0f);
                    }
                    break;
                case 19: // Metallic (xor-shift)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        uint32_t n = noiseSeed;
                        n ^= n << 13; n ^= n >> 17; n ^= n << 5;
                        noiseVal = ((n & 0xFF) / 128.0f) - 1.0f;
                    }
                    break;
                case 20: // AM Noise (slow)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        amPhase1 += 0.01f;
                        if (amPhase1 > 2.0f * M_PI) amPhase1 -= 2.0f * M_PI;
                        noiseVal = white * (0.5f + 0.5f * sinf(amPhase1));
                    }
                    break;
                case 21: // AM Noise (fast)
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    {
                        float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                        amPhase2 += 0.05f;
                        if (amPhase2 > 2.0f * M_PI) amPhase2 -= 2.0f * M_PI;
                        noiseVal = white * (0.5f + 0.5f * sinf(amPhase2));
                    }
                    break;
                case 22: // Ringmod Noise (slow)
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    ringPhase1 += 0.01f;
                    if (ringPhase1 > 2.0f * M_PI) ringPhase1 -= 2.0f * M_PI;
                    noiseVal = white * sinf(ringPhase1);
                }
                break;
                case 23: // Ringmod Noise (fast)
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    ringPhase2 += 0.05f;
                    if (ringPhase2 > 2.0f * M_PI) ringPhase2 -= 2.0f * M_PI;
                    noiseVal = white * sinf(ringPhase2);
                }
                break;
                case 24: // Envelope-followed Noise (slow)
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    envPhase1 += 0.005f;
                    if (envPhase1 > 2.0f * M_PI) envPhase1 -= 2.0f * M_PI;
                    noiseVal = white * fabsf(sinf(envPhase1));
                }
                break;
                case 25: // Envelope-followed Noise (fast)
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    envPhase2 += 0.03f;
                    if (envPhase2 > 2.0f * M_PI) envPhase2 -= 2.0f * M_PI;
                    noiseVal = white * fabsf(sinf(envPhase2));
                }
                break;
                case 26: // Blue+Pink Mix
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    float blue = white - blueLast;
                    blueLast = white;
                    pink = 0.98f * pink + 0.02f * white;
                    noiseVal = 0.5f * blue + 0.5f * pink;
                }
                break;
                case 27: // HP+LP Mix
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    hp1 = 0.8f * hp1 + white - (0.8f * hp1);
                    lp1 = 0.85f * lp1 + 0.15f * white;
                    noiseVal = 0.5f * hp1 + 0.5f * lp1;
                }
                break;
                case 28: // S&H + Bitcrush Mix
                if (++sAndHcnt1 > 40) {
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    sAndH1 = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    sAndHcnt1 = 0;
                }
                {
                    float bc = floorf(sAndH1 * 4.0f) / 4.0f;
                    noiseVal = 0.5f * sAndH1 + 0.5f * bc;
                }
                break;
                case 29: // White + Metallic Mix
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                {
                    float white = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                    uint32_t n = noiseSeed;
                    n ^= n << 13; n ^= n >> 17; n ^= n << 5;
                    float metallic = ((n & 0xFF) / 128.0f) - 1.0f;
                    noiseVal = 0.5f * white + 0.5f * metallic;
                }
                break;
                default: // fallback to White Noise
                    noiseSeed = 1664525 * noiseSeed + 1013904223;
                    noiseVal = ((noiseSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
                break;
                noiseSeed = 1664525 * noiseSeed + 1013904223;
                }
            // Apply noise envelope                
            float noiseEnv = computeADSR(self->noiseEnv, noiseA, noiseD, noiseS, noiseR, self->noiseGate);
            float noise = noiseVal * noiseEnv * noiseLevel;
            sample += noise;
                if (silent) self->voices[v].active = false;
                    sample += sum;
                    self->voices[v].age += 1.0f / SAMPLE_RATE;
                }

            // Output lowpass filter for smoothing
            float alpha = expf(-2.0f * M_PI * 3000.0f / SAMPLE_RATE);
            sample = self->lpState + alpha * (sample - self->lpState);
            self->lpState = sample;

        // Write output (attenuated)
        outL[f] = sample * 0.1f;
        outR[f] = sample * 0.1f;
    }
// Update gates
    self->lastTrigger1 = gateState1;
    self->lastTrigger2 = gateState2;
}
extern "C" bool draw(_NT_algorithm* base) {
    ModalInstrument* self = static_cast<ModalInstrument*>(base);



    // --- Noise Envelope Value Bar ---
    float envVal = self->noiseEnv.env;
    NT_drawText(180, 54, "N.Env:", 14, kNT_textLeft, kNT_textTiny);
    NT_drawShapeI(kNT_rectangle, 180, 56, 180 + (int)(envVal * 60), 62, 14);

    // --- Exciter AR Curve (simple) ---
    NT_drawText(162, 20, "AR:", 14, kNT_textLeft, kNT_textNormal);
    int x0 = 182, y0 = 10;
    int A = self->v[kParamExcitationAttack] / 10 ;   
    int R = self->v[kParamExcitationRelease] / 10 ;
    //int total = A + R + 20;
    int x = x0, y = y0 + 30;
    // Attack
    NT_drawShapeI(kNT_line, x, y, x + A, y - 25, 8);
    x += A; y -= 25;
    // Release
    NT_drawShapeI(kNT_line, x, y, x + R, y + 25 , 8);



   // --- Voices bar with numbers ---
    NT_drawText(5, 18, "Voices", 14, kNT_textLeft, kNT_textTiny);
    NT_drawText(5, 25, " 1 2 3 4 5 6 7 8", 14, kNT_textLeft, kNT_textTiny);
    
    int activeVoices = 0;
    for (int v = 0; v < NUM_VOICES; ++v) {
        if (self->voices[v].active)
            activeVoices++;
    }
    const int maxWidth = 74;
    int barWidth = (int)((activeVoices / (float)NUM_VOICES) * maxWidth);
    NT_drawShapeI(kNT_rectangle, 5, 26, 5 + barWidth, 34, 14);

  
    NT_drawText(128, 62, "HandpanModalXT2", 15, kNT_textCentre, kNT_textNormal);

    // Return false to allow standard parameter line at top
    return false;
}

// Required Disting NT API functions
extern "C" void parameterChanged(_NT_algorithm*, int) {}
extern "C" void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(ModalInstrument);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('H','A','N','X'),
    .name = "HandpanModalXT2",
    .description = "Modal Perc Synth (No Inharmonicity)",
    .numSpecifications = 0,
    .specifications = nullptr,
    .calculateStaticRequirements = nullptr,
    .initialise = nullptr,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
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