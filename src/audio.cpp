// audio.cpp - WASAPI shared-mode render thread and layer mixer
#include "audio.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>
#include <math.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

static const GUID kSubtypeFloat = { 0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };
static const GUID kSubtypePcm   = { 0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71} };

static const double kTwoPi = 6.283185307179586;
static const float  kCeiling = 0.95f;   // peak amplitude the limiter holds

struct AudioEngine::Device {
    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice*           dev = nullptr;
    IAudioClient*        client = nullptr;
    IAudioRenderClient*  render = nullptr;
    WAVEFORMATEX*        fmt = nullptr;
    LPWSTR               id = nullptr;
    UINT32               bufferFrames = 0;
    UINT32               channels = 2;
    bool                 isFloat = true;
};

// ---------------------------------------------------------------- format probing

static bool FormatIsFloat(const WAVEFORMATEX* f)
{
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* e = (const WAVEFORMATEXTENSIBLE*)f;
        return IsEqualGUID(e->SubFormat, kSubtypeFloat) != 0;
    }
    return false;
}

static bool FormatIsPcm16(const WAVEFORMATEX* f)
{
    if (f->wBitsPerSample != 16) return false;
    if (f->wFormatTag == WAVE_FORMAT_PCM) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* e = (const WAVEFORMATEXTENSIBLE*)f;
        return IsEqualGUID(e->SubFormat, kSubtypePcm) != 0;
    }
    return false;
}

// ---------------------------------------------------------------- lifetime

bool AudioEngine::start()
{
    if (thread_) return true;
    evStop_  = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
    evWake_  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    evAudio_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!evStop_ || !evWake_ || !evAudio_) return false;
    thread_ = CreateThread(nullptr, 0, &AudioEngine::threadProc, this, 0, nullptr);
    return thread_ != nullptr;
}

void AudioEngine::stop()
{
    if (thread_) {
        SetEvent(evStop_);
        WaitForSingleObject(thread_, 3000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (evStop_)  { CloseHandle(evStop_);  evStop_ = nullptr; }
    if (evWake_)  { CloseHandle(evWake_);  evWake_ = nullptr; }
    if (evAudio_) { CloseHandle(evAudio_); evAudio_ = nullptr; }
}

void AudioEngine::setPlaying(bool on)
{
    params.playing.store(on);
    if (evWake_) SetEvent(evWake_);
}

DWORD WINAPI AudioEngine::threadProc(LPVOID self)
{
    ((AudioEngine*)self)->run();
    return 0;
}

void AudioEngine::run()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        if (WaitForSingleObject(evStop_, 0) == WAIT_OBJECT_0) break;

        if (openDevice()) {
            deviceOk_.store(true);
            renderLoop();
            deviceOk_.store(false);
            closeDevice();
            // Reopening immediately after a device switch can fail, so pause briefly.
            if (WaitForSingleObject(evStop_, 250) == WAIT_OBJECT_0) break;
        }
        else {
            deviceOk_.store(false);
            closeDevice();
            // No usable render device. Retry every 1.5 seconds.
            if (WaitForSingleObject(evStop_, 1500) == WAIT_OBJECT_0) break;
        }
    }
    CoUninitialize();
}

// ---------------------------------------------------------------- device

bool AudioEngine::openDevice()
{
    closeDevice();
    dev_ = new Device();
    HRESULT hr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&dev_->enumr);
    if (FAILED(hr)) return false;

    hr = dev_->enumr->GetDefaultAudioEndpoint(eRender, eConsole, &dev_->dev);
    if (FAILED(hr)) return false;

    dev_->dev->GetId(&dev_->id);

    hr = dev_->dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&dev_->client);
    if (FAILED(hr)) return false;

    hr = dev_->client->GetMixFormat(&dev_->fmt);
    if (FAILED(hr) || !dev_->fmt) return false;

    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    hr = dev_->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, dev_->fmt, nullptr);

    if (FAILED(hr)) {
        // If the mix format is rejected, ask for 32-bit float stereo with conversion.
        WAVEFORMATEX want = {};
        want.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        want.nChannels       = 2;
        want.nSamplesPerSec  = dev_->fmt->nSamplesPerSec ? dev_->fmt->nSamplesPerSec : 48000;
        want.wBitsPerSample  = 32;
        want.nBlockAlign     = (want.nChannels * want.wBitsPerSample) / 8;
        want.nAvgBytesPerSec = want.nSamplesPerSec * want.nBlockAlign;
        want.cbSize          = 0;

        CoTaskMemFree(dev_->fmt);
        dev_->fmt = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        if (!dev_->fmt) return false;
        *dev_->fmt = want;

        hr = dev_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                flags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                0, 0, dev_->fmt, nullptr);
        if (FAILED(hr)) return false;
    }

    dev_->channels = dev_->fmt->nChannels;
    dev_->isFloat  = FormatIsFloat(dev_->fmt);
    if (!dev_->isFloat && !FormatIsPcm16(dev_->fmt)) return false;  // unsupported format

    hr = dev_->client->SetEventHandle(evAudio_);
    if (FAILED(hr)) return false;

    hr = dev_->client->GetBufferSize(&dev_->bufferFrames);
    if (FAILED(hr)) return false;

    hr = dev_->client->GetService(__uuidof(IAudioRenderClient), (void**)&dev_->render);
    if (FAILED(hr)) return false;

    sr_          = (double)dev_->fmt->nSamplesPerSec;
    aFreq_       = 1.0f - (float)exp(-1.0 / (0.030 * sr_));   // 30ms frequency glide
    aGain_       = 1.0f - (float)exp(-1.0 / (0.040 * sr_));   // 40ms gain glide
    aLimAttack_  = 1.0f - (float)exp(-1.0 / (0.002 * sr_));   // 2ms limiter attack
    aLimRelease_ = 1.0f - (float)exp(-1.0 / (0.200 * sr_));   // 200ms limiter release
    resetDsp();
    return true;
}

void AudioEngine::closeDevice()
{
    if (!dev_) return;
    if (dev_->render) dev_->render->Release();
    if (dev_->client) dev_->client->Release();
    if (dev_->fmt)    CoTaskMemFree(dev_->fmt);
    if (dev_->id)     CoTaskMemFree(dev_->id);
    if (dev_->dev)    dev_->dev->Release();
    if (dev_->enumr)  dev_->enumr->Release();
    delete dev_;
    dev_ = nullptr;
}

// Detect a default-device change by polling, rather than implementing the callback interface.
static bool DefaultDeviceChanged(IMMDeviceEnumerator* en, LPCWSTR curId)
{
    if (!en || !curId) return false;
    IMMDevice* d = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d)) || !d) return true;
    LPWSTR id = nullptr;
    bool changed = true;
    if (SUCCEEDED(d->GetId(&id)) && id) {
        changed = (lstrcmpW(id, curId) != 0);
        CoTaskMemFree(id);
    }
    d->Release();
    return changed;
}

void AudioEngine::renderLoop()
{
    DWORD taskIndex = 0;
    HANDLE mmTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    bool running = false;
    ULONGLONG lastDevCheck = GetTickCount64();

    for (;;) {
        const bool want = params.playing.load() || masterGain_ > 1.0e-5f;

        if (want && !running) {
            // Priming the buffer before Start avoids a gap on the first frames.
            BYTE* p = nullptr;
            if (SUCCEEDED(dev_->render->GetBuffer(dev_->bufferFrames, &p)) && p) {
                fill(p, dev_->bufferFrames);
                dev_->render->ReleaseBuffer(dev_->bufferFrames, 0);
            }
            if (FAILED(dev_->client->Start())) break;
            running = true;
        }
        else if (!want && running) {
            dev_->client->Stop();
            dev_->client->Reset();
            ResetEvent(evAudio_);
            resetDsp();
            limiting_.store(false);
            running = false;
        }

        HANDLE waits[3] = { evStop_, evWake_, evAudio_ };
        const DWORD count = running ? 3 : 2;
        // Wake less often while idle so the tray-resident app stays cheap.
        const DWORD r = WaitForMultipleObjects(count, waits, FALSE, running ? 2000 : 5000);

        if (r == WAIT_OBJECT_0)     break;      // shutdown requested
        if (r == WAIT_OBJECT_0 + 1) continue;   // play requested; re-evaluate above

        if (running && r == WAIT_OBJECT_0 + 2) {
            UINT32 padding = 0;
            if (FAILED(dev_->client->GetCurrentPadding(&padding))) break;
            const UINT32 avail = dev_->bufferFrames - padding;
            if (avail > 0) {
                BYTE* p = nullptr;
                if (FAILED(dev_->render->GetBuffer(avail, &p)) || !p) break;
                fill(p, avail);
                dev_->render->ReleaseBuffer(avail, 0);
            }
        }
        else if (running && r == WAIT_TIMEOUT) {
            break;  // No event for 2 seconds. Treat the device as dead and rebuild.
        }

        const ULONGLONG now = GetTickCount64();
        if (now - lastDevCheck >= (running ? 1000u : 5000u)) {
            lastDevCheck = now;
            if (DefaultDeviceChanged(dev_->enumr, dev_->id)) break;
        }
    }

    if (running) { dev_->client->Stop(); dev_->client->Reset(); }
    limiting_.store(false);
    if (mmTask) AvRevertMmThreadCharacteristics(mmTask);
}

// ---------------------------------------------------------------- generators

void AudioEngine::resetDsp()
{
    masterGain_ = 0.0f;
    curMaster_ = params.masterVol.load();
    limGain_ = 1.0f;
    for (int i = 0; i < kMaxLayers; i++) {
        LayerState& s = ls_[i];
        s.phase = 0.0;
        s.phase2 = 0.0;
        s.curGain = 0.0f;
        s.curFreq = params.layer[i].freqHz.load();
        s.brown = 0.0f;
        for (int k = 0; k < 7; k++) s.pink[k] = 0.0f;
        s.z1[0] = s.z1[1] = s.z2[0] = s.z2[1] = 0.0f;
        s.lastF = s.lastBw = -1.0f;
        s.coefCounter = 0;
        // Seed every layer differently. Sharing one noise source correlates the
        // outputs, and several narrow bands then fuse into a single voice.
        s.rng = 0x9E3779B9u * (unsigned)(i + 1) + 0x1234567u;
    }
}

void AudioEngine::updateBiquad(LayerState& s, float f0, float bwOct)
{
    if (f0 == s.lastF && bwOct == s.lastBw) return;
    s.lastF = f0; s.lastBw = bwOct;

    if (f0 < 20.0f) f0 = 20.0f;
    const double nyq = sr_ * 0.5;
    if (f0 > nyq * 0.94) f0 = (float)(nyq * 0.94);
    if (bwOct < 0.02f) bwOct = 0.02f;

    const double w0 = kTwoPi * f0 / sr_;
    const double sn = sin(w0), cs = cos(w0);
    const double alpha = sn * sinh(0.34657359027997264 * bwOct * w0 / sn);  // ln(2)/2
    const double a0 = 1.0 + alpha;

    s.b0 = (float)(alpha / a0);
    s.b1 = 0.0f;
    s.b2 = (float)(-alpha / a0);
    s.a1 = (float)(-2.0 * cs / a0);
    s.a2 = (float)((1.0 - alpha) / a0);

    // A narrower band puts out less energy, so compensate for perceived loudness.
    const float bwHz = f0 * (powf(2.0f, bwOct * 0.5f) - powf(2.0f, -bwOct * 0.5f));
    float n = 0.42f * sqrtf((float)nyq / (bwHz < 1.0f ? 1.0f : bwHz));
    if (n < 0.2f)  n = 0.2f;
    if (n > 25.0f) n = 25.0f;
    s.nbNorm = n;
}

// Two cascaded band-pass sections around the layer frequency. Any noise source
// can be run through it; a fully open bandwidth passes the sample untouched so
// a broadband noise keeps its own character.
float AudioEngine::bandPass(LayerState& s, float x0, float bwOct)
{
    if (bwOct >= kBwOpen) return x0;
    if (--s.coefCounter <= 0) { s.coefCounter = 32; updateBiquad(s, s.curFreq, bwOct); }
    float y = x0;
    for (int k = 0; k < 2; k++) {
        const float x = y;
        y = s.b0 * x + s.z1[k];
        s.z1[k] = s.b1 * x - s.a1 * y + s.z2[k];
        s.z2[k] = s.b2 * x - s.a2 * y;
    }
    return y * s.nbNorm;
}

float AudioEngine::genLayer(LayerState& s, int mode, float bwOct, float beatHz)
{
    // white noise (xorshift32)
    s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
    const float w = (float)(int)s.rng * (1.0f / 2147483648.0f);

    switch (mode) {
    case MODE_SINE:
        if (beatHz > 0.0f) {
            // Sum two tones spread half the beat rate above and below center.
            // Their difference is what the listener hears as the beat.
            const double half = 0.5 * (double)beatHz;
            s.phase  += kTwoPi * ((double)s.curFreq - half) / sr_;
            s.phase2 += kTwoPi * ((double)s.curFreq + half) / sr_;
            if (s.phase >= kTwoPi) s.phase -= kTwoPi;
            if (s.phase < 0.0)     s.phase += kTwoPi;
            if (s.phase2 >= kTwoPi) s.phase2 -= kTwoPi;
            return 0.5f * ((float)sin(s.phase) + (float)sin(s.phase2));
        }
        s.phase += kTwoPi * (double)s.curFreq / sr_;
        if (s.phase >= kTwoPi) s.phase -= kTwoPi;
        return (float)sin(s.phase);

    case MODE_NARROWBAND:
        return bandPass(s, w, bwOct);

    case MODE_WHITE:
        return bandPass(s, w * 0.5f, bwOct);

    case MODE_PINK: {
        s.pink[0] = 0.99886f * s.pink[0] + w * 0.0555179f;
        s.pink[1] = 0.99332f * s.pink[1] + w * 0.0750759f;
        s.pink[2] = 0.96900f * s.pink[2] + w * 0.1538520f;
        s.pink[3] = 0.86650f * s.pink[3] + w * 0.3104856f;
        s.pink[4] = 0.55000f * s.pink[4] + w * 0.5329522f;
        s.pink[5] = -0.7616f * s.pink[5] - w * 0.0168980f;
        const float out = (s.pink[0] + s.pink[1] + s.pink[2] + s.pink[3]
                         + s.pink[4] + s.pink[5] + s.pink[6] + w * 0.5362f) * 0.16f;
        s.pink[6] = w * 0.115926f;
        return bandPass(s, out, bwOct);
    }

    case MODE_BROWN:
        s.brown = (s.brown + 0.02f * w) / 1.02f;
        return bandPass(s, s.brown * 3.5f, bwOct);
    }
    return 0.0f;
}

void AudioEngine::fill(BYTE* dst, UINT32 frames)
{
    // Read the per-layer constants once, outside the frame loop.
    struct LP { int mode; float f, bw, g, gl, gr, beat; } lp[kMaxLayers];
    for (int i = 0; i < kMaxLayers; i++) {
        const LayerParams& src = params.layer[i];
        LP& p = lp[i];
        p.mode = src.mode.load(std::memory_order_relaxed);
        p.f    = src.freqHz.load(std::memory_order_relaxed);
        p.bw   = src.bandwidthOct.load(std::memory_order_relaxed);
        p.beat = src.beatHz.load(std::memory_order_relaxed);
        p.g    = src.enabled.load(std::memory_order_relaxed)
                 ? src.gain.load(std::memory_order_relaxed) : 0.0f;
        // Pan linearly. Constant-power panning would drop both sides to 0.707 at
        // center, a 3dB loss that does not suit a masking tone.
        const float bal = src.balance.load(std::memory_order_relaxed);
        p.gl = (bal <= 0.0f) ? 1.0f : (1.0f - bal);
        p.gr = (bal >= 0.0f) ? 1.0f : (1.0f + bal);
    }

    const float masterTarget = params.playing.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    // Keep master volume separate from the play/stop fade. Folding them together
    // would release the audio device at zero volume and delay the next ramp-up.
    const float volTarget = params.masterVol.load(std::memory_order_relaxed);

    const UINT32 ch = dev_->channels;
    float* fdst = (float*)dst;
    short* sdst = (short*)dst;

    for (UINT32 i = 0; i < frames; i++) {
        masterGain_ += (masterTarget - masterGain_) * aGain_;
        curMaster_ += (volTarget - curMaster_) * aGain_;

        float sumL = 0.0f, sumR = 0.0f;
        for (int k = 0; k < kMaxLayers; k++) {
            LayerState& s = ls_[k];
            s.curGain += (lp[k].g - s.curGain) * aGain_;
            // Skip layers that are off and have finished fading out.
            if (lp[k].g == 0.0f && s.curGain < 1.0e-6f) continue;
            s.curFreq += (lp[k].f - s.curFreq) * aFreq_;
            const float v = genLayer(s, lp[k].mode, lp[k].bw, lp[k].beat) * s.curGain;
            sumL += v * lp[k].gl;
            sumR += v * lp[k].gr;
        }

        const float mg = masterGain_ * curMaster_;
        sumL *= mg;
        sumR *= mg;

        // Output limiter. Stacked layers push the sum past 1.0 easily.
        const float aL = fabsf(sumL), aR = fabsf(sumR);
        const float mag = aL > aR ? aL : aR;
        const float need = (mag > kCeiling) ? (kCeiling / mag) : 1.0f;
        limGain_ += (need < limGain_) ? (need - limGain_) * aLimAttack_
                                      : (need - limGain_) * aLimRelease_;
        sumL *= limGain_;
        sumR *= limGain_;

        if (sumL > 0.99f) sumL = 0.99f; else if (sumL < -0.99f) sumL = -0.99f;
        if (sumR > 0.99f) sumR = 0.99f; else if (sumR < -0.99f) sumR = -0.99f;

        if (dev_->isFloat) {
            float* p = fdst + (size_t)i * ch;
            p[0] = sumL;
            if (ch > 1) p[1] = sumR;
            for (UINT32 c = 2; c < ch; c++) p[c] = 0.0f;
        }
        else {
            short* p = sdst + (size_t)i * ch;
            p[0] = (short)(sumL * 32767.0f);
            if (ch > 1) p[1] = (short)(sumR * 32767.0f);
            for (UINT32 c = 2; c < ch; c++) p[c] = 0;
        }
    }

    // Tell the UI when the limiter is actually pulling gain down.
    limiting_.store(limGain_ < 0.97f);
}
