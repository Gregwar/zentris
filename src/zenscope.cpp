// zenscope: shows what Zentris "hears" in a song — structure (segments, groups), scene levels and
// changes, pulse zones, spectrum, loudness / intensity / onsets and the beat grid — while playing it.
//
//   zenscope [--song ID|TITLE] [--at SEC] [songs or folders...]   (default: the built-in playlist)
//   SPACE play/pause, LEFT/RIGHT seek 5 s, UP/DOWN zoom the detail view, N/P next/previous song,
//   click the overview or the detail view to seek, 1-4 or click a sample button to hear it,
//   click X0.9 / X0.5 (top right) to slow the song down, ESC quits.
//   A writes a note at the playhead ("the drop should start earlier here"): ENTER saves it, with what the
//   analysis says around that time, to annotations.jsonl in the user data folder (--notes FILE to change).
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "library.hpp"
#include "platform.hpp"
#include "samples.hpp"
#include "mathutil.hpp"
#include "songplan.hpp"
#include "youtube.hpp"
#include "stb_easy_font.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace {

struct Col {
    float r, g, b, a = 1.f;
};
Col rgb(float r, float g, float b, float a = 1.f) { return {r, g, b, a}; }
Col withA(Col c, float a) { c.a = a; return c; }
Col fromLinear(const vec3& c, float a = 1.f) {
    return {std::pow(saturate(c.x), 1 / 2.2f), std::pow(saturate(c.y), 1 / 2.2f), std::pow(saturate(c.z), 1 / 2.2f), a};
}

const Col BG = rgb(0.065f, 0.068f, 0.085f), PANEL = rgb(0.10f, 0.105f, 0.13f), LABEL = rgb(0.55f, 0.57f, 0.65f),
          TEXT = rgb(0.9f, 0.9f, 0.94f), DIM = rgb(0.4f, 0.42f, 0.5f);

Col kindColor(int kind) {
    switch (kind) {
    case SEG_INTRO: return rgb(0.36f, 0.47f, 0.72f);
    case SEG_VERSE: return rgb(0.25f, 0.6f, 0.55f);
    case SEG_BUILD: return rgb(0.9f, 0.62f, 0.22f);
    case SEG_CHORUS: return rgb(0.82f, 0.36f, 0.62f);
    case SEG_DROP: return rgb(0.95f, 0.3f, 0.3f);
    case SEG_BREAK: return rgb(0.42f, 0.42f, 0.52f);
    default: return rgb(0.3f, 0.34f, 0.5f);
    }
}
Col levelColor(int level) {
    return level <= 0 ? rgb(0.28f, 0.4f, 0.62f) : level == 1 ? rgb(0.5f, 0.5f, 0.62f) : rgb(0.98f, 0.58f, 0.28f);
}
const char* levelName(int level) { return level <= 0 ? "CALM" : level == 1 ? "MID" : "PEAK"; }
Col sampleColor(int k) { return fromLinear(oklchToLinear(0.72f, 0.14f, 0.5f + k * 1.5708f)); }
const char* sampleLines(int k) { static const char* l[SMP_COUNT] = {"1 LINE", "2 LINES", "3 LINES", "4 LINES"}; return l[k]; }
Col groupColor(int g) { return fromLinear(oklchToLinear(0.75f, 0.13f, g * 2.39996f + 0.6f)); }

std::string mmss(double t) {
    char b[32];
    int s = (int)std::max(0.0, t);
    std::snprintf(b, sizeof(b), "%d:%02d", s / 60, s % 60);
    return b;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += (char)c;
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else o += (char)c;
    }
    return o;
}

// Reads the top-level field `key` of a one-line JSON object written by Scope::saveNote (string or number).
bool jsonField(const std::string& line, const std::string& key, std::string& out) {
    size_t p = line.find("\"" + key + "\":");
    if (p == std::string::npos) return false;
    p += key.size() + 3;
    out.clear();
    if (p >= line.size()) return false;
    if (line[p] != '"') {
        while (p < line.size() && line[p] != ',' && line[p] != '}') out += line[p++];
        return true;
    }
    for (p++; p < line.size() && line[p] != '"'; p++) {
        if (line[p] != '\\' || p + 1 >= line.size()) { out += line[p]; continue; }
        char e = line[++p];
        if (e == 'n') out += '\n';
        else if (e == 'u' && p + 4 < line.size()) out += (char)std::strtol(line.substr(p + 1, 4).c_str(), nullptr, 16), p += 4;
        else out += e;
    }
    return true;
}

void appendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) s += (char)(0xC0 | cp >> 6), s += (char)(0x80 | (cp & 0x3F));
    else if (cp < 0x10000) s += (char)(0xE0 | cp >> 12), s += (char)(0x80 | (cp >> 6 & 0x3F)), s += (char)(0x80 | (cp & 0x3F));
    else s += (char)(0xF0 | cp >> 18), s += (char)(0x80 | (cp >> 12 & 0x3F)), s += (char)(0x80 | (cp >> 6 & 0x3F)), s += (char)(0x80 | (cp & 0x3F));
}

const Col NOTE = rgb(0.4f, 0.95f, 0.9f);

GLuint compile(const char* vs, const char* fs) {
    auto stage = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[gl] shader error: %s\n", log);
        }
        return s;
    };
    GLuint p = glCreateProgram(), a = stage(GL_VERTEX_SHADER, vs), b = stage(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

// Minimal immediate-mode 2D drawing in window pixels (top-left origin).
class Draw2D {
public:
    void init() {
        prog_ = compile(R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main() { vCol = aCol; gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0); }
)",
                        R"(#version 330 core
in vec4 vCol;
out vec4 fragColor;
void main() { fragColor = vCol; }
)");
        imgProg_ = compile(R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform vec2 uRes;
out vec2 vUV;
void main() { vUV = aCol.xy; gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0); }
)",
                           R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
// Dark indigo -> magenta -> orange -> pale yellow.
vec3 cmap(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 a = vec3(0.04, 0.03, 0.09), b = vec3(0.32, 0.07, 0.45), c = vec3(0.85, 0.25, 0.4), d = vec3(0.99, 0.62, 0.2), e = vec3(1.0, 0.95, 0.7);
    if (t < 0.25) return mix(a, b, t / 0.25);
    if (t < 0.5) return mix(b, c, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c, d, (t - 0.5) / 0.25);
    return mix(d, e, (t - 0.75) / 0.25);
}
void main() { fragColor = vec4(cmap(texture(uTex, vUV).r), 1.0); }
)");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, (void*)8);
    }
    void begin(int w, int h) {
        w_ = w;
        h_ = h;
        v_.clear();
    }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, Col c) {
        float d[18] = {x0, y0, c.r, c.g, c.b, c.a, x1, y1, c.r, c.g, c.b, c.a, x2, y2, c.r, c.g, c.b, c.a};
        v_.insert(v_.end(), d, d + 18);
    }
    void rect(float x, float y, float w, float h, Col c) {
        if (w <= 0 || h <= 0) return;
        tri(x, y, x + w, y, x + w, y + h, c);
        tri(x, y, x + w, y + h, x, y + h, c);
    }
    void line(float x0, float y0, float x1, float y1, float th, Col c) {
        float dx = x1 - x0, dy = y1 - y0, l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-4f) return;
        float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
        tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
        tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    float textWidth(const std::string& s, float scale) {
        std::vector<char> t(s.begin(), s.end());
        t.push_back(0);
        stb_easy_font_spacing(0.4f);
        return stb_easy_font_width(t.data()) * scale;
    }
    // align: 0 left, 1 center, 2 right.
    void text(const std::string& s, float x, float y, float scale, Col c, int align = 0) {
        static char buf[120000];
        std::vector<char> t(s.begin(), s.end());
        t.push_back(0);
        stb_easy_font_spacing(0.4f);
        unsigned char white[4] = {255, 255, 255, 255};
        int nq = stb_easy_font_print(0, 0, t.data(), white, buf, sizeof(buf));
        float w = stb_easy_font_width(t.data()) * scale;
        float ox = align == 1 ? x - w * 0.5f : align == 2 ? x - w : x;
        for (int q = 0; q < nq; q++) {
            float* p = (float*)(buf + q * 64);
            float X[4], Y[4];
            for (int k = 0; k < 4; k++) {
                X[k] = std::round(ox + p[k * 4] * scale);
                Y[k] = std::round(y + p[k * 4 + 1] * scale);
            }
            tri(X[0], Y[0], X[1], Y[1], X[2], Y[2], c);
            tri(X[0], Y[0], X[2], Y[2], X[3], Y[3], c);
        }
    }
    void flush() {
        if (v_.empty()) return;
        glUseProgram(prog_);
        glUniform2f(glGetUniformLocation(prog_, "uRes"), (float)w_, (float)h_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, v_.size() * sizeof(float), v_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v_.size() / 6));
        v_.clear();
    }
    // Textured quad (heatmap); flushes pending shapes first to keep the painter's order.
    void image(GLuint tex, float x, float y, float w, float h, float u0, float u1) {
        flush();
        float d[36] = {x, y, u0, 1, 0, 0, x + w, y, u1, 1, 0, 0, x + w, y + h, u1, 0, 0, 0,
                       x, y, u0, 1, 0, 0, x + w, y + h, u1, 0, 0, 0, x, y + h, u0, 0, 0, 0};
        glUseProgram(imgProg_);
        glUniform2f(glGetUniformLocation(imgProg_, "uRes"), (float)w_, (float)h_);
        glUniform1i(glGetUniformLocation(imgProg_, "uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(d), d, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

private:
    GLuint prog_ = 0, imgProg_ = 0, vao_ = 0, vbo_ = 0;
    int w_ = 1, h_ = 1;
    std::vector<float> v_;
};

class Scope {
public:
    int run(int argc, char** argv);

private:
    void load(int index);
    void onTrackReady(std::shared_ptr<Track> t);
    void drawFrame(int w, int h);
    void drawOverview(float L, float R, float y);
    void drawDetail(float L, float R, float y, float bottom);
    void drawReadout(float L, float R, float y);
    void drawSamples(float L, float R, float y);
    void playSample(int i);
    // Notes (annotations) on the current song.
    std::string songKey() const { return files_.empty() ? "" : files_[index_].substr(0, files_[index_].find('\t')); }
    void loadNotes();
    void saveNote();
    void drawNoteEditor(int w, int h);
    static void onChar(GLFWwindow* w, unsigned cp);
    static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods);
    std::string notesPath_;
    struct Note { double at; std::string text; };
    std::vector<Note> notes_;
    bool typing_ = false, pausedBeforeNote_ = false;
    std::string draft_;
    double noteAt_ = 0;
    std::string flash_;
    double flashUntil_ = 0;
    double now() const { return track_ ? (seekPreview_ >= 0 ? seekPreview_ : audio_.position()) : 0; }

    GLFWwindow* win_ = nullptr;
    Draw2D d_;
    AudioEngine audio_;
    std::vector<std::string> files_;
    int index_ = 0, requested_ = 0, loadingIndex_ = 0;
    std::string loadingTitle_;
    std::shared_ptr<Track> track_;
    std::future<std::shared_ptr<Track>> loading_;
    SongPlan plan_;
    GLuint specTex_ = 0;
    int specW_ = 0;
    std::vector<float> ovLoud_, ovOnset_, ovInt_; // overview curves (max-pooled)
    bool paused_ = false;
    std::vector<SongSample> samples_;
    struct Box { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    std::vector<Box> sampleBoxes_;
    std::vector<std::pair<Box, float>> speedBoxes_; // speed buttons and the speed they set
    void drawSpeed(float R, float y);
    void setSpeed(float s);
    std::vector<double> sampleStarted_; // glfwGetTime() of the last play, per sample
    float span_ = 16.f;
    double seekPreview_ = -1; // shot mode: fixed time without playback
    // Overview / detail hit areas for mouse seeking.
    float ovL_ = 0, ovR_ = 1, ovTop_ = 0, ovBottom_ = 0, dtTop_ = 0, dtBottom_ = 0, dtT0_ = 0;
};

// N/P requests are remembered while a song loads; the latest one is loaded next.
void Scope::load(int index) {
    if (files_.empty()) return;
    requested_ = (index % (int)files_.size() + (int)files_.size()) % (int)files_.size();
    if (loading_.valid()) return;
    loadingIndex_ = requested_;
    std::string path = files_[loadingIndex_];
    loadingTitle_ = isYoutubeEntry(path) ? youtubeEntryTitle(path) : fs::path(path).stem().string();
    uint32_t sr = audio_.sampleRate();
    loading_ = std::async(std::launch::async, [path, sr]() { return loadTrack(path, sr); });
}

void Scope::onTrackReady(std::shared_ptr<Track> t) {
    if (!t) return;
    track_ = t;
    plan_ = planSong(t->analysis);
    const Analysis& an = t->analysis;
    const int n = (int)an.frames.size();

    // Spectrogram texture: 16 band rows, time max-pooled into at most 8192 columns.
    specW_ = std::max(1, std::min(n, 8192));
    std::vector<unsigned char> px((size_t)specW_ * NUM_BANDS);
    for (int x = 0; x < specW_; x++) {
        int f0 = (int)((int64_t)x * n / specW_), f1 = std::max(f0 + 1, (int)((int64_t)(x + 1) * n / specW_));
        for (int b = 0; b < NUM_BANDS; b++) {
            float m = 0;
            for (int f = f0; f < f1 && f < n; f++) m = std::max(m, an.frames[f].bands[b]);
            px[(size_t)b * specW_ + x] = (unsigned char)(saturate(m) * 255.f);
        }
    }
    if (!specTex_) glGenTextures(1, &specTex_);
    glBindTexture(GL_TEXTURE_2D, specTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, specW_, NUM_BANDS, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const int OV = 2048;
    ovLoud_.assign(OV, 0.f);
    ovOnset_.assign(OV, 0.f);
    ovInt_.assign(OV, 0.f);
    for (int x = 0; x < OV; x++) {
        int f0 = (int)((int64_t)x * n / OV), f1 = std::max(f0 + 1, (int)((int64_t)(x + 1) * n / OV));
        for (int f = f0; f < f1 && f < n; f++) {
            ovLoud_[x] = std::max(ovLoud_[x], an.frames[f].loud);
            ovOnset_[x] = std::max(ovOnset_[x], an.frames[f].onset);
            ovInt_[x] = std::max(ovInt_[x], an.intensity[f]);
        }
    }
    samples_ = extractSamples(*t);
    sampleStarted_.assign(samples_.size(), -1e9);
    for (const SongSample& sm : samples_)
        std::printf("[zenscope] sample %-4s %s-%s  %d beat(s)  score %.2f  %s\n", sampleName(sm.kind), mmss(sm.start).c_str(),
                    mmss(sm.end).c_str(), sm.beats, sm.score, sm.why.c_str());
    loadNotes();
    audio_.play(track_);
    audio_.setPaused(paused_);
    std::printf("[zenscope] %s: %zu segments, %zu scene phases, %zu pulse zones\n", t->title.c_str(),
                an.segments.size(), plan_.phases.size(), plan_.pulseZones.size());
}

void Scope::loadNotes() {
    notes_.clear();
    FILE* f = std::fopen(notesPath_.c_str(), "rb");
    if (!f) return;
    const std::string key = songKey();
    std::string line, song, at, text;
    for (int c; (c = std::fgetc(f)) != EOF;) {
        if (c != '\n') { line += (char)c; continue; }
        if (jsonField(line, "song", song) && song == key && jsonField(line, "at", at) && jsonField(line, "note", text))
            notes_.push_back({std::atof(at.c_str()), text});
        line.clear();
    }
    std::fclose(f);
}

// One JSON object per line: the note, where it is, and what the analysis said there (the whole scene plan
// and structure too, so a note can be checked against the analysis it was written about).
void Scope::saveNote() {
    if (!track_ || draft_.empty()) return;
    const Analysis& an = track_->analysis;
    const double t = noteAt_;
    const ScenePhase& ph = plan_.phases[plan_.phaseAt(t)];
    const Segment sg = an.segments.empty() ? Segment{} : an.segments[an.segmentAt(t)];
    std::error_code ec;
    fs::create_directories(fs::path(notesPath_).parent_path(), ec);
    FILE* f = std::fopen(notesPath_.c_str(), "ab");
    if (!f) {
        std::fprintf(stderr, "[zenscope] cannot write %s\n", notesPath_.c_str());
        return;
    }
    char date[32];
    std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    std::fprintf(f, "{\"song\":\"%s\",\"at\":%.2f,\"note\":\"%s\",\"title\":\"%s\",\"date\":\"%s\",\"bpm\":%.1f,",
                 jsonEscape(songKey()).c_str(), t, jsonEscape(draft_).c_str(), jsonEscape(track_->title).c_str(), date,
                 an.fp.bpm);
    std::fprintf(f, "\"phase\":{\"level\":\"%s\",\"start\":%.2f,\"end\":%.2f},", levelName(ph.level), ph.start, ph.end);
    std::fprintf(f, "\"segment\":{\"kind\":\"%s\",\"start\":%.2f,\"end\":%.2f,\"group\":%d,\"energy\":%.2f},",
                 segmentName(sg.kind), sg.start, sg.end, sg.cluster, sg.energy);
    std::fprintf(f, "\"phases\":[");
    for (size_t i = 0; i < plan_.phases.size(); i++)
        std::fprintf(f, "%s[%.2f,%.2f,\"%s\"]", i ? "," : "", plan_.phases[i].start, plan_.phases[i].end,
                     levelName(plan_.phases[i].level));
    std::fprintf(f, "],\"segments\":[");
    for (size_t i = 0; i < an.segments.size(); i++)
        std::fprintf(f, "%s[%.2f,%.2f,\"%s\",%d]", i ? "," : "", an.segments[i].start, an.segments[i].end,
                     segmentName(an.segments[i].kind), an.segments[i].cluster);
    std::fprintf(f, "]}\n");
    std::fclose(f);
    notes_.push_back({t, draft_});
    std::printf("[zenscope] note at %s: %s\n", mmss(t).c_str(), draft_.c_str());
    flash_ = "NOTE SAVED AT " + mmss(t);
    flashUntil_ = glfwGetTime() + 2.5;
}

// The note opens on the typed character "a" (not the key position), so it works with any keyboard layout
// and the "a" is not typed into the note.
void Scope::onChar(GLFWwindow* w, unsigned cp) {
    auto* s = (Scope*)glfwGetWindowUserPointer(w);
    if (!s->typing_) {
        if ((cp == 'a' || cp == 'A') && s->track_) {
            s->typing_ = true;
            s->draft_.clear();
            s->noteAt_ = s->audio_.position();
            s->pausedBeforeNote_ = s->paused_;
            s->paused_ = true;
            s->audio_.setPaused(true);
        }
        return;
    }
    if (cp >= 32 && s->draft_.size() < 400) appendUtf8(s->draft_, cp);
}

void Scope::onKey(GLFWwindow* w, int key, int, int action, int) {
    auto* s = (Scope*)glfwGetWindowUserPointer(w);
    if (!s->typing_ || action == GLFW_RELEASE) return;
    if (key == GLFW_KEY_BACKSPACE) {
        while (!s->draft_.empty() && ((unsigned char)s->draft_.back() & 0xC0) == 0x80) s->draft_.pop_back();
        if (!s->draft_.empty()) s->draft_.pop_back();
    } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_ESCAPE) {
        if (key != GLFW_KEY_ESCAPE) s->saveNote();
        s->typing_ = false;
        s->paused_ = s->pausedBeforeNote_;
        s->audio_.setPaused(s->paused_);
    }
}

void Scope::drawNoteEditor(int w, int h) {
    const float bw = std::min(1100.f, w - 80.f), bh = 120, x = (w - bw) * 0.5f, y = h * 0.5f - bh * 0.5f;
    d_.rect(0, 0, (float)w, (float)h, rgb(0, 0, 0, 0.45f));
    d_.rect(x, y, bw, bh, PANEL);
    d_.rect(x, y, 6, bh, NOTE);
    const Analysis& an = track_->analysis;
    const ScenePhase& ph = plan_.phases[plan_.phaseAt(noteAt_)];
    const char* seg = an.segments.empty() ? "?" : segmentName(an.segments[an.segmentAt(noteAt_)].kind);
    d_.text("NOTE AT " + mmss(noteAt_) + "   (SCENE " + levelName(ph.level) + ", " + seg + ")", x + 22, y + 14, 1.5f, NOTE);
    // Last part of the text if it is too long for the box.
    std::string shown = asciiFold(draft_);
    while (!shown.empty() && d_.textWidth(shown + "_", 2.f) > bw - 44) shown.erase(0, 1);
    d_.text(shown + ((int)(glfwGetTime() * 2) % 2 ? "_" : " "), x + 22, y + 46, 2.f, TEXT);
    d_.text("ENTER SAVE   ESC CANCEL   (" + asciiFold(notesPath_) + ")", x + 22, y + bh - 24, 1.2f, LABEL);
}

// Full-song view: structure, scene levels, pulse zones, spectrum, curves, bars.
void Scope::drawOverview(float L, float R, float y) {
    const Analysis& an = track_->analysis;
    const double dur = std::max(1.0, track_->duration());
    const float W = R - L;
    auto X = [&](double t) { return L + (float)(t / dur) * W; };
    ovL_ = L;
    ovR_ = R;
    ovTop_ = y;

    auto label = [&](const char* s, float yy, float h) { d_.text(s, L - 14, yy + h * 0.5f - 5, 1.3f, LABEL, 2); };

    // Time axis: ticks every 10 s, labelled every 30 s.
    const float hT = 18;
    label("TIME", y, hT);
    for (int s10 = 0; s10 * 10.0 <= dur; s10++) {
        float x = X(s10 * 10.0);
        bool major = s10 % 3 == 0;
        d_.rect(x, y + (major ? 10 : 13), 1, major ? 8 : 5, withA(LABEL, major ? 0.9f : 0.5f));
        if (major) d_.text(mmss(s10 * 10.0), x + 3, y, 1.1f, LABEL);
    }
    y += hT + 4;

    // Structure.
    const float hS = 50;
    label("STRUCTURE", y, hS);
    for (const Segment& sg : an.segments) {
        float x0 = X(sg.start), x1 = X(sg.end);
        Col c = kindColor(sg.kind);
        d_.rect(x0, y, x1 - x0 - 1, hS, withA(c, 0.85f));
        d_.rect(x0, y + hS - 6, x1 - x0 - 1, 6, groupColor(sg.cluster));
        std::string name = segmentName(sg.kind);
        char info[32];
        std::snprintf(info, sizeof(info), "G%d  %.2f", sg.cluster, sg.energy);
        if (d_.textWidth(name, 1.4f) + 8 < x1 - x0) d_.text(name, x0 + 5, y + 6, 1.4f, TEXT);
        if (d_.textWidth(info, 1.15f) + 8 < x1 - x0) d_.text(info, x0 + 5, y + 24, 1.15f, withA(TEXT, 0.75f));
    }
    y += hS + 8;

    // Phase timebar: one block per scene phase (what the game shows), played part bright, knob at the
    // playhead. Click anywhere on it to seek.
    const float hL = 30;
    label("PHASES", y, hL);
    const float px = X(now());
    for (size_t i = 0; i < plan_.phases.size(); i++) {
        const ScenePhase& p = plan_.phases[i];
        float x0 = X(p.start), x1 = X(p.end);
        Col c = levelColor(p.level);
        d_.rect(x0, y, x1 - x0 - 2, hL, withA(c, 0.35f));
        float played = std::min(x1 - 2, px) - x0;
        if (played > 0) d_.rect(x0, y, played, hL, c);
        std::string lbl = std::string(levelName(p.level)) + "  " + mmss(p.start);
        Col tc = played > d_.textWidth(lbl, 1.3f) + 8 ? BG : withA(TEXT, 0.9f); // readable on bright or dim part
        if (d_.textWidth(lbl, 1.3f) + 10 < x1 - x0) d_.text(lbl, x0 + 6, y + 9, 1.3f, tc);
        else if (d_.textWidth(levelName(p.level), 1.3f) + 10 < x1 - x0) d_.text(levelName(p.level), x0 + 6, y + 9, 1.3f, tc);
    }
    d_.rect(px - 7, y - 4, 14, hL + 8, TEXT);
    d_.rect(px - 4, y - 1, 8, hL + 2, BG);
    d_.rect(px - 2, y + 1, 4, hL - 2, TEXT);
    y += hL + 8;

    // Pulse zones (envelope).
    const float hP = 16;
    label("PULSES", y, hP);
    d_.rect(L, y, W, hP, PANEL);
    for (float x = L; x < R; x += 2) {
        float e = plan_.pulseEnvelope((x - L) / W * dur);
        d_.rect(x, y + hP * (1 - e), 2, hP * e, rgb(1.f, 0.62f, 0.3f, 0.35f + 0.6f * pulseTempoAmp(an.fp.bpm)));
    }
    y += hP + 8;

    // Spectrum.
    const float hSp = 110;
    label("SPECTRUM", y, hSp);
    d_.image(specTex_, L, y, W, hSp, 0.f, 1.f);
    y += hSp + 8;

    // Curves: loudness, intensity (drives levels and pace), onsets.
    const float hC = 80;
    label("ENERGY", y, hC);
    d_.rect(L, y, W, hC, PANEL);
    const int OV = (int)ovLoud_.size();
    for (int i = 0; i < OV; i++) {
        float x = L + W * i / OV;
        float o = ovOnset_[i];
        if (o > 0.25f) d_.rect(x, y + hC - o * hC * 0.35f, std::max(1.f, W / OV), o * hC * 0.35f, rgb(0.5f, 0.75f, 1.f, 0.45f));
    }
    for (int i = 1; i < OV; i++) {
        float x0 = L + W * (i - 1) / OV, x1 = L + W * i / OV;
        d_.line(x0, y + hC * (1 - ovLoud_[i - 1]), x1, y + hC * (1 - ovLoud_[i]), 1.f, rgb(0.6f, 0.6f, 0.7f, 0.6f));
        d_.line(x0, y + hC * (1 - ovInt_[i - 1]), x1, y + hC * (1 - ovInt_[i]), 2.2f, rgb(0.98f, 0.8f, 0.35f));
    }
    y += hC + 8;

    // Bars.
    const float hB = 14;
    label("BARS", y, hB);
    for (size_t i = an.downbeat; i < an.beats.size(); i += 4) d_.rect(X(an.beats[i]), y, 1, hB, withA(DIM, 0.8f));
    for (size_t i = 0; i < samples_.size(); i++) {
        float x0 = X(samples_[i].start), x1 = std::max(x0 + 4, X(samples_[i].end));
        d_.rect(x0, y, x1 - x0, hB, sampleColor(samples_[i].kind));
        d_.text(std::to_string(i + 1), x1 + 3, y + 2, 1.1f, sampleColor(samples_[i].kind));
    }
    y += hB;
    ovBottom_ = y;

    // Scene changes across all rows, and the playhead.
    for (size_t i = 1; i < plan_.phases.size(); i++) {
        float x = X(plan_.phases[i].start);
        d_.rect(x - 1, ovTop_ - 6, 2, ovBottom_ - ovTop_ + 12, rgb(1, 1, 1, 0.45f));
    }
    d_.rect(px - 1, ovTop_ - 10, 2, ovBottom_ - ovTop_ + 20, rgb(1, 1, 1, 0.95f));

    // Notes: a flag above the time axis.
    for (const Note& n : notes_) {
        float x = X(n.at);
        d_.rect(x - 1, ovTop_ - 10, 2, ovBottom_ - ovTop_ + 20, withA(NOTE, 0.7f));
        d_.tri(x - 6, ovTop_ - 18, x + 6, ovTop_ - 18, x, ovTop_ - 8, NOTE);
    }
}

// Zoomed view around the playhead at analysis resolution.
void Scope::drawDetail(float L, float R, float y, float bottom) {
    const Analysis& an = track_->analysis;
    const double dur = std::max(1.0, track_->duration());
    const float W = R - L;
    const double t0 = now() - span_ * 0.35;
    auto X = [&](double t) { return L + (float)((t - t0) / span_) * W; };
    dtTop_ = y;
    dtT0_ = (float)t0;
    auto label = [&](const char* s, float yy, float h) { d_.text(s, L - 14, yy + h * 0.5f - 5, 1.3f, LABEL, 2); };

    // Segments strip.
    const float hS = 20;
    label("DETAIL", y, hS);
    for (const Segment& sg : an.segments) {
        float x0 = std::max(L, X(sg.start)), x1 = std::min(R, X(sg.end));
        if (x1 <= x0) continue;
        d_.rect(x0, y, x1 - x0, hS, withA(kindColor(sg.kind), 0.85f));
        if (d_.textWidth(segmentName(sg.kind), 1.2f) + 8 < x1 - x0) d_.text(segmentName(sg.kind), x0 + 5, y + 5, 1.2f, TEXT);
    }
    y += hS + 6;

    // Spectrum window.
    float hSp = std::max(60.f, (bottom - y) * 0.45f);
    label("SPECTRUM", y, hSp);
    d_.rect(L, y, W, hSp, BG);
    double a = std::max(0.0, t0), b = std::min(dur, t0 + span_);
    if (b > a) d_.image(specTex_, X(a), y, X(b) - X(a), hSp, (float)(a / dur), (float)(b / dur));
    y += hSp + 6;

    // Loudness / intensity / onset at frame resolution.
    float hC = std::max(50.f, bottom - y - 34);
    label("ENERGY", y, hC);
    d_.rect(L, y, W, hC, PANEL);
    const int n = (int)an.frames.size();
    int f0 = std::clamp((int)(a / an.hop), 0, n - 1), f1 = std::clamp((int)(b / an.hop), 0, n - 1);
    int step = std::max(1, (f1 - f0) / 1500);
    float plx = -1, ply = 0, pix = 0, piy = 0;
    for (int f = f0; f <= f1; f += step) {
        float x = X(f * an.hop);
        float o = an.frames[f].onset;
        if (o > 0.15f) d_.rect(x, y + hC - o * hC * 0.6f, 2, o * hC * 0.6f, rgb(0.5f, 0.75f, 1.f, 0.35f + 0.5f * o));
        float ly = y + hC * (1 - an.frames[f].loud), iy = y + hC * (1 - an.intensity[f]);
        if (plx >= 0) {
            d_.line(plx, ply, x, ly, 1.f, rgb(0.6f, 0.6f, 0.7f, 0.7f));
            d_.line(pix, piy, x, iy, 2.2f, rgb(0.98f, 0.8f, 0.35f));
        }
        plx = pix = x;
        ply = ly;
        piy = iy;
    }
    y += hC + 6;

    // Beats: bar lines taller, with the bar's first beat marked "1".
    const float hB = 22;
    label("BEATS", y, hB);
    for (size_t i = 0; i < samples_.size(); i++) {
        float x0 = std::max(L, X(samples_[i].start)), x1 = std::min(R, X(samples_[i].end));
        if (x1 <= x0) continue;
        d_.rect(x0, y, x1 - x0, hB, withA(sampleColor(samples_[i].kind), 0.3f));
        if (x1 - x0 > 60) d_.text(std::string(sampleName(samples_[i].kind)), x1 - 6, y + 6, 1.1f, sampleColor(samples_[i].kind), 2);
    }
    for (size_t i = 0; i < an.beats.size(); i++) {
        float x = X(an.beats[i]);
        if (x < L || x > R) continue;
        bool bar = ((int)i - an.downbeat) % 4 == 0;
        d_.rect(x, y + (bar ? 0 : 8), bar ? 2 : 1, bar ? hB : hB - 8, bar ? withA(TEXT, 0.8f) : withA(DIM, 0.9f));
        if (bar) d_.text("1", x + 4, y + 1, 1.1f, withA(LABEL, 0.9f));
    }
    y += hB;
    dtBottom_ = y;

    // Scene changes and playhead.
    for (size_t i = 1; i < plan_.phases.size(); i++) {
        float x = X(plan_.phases[i].start);
        if (x >= L && x <= R) d_.rect(x - 1, dtTop_ - 4, 3, dtBottom_ - dtTop_ + 8, rgb(1, 1, 1, 0.5f));
    }
    float px = X(now());
    d_.rect(px - 1, dtTop_ - 6, 2, dtBottom_ - dtTop_ + 12, rgb(1, 1, 1, 0.95f));

    // Notes, with their text.
    for (const Note& n : notes_) {
        float x = X(n.at);
        if (x < L || x > R) continue;
        d_.rect(x - 1, dtTop_ - 6, 3, dtBottom_ - dtTop_ + 12, NOTE);
        std::string t = asciiFold(n.text);
        float tw = d_.textWidth(t, 1.3f);
        float tx = x + 8 + tw > R ? x - 8 - tw : x + 8;
        d_.rect(tx - 4, dtTop_ + 26, tw + 8, 20, withA(BG, 0.85f));
        d_.text(t, tx, dtTop_ + 30, 1.3f, NOTE);
    }
}

// What the game is doing right now.
void Scope::drawReadout(float L, float R, float y) {
    const Analysis& an = track_->analysis;
    const double t = now();
    const Segment& sg = an.segments.empty() ? Segment{} : an.segments[an.segmentAt(t)];
    const ScenePhase& ph = plan_.phases[plan_.phaseAt(t)];
    const StructureProfile prof = structureProfile(an, t);
    const FrameFeatures f = an.at(t);
    double bp = an.beatPosition(t);
    int beatIdx = (int)std::floor(bp);
    int inBar = ((beatIdx - an.downbeat) % 4 + 4) % 4 + 1;
    float phase = (float)(bp - beatIdx);

    d_.rect(L, y, R - L, 70, PANEL);
    float x = L + 14;
    d_.text(mmss(t) + " / " + mmss(track_->duration()), x, y + 10, 2.f, TEXT);
    const float sp = audio_.speed();
    char speedBuf[32];
    std::snprintf(speedBuf, sizeof(speedBuf), "PLAYING X%g", sp);
    std::string state = paused_ ? "PAUSED" : sp == 1.f ? "PLAYING" : speedBuf;
    d_.text(state, x, y + 40, 1.3f, paused_ ? LABEL : sp == 1.f ? rgb(0.5f, 0.85f, 0.6f) : rgb(1.f, 0.7f, 0.35f));
    x += 190;

    d_.rect(x, y + 10, 14, 50, kindColor(sg.kind));
    d_.text(segmentName(sg.kind), x + 24, y + 10, 2.f, TEXT);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "GROUP %d   ENERGY %.2f   RISE %+.2f", sg.cluster, sg.energy, sg.rise);
    d_.text(buf, x + 24, y + 40, 1.3f, LABEL);
    x += 330;

    d_.rect(x, y + 10, 14, 50, levelColor(ph.level));
    d_.text(std::string("SCENE ") + levelName(ph.level), x + 24, y + 10, 2.f, TEXT);
    float pulse = plan_.pulseEnvelope(t);
    std::snprintf(buf, sizeof(buf), "PULSE ZONE %.2f   HITS %s", pulse, hitTempoAmp(an.fp.bpm) > 0.01f ? "ON" : "OFF");
    d_.text(buf, x + 24, y + 40, 1.3f, LABEL);
    x += 300;

    // Profile meters.
    const char* names[4] = {"DENSITY", "SPEED", "GLOW", "SATUR."};
    float vals[4] = {prof.density, prof.speed, prof.glow, prof.saturation};
    for (int i = 0; i < 4; i++) {
        float yy = y + 8 + i * 15;
        d_.text(names[i], x, yy, 1.1f, LABEL);
        d_.rect(x + 70, yy + 1, 110, 8, BG);
        d_.rect(x + 70, yy + 1, 110 * saturate(vals[i] / 1.5f), 8, rgb(0.98f, 0.8f, 0.35f));
    }
    x += 200;

    // Beat position in the bar.
    for (int i = 1; i <= 4; i++) {
        bool cur = i == inBar;
        float s = cur ? 18 + 10 * std::exp(-phase * 5) : 14;
        float cx = x + (i - 1) * 32 + 14, cy = y + 28;
        d_.rect(cx - s / 2, cy - s / 2, s, s, cur ? (i == 1 ? rgb(1, 0.62f, 0.3f) : TEXT) : withA(DIM, 0.6f));
    }
    std::snprintf(buf, sizeof(buf), "BAR BEAT %d", inBar);
    d_.text(buf, x, y + 50, 1.2f, LABEL);
    x += 150;

    // Live band levels.
    if (x + 16 * 7 < R) {
        for (int b = 0; b < NUM_BANDS; b++) {
            float v = f.bands[b];
            d_.rect(x + b * 7, y + 60 - 50 * v, 5, 50 * v, rgb(0.5f + 0.5f * v, 0.35f + 0.3f * v, 0.7f - 0.3f * v));
        }
        d_.text("BANDS", x, y + 62, 1.0f, LABEL);
    }
}

// Clicking the active speed again goes back to normal speed.
void Scope::setSpeed(float s) { audio_.setSpeed(audio_.speed() == s ? 1.f : s); }

// Speed buttons, right-aligned at the top.
void Scope::drawSpeed(float R, float y) {
    speedBoxes_.clear();
    const float speeds[3] = {1.f, 0.9f, 0.5f};
    const char* names[3] = {"X1", "X0.9", "X0.5"};
    const float bw = 64, bh = 26, gap = 8;
    float x = R - 3 * bw - 2 * gap;
    d_.text("SPEED", x - 12, y + 8, 1.3f, LABEL, 2);
    for (int i = 0; i < 3; i++, x += bw + gap) {
        bool on = audio_.speed() == speeds[i];
        Col c = i == 0 ? rgb(0.5f, 0.85f, 0.6f) : rgb(1.f, 0.7f, 0.35f);
        d_.rect(x, y, bw, bh, on ? c : PANEL);
        d_.text(names[i], x + bw * 0.5f, y + 8, 1.3f, on ? BG : TEXT, 1);
        speedBoxes_.push_back({{x, y, x + bw, y + bh}, speeds[i]});
    }
}

void Scope::playSample(int i) {
    if (i < 0 || i >= (int)samples_.size()) return;
    audio_.playSample(samples_[i].pcm);
    sampleStarted_[i] = glfwGetTime();
}

// One button per sample: click (or 1-4) plays it over the song, or alone when paused.
void Scope::drawSamples(float L, float R, float y) {
    const float hB = 46, gap = 12;
    d_.text("SAMPLES", L - 14, y + hB * 0.5f - 5, 1.3f, LABEL, 2);
    sampleBoxes_.clear();
    if (samples_.empty()) {
        d_.text("NO SAMPLES (SONG TOO SHORT OR NO BEAT GRID)", L, y + 16, 1.3f, DIM);
        return;
    }
    const float bw = (R - L - gap * (SMP_COUNT - 1)) / SMP_COUNT;
    const double nowT = glfwGetTime();
    for (size_t i = 0; i < samples_.size(); i++) {
        const SongSample& sm = samples_[i];
        float x = L + i * (bw + gap);
        sampleBoxes_.push_back({x, y, x + bw, y + hB});
        Col c = sampleColor(sm.kind);
        double since = nowT - sampleStarted_[i], len = sm.end - sm.start;
        bool playing = since >= 0 && since < len;
        d_.rect(x, y, bw, hB, playing ? withA(c, 0.22f) : PANEL);
        d_.rect(x, y, 6, hB, c);
        if (playing) d_.rect(x + 6, y + hB - 3, (bw - 6) * (float)(since / len), 3, c); // progress
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%zu  %s", i + 1, sampleName(sm.kind));
        d_.text(buf, x + 16, y + 7, 1.8f, TEXT);
        std::snprintf(buf, sizeof(buf), "%s  %s  %d BEAT%s  %.2f S", sampleLines(sm.kind), mmss(sm.start).c_str(), sm.beats,
                      sm.beats > 1 ? "S" : "", len);
        d_.text(buf, x + bw - 10, y + 9, 1.15f, LABEL, 2);
        d_.text(sm.why, x + 16, y + 29, 1.15f, withA(TEXT, 0.7f));
    }
}

void Scope::drawFrame(int w, int h) {
    d_.begin(w, h);
    d_.rect(0, 0, (float)w, (float)h, BG);
    const float L = 150, R = w - 30.f;
    if (!track_) {
        d_.text(files_.empty() ? "NO SONGS FOUND" : "ANALYZING...", w * 0.5f, h * 0.5f - 10, 3.f, TEXT, 1);
        d_.flush();
        return;
    }
    const Footprint& fp = track_->analysis.fp;
    d_.text(asciiFold(track_->title), 30, 20, 2.6f, TEXT);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%.1f BPM   %s   BRIGHTNESS %.2f   BASS %.2f   AIR %.2f   DYNAMICS %.2f   DENSITY %.2f   "
                  "%zu SEGMENTS   %zu SCENE CHANGES",
                  fp.bpm, keyName(fp.key, fp.minor).c_str(), fp.brightness, fp.bassWeight, fp.airWeight, fp.dynamics,
                  fp.density, track_->analysis.segments.size(), plan_.phases.size() - 1);
    d_.text(buf, 30, 52, 1.4f, LABEL);
    std::snprintf(buf, sizeof(buf), "SONG %d/%zu", index_ + 1, files_.size());
    d_.text(buf, R, 22, 1.4f, LABEL, 2);
    if (loading_.valid())
        d_.text("LOADING  " + asciiFold(loadingTitle_) + " ...", R - d_.textWidth(buf, 1.4f) - 30, 22, 1.3f,
                rgb(1.f, 0.7f, 0.35f), 2);
    drawSpeed(R, 46);

    drawOverview(L, R, 90);
    float y = ovBottom_ + 22;
    drawReadout(L, R, y);
    y += 70 + 16;
    drawSamples(L, R, y);
    y += 46 + 22;
    drawDetail(L, R, y, (float)h - 70);

    // Legend and controls.
    float lx = L, ly = h - 50.f;
    for (int k = 0; k < SEG_COUNT; k++) {
        d_.rect(lx, ly, 12, 12, kindColor(k));
        d_.text(segmentName(k), lx + 18, ly + 1, 1.2f, LABEL);
        lx += 18 + d_.textWidth(segmentName(k), 1.2f) + 22;
    }
    lx += 20;
    for (int l = 0; l < 3; l++) {
        d_.rect(lx, ly, 12, 12, levelColor(l));
        d_.text(levelName(l), lx + 18, ly + 1, 1.2f, LABEL);
        lx += 18 + d_.textWidth(levelName(l), 1.2f) + 22;
    }
    d_.text("WHITE LINES: SCENE CHANGES   YELLOW: INTENSITY   GREY: LOUDNESS   BLUE: ONSETS   TABS: GROUPS", L,
            h - 26.f, 1.2f, DIM);
    d_.text("SPACE PLAY/PAUSE  LEFT/RIGHT SEEK  UP/DOWN ZOOM  N/P SONG  CLICK SEEK  1-4 SAMPLES  A NOTE  ESC QUIT", R,
            h - 26.f, 1.2f, DIM, 2);
    if (glfwGetTime() < flashUntil_) d_.text(flash_, R - 340, 52, 1.4f, NOTE, 2); // left of the speed buttons
    if (typing_) drawNoteEditor(w, h);
    d_.flush();
}

int Scope::run(int argc, char** argv) {
    std::vector<std::string> paths;
    std::string shotPath, song;
    double shotAt = -1;
    notesPath_ = (platform::dataDir() / "zentris" / "annotations.jsonl").string();
    bool mute = false, hidden = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
        else if (a == "--at" && i + 1 < argc) shotAt = std::stod(argv[++i]);
        else if (a == "--song" && i + 1 < argc) song = argv[++i];
        else if (a == "--notes" && i + 1 < argc) notesPath_ = argv[++i];
        else if (a == "--mute") mute = true;
        else if (a == "--hidden") hidden = true;
        else if (a == "-h" || a == "--help") {
            std::printf("usage: zenscope [options] [songs or folders...]\n"
                        "  (default: the same built-in YouTube playlist as zentris; ytdl:<video id> plays one\n"
                        "   YouTube song)\n"
                        "  --song ID|TITLE   start at this song (YouTube id, or part of the title)\n"
                        "  --at SEC          start at this time\n"
                        "  --notes FILE      where A writes notes (default: %s)\n"
                        "  --mute            no sound\n"
                        "  --shot FILE.png   render one frame (at --at, default 60 s) then exit\n",
                        notesPath_.c_str());
            return 0;
        } else paths.push_back(a);
    }
    // No songs given: the default playlist (same as zentris).
    if (paths.empty()) paths.push_back(DEFAULT_PLAYLIST);
    Library lib;
    lib.scan(paths);
    files_ = lib.files();
    const bool shot = !shotPath.empty();
    if (shot && shotAt < 0) shotAt = 60;
    int first = 0;
    if (!song.empty()) {
        std::string q = asciiFold(song);
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        first = -1;
        for (size_t i = 0; i < files_.size() && first < 0; i++) {
            std::string key = files_[i].substr(0, files_[i].find('\t'));
            std::string title = isYoutubeEntry(files_[i]) ? youtubeEntryTitle(files_[i]) : fs::path(files_[i]).stem().string();
            title = asciiFold(title);
            std::transform(title.begin(), title.end(), title.begin(), ::tolower);
            if (key == song || key == "ytdl:" + song || title.find(q) != std::string::npos) first = (int)i;
        }
        if (first < 0) {
            std::fprintf(stderr, "[zenscope] no song matches \"%s\"; trying it as a YouTube id\n", song.c_str());
            files_.insert(files_.begin(), youtubeEntryForId(song));
            first = 0;
        }
    }
    std::printf("[zenscope] notes: %s\n", notesPath_.c_str());

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (hidden || shot) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    win_ = glfwCreateWindow(1680, 960, "Zentris - scope", nullptr, nullptr);
    if (!win_) return 1;
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(shot ? 0 : 1);
    if (!gladLoadGL(glfwGetProcAddress)) return 1;
    glGetError();
    d_.init();

    audio_.init();
    audio_.setMuted(mute || shot);
    glfwSetWindowUserPointer(win_, this);
    glfwSetCharCallback(win_, onChar);
    glfwSetKeyCallback(win_, onKey);
    load(first);
    bool startSeek = shotAt >= 0;

    bool prevKeys[512] = {}, typedLastFrame = false;
    bool prevMouse = false;
    while (!glfwWindowShouldClose(win_)) {
        glfwPollEvents();
        if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            index_ = loadingIndex_;
            onTrackReady(loading_.get());
            if (requested_ != loadingIndex_) load(requested_);
            // Download the neighbours ahead (YouTube) so N/P are quick.
            std::vector<std::string> ahead;
            for (int d : {1, -1}) {
                const std::string& e = files_[(index_ + d + files_.size()) % files_.size()];
                if (isYoutubeEntry(e)) ahead.push_back(e);
            }
            if (!ahead.empty()) prefetchYoutubeAudio(ahead);
            if (shot) {
                seekPreview_ = shotAt;
                audio_.setPaused(true);
            } else if (startSeek && track_) {
                audio_.seek(shotAt);
                startSeek = false;
            }
        }
        // While a note is typed (and on the frame it closes), keys only go to the note. pressed() still runs
        // for every key each frame, so a key held while typing is not seen as a new press afterwards.
        const bool keysLive = !typing_ && !typedLastFrame;
        typedLastFrame = typing_;
        auto pressed = [&](int k) {
            bool d = glfwGetKey(win_, k) == GLFW_PRESS;
            bool p = d && !prevKeys[k];
            prevKeys[k] = d;
            return p && keysLive;
        };
        if (pressed(GLFW_KEY_ESCAPE)) break;
        if (pressed(GLFW_KEY_SPACE)) {
            paused_ = !paused_;
            audio_.setPaused(paused_);
        }
        if (pressed(GLFW_KEY_N)) load(index_ + 1);
        if (pressed(GLFW_KEY_P)) load(index_ - 1);
        if (pressed(GLFW_KEY_RIGHT) && track_) audio_.seek(audio_.position() + 5);
        if (pressed(GLFW_KEY_LEFT) && track_) audio_.seek(audio_.position() - 5);
        if (pressed(GLFW_KEY_UP)) span_ = std::max(4.f, span_ * 0.5f);
        if (pressed(GLFW_KEY_DOWN)) span_ = std::min(64.f, span_ * 2.f);
        for (int k = 0; k < SMP_COUNT; k++)
            if (pressed(GLFW_KEY_1 + k)) playSample(k);

        bool mouse = glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (mouse && track_) {
            double mx, my;
            glfwGetCursorPos(win_, &mx, &my);
            int ww, wh, fw, fh;
            glfwGetWindowSize(win_, &ww, &wh);
            glfwGetFramebufferSize(win_, &fw, &fh);
            mx *= (double)fw / std::max(1, ww);
            my *= (double)fh / std::max(1, wh);
            auto inside = [&](const Box& b) { return mx >= b.x0 && mx <= b.x1 && my >= b.y0 && my <= b.y1; };
            if (!prevMouse) {
                for (size_t i = 0; i < sampleBoxes_.size(); i++)
                    if (inside(sampleBoxes_[i])) playSample((int)i);
                for (const auto& [b, s] : speedBoxes_)
                    if (inside(b)) setSpeed(s);
            }
            if (mx >= ovL_ && mx <= ovR_) {
                if (my >= ovTop_ && my <= ovBottom_)
                    audio_.seek((mx - ovL_) / (ovR_ - ovL_) * track_->duration());
                else if (!prevMouse && my >= dtTop_ && my <= dtBottom_)
                    audio_.seek(dtT0_ + (mx - ovL_) / (ovR_ - ovL_) * span_);
            }
        }
        prevMouse = mouse;
        if (track_ && audio_.finished()) load(index_ + 1);

        int w, h;
        glfwGetFramebufferSize(win_, &w, &h);
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawFrame(w, h);

        if (shot && track_) {
            std::vector<unsigned char> px((size_t)w * h * 3), fl(px.size());
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < h; y++) std::memcpy(&fl[(size_t)y * w * 3], &px[(size_t)(h - 1 - y) * w * 3], w * 3);
            stbi_write_png(shotPath.c_str(), w, h, 3, fl.data(), w * 3);
            std::printf("[zenscope] wrote %s\n", shotPath.c_str());
            break;
        }
        glfwSwapBuffers(win_);
    }
    audio_.shutdown();
    glfwDestroyWindow(win_);
    glfwTerminate();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    platform::lineBufferStdout();
    Scope s;
    int rc = s.run(argc, argv);
    std::fflush(stdout);
    std::_Exit(rc); // don't wait for background downloads/analysis
}
