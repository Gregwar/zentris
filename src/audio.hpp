#pragma once
// Audio output: song playback with crossfades between tracks.
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "analysis.hpp"

struct ma_device;

struct Track {
    std::string path, title;
    std::vector<float> pcm; // interleaved stereo at the engine sample rate
    uint32_t sampleRate = 48000;
    uint64_t frames() const { return pcm.size() / 2; }
    double duration() const { return (double)frames() / sampleRate; }
    Analysis analysis;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    bool init();
    void shutdown();
    uint32_t sampleRate() const { return sampleRate_; }

    void play(std::shared_ptr<Track> track); // crossfades from the current track
    std::shared_ptr<Track> current() const { return current_; }
    double position() const;                 // seconds into the current track
    bool finished() const;                   // current track reached its end
    void seek(double seconds);               // jump inside the current track
    void setPaused(bool p) { paused_ = p; }
    bool paused() const { return paused_; }
    void setMuted(bool m) { muted_ = m; }

private:
    static void dataCallback(ma_device* dev, void* out, const void* in, unsigned int frameCount);
    void render(float* out, unsigned int frames);

    ma_device* device_ = nullptr;
    uint32_t sampleRate_ = 48000;
    std::mutex mutex_;
    std::shared_ptr<Track> current_, previous_;
    uint64_t pos_ = 0, prevPos_ = 0;
    std::atomic<uint64_t> atomicPos_{0};
    float fade_ = 1.f, prevFade_ = 0.f;
    std::atomic<bool> paused_{false}, muted_{false};
};
