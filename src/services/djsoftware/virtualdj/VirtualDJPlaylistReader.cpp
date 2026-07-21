#include "VirtualDJPlaylistReader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

namespace fs = std::filesystem;

namespace BeatMate::Services::VirtualDJ {

namespace {

std::string trimLine(const std::string& raw) {
    std::string s = raw;
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF
        && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'
                        || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.erase(0, 1);
    }
    return s;
}

std::vector<VDJPlaylistEntry> parseM3u(const fs::path& file) {
    std::vector<VDJPlaylistEntry> out;
    std::ifstream in(file, std::ios::binary);
    if (!in) return out;
    std::string line;
    int pos = 0;
    while (std::getline(in, line)) {
        auto t = trimLine(line);
        if (t.empty() || t[0] == '#') continue;
        VDJPlaylistEntry e;
        e.filePath = t;
        e.position = pos++;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<VDJPlaylistEntry> parseVdjFolder(const fs::path& file) {
    std::vector<VDJPlaylistEntry> out;
    juce::File f{juce::String(file.string())};
    if (!f.existsAsFile()) return out;

    if (auto xml = juce::XmlDocument::parse(f)) {
        int pos = 0;
        for (auto* e = xml->getFirstChildElement(); e != nullptr; e = e->getNextElement()) {
            if (e->getTagName().equalsIgnoreCase("song")) {
                juce::String path = e->getStringAttribute("path");
                if (path.isEmpty()) path = e->getStringAttribute("FilePath");
                if (path.isEmpty()) continue;
                VDJPlaylistEntry entry;
                entry.filePath = path.toStdString();
                entry.position = pos++;
                out.push_back(std::move(entry));
            }
        }
        if (!out.empty()) return out;
    }

    return parseM3u(file);
}

bool isPlaylistExtension(const fs::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".m3u" || ext == ".m3u8" || ext == ".vdjfolder";
}

void walkDir(const fs::path& dir, const std::string& logicalParent,
             std::vector<VDJPlaylistInfo>& out) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

    if (!logicalParent.empty()) {
        VDJPlaylistInfo folder;
        folder.isFolder = true;
        folder.fullPath = logicalParent;
        folder.name = dir.filename().string();
        folder.parentPath = fs::path(logicalParent).parent_path().generic_string();
        if (folder.parentPath.empty()) folder.parentPath = "/";
        folder.filePath = dir.string();
        out.push_back(std::move(folder));
    }

    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto& p = entry.path();
        if (entry.is_directory(ec)) {
            const std::string childLogical = logicalParent.empty()
                ? std::string("/") + p.filename().string()
                : logicalParent + "/" + p.filename().string();
            walkDir(p, childLogical, out);
        } else if (entry.is_regular_file(ec) && isPlaylistExtension(p)) {
            VDJPlaylistInfo info;
            info.isFolder = false;
            info.name = p.stem().string();
            info.fullPath = (logicalParent.empty() ? std::string("/") : logicalParent + "/") + info.name;
            info.parentPath = logicalParent.empty() ? std::string("/") : logicalParent;
            info.filePath = p.string();

            if (p.extension() == ".vdjfolder") {
                info.entries = parseVdjFolder(p);
            } else {
                info.entries = parseM3u(p);
            }
            out.push_back(std::move(info));
        }
    }
}

} // namespace

std::string VirtualDJPlaylistReader::findDefaultVdjRoot() {
    std::vector<juce::File> candidates;

    if (auto* la = std::getenv("LOCALAPPDATA"); la && *la) {
        candidates.push_back(juce::File(juce::String(la)).getChildFile("VirtualDJ"));
    }
    if (auto* ad = std::getenv("APPDATA"); ad && *ad) {
        candidates.push_back(juce::File(juce::String(ad)).getChildFile("VirtualDJ"));
    }
    candidates.push_back(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("VirtualDJ"));
    candidates.push_back(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                             .getChildFile("VirtualDJ"));

    for (const auto& c : candidates) {
        if (c.isDirectory()) {
            spdlog::info("VirtualDJPlaylistReader: VDJ root = {}",
                         c.getFullPathName().toStdString());
            return c.getFullPathName().toStdString();
        }
    }
    return {};
}

std::vector<VDJPlaylistInfo>
VirtualDJPlaylistReader::readPlaylists(const std::string& vdjRoot) {
    std::vector<VDJPlaylistInfo> out;
    const std::string root = vdjRoot.empty() ? findDefaultVdjRoot() : vdjRoot;
    if (root.empty()) {
        spdlog::warn("VirtualDJPlaylistReader: VirtualDJ root not found");
        return out;
    }

    const char* kSubDirs[] = { "MyLists", "Playlists", "Folders", "VirtualFolders" };
    bool anyFound = false;
    for (const char* sub : kSubDirs) {
        fs::path dir = fs::path(root) / sub;
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
        anyFound = true;
        const std::string before = std::to_string(out.size());
        walkDir(dir, "", out);
        spdlog::info("VirtualDJPlaylistReader: scanned {} -> {} nodes total",
                     dir.string(), out.size());
    }
    if (!anyFound) {
        spdlog::warn("VirtualDJPlaylistReader: no MyLists/Playlists/Folders dir in {}", root);
    }
    return out;
}

} // namespace BeatMate::Services::VirtualDJ
