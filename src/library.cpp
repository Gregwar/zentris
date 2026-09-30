#include "library.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "mathutil.hpp"
#include "miniaudio.h"
#include "youtube.hpp"

namespace fs = std::filesystem;

static bool isAudioFile(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp3" || ext == ".wav" || ext == ".flac";
}

void Library::scan(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        if (isUrl(p)) { // YouTube playlist / video (through yt-dlp)
            for (auto& e : resolveYoutube(p)) files_.push_back(e);
            continue;
        }
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            std::vector<std::string> dir; // a folder plays in alphabetical order
            for (auto& e : fs::recursive_directory_iterator(p, ec))
                if (e.is_regular_file() && isAudioFile(e.path())) dir.push_back(e.path().string());
            std::sort(dir.begin(), dir.end());
            files_.insert(files_.end(), dir.begin(), dir.end());
        } else if (fs::is_regular_file(p, ec) && isAudioFile(p)) {
            files_.push_back(p);
        }
    }
    // Drop duplicates, keeping the given order (a YouTube playlist keeps its own order).
    std::vector<std::string> unique;
    for (auto& f : files_)
        if (std::find(unique.begin(), unique.end(), f) == unique.end()) unique.push_back(f);
    files_.swap(unique);
}

std::string Library::pickNext(uint64_t seed) {
    if (files_.empty()) return {};
    if (queue_.empty()) {
        // queue_ is consumed from the back: fill it reversed so the list plays in its order.
        for (size_t i = files_.size(); i > 0; i--) queue_.push_back(i - 1);
        if (shuffle_) {
            Rng rng(seed);
            for (size_t i = queue_.size(); i > 1; i--) std::swap(queue_[i - 1], queue_[rng.next() % i]);
            // Never repeat the song that just played when reshuffling.
            if (queue_.size() > 1 && queue_.back() == lastPlayed_) std::swap(queue_.back(), queue_.front());
        }
    }
    // Shuffle mode: prefer a song that is ready to play now (local file or already downloaded) among the
    // next few, so the next song is available quickly even while downloads are running.
    for (int i = (int)queue_.size() - 1, n = 0; shuffle_ && i >= 0 && n < 6; i--, n++) {
        const std::string& e = files_[queue_[i]];
        if (!isYoutubeEntry(e) || isYoutubeCached(e)) {
            std::swap(queue_[i], queue_.back());
            break;
        }
    }
    size_t idx = queue_.back();
    queue_.pop_back();
    lastPlayed_ = idx;
    return files_[idx];
}

void Library::prefetch(uint64_t seed) {
    if (pending_.valid() || files_.empty()) return;
    std::string path = pickNext(seed);
    loadingTitle_ = isYoutubeEntry(path) ? youtubeEntryTitle(path) : fs::path(path).stem().string();
    std::printf("[library] preparing next song: %s%s\n", loadingTitle_.c_str(),
                isYoutubeEntry(path) && !isYoutubeCached(path) ? " (downloading)" : "");
    std::fflush(stdout);
    uint32_t sr = sampleRate_;
    pending_ = std::async(std::launch::async, [path, sr]() { return loadTrack(path, sr); });
    downloadAhead();
}

void Library::downloadAhead() {
    std::vector<std::string> todo;
    for (int i = (int)queue_.size() - 1; i >= 0 && (int)todo.size() < AHEAD; i--) {
        const std::string& e = files_[queue_[i]];
        if (isYoutubeEntry(e)) todo.push_back(e);
    }
    if (!todo.empty()) prefetchYoutubeAudio(todo);
}

std::shared_ptr<Track> Library::takeReady() {
    if (!pending_.valid()) return nullptr;
    if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return pending_.get();
}

std::shared_ptr<Track> loadTrack(const std::string& entry, uint32_t sampleRate) {
    auto t0 = std::chrono::steady_clock::now();
    std::string path = entry;
    if (isYoutubeEntry(entry)) {
        path = fetchYoutubeAudio(entry);
        if (path.empty()) return nullptr;
    }
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, sampleRate);
    ma_decoder dec;
    if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) {
        std::fprintf(stderr, "[library] cannot decode %s\n", path.c_str());
        return nullptr;
    }
    auto track = std::make_shared<Track>();
    track->path = path;
    track->title = isYoutubeEntry(entry) ? youtubeEntryTitle(entry) : fs::path(path).stem().string();
    track->sampleRate = sampleRate;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) == MA_SUCCESS && total > 0)
        track->pcm.reserve((size_t)total * 2);
    std::vector<float> chunk(4096 * 2);
    for (;;) {
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &got);
        if (got == 0) break;
        track->pcm.insert(track->pcm.end(), chunk.begin(), chunk.begin() + got * 2);
    }
    ma_decoder_uninit(&dec);
    if (track->pcm.empty()) return nullptr;

    // Trim silence at both ends (below -54 dBFS), keeping a short margin so attacks are not clipped.
    {
        const float thr = 0.002f;
        const size_t n = track->frames(), margin = sampleRate / 20; // 50 ms
        size_t first = 0, last = n;
        while (first < n && std::fabs(track->pcm[first * 2]) < thr && std::fabs(track->pcm[first * 2 + 1]) < thr) first++;
        while (last > first && std::fabs(track->pcm[(last - 1) * 2]) < thr && std::fabs(track->pcm[(last - 1) * 2 + 1]) < thr) last--;
        first = first > margin ? first - margin : 0;
        last = std::min(n, last + margin);
        if (last > first && (first > 0 || last < n)) {
            std::vector<float> trimmed(track->pcm.begin() + first * 2, track->pcm.begin() + last * 2);
            track->pcm.swap(trimmed);
            std::printf("[library] trimmed %.2f s of silence at the start, %.2f s at the end\n",
                        (double)first / sampleRate, (double)(n - last) / sampleRate);
        }
    }

    double decodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::vector<float> mono(track->frames());
    for (size_t i = 0; i < mono.size(); i++) mono[i] = 0.5f * (track->pcm[i * 2] + track->pcm[i * 2 + 1]);
    track->analysis = analyzeAudio(mono, sampleRate);

    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const Footprint& fp = track->analysis.fp;
    std::printf("[library] %s  (decode %.0f ms, total %.0f ms)\n  bpm %.1f  key %s  bright %.2f  bass %.2f  air %.2f  dyn %.2f  "
                "dens %.2f  sections %zu  hash %016llx\n",
                track->title.c_str(), decodeMs, ms, fp.bpm, keyName(fp.key, fp.minor).c_str(), fp.brightness, fp.bassWeight,
                fp.airWeight, fp.dynamics, fp.density, fp.sections.size(), (unsigned long long)fp.hash);
    std::fflush(stdout);
    return track;
}
