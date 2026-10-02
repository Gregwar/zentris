#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "miniaudio.h"

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    device_ = new ma_device();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 48000;
    cfg.dataCallback = [](ma_device* d, void* o, const void* i, ma_uint32 n) { dataCallback(d, o, i, n); };
    cfg.pUserData = this;
    if (ma_device_init(nullptr, &cfg, device_) != MA_SUCCESS) {
        std::fprintf(stderr, "[audio] could not open playback device\n");
        delete device_;
        device_ = nullptr;
        return false;
    }
    sampleRate_ = device_->sampleRate;
    if (ma_device_start(device_) != MA_SUCCESS) {
        std::fprintf(stderr, "[audio] could not start playback device\n");
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
        return false;
    }
    return true;
}

void AudioEngine::shutdown() {
    if (device_) {
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
    }
}

void AudioEngine::play(std::shared_ptr<Track> track) {
    std::lock_guard<std::mutex> lock(mutex_);
    previous_ = current_;
    prevPos_ = pos_;
    prevFade_ = previous_ ? fade_ : 0.f;
    current_ = std::move(track);
    pos_ = 0;
    frac_ = 0;
    atomicPos_ = 0;
    fade_ = 0.f;
}

void AudioEngine::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_) return;
    uint64_t f = (uint64_t)std::max(0.0, seconds * sampleRate_);
    pos_ = std::min<uint64_t>(f, current_->frames());
    frac_ = 0;
    atomicPos_ = pos_;
}

void AudioEngine::playSample(std::shared_ptr<const std::vector<float>> pcm, float gain) {
    if (!pcm || pcm->empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= 4) voices_.erase(voices_.begin()); // drop the oldest
    voices_.push_back({std::move(pcm), 0, gain});
}

double AudioEngine::position() const {
    if (!current_) return 0;
    return (double)atomicPos_.load() / sampleRate_;
}

bool AudioEngine::finished() const {
    return current_ && atomicPos_.load() >= current_->frames();
}

void AudioEngine::dataCallback(ma_device* dev, void* out, const void*, unsigned int frameCount) {
    static_cast<AudioEngine*>(dev->pUserData)->render(static_cast<float*>(out), frameCount);
}

void AudioEngine::render(float* out, unsigned int frames) {
    for (unsigned int i = 0; i < frames * 2; i++) out[i] = 0.f;
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_)
        for (unsigned int i = 0; i < frames * 2 && v.pos < v.pcm->size(); i++, v.pos++) out[i] += (*v.pcm)[v.pos] * v.gain;
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pos >= v.pcm->size(); }),
                  voices_.end());
    if (paused_) {
        if (muted_)
            for (unsigned int i = 0; i < frames * 2; i++) out[i] = 0.f;
        return;
    }

    const float fadeStep = 1.0f / (1.2f * sampleRate_);
    auto mixTrack = [&](Track* t, uint64_t& pos, float& fade, bool fadingIn) {
        if (!t) return;
        const uint64_t n = t->frames();
        for (unsigned int i = 0; i < frames && pos < n; i++, pos++) {
            fade = fadingIn ? std::min(1.f, fade + fadeStep) : std::max(0.f, fade - fadeStep);
            // Equal-power-ish curve.
            float g = std::sin(fade * 1.5707963f);
            out[i * 2] += t->pcm[pos * 2] * g;
            out[i * 2 + 1] += t->pcm[pos * 2 + 1] * g;
        }
    };
    const float target = speed_, glide = glide_;
    float speed = curSpeed_;
    if (glide <= 0.f) speed = target;
    const float glideStep = glide > 0.f ? 0.5f / (glide * sampleRate_) : 0.f;
    if ((speed == 1.f && target == 1.f) || !current_) mixTrack(current_.get(), pos_, fade_, true);
    else {
        // Slowed down: read between frames with linear interpolation.
        const Track* t = current_.get();
        const uint64_t n = t->frames();
        for (unsigned int i = 0; i < frames && pos_ < n; i++) {
            if (speed != target) speed = speed < target ? std::min(target, speed + glideStep) : std::max(target, speed - glideStep);
            fade_ = std::min(1.f, fade_ + fadeStep);
            float g = std::sin(fade_ * 1.5707963f), a = (float)frac_;
            uint64_t q = std::min(pos_ + 1, n - 1);
            out[i * 2] += (t->pcm[pos_ * 2] * (1 - a) + t->pcm[q * 2] * a) * g;
            out[i * 2 + 1] += (t->pcm[pos_ * 2 + 1] * (1 - a) + t->pcm[q * 2 + 1] * a) * g;
            frac_ += speed;
            while (frac_ >= 1.0) {
                frac_ -= 1.0;
                pos_++;
            }
        }
        if (pos_ >= n) speed = target;
    }
    curSpeed_ = speed;
    if (previous_) {
        mixTrack(previous_.get(), prevPos_, prevFade_, false);
        if (prevFade_ <= 0.f || prevPos_ >= previous_->frames()) previous_.reset();
    }
    atomicPos_ = pos_;

    if (muted_)
        for (unsigned int i = 0; i < frames * 2; i++) out[i] = 0.f;
    // Soft clip safety, transparent below 0.9.
    for (unsigned int i = 0; i < frames * 2; i++) {
        float a = std::fabs(out[i]);
        if (a > 0.9f) out[i] = std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) * 10.f), out[i]);
    }
}
