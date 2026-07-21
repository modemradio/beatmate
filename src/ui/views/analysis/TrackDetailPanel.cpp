#include "TrackDetailPanel.h"
#include "AnalysisColumns.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
#include "../../../services/library/TrackCacheService.h"
#include "../../../app/ServiceLocator.h"
#include "../../../core/analysis/WaveformCacheService.h"
#include "../../../core/analysis/CamelotUtil.h"
#include "../../../core/mixing/HarmonicMixer.h"
#include "../../../models/TrackAnalysis.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <spdlog/spdlog.h>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI {

namespace {

constexpr int kContentHeight = 744;
constexpr int kPad = 16;

bool looksLikeCamelot(const juce::String& s)
{
    const int num = s.initialSectionContainingOnly("0123456789").getIntValue();
    return num >= 1 && num <= 12
        && (s.endsWithIgnoreCase("A") || s.endsWithIgnoreCase("B"));
}

// Camelot depuis camelotKey, sinon converti depuis la cle musicale analysee (Em, F#m...)
juce::String resolveCamelot(const std::string& camelotKey, const std::string& key)
{
    juce::String c(camelotKey);
    if (looksLikeCamelot(c)) return c.toUpperCase();
    c = juce::String(Core::toCamelot(camelotKey));
    if (looksLikeCamelot(c)) return c.toUpperCase();
    c = juce::String(key);
    if (looksLikeCamelot(c)) return c.toUpperCase();
    c = juce::String(Core::toCamelot(key));
    return looksLikeCamelot(c) ? c.toUpperCase() : juce::String();
}

juce::Colour sectionColour(const juce::String& type)
{
    if (type.equalsIgnoreCase("Intro"))     return Colors::accent();
    if (type.equalsIgnoreCase("Verse"))     return Colors::primary();
    if (type.equalsIgnoreCase("Chorus"))    return Colors::secondary();
    if (type.equalsIgnoreCase("Drop"))      return Colors::error();
    if (type.equalsIgnoreCase("Buildup"))   return Colors::warning();
    if (type.equalsIgnoreCase("Breakdown")) return Colors::energyMedium();
    if (type.equalsIgnoreCase("Bridge"))    return Colors::stemVocals();
    if (type.equalsIgnoreCase("Outro"))     return Colors::textMuted();
    return Colors::textDim();
}

juce::String formatDuration(double seconds)
{
    const int total = static_cast<int>(seconds + 0.5);
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

} // namespace

TrackDetailPanel::Content::Content(TrackDetailPanel& owner) : panel(owner)
{
    setSize(100, kContentHeight);
}

TrackDetailPanel::TrackDetailPanel()
{
    m_energyArc = std::make_unique<EnergyArcWidget>();
    m_energyGraph = std::make_unique<EnergyGraphWidget>();
    m_camelotWheel = std::make_unique<CamelotWheelWidget>();
    m_structure = std::make_unique<TrackStructureVisualizer>();
    m_waveform = std::make_unique<MiniWaveformInline>();
    m_content = std::make_unique<Content>(*this);

    m_content->addAndMakeVisible(*m_energyArc);
    m_content->addAndMakeVisible(*m_energyGraph);
    m_content->addAndMakeVisible(*m_camelotWheel);
    m_content->addAndMakeVisible(*m_structure);
    m_content->addAndMakeVisible(*m_waveform);

    m_viewport.setViewedComponent(m_content.get(), false);
    m_viewport.setScrollBarsShown(true, false, true, false);
    m_viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                                Colors::bgLighter());
    addAndMakeVisible(m_viewport);
    m_viewport.setVisible(false);
}

TrackDetailPanel::~TrackDetailPanel()
{
    ++m_waveformGeneration;
}

void TrackDetailPanel::setTrack(const Models::Track& track)
{
    m_track = track;
    m_hasTrack = true;
    rebuildFromTrack();
    m_viewport.setVisible(true);
    repaint();
}

void TrackDetailPanel::clearTrack()
{
    m_track = {};
    m_hasTrack = false;
    m_hasWaveform = false;
    m_hasEnergyCurve = false;
    m_hasStructure = false;
    ++m_waveformGeneration;
    m_viewport.setVisible(false);
    repaint();
}

juce::String TrackDetailPanel::confidenceText(float confidence) const
{
    if (confidence <= 0.0f)
        return {};
    return juce::String(static_cast<int>(confidence * 100.0f + 0.5f)) + "%";
}

void TrackDetailPanel::rebuildFromTrack()
{
    if (m_track.bpmConfidence <= 0.0f || m_track.keyConfidence <= 0.0f) {
        if (auto* cacheService = BeatMate::g_serviceLocator
                ? BeatMate::g_serviceLocator->tryGet<Services::Library::TrackCacheService>()
                : nullptr)
        {
            if (auto cache = cacheService->getCache(m_track.id)) {
                if (m_track.bpmConfidence <= 0.0f)
                    m_track.bpmConfidence = static_cast<float>(cache->bpmConfidence);
                if (m_track.keyConfidence <= 0.0f)
                    m_track.keyConfidence = static_cast<float>(cache->keyConfidence);
            }
        }
    }

    m_energyArc->setEnergy(m_track.energy);

    m_hasEnergyCurve = false;
    if (!m_track.energySegments.empty()) {
        try {
            auto j = nlohmann::json::parse(m_track.energySegments);
            std::vector<float> curve;
            for (const auto& seg : j)
                curve.push_back(static_cast<float>(seg.value("energy", 0)));
            if (curve.size() >= 2) {
                m_energyGraph->setScale(1, 10);
                m_energyGraph->setEnergyData(curve);
                m_hasEnergyCurve = true;
            }
        } catch (const std::exception& e) {
            spdlog::warn("[TrackDetail] energySegments parse failed for id={}: {}", m_track.id, e.what());
        }
    }
    m_energyGraph->setVisible(false);

    m_hasStructure = false;
    if (!m_track.sections.empty()) {
        try {
            auto j = nlohmann::json::parse(m_track.sections);
            std::vector<StructureSection> sections;
            for (const auto& s : j) {
                StructureSection sec;
                sec.label = juce::String(s.value("type", std::string{}));
                sec.start = s.value("startTime", 0.0);
                sec.end = s.value("endTime", 0.0);
                sec.color = sectionColour(sec.label);
                sections.push_back(sec);
            }
            if (!sections.empty()) {
                m_structure->setSections(sections);
                m_hasStructure = true;
            }
        } catch (const std::exception& e) {
            spdlog::warn("[TrackDetail] sections parse failed for id={}: {}", m_track.id, e.what());
        }
    }
    m_structure->setVisible(m_hasStructure);

    juce::String camelot = resolveCamelot(m_track.camelotKey, m_track.key);
    if (camelot.isNotEmpty()) {
        m_camelotWheel->setHighlightedKey(camelot.toUpperCase());
        Core::HarmonicMixer mixer;
        juce::StringArray compat;
        for (const auto& k : mixer.getCompatibleKeys(camelot.toUpperCase().toStdString()))
            compat.add(juce::String(k));
        m_camelotWheel->setCompatibleKeys(compat);
    } else {
        m_camelotWheel->setHighlightedKey({});
        m_camelotWheel->setCompatibleKeys({});
    }

    m_hasWaveform = false;
    m_waveform->setVisible(false);
    loadWaveformAsync();
}

void TrackDetailPanel::loadWaveformAsync()
{
    const int generation = ++m_waveformGeneration;
    const auto trackId = std::to_string(m_track.id);
    juce::Component::SafePointer<TrackDetailPanel> self(this);

    std::thread([generation, trackId, self] {
        auto cacheDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate").getChildFile("waveform_cache")
            .getFullPathName().toStdString();
        Core::WaveformCacheService wfCache;
        Core::ColouredWaveformData data;
        const bool loaded = wfCache.load(trackId, data, cacheDir) && !data.points.empty();

        std::vector<float> low, mid, high;
        if (loaded) {
            constexpr size_t kMaxPoints = 700;
            const size_t stride = juce::jmax<size_t>(1, data.points.size() / kMaxPoints);
            low.reserve(data.points.size() / stride + 1);
            mid.reserve(data.points.size() / stride + 1);
            high.reserve(data.points.size() / stride + 1);
            for (size_t i = 0; i < data.points.size(); i += stride) {
                const auto& p = data.points[i];
                low.push_back(p.low * p.amplitude);
                mid.push_back(p.mid * p.amplitude);
                high.push_back(p.high * p.amplitude);
            }
        }

        juce::MessageManager::callAsync([self, generation, loaded,
                                         low = std::move(low), mid = std::move(mid),
                                         high = std::move(high)]() mutable {
            if (self == nullptr || generation != self->m_waveformGeneration)
                return;
            if (loaded) {
                self->m_waveform->setWaveformData(low, mid, high);
                self->m_hasWaveform = true;
                self->m_waveform->setVisible(true);
            }
            self->m_content->repaint();
        });
    }).detach();
}

void TrackDetailPanel::retranslateUi()
{
    repaint();
    if (m_content)
        m_content->repaint();
}

void TrackDetailPanel::resized()
{
    m_viewport.setBounds(getLocalBounds().reduced(2));
    const int contentW = m_viewport.getMaximumVisibleWidth();
    m_content->setSize(contentW, kContentHeight);
}

void TrackDetailPanel::paint(juce::Graphics& g)
{
    ProDraw::glassPanel(g, getLocalBounds().toFloat(), 12.0f);
    if (!m_hasTrack) {
        ProDraw::emptyState(g, getLocalBounds(),
                            juce::CharPointer_UTF8("\xe2\x99\xab"),
                            BM_TJ("analysis.detail.emptyTitle"),
                            BM_TJ("analysis.detail.emptyBody"),
                            Colors::secondary());
    }
}

void TrackDetailPanel::Content::resized()
{
    const int w = getWidth();
    panel.m_waveform->setBounds(kPad, 196, w - kPad * 2, 64);
    panel.m_structure->setBounds(kPad, 292, w - kPad * 2, 44);
    panel.m_energyArc->setBounds(w - kPad - 66, 96, 66, 66);
    panel.m_camelotWheel->setBounds((w - 180) / 2, 452, 180, 180);
}

void TrackDetailPanel::Content::paint(juce::Graphics& g)
{
    if (!panel.m_hasTrack)
        return;

    const auto& t = panel.m_track;
    const int w = getWidth();
    const int innerW = w - kPad * 2;

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font("Segoe UI", 15.0f, juce::Font::bold));
    g.drawFittedText(juce::String::fromUTF8(t.title.c_str()), kPad, 10, innerW, 20,
                     juce::Justification::centredLeft, 1);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    g.drawFittedText(juce::String::fromUTF8(t.artist.c_str()), kPad, 30, innerW, 16,
                     juce::Justification::centredLeft, 1);

    {
        const bool analyzed = t.analyzed;
        const juce::String stateText = analyzed ? BM_TJ("analysis.detail.analyzed")
                                                : BM_TJ("analysis.detail.pending");
        const juce::Colour stateColour = analyzed ? Colors::success() : Colors::warning();
        const float pillW = juce::jmax(90.0f,
            juce::GlyphArrangement::getStringWidth(Type::label(), stateText) + 34.0f);
        ProDraw::statusPill(g, stateText, { static_cast<float>(kPad), 50.0f, pillW, 20.0f }, stateColour);

        if (!t.mood.empty())
            ProDraw::badge(g, juce::String::fromUTF8(t.mood.c_str()),
                           static_cast<float>(kPad) + pillW + 8.0f, 50.0f, 90.0f, 20.0f,
                           Colors::stemVocals());
    }

    ProDraw::sectionHeader(g, BM_TJ("analysis.detail.measures"), kPad, 78, Colors::primary());

    {
        const int tileY = 96;
        const int tileH = 66;
        const int tileW = (innerW - 66 - 16) / 2;

        g.setColour(Colors::glassWhite());
        g.fillRoundedRectangle(static_cast<float>(kPad), static_cast<float>(tileY),
                               static_cast<float>(tileW), static_cast<float>(tileH), 8.0f);
        g.setColour(Colors::glassBorder());
        g.drawRoundedRectangle(static_cast<float>(kPad), static_cast<float>(tileY),
                               static_cast<float>(tileW), static_cast<float>(tileH), 8.0f, 0.8f);
        g.setColour(Colors::textMuted());
        g.setFont(Type::label());
        g.drawText("BPM", kPad + 10, tileY + 8, tileW - 20, 12, juce::Justification::centredLeft);
        g.setColour(Colors::bpmBadge());
        g.setFont(juce::Font("Segoe UI", 22.0f, juce::Font::bold));
        g.drawText(t.bpm > 0.0 ? juce::String(t.bpm, 1) : juce::String::fromUTF8("\xe2\x80\x94"),
                   kPad + 10, tileY + 22, tileW - 20, 26, juce::Justification::centredLeft);
        const auto bpmConf = panel.confidenceText(t.bpmConfidence);
        if (bpmConf.isNotEmpty()) {
            g.setColour(Colors::textMuted());
            g.setFont(Type::caption());
            g.drawText(BM_TJ("analysis.detail.confidence") + " " + bpmConf,
                       kPad + 10, tileY + 50, tileW - 20, 12, juce::Justification::centredLeft);
        }

        const int keyX = kPad + tileW + 8;
        g.setColour(Colors::glassWhite());
        g.fillRoundedRectangle(static_cast<float>(keyX), static_cast<float>(tileY),
                               static_cast<float>(tileW), static_cast<float>(tileH), 8.0f);
        g.setColour(Colors::glassBorder());
        g.drawRoundedRectangle(static_cast<float>(keyX), static_cast<float>(tileY),
                               static_cast<float>(tileW), static_cast<float>(tileH), 8.0f, 0.8f);
        g.setColour(Colors::textMuted());
        g.setFont(Type::label());
        g.drawText(BM_TJ("analysis.col.key"), keyX + 10, tileY + 8, tileW - 20, 12,
                   juce::Justification::centredLeft);
        juce::String camelot = resolveCamelot(t.camelotKey, t.key);
        const juce::String keyDisplay = camelot.isNotEmpty()
            ? camelot.toUpperCase()
            : (t.key.empty() ? juce::String::fromUTF8("\xe2\x80\x94") : juce::String(t.key));
        g.setColour(camelot.isNotEmpty() ? AnalysisColumns::camelotColour(camelot) : Colors::keyBadge());
        g.setFont(juce::Font("Segoe UI", 22.0f, juce::Font::bold));
        g.drawText(keyDisplay, keyX + 10, tileY + 22, tileW - 20, 26, juce::Justification::centredLeft);
        const auto keyConf = panel.confidenceText(t.keyConfidence);
        if (keyConf.isNotEmpty()) {
            g.setColour(Colors::textMuted());
            g.setFont(Type::caption());
            g.drawText(BM_TJ("analysis.detail.confidence") + " " + keyConf,
                       keyX + 10, tileY + 50, tileW - 20, 12, juce::Justification::centredLeft);
        }
    }

    ProDraw::sectionHeader(g, BM_TJ("analysis.detail.waveform"), kPad, 176, Colors::accent());
    if (!panel.m_hasWaveform) {
        g.setColour(Colors::textMuted());
        g.setFont(Type::body());
        g.drawText(BM_TJ("analysis.detail.notAvailable"), kPad, 196, innerW, 64,
                   juce::Justification::centred);
    }

    ProDraw::sectionHeader(g, BM_TJ("analysis.detail.structure"), kPad, 272, Colors::secondary());
    if (!panel.m_hasStructure) {
        g.setColour(Colors::textMuted());
        g.setFont(Type::body());
        g.drawText(BM_TJ("analysis.detail.notAvailable"), kPad, 292, innerW, 44,
                   juce::Justification::centred);
    }

    ProDraw::sectionHeader(g, BM_TJ("analysis.detail.loudness"), kPad, 348, Colors::success());
    {
        const int tileY = 366;
        const int tileH = 54;
        const int tileW = (innerW - 16) / 3;
        struct LoudTile { juce::String label, value; };
        const bool hasLufs = t.lufs != 0.0f;
        const bool hasLra = t.loudnessRange > 0.0f;
        const bool hasPeak = t.truePeak > -99.0f;
        const juce::String dash = juce::String::fromUTF8("\xe2\x80\x94");
        const LoudTile tiles[3] = {
            { "LUFS", hasLufs ? juce::String(t.lufs, 1) : dash },
            { "LRA",  hasLra  ? juce::String(t.loudnessRange, 1) + " LU" : dash },
            { "PEAK", hasPeak ? juce::String(t.truePeak, 1) + " dBTP" : dash },
        };
        for (int i = 0; i < 3; ++i) {
            const int tx = kPad + i * (tileW + 8);
            g.setColour(Colors::glassWhite());
            g.fillRoundedRectangle(static_cast<float>(tx), static_cast<float>(tileY),
                                   static_cast<float>(tileW), static_cast<float>(tileH), 8.0f);
            g.setColour(Colors::glassBorder());
            g.drawRoundedRectangle(static_cast<float>(tx), static_cast<float>(tileY),
                                   static_cast<float>(tileW), static_cast<float>(tileH), 8.0f, 0.8f);
            g.setColour(Colors::textMuted());
            g.setFont(Type::caption());
            g.drawText(tiles[i].label, tx, tileY + 7, tileW, 12, juce::Justification::centred);
            g.setColour(Colors::textPrimary());
            g.setFont(juce::FontOptions("Consolas", 14.0f, juce::Font::bold));
            g.drawText(tiles[i].value, tx, tileY + 24, tileW, 20, juce::Justification::centred);
        }
    }

    ProDraw::sectionHeader(g, BM_TJ("analysis.detail.camelot"), kPad, 432, Colors::keyBadge());

    ProDraw::sectionHeader(g, BM_TJ("analysis.detail.file"), kPad, 644, Colors::textSecondary());
    {
        g.setFont(Type::body());
        int my = 664;
        auto metaLine = [&](const juce::String& label, const juce::String& value) {
            g.setColour(Colors::textMuted());
            g.drawText(label, kPad, my, 110, 16, juce::Justification::centredLeft);
            g.setColour(Colors::textSecondary());
            g.drawText(value, kPad + 114, my, innerW - 114, 16, juce::Justification::centredLeft);
            my += 20;
        };
        metaLine(BM_TJ("analysis.detail.duration"),
                 t.duration > 0.0 ? formatDuration(t.duration) : juce::String::fromUTF8("\xe2\x80\x94"));
        const juce::String sep = juce::String::fromUTF8(" \xc2\xb7 ");
        juce::String fmt = juce::String(t.fileFormat).toUpperCase();
        if (t.bitRate > 0)
            fmt += (fmt.isEmpty() ? juce::String() : sep) + juce::String(t.bitRate) + " kbps";
        if (t.sampleRate > 0)
            fmt += (fmt.isEmpty() ? juce::String() : sep) + juce::String(t.sampleRate / 1000.0, 1) + " kHz";
        metaLine(BM_TJ("analysis.detail.format"),
                 fmt.isEmpty() ? juce::String::fromUTF8("\xe2\x80\x94") : fmt);
        metaLine(BM_TJ("analysis.detail.analyzedDate"),
                 t.analyzedDate > 0
                     ? juce::Time(t.analyzedDate * 1000).toString(true, true, false, true)
                     : juce::String::fromUTF8("\xe2\x80\x94"));
    }
}

} // namespace BeatMate::UI
