#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../models/Track.h"

namespace BeatMate::UI {

struct StagedFile {
    juce::String path;
    juce::String fileName;
    juce::String ext;
    bool selected = true;

    enum class MetaState { Pending, Loaded, Failed };
    enum class DupState { Unknown, Unique, ExactDuplicate, ProbableDuplicate };

    MetaState metaState = MetaState::Pending;
    DupState dupState = DupState::Unknown;
    float dupConfidence = 0.0f;
    juce::String dupMatchLabel;

    Models::Track meta;
    juce::Image cover;

    juce::String titleOverride;
    juce::String artistOverride;
    juce::String albumOverride;
    juce::String genreOverride;

    juce::String displayTitle() const
    {
        if (titleOverride.isNotEmpty()) return titleOverride;
        if (!meta.title.empty()) return juce::String::fromUTF8(meta.title.c_str());
        return fileName;
    }
    juce::String displayArtist() const
    {
        if (artistOverride.isNotEmpty()) return artistOverride;
        return juce::String::fromUTF8(meta.artist.c_str());
    }
    juce::String displayAlbum() const
    {
        if (albumOverride.isNotEmpty()) return albumOverride;
        return juce::String::fromUTF8(meta.album.c_str());
    }
    juce::String displayGenre() const
    {
        if (genreOverride.isNotEmpty()) return genreOverride;
        return juce::String::fromUTF8(meta.genre.c_str());
    }
};

} // namespace BeatMate::UI
