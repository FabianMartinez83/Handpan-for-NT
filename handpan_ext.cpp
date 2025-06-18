// Disting NT Plugin - Advanced Modal Percussion Synth
// Author: Fabian Martinez (extended with GPT-4o)
// Features: Modal synthesis, excitation modeling, inharmonic control, noise with ADSR

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
    float freq, gain, bandwidth; // Mode parameters
    float env, age;              // Envelope and age for this mode
    float y1, y2;                // Filter state
    float a1, a2, r;             // Filter coefficients

    // Initialize the resonator with frequency, gain, and bandwidth
    void init(float f, float g, float bw) {
        freq = f;
        gain = g;
        bandwidth = fmaxf(bw, 0.05f); // Minimum bandwidth 0.05 Hz
        env = 1.0f;
        age = 0.0f;
        y1 = 0.0f;
        y2 = 0.0f;
        r = expf(-M_PI * bandwidth / SAMPLE_RATE);
        a1 = -2.0f * r * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
        a2 = r * r;
    }

    // Process one sample through the resonator
    float process(float x, int type = 0) {
        switch (type) {
            case 1: env *= 0.9995f; break; // Exponential decay
            case 2: x += 0.00005f * y1; break; // Slight feedback
            case 3: x = 0.5f * (x + gain * x); break; // Oversampled feed
            case 4: gain *= (0.95f + 0.05f * sinf(age * 3.1415f)); break; // Modulated gain
            case 5: // Wide bandwidth
                bandwidth *= 1.2f;
                r = expf(-M_PI * bandwidth / SAMPLE_RATE);
                a1 = -2.0f * r * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
                a2 = r * r;
                break;
            default: break;
        }
        float y = gain * x - a1 * y1 - a2 * y2; // Resonator difference equation
        y2 = y1;
        y1 = y;
        age += 1.0f / SAMPLE_RATE;
        return y * env;
    }
};

//--------------------------------------------------------------
// Excitation: buffer for the initial impulse (not for continuous noise)
//--------------------------------------------------------------
struct Excitation {
    float buffer[EXCITATION_BUFFER_SIZE];
    int pos;
    float mixNoise = 0.0f;

    // Get next sample from the excitation buffer
    float next() {
        float value = (pos < EXCITATION_BUFFER_SIZE ? buffer[pos++] : 0.0f);
        return softclip(value) * 0.1f; // Softclip and attenuate
    }

    // Get current value (for debugging, not used for output)
    float raw() const {
        int idx = pos < EXCITATION_BUFFER_SIZE ? pos : (EXCITATION_BUFFER_SIZE - 1);
        return buffer[idx];
    }

    // Generate the excitation buffer (impulse shape)
    void generate(int type, int instrType, float inharmonicity = 0.0f, float noiseAmount = 0.0f, int a = 64, int d = 128, float s = 0.0f, int r = 256) {
        if (!noiseInit) {
            for (int i = 0; i < EXCITATION_NOISETABLE_SIZE; ++i)
                noiseTable[i] = ((rand() % 2000) / 1000.0f) - 1.0f;
            noiseInit = true;
        }
        pos = 0;
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
            case 14: for (int i = 0; i < 24; ++i) buffer[i] = (((rand() % 2000) / 1000.0f) - 1.0f) * expf(-0.2f * i);
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

        // Optional: add inharmonicity to the excitation (not continuous noise)
        if (inharmonicity > 0.0f) {
            for (int i = 0; i < EXCITATION_BUFFER_SIZE; ++i)
                buffer[i] *= 1.0f + 0.002f * inharmonicity * sinf(i * 0.1f);
        }

        // Ensure at least 3 nonzero values
        if (buffer[0] == 0.0f && buffer[1] == 0.0f && buffer[2] == 0.0f) {
            buffer[0] = 0.2f;
            buffer[1] = 0.4f;
            buffer[2] = 0.6f;
        }
    }
};

//--------------------------------------------------------------
// Envelope: generic ADSR envelope
//--------------------------------------------------------------
struct Envelope {
    float env = 0.0f; // Current envelope value
    int stage = 0;    // 0=off, 1=attack, 2=decay, 3=sustain, 4=release
    int pos = 0;      // Sample counter for current stage
};

//--------------------------------------------------------------
// Voice: one polyphonic voice
//--------------------------------------------------------------
struct Voice {
    bool active;                        // Is this voice active?
    float age;                          // How long has this voice been active?
    float modeFreqOffset[MAX_MODES];    // Per-mode frequency offsets (not used here)
    ModalResonator modes[MAX_MODES];    // Modal resonators
    Excitation excitation;              // Excitation buffer
    Envelope ampEnv;                    // Amplitude envelope (not used here)
    Envelope noiseEnv;                  // Noise envelope (for continuous noise)
};

//--------------------------------------------------------------
// Main algorithm structure
//--------------------------------------------------------------
struct ModalInstrument : _NT_algorithm {
    Voice voices[NUM_VOICES]; // All voices
    float lastTrigger;        // Last trigger state
    float lpState;            // Lowpass filter state for output
};

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
    "Handpan",         // 0
    "Steel Drum",      // 1
    "Bell",            // 2
    "Gong",            // 3
    "Triangle",        // 4
    "Tabla",           // 5
    "Conga",           // 6
    "Tom",             // 7
    "Timpani",         // 8
    "Udu",             // 9
    "Slit Drum",       // 10
    "Hi-Hat",          // 11
    "Cowbell",         // 12
    "Frame Drum",      // 13
    "Kalimba",         // 14
    "Woodblock",       // 15
    "Glass Bowl",      // 16
    "Metal Pipe",      // 17
    "Snare",           // 18
    "Bottle",          // 19
    "Deep Gong",       // 20
    "Ceramic Pot",     // 21
    "Plate",           // 22
    "Agogo Bell",      // 23
    "Water Drop",      // 24
    "Anvil",           // 25
    "Marimba",         // 26
    "Vibraphone",      // 27
    "Glass Harmonica", // 28
    "Oil Drum",        // 29
    "Synth Tom",       // 30
    "Spring Drum",     // 31
    "Brake Drum",      // 32
    "Wind Chime",      // 33
    "Tibetan Bowl",    // 34
    "Plastic Tube",    // 35
    "Gamelan Gong",    // 36
    "Sheet Metal",     // 37
    "Toy Piano",       // 38
    "Metal Rod",       // 39
    "Waterphone",      // 40
    "Steel Plate",     // 41
    "Large Bell",      // 42
    "Cowbell 2",       // 43
    "Trash Can",       // 44
    "Sheet Glass",     // 45
    "Pipe Organ",      // 46
    "Alien Metal",     // 47
    "Broken Cymbal",   // 48
    "Submarine Hull",  // 49
    "Random Metal"     // 50
};

static const char* excitationTypes[] = {
    "Finger Hard", "Finger Soft", "Hand Smash", "Hard Mallet", "SoftMallet",
    "Handpan", "Hard Steel", "Ding", "Chime", "Custom",
    "Muted Slap", "Brush", "Double Tap", "Reverse", "Noise Burst", "Triangle Pulse", "Sine Burst"
};

static const char* resonatorTypes[] = {
    "Standard", "Decay", "DC Loop", "Oversample", "Wobble", "Wide BW"
};

static const _NT_parameter parameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Trigger", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("Note CV", 1, 2)
    { "Decay", 100, 8000, 1500, kNT_unitMs, kNT_scalingNone, nullptr },
    { "Base Freq", 2000, 22000, 4400, kNT_unitHz, kNT_scaling100, nullptr },
    { "Instrument", 0, 50, 0, kNT_unitEnum, kNT_scalingNone, instrumentTypes },
    { "Excitation", 0, 16, 0, kNT_unitEnum, kNT_scalingNone, excitationTypes },
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

static const _NT_parameterPage pages[] = {
    { "Modal Synth", ARRAY_SIZE(page1), page1 },
    { "Timbre & FX", ARRAY_SIZE(page2), page2 },
    { "Outputs", ARRAY_SIZE(page3), page3 },
    { "CV Inputs", ARRAY_SIZE(page4), page4 },
    { "Resonator", ARRAY_SIZE(page5), page5 }
};

static const _NT_parameterPages parameterPages = { ARRAY_SIZE(pages), pages };

//--------------------------------------------------------------
// ModalConfig: defines the modal structure for each instrument
//--------------------------------------------------------------
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

//--------------------------------------------------------------
// Algorithm construct function
//--------------------------------------------------------------
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t*) {
    ModalInstrument* self = new(ptrs.sram) ModalInstrument; // Allocate ModalInstrument in SRAM
    self->parameters = parameters;                          // Set parameter array
    self->parameterPages = &parameterPages;                 // Set parameter pages
    self->lastTrigger = 0.0f;                              // Initialize last trigger state
    self->lpState = 0.0f;                                  // Initialize lowpass filter state
    return self;
}

//--------------------------------------------------------------
// Compute a generic ADSR envelope (used for noise)
//--------------------------------------------------------------
float computeADSR(Envelope& env, int attack, int decay, float sustain, int release) {
    float value = 0.0f;
    switch (env.stage) {
        case 1: // Attack
            value = env.pos / (float)attack;
            if (++env.pos >= attack) { env.stage = 2; env.pos = 0; }
            break;
        case 2: // Decay
            value = 1.0f - (1.0f - sustain) * env.pos / (float)decay;
            if (++env.pos >= decay) { env.stage = 3; env.pos = 0; }
            break;
        case 3: // Sustain
            value = sustain;
            break;
        case 4: // Release
            value = env.env * (1.0f - env.pos / (float)release);
            if (++env.pos >= release) { env.stage = 0; value = 0.0f; }
            break;
        default: value = 0.0f; break;
    }
    env.env = value;
    return value;
}

//--------------------------------------------------------------
// Main processing loop
//--------------------------------------------------------------
extern "C" void step(_NT_algorithm* base, float* busFrames, int numFramesBy4) {
    ModalInstrument* self = static_cast<ModalInstrument*>(base); // Cast to our struct
    int numFrames = numFramesBy4 * 4;                            // Total number of frames

    // Read noise ADSR parameters from UI
    int noiseA = self->v[kParamNoiseAttack];
    int noiseD = self->v[kParamNoiseDecay];
    int noiseR = self->v[kParamNoiseRelease];
    float noiseS = self->v[kParamNoiseSustain] / 100.0f;
    float noiseLevel = self->v[kParamNoiseLevel] / 100.0f;

    // Get input and output buffers
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

    // Read other parameters
    float baseHzParam = self->v[kParamBaseFreq]; // Base frequency parameter
  
    bool inharmOn = self->v[kParamInharmEnable] > 0;
    float inharmAmt = self->v[kParamInharmLevel] / 100.0f;
    int instrType = self->v[kParamInstrumentType];
    int excTypeParam = self->v[kParamExcitationType];

    ModalConfig config = getModalConfig(instrType); // Get modal config for instrument
    float gateState = self->lastTrigger;            // Previous gate state

    for (int f = 0; f < numFrames; ++f) {
        // Calculate base frequency with CV and parameter
        float noteV = noteCV ? noteCV[f] : 0.0f;
        float noteFactor = powf(2.0f, noteV);
        float cvBase = (cvFreq && fabsf(cvFreq[f]) > 0.001f) ? cvFreq[f] : 0.0f;
        float baseHz = ((cvBase > 0.0f) ? fmaxf(cvBase, 20.0f) : baseHzParam) * noteFactor;
        baseHz = fmaxf(baseHz, 40.0f); // Never under 40 Hz
        // Calculate decay with CV and parameter
        float decayCV = (cvDecay ? cvDecay[f] : 0.0f);
        float decayMs = self->v[kParamDecay] + decayCV * 8000.0f; // CV skaliert auf 0...8s
        decayMs = fmaxf(decayMs, 100.0f);
        float decay = decayMs / 1000.0f;

        // Calculate excitation type with CV and parameter
        int excType = excTypeParam;
        if (cvExcit && fabsf(cvExcit[f]) > 0.01f) {
            excType = static_cast<int>(fminf(cvExcit[f] * 4.99f, 4.0f));
        }

        // Gate logic: detect rising edge
        float currentGate = trig[f];
        bool gateOn = (currentGate >= 0.5f);

        // On trigger (rising edge), allocate a voice and initialize
        if (!gateState && gateOn) {
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
            voice.excitation.generate(excType, instrType, inharmOn ? inharmAmt : 0.0f, noiseLevel, noiseA, noiseD, noiseS, noiseR);
            voice.noiseEnv.stage = 1; // Start noise envelope (attack)
            voice.noiseEnv.pos = 0;
            voice.noiseEnv.env = 0.0f;

            float dampingFactor = 1.0f;
            if (instrType == 3 || instrType == 4) dampingFactor = 0.7f; // Gong/Triangle
            else if (instrType == 8) decay *= 2.5f; // Timpani
            else if (instrType == 13) decay *= 2.0f; // Frame Drum

            float* ratios = config.ratios;
            float* gains = config.gains;
            for (int m = 0; m < config.count; ++m) {
                float freq = baseHz * ratios[m];
                if (inharmOn) {
                    static const float inharmonicOffset[MAX_MODES] = {-0.004f, +0.006f, -0.002f, +0.007f, -0.005f, +0.003f, -0.001f, +0.002f, +0.001f, -0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
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

        // On gate off, start noise envelope release for all active voices
        if (gateState && !gateOn) {
            for (int v = 0; v < NUM_VOICES; ++v) {
                if (self->voices[v].active) {
                    self->voices[v].noiseEnv.stage = 4; // Release
                    self->voices[v].noiseEnv.pos = 0;
                }
            }
        }

        gateState = gateOn; // Update previous gate state

        // Process all voices
        float sample = 0.0f;
        for (int v = 0; v < NUM_VOICES; ++v) {
            if (!self->voices[v].active) continue;
            float exc = self->voices[v].excitation.next(); // Get next excitation sample

            float sum = 0.0f;
            bool silent = true;
            for (int m = 0; m < config.count; ++m) {
                float s = self->voices[v].modes[m].process(exc, self->v[kParamResonatorType]);
                sum += s;
                if (fabsf(s) > 0.0005f) silent = false;
            }

            // --- CONTINUOUS NOISE LAYER ---
            // Compute noise envelope for this voice
            float noiseEnv = computeADSR(self->voices[v].noiseEnv, noiseA, noiseD, noiseS, noiseR);
            // Generate white noise sample in [-1, 1]
            float noise = (((rand() % 2000) / 1000.0f) - 1.0f) * noiseEnv * noiseLevel;
            // Add noise to output sample
            sample += noise;

            if (silent) self->voices[v].active = false; // Deactivate if silent
            sample += sum; // Add modal sum to output
            self->voices[v].age += 1.0f / SAMPLE_RATE; // Advance voice age
        }

        // Output lowpass filter for smoothing
        float alpha = expf(-2.0f * M_PI * 3000.0f / SAMPLE_RATE);
        sample = self->lpState + alpha * (sample - self->lpState);
        self->lpState = sample;

        // Write output (attenuated)
        outL[f] = sample * 0.1f;
        outR[f] = sample * 0.1f;
    }

    self->lastTrigger = gateState; // Store last trigger state
}

//--------------------------------------------------------------
// Required Disting NT API functions
//--------------------------------------------------------------
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