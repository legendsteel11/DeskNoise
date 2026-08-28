// audio.h - WASAPI shared-mode render engine and layer mixer
#pragma once
#include <windows.h>
#include <atomic>

enum SoundMode {
    MODE_SINE = 0,      // pure tone
    MODE_NARROWBAND,    // narrow-band noise
    MODE_WHITE,         // white noise
    MODE_PINK,          // pink noise
    MODE_BROWN          // brown noise
};

static const int kMaxLayers = 3;

// Bandwidth at or above this is treated as "no band filter at all", which is
// how a broadband noise keeps its original character.
static const float kBwOpen = 3.9f;

// One layer. Written by the UI thread, read by the audio thread.
struct LayerParams {
    std::atomic<bool>  enabled{ false };
    std::atomic<int>   mode{ MODE_SINE };
    std::atomic<float> freqHz{ 4000.0f };
    std::atomic<float> bandwidthOct{ 0.5f };
    std::atomic<float> gain{ 0.1f };     // linear gain (0..1)
    std::atomic<float> balance{ 0.0f };  // -1 left, 0 center, +1 right
    std::atomic<float> beatHz{ 0.0f };   // pure-tone beat rate; 0 disables it
};

struct AudioParams {
    LayerParams        layer[kMaxLayers];
    std::atomic<bool>  playing{ false };
    std::atomic<float> masterVol{ 1.0f };   // master volume (0..1)
};

class AudioEngine {
public:
    bool start();   // spin up the render thread
    void stop();    // stop the render thread and clean up

    void setPlaying(bool on);
    bool isDeviceOk()  const { return deviceOk_.load(); }
    bool isLimiting()  const { return limiting_.load(); }   // summed output is over the ceiling

    AudioParams params;

private:
    struct LayerState {
        double   phase = 0.0;
        double   phase2 = 0.0;   // second oscillator, used for beats
        float    curFreq = 4000.0f;
        float    curGain = 0.0f;
        unsigned rng = 1u;
        float    pink[7] = { 0 };
        float    brown = 0.0f;
        float    b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float    z1[2] = { 0 }, z2[2] = { 0 };
        float    nbNorm = 1.0f;
        int      coefCounter = 0;
        float    lastF = -1.0f, lastBw = -1.0f;
    };

    static DWORD WINAPI threadProc(LPVOID self);
    void  run();
    bool  openDevice();
    void  closeDevice();
    void  renderLoop();
    void  fill(BYTE* dst, UINT32 frames);
    void  resetDsp();
    void  updateBiquad(LayerState& s, float f0, float bwOct);
    float bandPass(LayerState& s, float x0, float bwOct);
    float genLayer(LayerState& s, int mode, float bwOct, float beatHz);

    HANDLE thread_ = nullptr;
    HANDLE evStop_ = nullptr;   // thread shutdown request
    HANDLE evWake_ = nullptr;   // play request while idle
    HANDLE evAudio_ = nullptr;  // WASAPI buffer request

    std::atomic<bool> deviceOk_{ false };
    std::atomic<bool> limiting_{ false };

    struct Device;
    Device* dev_ = nullptr;

    double     sr_ = 48000.0;
    float      aFreq_ = 0.0f, aGain_ = 0.0f;
    float      aLimAttack_ = 0.0f, aLimRelease_ = 0.0f;
    float      masterGain_ = 0.0f;   // play/stop fade
    float      curMaster_ = 1.0f;    // smoothed master volume
    float      limGain_ = 1.0f;      // limiter gain
    LayerState ls_[kMaxLayers];
};
