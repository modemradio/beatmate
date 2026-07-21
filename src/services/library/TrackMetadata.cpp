#include "TrackMetadata.h"

#include <spdlog/spdlog.h>

#ifndef BEATMATE_NO_TAGLIB
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/toolkit/tpropertymap.h>
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/id3v2frame.h>
#include <taglib/mpeg/id3v2/frames/attachedpictureframe.h>
#include <taglib/flac/flacfile.h>
#include <taglib/flac/flacpicture.h>
#include <taglib/riff/wav/wavfile.h>
#include <taglib/ogg/oggfile.h>
#include <taglib/ogg/vorbis/vorbisfile.h>
#include <taglib/mp4/mp4file.h>
#include <taglib/mp4/mp4tag.h>
#include <taglib/mp4/mp4coverart.h>
#include <taglib/riff/aiff/aifffile.h>
#include <taglib/asf/asffile.h>
#include <taglib/ogg/xiphcomment.h>
#endif

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <juce_audio_formats/juce_audio_formats.h>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

std::optional<Models::Track> TrackMetadata::readMetadata(const std::string& filePath) {
    // u8path obligatoire : chemins Unicode Windows
    fs::path fsPath;
    try {
        fsPath = fs::u8path(filePath);
    } catch (...) {
        fsPath = fs::path(filePath);
    }

    try {
        if (!fs::exists(fsPath)) {
            spdlog::error("TrackMetadata: File not found: {}", filePath);
            return std::nullopt;
        }
    } catch (const std::exception& e) {
        spdlog::error("TrackMetadata: Filesystem error for '{}': {}", filePath, e.what());
        return std::nullopt;
    }

    if (!isSupportedFormat(filePath)) {
        spdlog::warn("TrackMetadata: Unsupported format: {}", filePath);
        return std::nullopt;
    }

#ifndef BEATMATE_NO_TAGLIB
    try {
#ifdef _WIN32
        TagLib::FileRef f(fsPath.wstring().c_str());
#else
        TagLib::FileRef f(filePath.c_str());
#endif
        if (f.isNull() || !f.tag()) {
            spdlog::error("TrackMetadata: Cannot read tags from: {}", filePath);
            return std::nullopt;
        }

        Models::Track track;
        track.filePath = filePath;

        TagLib::Tag* tag = f.tag();
        track.title = tag->title().toCString(true);
        track.artist = tag->artist().toCString(true);
        track.album = tag->album().toCString(true);
        track.genre = tag->genre().toCString(true);
        track.year = static_cast<int>(tag->year());
        track.comment = tag->comment().toCString(true);

        if (f.audioProperties()) {
            auto* props = f.audioProperties();
            track.duration = static_cast<double>(props->lengthInMilliseconds()) / 1000.0;
            track.sampleRate = props->sampleRate();
            track.channels = props->channels();
            track.bitRate = props->bitrate();
        }

        try {
            track.fileSize = static_cast<int64_t>(fs::file_size(fsPath));
        } catch (...) {
            track.fileSize = 0;
        }
        track.fileFormat = detectFileFormat(filePath);

        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        track.dateAdded = now;
        track.lastModified = now;

        TagLib::PropertyMap properties = f.file()->properties();

        if (properties.contains("BPM")) {
            try {
                track.bpm = std::stod(properties["BPM"].front().toCString());
            } catch (...) {}
        }

        if (properties.contains("INITIALKEY")) {
            std::string rawKey = properties["INITIALKEY"].front().toCString(true);
            // Clean up UTF-16 BOM artifacts (ÿþ prefix from Rekordbox/VirtualDJ)
            if (rawKey.size() >= 2 && (unsigned char)rawKey[0] == 0xFF && (unsigned char)rawKey[1] == 0xFE) {
                rawKey = rawKey.substr(2);
            }
            // Remove null bytes that may appear in UTF-16 LE strings
            rawKey.erase(std::remove(rawKey.begin(), rawKey.end(), '\0'), rawKey.end());
            while (!rawKey.empty() && (rawKey.front() == ' ' || rawKey.front() == '\t')) rawKey.erase(rawKey.begin());
            while (!rawKey.empty() && (rawKey.back() == ' ' || rawKey.back() == '\t')) rawKey.pop_back();
            track.key = rawKey;
            track.camelotKey = rawKey;
        }

        if (properties.contains("ENERGY")) {
            try {
                track.energy = std::stof(properties["ENERGY"].front().toCString());
            } catch (...) {}
        }

        if (properties.contains("MOOD")) {
            track.mood = properties["MOOD"].front().toCString(true);
        }

        if (properties.contains("GROUPING")) {
            track.grouping = properties["GROUPING"].front().toCString(true);
        }

        if (properties.contains("LABEL")) {
            track.label = properties["LABEL"].front().toCString(true);
        }

        if (track.title.empty()) {
            track.title = fs::path(filePath).stem().string();
        }

        spdlog::debug("TrackMetadata: Read metadata from: {} - '{}' by '{}' BPM={:.1f} Key='{}'",
            filePath, track.title, track.artist, track.bpm, track.key);
        return track;

    } catch (const std::exception& e) {
        spdlog::error("TrackMetadata: Exception reading {}: {}", filePath, e.what());
        return std::nullopt;
    } catch (...) {
        spdlog::error("TrackMetadata: Unknown exception reading {}", filePath);
        return std::nullopt;
    }
#else
    Models::Track track;
    track.filePath = filePath;
    track.fileSize = static_cast<int64_t>(fs::file_size(filePath));
    track.fileFormat = detectFileFormat(filePath);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    track.dateAdded = now;
    track.lastModified = now;

    juce::AudioFormatManager fmtMgr;
    fmtMgr.registerBasicFormats();
    juce::File file(juce::String(filePath));
    auto* rawReader = fmtMgr.createReaderFor(juce::File(juce::String(filePath)));
    std::unique_ptr<juce::AudioFormatReader> reader(rawReader);

    if (reader) {
        track.duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        track.sampleRate = static_cast<int>(reader->sampleRate);
        track.channels = static_cast<int>(reader->numChannels);
        track.bitRate = static_cast<int>(reader->bitsPerSample * reader->sampleRate * reader->numChannels / 1000);

        auto& meta = reader->metadataValues;

        if (meta.containsKey("title") && meta["title"].isNotEmpty())
            track.title = meta["title"].toStdString();
        else if (meta.containsKey("TIT2") && meta["TIT2"].isNotEmpty())
            track.title = meta["TIT2"].toStdString();
        else
            track.title = fs::path(filePath).stem().string();

        if (meta.containsKey("artist") && meta["artist"].isNotEmpty())
            track.artist = meta["artist"].toStdString();
        else if (meta.containsKey("TPE1") && meta["TPE1"].isNotEmpty())
            track.artist = meta["TPE1"].toStdString();

        if (meta.containsKey("album") && meta["album"].isNotEmpty())
            track.album = meta["album"].toStdString();
        else if (meta.containsKey("TALB") && meta["TALB"].isNotEmpty())
            track.album = meta["TALB"].toStdString();

        if (meta.containsKey("genre") && meta["genre"].isNotEmpty())
            track.genre = meta["genre"].toStdString();
        else if (meta.containsKey("TCON") && meta["TCON"].isNotEmpty())
            track.genre = meta["TCON"].toStdString();

        if (meta.containsKey("year") && meta["year"].isNotEmpty())
            track.year = meta["year"].getIntValue();
        else if (meta.containsKey("TDRC") && meta["TDRC"].isNotEmpty())
            track.year = meta["TDRC"].getIntValue();
        else if (meta.containsKey("TYER") && meta["TYER"].isNotEmpty())
            track.year = meta["TYER"].getIntValue();

        if (meta.containsKey("comment") && meta["comment"].isNotEmpty())
            track.comment = meta["comment"].toStdString();

        if (meta.containsKey("TBPM") && meta["TBPM"].isNotEmpty()) {
            try { track.bpm = meta["TBPM"].getDoubleValue(); } catch (...) {}
        } else if (meta.containsKey("bpm") && meta["bpm"].isNotEmpty()) {
            try { track.bpm = meta["bpm"].getDoubleValue(); } catch (...) {}
        }

        if (meta.containsKey("TKEY") && meta["TKEY"].isNotEmpty()) {
            track.key = meta["TKEY"].toStdString();
            track.camelotKey = track.key;
        } else if (meta.containsKey("initialkey") && meta["initialkey"].isNotEmpty()) {
            track.key = meta["initialkey"].toStdString();
            track.camelotKey = track.key;
        }

        if (meta.containsKey("energy") && meta["energy"].isNotEmpty()) {
            try { track.energy = meta["energy"].getFloatValue(); } catch (...) {}
        }

        if (meta.containsKey("TPUB") && meta["TPUB"].isNotEmpty())
            track.label = meta["TPUB"].toStdString();

        spdlog::debug("TrackMetadata: JUCE metadata for '{}': title='{}' artist='{}' bpm={:.1f} key='{}'",
            filePath, track.title, track.artist, track.bpm, track.key);
    } else {
        track.title = fs::path(filePath).stem().string();
        spdlog::warn("TrackMetadata: Cannot read metadata for: {}", filePath);
    }

    return track;
#endif
}

bool TrackMetadata::writeMetadata(const std::string& filePath, const Models::Track& track) {
#ifndef BEATMATE_NO_TAGLIB
    fs::path fsPath;
    try { fsPath = fs::u8path(filePath); } catch (...) { fsPath = fs::path(filePath); }

    try {
        if (!fs::exists(fsPath)) {
            spdlog::error("TrackMetadata: File not found for writing: {}", filePath);
            return false;
        }
    } catch (...) {
        spdlog::error("TrackMetadata: Filesystem error for writing: {}", filePath);
        return false;
    }

    try {
#ifdef _WIN32
        TagLib::FileRef f(fsPath.wstring().c_str());
#else
        TagLib::FileRef f(filePath.c_str());
#endif
        if (f.isNull() || !f.tag()) {
            spdlog::error("TrackMetadata: Cannot open file for writing: {}", filePath);
            return false;
        }

        TagLib::Tag* tag = f.tag();
        tag->setTitle(TagLib::String(track.title, TagLib::String::UTF8));
        tag->setArtist(TagLib::String(track.artist, TagLib::String::UTF8));
        tag->setAlbum(TagLib::String(track.album, TagLib::String::UTF8));
        tag->setGenre(TagLib::String(track.genre, TagLib::String::UTF8));
        tag->setYear(static_cast<unsigned int>(track.year));
        tag->setComment(TagLib::String(track.comment, TagLib::String::UTF8));

        TagLib::PropertyMap properties = f.file()->properties();

        if (track.bpm > 0) {
            properties.replace("BPM", TagLib::StringList(TagLib::String(std::to_string(static_cast<int>(track.bpm)))));
        }
        if (!track.key.empty()) {
            properties.replace("INITIALKEY", TagLib::StringList(TagLib::String(track.key, TagLib::String::UTF8)));
        }
        if (!track.mood.empty()) {
            properties.replace("MOOD", TagLib::StringList(TagLib::String(track.mood, TagLib::String::UTF8)));
        }
        if (!track.grouping.empty()) {
            properties.replace("GROUPING", TagLib::StringList(TagLib::String(track.grouping, TagLib::String::UTF8)));
        }
        if (!track.label.empty()) {
            properties.replace("LABEL", TagLib::StringList(TagLib::String(track.label, TagLib::String::UTF8)));
        }

        f.file()->setProperties(properties);

        if (!f.save()) {
            spdlog::error("TrackMetadata: Failed to save metadata to: {}", filePath);
            return false;
        }

        spdlog::debug("TrackMetadata: Wrote metadata to: {}", filePath);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("TrackMetadata: Exception writing {}: {}", filePath, e.what());
        return false;
    }
#else
    (void)filePath; (void)track;
    spdlog::warn("TrackMetadata: TagLib not available, cannot write metadata");
    return false;
#endif
}

std::vector<uint8_t> TrackMetadata::readAlbumArt(const std::string& filePath) {
    std::vector<uint8_t> artData;

    fs::path fsPath;
    try { fsPath = fs::u8path(filePath); } catch (...) { fsPath = fs::path(filePath); }

    try {
        if (!fs::exists(fsPath)) {
            return artData;
        }
    } catch (...) {
        return artData;
    }

#ifndef BEATMATE_NO_TAGLIB
    try {
        std::string ext = getFileExtension(filePath);
#ifdef _WIN32
        auto wpath = fsPath.wstring().c_str();
#else
        auto wpath = filePath.c_str();
#endif

        if (ext == "mp3") {
            TagLib::MPEG::File file(wpath);
            if (auto* id3v2 = file.ID3v2Tag()) {
                auto frames = id3v2->frameListMap()["APIC"];
                if (!frames.isEmpty()) {
                    auto* pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                    if (pic) {
                        auto data = pic->picture();
                        artData.assign(data.data(), data.data() + data.size());
                    }
                }
            }
        }
        else if (ext == "flac") {
            TagLib::FLAC::File file(wpath);
            auto pictures = file.pictureList();
            if (!pictures.isEmpty()) {
                auto data = pictures.front()->data();
                artData.assign(data.data(), data.data() + data.size());
            }
        }
        else if (ext == "m4a" || ext == "aac" || ext == "mp4") {
            TagLib::MP4::File file(wpath);
            if (auto* mp4tag = file.tag()) {
                if (mp4tag->contains("covr")) {
                    auto coverList = mp4tag->item("covr").toCoverArtList();
                    if (!coverList.isEmpty()) {
                        auto data = coverList.front().data();
                        artData.assign(data.data(), data.data() + data.size());
                    }
                }
            }
        }
        else if (ext == "aiff" || ext == "aif") {
            TagLib::RIFF::AIFF::File file(wpath);
            if (auto* id3v2 = file.tag()) {
                auto frames = id3v2->frameListMap()["APIC"];
                if (!frames.isEmpty()) {
                    auto* pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                    if (pic) {
                        auto data = pic->picture();
                        artData.assign(data.data(), data.data() + data.size());
                    }
                }
            }
        }

        if (!artData.empty()) {
            spdlog::debug("TrackMetadata: Read album art from {} ({} bytes)", filePath, artData.size());
        }

    } catch (const std::exception& e) {
        spdlog::error("TrackMetadata: Exception reading album art from {}: {}", filePath, e.what());
    }
#endif

    return artData;
}

bool TrackMetadata::writeAlbumArt(const std::string& filePath, const std::vector<uint8_t>& data) {
    if (data.empty()) return false;

    fs::path fsPath;
    try { fsPath = fs::u8path(filePath); } catch (...) { fsPath = fs::path(filePath); }

    try {
        if (!fs::exists(fsPath)) return false;
    } catch (...) { return false; }

#ifndef BEATMATE_NO_TAGLIB
    try {
        std::string ext = getFileExtension(filePath);
#ifdef _WIN32
        auto wpath = fsPath.wstring().c_str();
#else
        auto wpath = filePath.c_str();
#endif

        if (ext == "mp3") {
            TagLib::MPEG::File file(wpath);
            auto* id3v2 = file.ID3v2Tag(true);
            if (!id3v2) return false;

            id3v2->removeFrames("APIC");

            auto* frame = new TagLib::ID3v2::AttachedPictureFrame();
            frame->setMimeType("image/jpeg");
            frame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
            frame->setPicture(TagLib::ByteVector(reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size())));
            id3v2->addFrame(frame);

            return file.save();
        }
        else if (ext == "flac") {
            TagLib::FLAC::File file(wpath);
            file.removePictures();

            auto* pic = new TagLib::FLAC::Picture();
            pic->setMimeType("image/jpeg");
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setData(TagLib::ByteVector(reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size())));
            file.addPicture(pic);

            return file.save();
        }
        else if (ext == "m4a" || ext == "aac" || ext == "mp4") {
            TagLib::MP4::File file(wpath);
            auto* mp4tag = file.tag();
            if (!mp4tag) return false;

            TagLib::MP4::CoverArt cover(TagLib::MP4::CoverArt::JPEG,
                TagLib::ByteVector(reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size())));
            TagLib::MP4::CoverArtList coverList;
            coverList.append(cover);
            mp4tag->setItem("covr", coverList);

            return file.save();
        }

        spdlog::warn("TrackMetadata: Album art write not supported for format: {}", ext);
        return false;

    } catch (const std::exception& e) {
        spdlog::error("TrackMetadata: Exception writing album art to {}: {}", filePath, e.what());
        return false;
    }
#else
    spdlog::warn("TrackMetadata: TagLib not available, cannot write album art");
    return false;
#endif
}

bool TrackMetadata::isSupportedFormat(const std::string& filePath) {
    auto exts = getSupportedExtensions();
    std::string ext = fs::path(filePath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::vector<std::string> TrackMetadata::getSupportedExtensions() {
    return {"mp3", "flac", "wav", "ogg", "aac", "m4a", "aiff", "aif", "wma", "mp4", "opus"};
}

std::string TrackMetadata::getFileExtension(const std::string& filePath) const {
    std::string ext = fs::path(filePath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string TrackMetadata::detectFileFormat(const std::string& filePath) const {
    return getFileExtension(filePath);
}

std::optional<Models::Track> TrackMetadata::readMP3Metadata(const std::string& filePath) {
    return readMetadata(filePath);
}

std::optional<Models::Track> TrackMetadata::readFLACMetadata(const std::string& filePath) {
    return readMetadata(filePath);
}

std::optional<Models::Track> TrackMetadata::readWAVMetadata(const std::string& filePath) {
    return readMetadata(filePath);
}

std::optional<Models::Track> TrackMetadata::readOGGMetadata(const std::string& filePath) {
    return readMetadata(filePath);
}

std::optional<Models::Track> TrackMetadata::readAACMetadata(const std::string& filePath) {
    return readMetadata(filePath);
}

std::optional<Models::Track> TrackMetadata::readAIFFMetadata(const std::string& filePath) {
    return readMetadata(filePath);
}

std::optional<Models::Track> TrackMetadata::readWMAMetadata(const std::string& filePath) {
    return readMetadata(filePath);
}

} // namespace BeatMate::Services::Library
