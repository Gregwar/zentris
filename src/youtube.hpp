#pragma once
// YouTube playlists / videos as a song source, through the external yt-dlp tool (personal use: downloading
// from YouTube is against its terms of service). Songs are fetched on demand and cached as MP3.
#include <string>
#include <vector>

// True for http(s) URLs.
bool isUrl(const std::string& s);

// Library entries for remote songs look like "ytdl:<id>\t<title>".
bool isYoutubeEntry(const std::string& entry);
std::string youtubeEntryTitle(const std::string& entry);

// Expands a playlist / video URL into library entries (empty on failure; prints why).
std::vector<std::string> resolveYoutube(const std::string& url);

// True if the entry's audio is already in the cache.
bool isYoutubeCached(const std::string& entry);

// Downloads (or reuses from cache) the audio of an entry now; returns a local file path, or "" on failure.
// Never waits behind background downloads (only for the same song if it is already being downloaded).
std::string fetchYoutubeAudio(const std::string& entry);

// Queues songs for background download (one at a time, never blocking the caller).
void prefetchYoutubeAudio(const std::vector<std::string>& entries);
