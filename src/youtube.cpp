#include "youtube.hpp"

#include "platform.hpp"

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

std::string shellQuote(const std::string& s) { return platform::quoteArg(s); }

// yt-dlp: the one installed alongside the pip/pipx/uvx package, else from PATH, else the usual
// pipx/pip user location. YouTube extraction needs a JavaScript runtime: yt-dlp uses deno by
// default; if only node is installed, tell it to use node.
std::string ytdlp() {
    std::string tool;
    const char* bundled = std::getenv("ZENTRIS_YTDLP");
    std::error_code ec;
    if (bundled && *bundled && fs::exists(bundled, ec)) tool = shellQuote(bundled);
    else if (std::string p = platform::findExecutable("yt-dlp"); !p.empty()) tool = shellQuote(p);
    else if (fs::path p = platform::homeDir() / ".local" / "bin" / "yt-dlp"; fs::exists(p, ec)) tool = shellQuote(p.string());
    if (tool.empty()) return "";
    if (platform::findExecutable("deno").empty() && !platform::findExecutable("node").empty()) tool += " --js-runtimes node";
    return tool;
}

std::string cacheDir() {
    fs::path dir = platform::cacheDir() / "zentris" / "youtube";
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

// A link with a list= parameter (e.g. watch?v=...&list=...) means the whole playlist.
static std::string playlistUrl(const std::string& url) {
    size_t p = url.find("list=");
    if (p == std::string::npos || (p > 0 && url[p - 1] != '?' && url[p - 1] != '&')) return url;
    std::string id;
    for (size_t i = p + 5; i < url.size() && url[i] != '&' && url[i] != '#'; i++) id += url[i];
    if (!safeId(id)) return url;
    return "https://www.youtube.com/playlist?list=" + id;
}

std::vector<std::string> resolveYoutube(const std::string& rawUrl) {
    std::vector<std::string> out;
    const std::string url = playlistUrl(rawUrl);
    std::string tool = ytdlp();
    if (tool.empty()) {
        std::fprintf(stderr, "[youtube] yt-dlp not found: install it (e.g. `pipx install yt-dlp`) to play %s\n",
                     url.c_str());
        return out;
    }
    std::string cmd = tool + " --flat-playlist --yes-playlist --no-warnings --print " + shellQuote("%(id)s\t%(title)s") +
                      " " + shellQuote(url);
    std::printf("[youtube] listing %s ...\n", url.c_str());
    FILE* p = platform::openRead(cmd);
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
    platform::closeRead(p);
    std::printf("[youtube] %zu songs\n", out.size());
    if (out.size() == 1 && url.find("list=") == std::string::npos)
        std::printf("[youtube] tip: this link is a single video. For a playlist, pass its list= link and put the URL\n"
                    "          in quotes: an unquoted '&' cuts the URL in the shell.\n");
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
                      shellQuote(out);
    // ffmpeg converts the audio: the launcher points to a bundled one when the system has none.
    if (const char* ff = std::getenv("ZENTRIS_FFMPEG"); ff && *ff) cmd += " --ffmpeg-location " + shellQuote(ff);
    cmd += " -- " + id;
    bool ok = platform::run(cmd) == 0 && fs::exists(fs::path(cacheDir()) / (id + ".mp3"));
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
