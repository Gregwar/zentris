#pragma once
// Song library: scans folders, shuffles, decodes + analyzes songs on a background thread.
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "audio.hpp"

class Library {
public:
    // paths can be files or directories.
    void scan(const std::vector<std::string>& paths);
    size_t size() const { return files_.size(); }
    bool empty() const { return files_.empty(); }
    const std::vector<std::string>& files() const { return files_; }

    void setSampleRate(uint32_t sr) { sampleRate_ = sr; }
    // Songs play in the given order (folders alphabetically, playlists in their order) unless shuffled.
    void setShuffle(bool s) { shuffle_ = s; }
    // Starts loading the next song of the shuffled queue (no-op if already loading).
    void prefetch(uint64_t seed);
    // Returns the prefetched track if ready, else nullptr.
    std::shared_ptr<Track> takeReady();
    bool loading() const { return pending_.valid(); }
    const std::string& loadingTitle() const { return loadingTitle_; } // song being prepared

private:
    std::string pickNext(uint64_t seed);
    std::vector<std::string> files_;
    std::vector<size_t> queue_;
    size_t lastPlayed_ = (size_t)-1;
    uint32_t sampleRate_ = 48000;
    bool shuffle_ = false;
    std::future<std::shared_ptr<Track>> pending_;
    std::string loadingTitle_;
    // Remote songs: keep the next few downloaded ahead so skipping is instant.
    void downloadAhead();
    static constexpr int AHEAD = 3;
};

std::shared_ptr<Track> loadTrack(const std::string& path, uint32_t sampleRate);

// The HUD font is ASCII only: folds UTF-8 text to ASCII (accents removed, quotes/dashes simplified,
// other symbols dropped).
std::string asciiFold(const std::string& utf8);
