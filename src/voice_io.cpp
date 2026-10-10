

#include "voice_io.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

#if defined(COOP_VOICE)
#include <opus.h>
#if defined(COOP_VOICE_SDL)
#include <SDL3/SDL_audio.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#elif defined(__ANDROID__)
#include <aaudio/AAudio.h>
#endif
#endif

namespace voice_io {

namespace {
const char* const kDefaultMic = "Default Microphone";
}

#if defined(COOP_VOICE)

namespace {

constexpr int kRate = 48000;
constexpr int kFrame = 960;
constexpr int kMaxOpus = 400;
constexpr int kBitrate = 16000;
constexpr float kLoudRms = 0.012f;
constexpr int kLeadFrames = 2;
constexpr int kMaxBehindFrames = 12;
constexpr int kMaxConcealed = 2;

class Speakers {
public:
    virtual ~Speakers() = default;
    virtual bool add(const std::string& who) = 0;
    virtual void remove(const std::string& who) = 0;
    virtual void put(const std::string& who, const int16_t* stereo, int frames) = 0;
    virtual int queued(const std::string& who) = 0;
    virtual void clear(const std::string& who) = 0;
    virtual void set_gain(float gain) = 0;
};

class Mixer : public Speakers {
public:
    bool add(const std::string& who) override {
        std::lock_guard lock(mutex_);
        lanes_[who];
        return true;
    }
    void remove(const std::string& who) override {
        std::lock_guard lock(mutex_);
        lanes_.erase(who);
    }
    void put(const std::string& who, const int16_t* stereo, int frames) override {
        std::lock_guard lock(mutex_);
        auto it = lanes_.find(who);
        if (it != lanes_.end()) it->second.buf.insert(it->second.buf.end(), stereo, stereo + frames * 2);
    }
    int queued(const std::string& who) override {
        std::lock_guard lock(mutex_);
        auto it = lanes_.find(who);
        return it != lanes_.end() ? static_cast<int>(it->second.buf.size() / 2) : 0;
    }
    void clear(const std::string& who) override {
        std::lock_guard lock(mutex_);
        auto it = lanes_.find(who);
        if (it != lanes_.end()) {
            it->second.buf.clear();
            it->second.frac = 0.0;
        }
    }
    void set_gain(float gain) override {
        std::lock_guard lock(mutex_);
        gain_ = gain;
    }

    void pull(int16_t* out, int n, int rate) {
        std::lock_guard lock(mutex_);
        const double step = rate > 0 ? static_cast<double>(kRate) / rate : 1.0;
        for (int i = 0; i < n; ++i) {
            float left = 0.0f, right = 0.0f;
            for (auto& [who, lane] : lanes_) {
                if (lane.buf.size() < 2) continue;
                const bool next = lane.buf.size() >= 4;
                const float t = static_cast<float>(lane.frac);
                const float l0 = lane.buf[0], r0 = lane.buf[1];
                const float l1 = next ? lane.buf[2] : l0, r1 = next ? lane.buf[3] : r0;
                left += l0 + (l1 - l0) * t;
                right += r0 + (r1 - r0) * t;
                lane.frac += step;
                while (lane.frac >= 1.0 && lane.buf.size() >= 2) {
                    lane.buf.pop_front();
                    lane.buf.pop_front();
                    lane.frac -= 1.0;
                }
            }
            out[i * 2] = static_cast<int16_t>(std::clamp(left * gain_, -32768.0f, 32767.0f));
            out[i * 2 + 1] = static_cast<int16_t>(std::clamp(right * gain_, -32768.0f, 32767.0f));
        }
    }

private:
    struct Lane {
        std::deque<int16_t> buf;
        double frac = 0.0;
    };
    std::mutex mutex_;
    std::map<std::string, Lane> lanes_;
    float gain_ = 1.0f;
};

#if defined(__APPLE__) && !defined(COOP_VOICE_SDL)

class AppleSpeakers : public Mixer {
public:
    bool open() {
        AudioStreamBasicDescription f{};
        f.mSampleRate = kRate;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        f.mBytesPerPacket = 4;
        f.mFramesPerPacket = 1;
        f.mBytesPerFrame = 4;
        f.mChannelsPerFrame = 2;
        f.mBitsPerChannel = 16;
        if (AudioQueueNewOutput(&f, &AppleSpeakers::fill, this, nullptr, nullptr, 0, &queue_) != noErr) {
            queue_ = nullptr;
            return false;
        }
        for (AudioQueueBufferRef& b : buffers_) {
            if (AudioQueueAllocateBuffer(queue_, kFrame * 4, &b) != noErr) return false;
            fill(this, queue_, b);
        }
        return AudioQueueStart(queue_, nullptr) == noErr;
    }
    ~AppleSpeakers() override {
        if (queue_ != nullptr) {
            AudioQueueStop(queue_, true);
            AudioQueueDispose(queue_, true);
        }
    }

private:
    static void fill(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer) {
        auto* self = static_cast<AppleSpeakers*>(user);
        const int n = static_cast<int>(buffer->mAudioDataBytesCapacity / 4);
        self->pull(static_cast<int16_t*>(buffer->mAudioData), n, kRate);
        buffer->mAudioDataByteSize = static_cast<UInt32>(n * 4);
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }
    AudioQueueRef queue_ = nullptr;
    AudioQueueBufferRef buffers_[3] = {};
};
#endif

#if defined(__ANDROID__) && !defined(COOP_VOICE_SDL)

class AndroidSpeakers : public Mixer {
public:
    bool open() {
        AAudioStreamBuilder* builder = nullptr;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setSampleRate(builder, kRate);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setDataCallback(builder, &AndroidSpeakers::fill, this);
        const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream_);
        AAudioStreamBuilder_delete(builder);
        if (opened != AAUDIO_OK || stream_ == nullptr) {
            stream_ = nullptr;
            return false;
        }
        rate_ = AAudioStream_getSampleRate(stream_);
        channels_ = AAudioStream_getChannelCount(stream_);
        return AAudioStream_requestStart(stream_) == AAUDIO_OK;
    }
    ~AndroidSpeakers() override {
        if (stream_ != nullptr) {
            AAudioStream_requestStop(stream_);
            AAudioStream_close(stream_);
        }
    }

private:
    static aaudio_data_callback_result_t fill(AAudioStream*, void* user, void* data, int32_t frames) {
        auto* self = static_cast<AndroidSpeakers*>(user);
        auto* out = static_cast<int16_t*>(data);
        if (self->channels_ == 2) {
            self->pull(out, frames, self->rate_);
        } else {

            static thread_local std::vector<int16_t> stereo;
            stereo.resize(static_cast<size_t>(frames) * 2);
            self->pull(stereo.data(), frames, self->rate_);
            const int32_t ch = std::max<int32_t>(self->channels_, 1);
            for (int32_t i = 0; i < frames; ++i) {
                const int16_t l = stereo[i * 2], r = stereo[i * 2 + 1];
                if (ch == 1) {
                    out[i] = static_cast<int16_t>((static_cast<int>(l) + r) / 2);
                    continue;
                }
                for (int32_t c = 0; c < ch; ++c) out[i * ch + c] = c == 0 ? l : c == 1 ? r : 0;
            }
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    AAudioStream* stream_ = nullptr;
    int32_t rate_ = kRate;
    int32_t channels_ = 2;
};
#endif

#if defined(COOP_VOICE_SDL)
struct Sdl {
    decltype(&SDL_GetAudioRecordingDevices) recordingDevices = nullptr;
    decltype(&SDL_GetAudioDeviceName) deviceName = nullptr;
    decltype(&SDL_OpenAudioDeviceStream) openDeviceStream = nullptr;
    decltype(&SDL_ResumeAudioStreamDevice) resumeStreamDevice = nullptr;
    decltype(&SDL_OpenAudioDevice) openDevice = nullptr;
    decltype(&SDL_ResumeAudioDevice) resumeDevice = nullptr;
    decltype(&SDL_CloseAudioDevice) closeDevice = nullptr;
    decltype(&SDL_CreateAudioStream) createStream = nullptr;
    decltype(&SDL_BindAudioStream) bindStream = nullptr;
    decltype(&SDL_DestroyAudioStream) destroyStream = nullptr;
    decltype(&SDL_GetAudioStreamAvailable) available = nullptr;
    decltype(&SDL_GetAudioStreamData) read = nullptr;
    decltype(&SDL_PutAudioStreamData) write = nullptr;
    decltype(&SDL_GetAudioStreamQueued) queued = nullptr;
    decltype(&SDL_ClearAudioStream) clear = nullptr;
    decltype(&SDL_SetAudioStreamGain) gain = nullptr;
    decltype(&SDL_free) free = nullptr;
    bool ok = false;
};

void* find_sdl(const char* name) {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleA("SDL3.dll");
    return module != nullptr ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
#else
    if (void* inGame = dlsym(RTLD_DEFAULT, name)) return inGame;
    void* lib = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD);
    return lib != nullptr ? dlsym(lib, name) : nullptr;
#endif
}

template <typename Fn>
bool look_up(Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(find_sdl(name));
    return fn != nullptr;
}

const Sdl& sdl() {
    static Sdl s = [] {
        Sdl out;
        bool all = true;
        all &= look_up(out.recordingDevices, "SDL_GetAudioRecordingDevices");
        all &= look_up(out.deviceName, "SDL_GetAudioDeviceName");
        all &= look_up(out.openDeviceStream, "SDL_OpenAudioDeviceStream");
        all &= look_up(out.resumeStreamDevice, "SDL_ResumeAudioStreamDevice");
        all &= look_up(out.openDevice, "SDL_OpenAudioDevice");
        all &= look_up(out.resumeDevice, "SDL_ResumeAudioDevice");
        all &= look_up(out.closeDevice, "SDL_CloseAudioDevice");
        all &= look_up(out.createStream, "SDL_CreateAudioStream");
        all &= look_up(out.bindStream, "SDL_BindAudioStream");
        all &= look_up(out.destroyStream, "SDL_DestroyAudioStream");
        all &= look_up(out.available, "SDL_GetAudioStreamAvailable");
        all &= look_up(out.read, "SDL_GetAudioStreamData");
        all &= look_up(out.write, "SDL_PutAudioStreamData");
        all &= look_up(out.queued, "SDL_GetAudioStreamQueued");
        all &= look_up(out.clear, "SDL_ClearAudioStream");
        all &= look_up(out.gain, "SDL_SetAudioStreamGain");
        all &= look_up(out.free, "SDL_free");
        out.ok = all;
        return out;
    }();
    return s;
}

const SDL_AudioSpec kSpec = {SDL_AUDIO_S16, 1, kRate};
const SDL_AudioSpec kPlaySpec = {SDL_AUDIO_S16, 2, kRate};

class SdlSpeakers : public Speakers {
public:
    bool open() {
        device_ = sdl().openDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kPlaySpec);
        if (device_ == 0) return false;
        sdl().resumeDevice(device_);
        return true;
    }
    ~SdlSpeakers() override {
        for (auto& [who, stream] : streams_) sdl().destroyStream(stream);
        if (device_ != 0) sdl().closeDevice(device_);
    }
    bool add(const std::string& who) override {
        SDL_AudioStream* stream = sdl().createStream(&kPlaySpec, &kPlaySpec);
        if (stream == nullptr) return false;
        if (!sdl().bindStream(device_, stream)) {
            sdl().destroyStream(stream);
            return false;
        }
        sdl().gain(stream, gain_);
        streams_[who] = stream;
        return true;
    }
    void remove(const std::string& who) override {
        auto it = streams_.find(who);
        if (it == streams_.end()) return;
        sdl().destroyStream(it->second);
        streams_.erase(it);
    }
    void put(const std::string& who, const int16_t* stereo, int frames) override {
        if (SDL_AudioStream* s = find(who)) sdl().write(s, stereo, frames * 4);
    }
    int queued(const std::string& who) override {
        SDL_AudioStream* s = find(who);
        return s != nullptr ? sdl().queued(s) / 4 : 0;
    }
    void clear(const std::string& who) override {
        if (SDL_AudioStream* s = find(who)) sdl().clear(s);
    }
    void set_gain(float gain) override {
        gain_ = gain;
        for (auto& [who, stream] : streams_) sdl().gain(stream, gain);
    }

private:
    SDL_AudioStream* find(const std::string& who) {
        auto it = streams_.find(who);
        return it != streams_.end() ? it->second : nullptr;
    }
    SDL_AudioDeviceID device_ = 0;
    std::map<std::string, SDL_AudioStream*> streams_;
    float gain_ = 1.0f;
};
#endif

bool platform_ready() {
#if defined(COOP_VOICE_SDL)
    return sdl().ok;
#else
    return true;
#endif
}

std::unique_ptr<Speakers> open_speakers() {
#if defined(COOP_VOICE_SDL)
    auto out = std::make_unique<SdlSpeakers>();
#elif defined(__APPLE__)
    auto out = std::make_unique<AppleSpeakers>();
#elif defined(__ANDROID__)
    auto out = std::make_unique<AndroidSpeakers>();
#endif
    if (!out->open()) return nullptr;
    return out;
}

struct Decoder {
    OpusDecoder* opus = nullptr;
    uint32_t lastSeq = 0;
};

}

struct Device::State {
    std::string problem;
    bool on = false;
    bool micOn = false;
    std::string mic;
    float micGain = 1.0f;
    float outGain = 1.0f;

    std::unique_ptr<Speakers> speakers;
    std::chrono::steady_clock::time_point lastSpeakers{};
    std::map<std::string, Decoder> voices;

#if defined(COOP_VOICE_SDL)
    SDL_AudioStream* capture = nullptr;
    std::string captureMic;
    std::chrono::steady_clock::time_point lastOpen{};
    OpusEncoder* encoder = nullptr;
    std::vector<int16_t> pending;
    uint32_t seq = 0;

    void close_capture() {
        if (capture != nullptr) sdl().destroyStream(capture);
        capture = nullptr;
        captureMic.clear();
        pending.clear();
    }

    SDL_AudioDeviceID mic_id(const std::string& name) {
        if (name.empty() || name == kDefaultMic) return SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        int count = 0;
        SDL_AudioDeviceID* ids = sdl().recordingDevices(&count);
        SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        for (int i = 0; ids != nullptr && i < count; ++i) {
            const char* n = sdl().deviceName(ids[i]);
            if (n != nullptr && name == n) found = ids[i];
        }
        if (ids != nullptr) sdl().free(ids);
        return found;
    }

    void open_capture() {
        close_capture();
        lastOpen = std::chrono::steady_clock::now();
        if (encoder == nullptr) {
            int err = 0;
            encoder = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
            if (encoder == nullptr) {
                problem = "Couldn't start the voice encoder";
                return;
            }
            opus_encoder_ctl(encoder, OPUS_SET_BITRATE(kBitrate));
            opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
            opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
        }
        capture = sdl().openDeviceStream(mic_id(mic), &kSpec, nullptr, nullptr);
        if (capture == nullptr) {
            problem = "No microphone found";
            return;
        }
        sdl().resumeStreamDevice(capture);
        captureMic = mic;
    }
#endif

    void drop_voice(const std::string& who) {
        auto it = voices.find(who);
        if (it == voices.end()) return;
        if (it->second.opus != nullptr) opus_decoder_destroy(it->second.opus);
        if (speakers) speakers->remove(who);
        voices.erase(it);
    }

    void close_speakers() {
        while (!voices.empty()) drop_voice(voices.begin()->first);
        speakers.reset();
    }
};

Device::Device() : s_(std::make_unique<State>()) {}

Device::~Device() {
    close();
}

std::vector<std::string> Device::microphones() {
    std::vector<std::string> out{kDefaultMic};
#if defined(COOP_VOICE_SDL)
    if (!sdl().ok) return out;
    int count = 0;
    SDL_AudioDeviceID* ids = sdl().recordingDevices(&count);
    for (int i = 0; ids != nullptr && i < count; ++i) {
        const char* n = sdl().deviceName(ids[i]);
        if (n != nullptr && n[0] != '\0') out.emplace_back(n);
    }
    if (ids != nullptr) sdl().free(ids);
#endif
    return out;
}

void Device::set(bool on, bool micOn, const std::string& mic, float micGain, float outGain) {
    State& s = *s_;
    if (!platform_ready()) {
        s.problem = "Voice isn't available here";
        return;
    }
    s.micGain = std::clamp(micGain, 0.0f, 2.0f);
    s.outGain = std::clamp(outGain, 0.0f, 2.0f);
    if (!on) {
        if (s.on) close();
        s.on = false;
        s.problem.clear();
        return;
    }
    s.on = true;
    s.mic = mic;
    const auto now = std::chrono::steady_clock::now();
#if defined(COOP_VOICE_SDL)
    const bool retry = micOn && s.capture == nullptr && now - s.lastOpen > std::chrono::seconds(3);
    if (s.micOn != micOn || retry || (micOn && s.capture != nullptr && s.captureMic != mic)) {
        s.micOn = micOn;
        s.problem.clear();
        if (micOn) {
            s.open_capture();
        } else {
            s.close_capture();
        }
    }
#else
    (void)micOn;
#endif
    if (!s.speakers && now - s.lastSpeakers > std::chrono::seconds(3)) {
        s.lastSpeakers = now;
        s.speakers = open_speakers();
        if (!s.speakers) s.problem = "Couldn't open the speakers";
    }
    if (s.speakers) s.speakers->set_gain(s.outGain);
}

std::vector<Frame> Device::take() {
    std::vector<Frame> out;
#if defined(COOP_VOICE_SDL)
    State& s = *s_;
    if (s.capture == nullptr || s.encoder == nullptr) return out;
    int bytes = sdl().available(s.capture);
    while (bytes >= 2) {
        int16_t chunk[kFrame];
        const int want = std::min(bytes, static_cast<int>(sizeof(chunk))) & ~1;
        const int got = sdl().read(s.capture, chunk, want);
        if (got <= 0) break;
        s.pending.insert(s.pending.end(), chunk, chunk + got / 2);
        bytes -= got;
    }
    size_t at = 0;
    while (s.pending.size() - at >= static_cast<size_t>(kFrame)) {
        int16_t pcm[kFrame];
        double energy = 0.0;
        for (int i = 0; i < kFrame; ++i) {
            const float v = std::clamp(static_cast<float>(s.pending[at + i]) * s.micGain, -32768.0f, 32767.0f);
            pcm[i] = static_cast<int16_t>(v);
            const double n = v / 32768.0;
            energy += n * n;
        }
        at += kFrame;
        Frame f;
        f.opus.resize(kMaxOpus);
        const int n = opus_encode(s.encoder, pcm, kFrame, f.opus.data(), kMaxOpus);
        if (n <= 0) continue;
        f.opus.resize(static_cast<size_t>(n));
        f.seq = ++s.seq;
        f.loud = std::sqrt(energy / kFrame) > kLoudRms;
        out.push_back(std::move(f));
    }
    s.pending.erase(s.pending.begin(), s.pending.begin() + static_cast<std::ptrdiff_t>(at));

    if (s.pending.size() > static_cast<size_t>(kFrame * 10)) s.pending.clear();
#endif
    return out;
}

void Device::play(const std::string& who, uint32_t seq, const uint8_t* opus, size_t size, float gain, float pan) {
    State& s = *s_;
    if (!s.on || !s.speakers || opus == nullptr || size == 0 || size > kMaxOpus) return;
    auto found = s.voices.find(who);
    if (found == s.voices.end()) {
        int err = 0;
        Decoder d;
        d.opus = opus_decoder_create(kRate, 1, &err);
        if (d.opus == nullptr) return;
        if (!s.speakers->add(who)) {
            opus_decoder_destroy(d.opus);
            return;
        }
        found = s.voices.emplace(who, d).first;
    }
    Decoder& v = found->second;

    if (v.lastSeq != 0 && static_cast<int32_t>(seq - v.lastSeq) <= 0) return;
    const uint32_t missing = v.lastSeq == 0 ? 0 : seq - v.lastSeq - 1;
    v.lastSeq = seq;

    const int queued = s.speakers->queued(who);
    if (queued > kMaxBehindFrames * kFrame) s.speakers->clear(who);
    if (queued <= 0) {
        static const int16_t kSilence[kFrame * kLeadFrames * 2] = {};
        s.speakers->put(who, kSilence, kFrame * kLeadFrames);
    }

    const float level = std::clamp(gain, 0.0f, 1.0f);
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
    const float toLeft = level * std::clamp(std::cos(angle) * 1.41421356f, 0.2f, 1.0f);
    const float toRight = level * std::clamp(std::sin(angle) * 1.41421356f, 0.2f, 1.0f);
    int16_t pcm[kFrame];
    int16_t stereo[kFrame * 2];
    const auto put = [&](int samples) {
        if (samples <= 0) return;
        for (int i = 0; i < samples; ++i) {
            const float v = static_cast<float>(pcm[i]);
            stereo[i * 2] = static_cast<int16_t>(v * toLeft);
            stereo[i * 2 + 1] = static_cast<int16_t>(v * toRight);
        }
        s.speakers->put(who, stereo, samples);
    };
    if (missing > 0 && missing <= static_cast<uint32_t>(kMaxConcealed) + 1) {

        for (uint32_t i = 1; i < missing; ++i) put(opus_decode(v.opus, nullptr, 0, pcm, kFrame, 0));
        put(opus_decode(v.opus, opus, static_cast<opus_int32>(size), pcm, kFrame, 1));
    }
    put(opus_decode(v.opus, opus, static_cast<opus_int32>(size), pcm, kFrame, 0));
}

void Device::forget(const std::string& who) {
    s_->drop_voice(who);
}

void Device::close() {
    if (!s_) return;
#if defined(COOP_VOICE_SDL)
    if (sdl().ok) s_->close_capture();
    if (s_->encoder != nullptr) opus_encoder_destroy(s_->encoder);
    s_->encoder = nullptr;
#endif
    s_->close_speakers();
    s_->micOn = false;
}

const std::string& Device::problem() const {
    return s_->problem;
}

#else

struct Device::State {
    std::string problem = "Voice isn't available here";
};

Device::Device() : s_(std::make_unique<State>()) {}
Device::~Device() = default;
std::vector<std::string> Device::microphones() { return {kDefaultMic}; }
void Device::set(bool, bool, const std::string&, float, float) {}
std::vector<Frame> Device::take() { return {}; }
void Device::play(const std::string&, uint32_t, const uint8_t*, size_t, float, float) {}
void Device::forget(const std::string&) {}
void Device::close() {}
const std::string& Device::problem() const { return s_->problem; }

#endif

}
