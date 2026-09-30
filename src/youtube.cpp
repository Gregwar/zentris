#include "youtube.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::string shellQuote(const std::string& s) {
    std::string r = "'";
    for (char c : s) r += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return r + "'";
}

// yt-dlp from PATH, else the usual pipx/pip user location. YouTube extraction needs a JavaScript
// runtime: yt-dlp uses deno by default; if only node is installed, tell it to use node.
std::string ytdlp() {
    std::string tool;
    if (std::system("command -v yt-dlp >/dev/null 2>&1") == 0) tool = "yt-dlp";
    else if (const char* home = std::getenv("HOME")) {
        std::string p = std::string(home) + "/.local/bin/yt-dlp";
        if (fs::exists(p)) tool = shellQuote(p);
    }
    if (tool.empty()) return "";
    if (std::system("command -v deno >/dev/null 2>&1") != 0 && std::system("command -v node >/dev/null 2>&1") == 0)
        tool += " --js-runtimes node";
    return tool;
}

std::string cacheDir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".cache";
    fs::path dir = base / "zentetris" / "youtube";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
    return true;
}

} // namespace

bool isUrl(const std::string& s) { return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0; }

bool isYoutubeEntry(const std::string& entry) { return entry.rfind("ytdl:", 0) == 0; }

std::string youtubeEntryTitle(const std::string& entry) {
    size_t tab = entry.find('\t');
    return tab == std::string::npos ? entry.substr(5) : entry.substr(tab + 1);
}

std::vector<std::string> resolveYoutube(const std::string& url) {
    std::vector<std::string> out;
    std::string tool = ytdlp();
    if (tool.empty()) {
        std::fprintf(stderr, "[youtube] yt-dlp not found: install it (e.g. `pipx install yt-dlp`) to play %s\n",
                     url.c_str());
        return out;
    }
    std::string cmd = tool + " --flat-playlist --no-warnings --print '%(id)s\t%(title)s' " + shellQuote(url) +
                      " 2>/dev/null";
    std::printf("[youtube] listing %s ...\n", url.c_str());
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char line[2048];
    while (std::fgets(line, sizeof(line), p)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t tab = s.find('\t');
        std::string id = s.substr(0, tab);
        if (!safeId(id)) continue;
        std::string title = tab == std::string::npos ? id : s.substr(tab + 1);
        if (title == "[Private video]" || title == "[Deleted video]") continue;
        out.push_back("ytdl:" + id + "\t" + title);
    }
    pclose(p);
    std::printf("[youtube] %zu songs\n", out.size());
    return out;
}

static std::string entryId(const std::string& entry) {
    size_t tab = entry.find('\t');
    return entry.substr(5, tab == std::string::npos ? std::string::npos : tab - 5);
}

bool isYoutubeCached(const std::string& entry) {
    std::string id = entryId(entry);
    if (!safeId(id)) return false;
    std::error_code ec;
    fs::path file = fs::path(cacheDir()) / (id + ".mp3");
    return fs::exists(file, ec) && fs::file_size(file, ec) > 0;
}

namespace {
// Download bookkeeping: songs being downloaded right now, and the background queue.
std::mutex dlMutex;
std::condition_variable dlDone;
std::set<std::string> inProgress;
std::deque<std::string> backgroundQueue;
bool workerRunning = false;

bool download(const std::string& id, const std::string& title) {
    std::string tool = ytdlp();
    if (tool.empty()) return false;
    std::printf("[youtube] downloading %s ...\n", title.c_str());
    std::fflush(stdout);
    std::string out = (fs::path(cacheDir()) / "%(id)s.%(ext)s").string();
    std::string cmd = tool + " -q --no-warnings --no-playlist -f bestaudio -x --audio-format mp3 --audio-quality 2 -o " +
                      shellQuote(out) + " -- " + id + " >/dev/null 2>&1";
    bool ok = std::system(cmd.c_str()) == 0 && fs::exists(fs::path(cacheDir()) / (id + ".mp3"));
    if (!ok) std::fprintf(stderr, "[youtube] could not download %s (%s)\n", title.c_str(), id.c_str());
    return ok;
}
} // namespace

std::string fetchYoutubeAudio(const std::string& entry) {
    const std::string id = entryId(entry);
    if (!safeId(id)) return "";
    const std::string file = (fs::path(cacheDir()) / (id + ".mp3")).string();
    {
        std::unique_lock<std::mutex> lock(dlMutex);
        dlDone.wait(lock, [&] { return !inProgress.count(id); }); // same song already downloading
        if (isYoutubeCached(entry)) return file;
        inProgress.insert(id);
        backgroundQueue.erase(std::remove(backgroundQueue.begin(), backgroundQueue.end(), entry), backgroundQueue.end());
    }
    bool ok = download(id, youtubeEntryTitle(entry));
    {
        std::lock_guard<std::mutex> lock(dlMutex);
        inProgress.erase(id);
    }
    dlDone.notify_all();
    return ok ? file : "";
}

void prefetchYoutubeAudio(const std::vector<std::string>& entries) {
    std::lock_guard<std::mutex> lock(dlMutex);
    for (const auto& e : entries) {
        if (!safeId(entryId(e)) || isYoutubeCached(e) || inProgress.count(entryId(e))) continue;
        if (std::find(backgroundQueue.begin(), backgroundQueue.end(), e) == backgroundQueue.end())
            backgroundQueue.push_back(e);
    }
    if (workerRunning || backgroundQueue.empty()) return;
    workerRunning = true;
    std::thread([] {
        for (;;) {
            std::string e, id;
            {
                std::lock_guard<std::mutex> lock(dlMutex);
                while (!backgroundQueue.empty() &&
                       (isYoutubeCached(backgroundQueue.front()) || inProgress.count(entryId(backgroundQueue.front()))))
                    backgroundQueue.pop_front();
                if (backgroundQueue.empty()) { workerRunning = false; return; }
                e = backgroundQueue.front();
                backgroundQueue.pop_front();
                id = entryId(e);
                inProgress.insert(id);
            }
            download(id, youtubeEntryTitle(e));
            {
                std::lock_guard<std::mutex> lock(dlMutex);
                inProgress.erase(id);
            }
            dlDone.notify_all();
        }
    }).detach();
}
