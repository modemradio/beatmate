#include "NowPlayingBar.h"
#include "../waveform/OverviewWaveformWidget.h"
#include "../../styles/ColorPalette.h"
#include "../../../core/analysis/RgbPeaksGenerator.h"
#include "../../../services/library/WaveformPrecacheService.h"
#include "../../../app/ServiceLocator.h"

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }
#include "../../../services/config/I18n.h"
#include "../../../core/audio/AudioPlayer.h"
#include "../../../core/audio/AudioFileReader.h"
#include "../../../core/audio/AudioTrack.h"
#include "../../../core/audio/StreamingPlayer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

namespace {
void resolveTitleArtistFromFile(const juce::String& filePath,
                                juce::String& outTitle,
                                juce::String& outArtist)
{
    juce::File f(filePath);
    juce::String fileTitle, fileArtist;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (auto* reader = fm.createReaderFor(f)) {
        const auto& md = reader->metadataValues;
        fileTitle  = md.getValue("title", md.getValue("TITLE",
                                 md.getValue("IART", md.getValue("INAM", ""))));
        fileArtist = md.getValue("artist", md.getValue("ARTIST",
                                  md.getValue("IART", md.getValue("author", ""))));
        delete reader;
    }

    if (outTitle.isEmpty())
        outTitle = fileTitle.isNotEmpty() ? fileTitle : f.getFileNameWithoutExtension();
    if (outArtist.isEmpty())
        outArtist = fileArtist.isNotEmpty() ? fileArtist
                                            : (f.getParentDirectory().getFileName().isNotEmpty()
                                                   ? f.getParentDirectory().getFileName()
                                                   : juce::String("-"));
}
} // namespace

namespace BeatMate::UI {

static NowPlayingBar* s_nowPlayingInstance = nullptr;

NowPlayingBar* NowPlayingBar::instance() { return s_nowPlayingInstance; }
void NowPlayingBar::setInstance(NowPlayingBar* bar) { s_nowPlayingInstance = bar; }

NowPlayingBar::NowPlayingBar()
{
    setupUI();
}

NowPlayingBar::~NowPlayingBar()
{
    if (s_nowPlayingInstance == this)
        s_nowPlayingInstance = nullptr;
    stopTimer();
}

void NowPlayingBar::stopPlayback()
{
    if (m_streamingPlayer)
        m_streamingPlayer->pause();
    if (m_audioPlayer)
        m_audioPlayer->pause();
    m_playing = false;
    stopTimer();
    repaint();
}

void NowPlayingBar::setupUI()
{
    m_miniWaveform = std::make_unique<OverviewWaveformWidget>();
    addAndMakeVisible(*m_miniWaveform);

    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("player.noTrack"));
    m_titleLabel->setFont(Fonts::uiFont(14.0f, Fonts::Weight::SemiBold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_artistLabel = std::make_unique<juce::Label>("a", "-");
    m_artistLabel->setFont(Fonts::uiFont(12.0f));
    m_artistLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_artistLabel);

    m_seekSlider = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
    m_seekSlider->setRange(0.0, 1.0, 0.001);
    m_seekSlider->setValue(0.0, juce::dontSendNotification);
    m_seekSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    m_seekSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_seekSlider->setColour(juce::Slider::textBoxTextColourId, juce::Colours::transparentBlack);
    m_seekSlider->onValueChange = [this]()
    {
        if (m_durationSec > 0.0)
        {
            double seekPos = m_seekSlider->getValue() * m_durationSec;
            if (m_streamingPlayer && m_streamingPlayer->isPlaying()) {
                m_streamingPlayer->setPosition(seekPos);
            } else if (m_audioPlayer) {
                m_audioPlayer->setPosition(seekPos);
            }
            m_positionSec = seekPos;
            m_progress = m_seekSlider->getValue();
            updateTimeDisplay();
            if (m_miniWaveform)
                m_miniWaveform->setPlayheadPosition(m_progress);
        }
    };
    m_seekSlider->onDragStart = [this]() { m_seekDragging = true; };
    m_seekSlider->onDragEnd = [this]() { m_seekDragging = false; };
    addAndMakeVisible(*m_seekSlider);

    m_volumeSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_volumeSlider->setRange(0.0, 2.0, 0.01);
    m_volumeSlider->setValue(1.0, juce::dontSendNotification);
    m_volumeSlider->setDoubleClickReturnValue(true, 1.0);
    m_volumeSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    m_volumeSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_volumeSlider->setColour(juce::Slider::thumbColourId, Colors::primaryHover());
    m_volumeSlider->setTooltip(BM_TJ("player.volume"));
    m_volumeSlider->onValueChange = [this]()
    {
        const float v = static_cast<float>(m_volumeSlider->getValue());
        if (m_audioPlayer)
            m_audioPlayer->setVolume(v);
        if (m_streamingPlayer)
            m_streamingPlayer->setGain(v);
    };
    addAndMakeVisible(*m_volumeSlider);

    m_timePositionLabel = std::make_unique<juce::Label>("tp", "0:00");
    m_timePositionLabel->setFont(Fonts::monoFont(11.0f, Fonts::Weight::Medium));
    m_timePositionLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_timePositionLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*m_timePositionLabel);

    m_timeDurationLabel = std::make_unique<juce::Label>("td", "/ 0:00");
    m_timeDurationLabel->setFont(Fonts::monoFont(11.0f));
    m_timeDurationLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    m_timeDurationLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*m_timeDurationLabel);

    setInterceptsMouseClicks(true, true);
}

void NowPlayingBar::setAudioPlayer(Core::AudioPlayer* player) { m_audioPlayer = player; }
void NowPlayingBar::setAudioFileReader(Core::AudioFileReader* reader) { m_audioFileReader = reader; }
void NowPlayingBar::setStreamingPlayer(Core::StreamingPlayer* streamer) { m_streamingPlayer = streamer; }

void NowPlayingBar::loadAndPlay(const juce::String& filePath)
{
    juce::String title, artist;
    resolveTitleArtistFromFile(filePath, title, artist);
    loadAndPlay(filePath, title, artist);
}

void NowPlayingBar::loadAndPlay(const juce::String& filePath,
                                const juce::String& title,
                                const juce::String& artist)
{
    loadAndPlay(filePath, title, artist, 0.0, "-");
}

void NowPlayingBar::loadAndPlay(const juce::String& filePath,
                                const juce::String& titleIn,
                                const juce::String& artistIn,
                                double bpm,
                                const juce::String& key)
{
    if (filePath.isEmpty())
    {
        spdlog::warn("NowPlayingBar::loadAndPlay - Empty file path");
        return;
    }

    juce::String title = titleIn;
    juce::String artist = artistIn;
    if (title.isEmpty() || artist.isEmpty())
        resolveTitleArtistFromFile(filePath, title, artist);

    setTrackInfo(title, artist, bpm, key);
    spdlog::info("[Preview] title='{}' artist='{}' file='{}'",
                 title.toStdString(), artist.toStdString(), filePath.toStdString());

    if (m_streamingPlayer)
    {
        m_currentFilePath = filePath;
        m_playing = true;
        if (m_audioPlayer) m_audioPlayer->stop();
        if (m_miniWaveform) m_miniWaveform->setWaveformData({});

        juce::Thread::launch([this, filePath]() {
            juce::File f(filePath);
            bool ok = m_streamingPlayer->loadAndPlay(f, 0.0);

            std::vector<float> peaks;
            if (ok)
            {
                const std::string path = filePath.toStdString();
                if (Core::RgbPeaksGenerator::isCacheValid(path))
                {
                    Core::RgbPeaksData rgb;
                    if (Core::RgbPeaksGenerator::read(Core::RgbPeaksGenerator::cacheFileFor(path), rgb)
                        && rgb.valid() && !rgb.peaks.empty())
                    {
                        constexpr int kBars = 300;
                        const size_t n = rgb.peaks.size();
                        peaks.resize(kBars);
                        for (int b = 0; b < kBars; ++b)
                        {
                            const size_t i0 = static_cast<size_t>(
                                static_cast<double>(b) / kBars * static_cast<double>(n));
                            const size_t i1 = std::max(i0 + 1, static_cast<size_t>(
                                static_cast<double>(b + 1) / kBars * static_cast<double>(n)));
                            float m = 0.0f;
                            for (size_t k = i0; k < i1 && k < n; ++k)
                                m = std::max(m, std::abs(rgb.peaks[k]));
                            peaks[static_cast<size_t>(b)] = m;
                        }
                    }
                }
            }

            juce::MessageManager::callAsync([this, ok, peaks = std::move(peaks)]() {
                if (!ok) { m_playing = false; return; }
                if (m_streamingPlayer && m_volumeSlider)
                    m_streamingPlayer->setGain(static_cast<float>(m_volumeSlider->getValue()));
                m_durationSec = m_streamingPlayer->getDuration();
                m_positionSec = 0.0;
                m_progress = 0.0;
                m_playing = true;
                if (m_seekSlider) m_seekSlider->setValue(0.0, juce::dontSendNotification);
                if (!peaks.empty() && m_miniWaveform)
                    m_miniWaveform->setWaveformData(peaks);
                else
                    requestMiniWaveformFromService(m_currentFilePath);
                updateTimeDisplay();
                startTimer(100);
                repaint();
            });
        });
        return;
    }

    if (!m_audioPlayer || !m_audioFileReader)
    {
        spdlog::warn("NowPlayingBar::loadAndPlay - AudioPlayer or AudioFileReader not set");
        return;
    }

    if (filePath == m_currentFilePath && m_audioPlayer->getTrack() != nullptr)
    {
        m_audioPlayer->setPosition(0.0);
        m_audioPlayer->play();
        m_positionSec = 0.0;
        m_progress = 0.0;
        m_playing = true;
        m_seekSlider->setValue(0.0, juce::dontSendNotification);
        startTimer(100);
        repaint();
        return;
    }

    m_currentFilePath = filePath;
    m_playing = false;
    repaint();

    auto reader = m_audioFileReader;
    auto player = m_audioPlayer;
    auto pathStr = filePath.toStdString();
    auto* self = this;

    std::thread([reader, player, pathStr, self]() {
        auto startTime = std::chrono::steady_clock::now();

        auto previewTrack = reader->readRange(pathStr, 0.0, 30.0);
        if (!previewTrack) {
            spdlog::error("NowPlayingBar: Failed to load: {}", pathStr);
            return;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        spdlog::info("NowPlayingBar: preview start in {}ms", elapsed);

        juce::MessageManager::callAsync([player, previewTrack, self]() {
            player->loadTrack(previewTrack);
            player->play();
            self->m_durationSec = player->getDuration();
            self->m_positionSec = 0.0;
            self->m_progress = 0.0;
            self->m_playing = true;
            self->m_seekSlider->setValue(0.0, juce::dontSendNotification);
            self->updateTimeDisplay();
            self->startTimer(100);
            self->repaint();
        });

        auto fullTrack = reader->readFile(pathStr);
        if (!fullTrack) return;

        const float* rawData = fullTrack->getRawData();
        size_t totalSamples = fullTrack->getTotalSamples();
        int channels = fullTrack->getChannels();
        size_t numFrames = fullTrack->getNumFrames();
        int resolution = static_cast<int>(numFrames / 200);

        std::vector<float> peaks;
        if (resolution > 0 && rawData) {
            peaks.reserve(200);
            for (size_t i = 0; i < numFrames; i += static_cast<size_t>(resolution)) {
                float maxVal = 0.0f;
                size_t end = std::min(i + static_cast<size_t>(resolution), numFrames);
                for (size_t j = i; j < end; ++j) {
                    size_t idx = j * channels;
                    if (idx < totalSamples) {
                        float s = std::abs(rawData[idx]);
                        if (s > maxVal) maxVal = s;
                    }
                }
                peaks.push_back(maxVal);
            }
        }

        juce::MessageManager::callAsync([player, fullTrack, peaks = std::move(peaks), self]() {
            double currentPos = player->getPosition();
            player->loadTrack(fullTrack);
            player->setPosition(currentPos);
            player->play();
            self->m_durationSec = player->getDuration();
            self->m_playing = true;
            if (!peaks.empty() && self->m_miniWaveform)
                self->m_miniWaveform->setWaveformData(peaks);
            self->updateTimeDisplay();
            self->repaint();
        });
    }).detach();
}

void NowPlayingBar::requestMiniWaveformFromService(const juce::String& filePath)
{
    if (!BeatMate::g_serviceLocator || filePath.isEmpty())
        return;
    auto* svc = BeatMate::g_serviceLocator->tryGet<Services::Library::WaveformPrecacheService>();
    if (!svc)
        return;
    juce::Component::SafePointer<NowPlayingBar> self(this);
    svc->requestPriority(filePath.toStdString(), nullptr,
        [self, filePath](const Core::RgbPeaksData& d) {
            auto* bar = self.getComponent();
            if (!bar || !d.valid() || d.peaks.empty() || bar->m_currentFilePath != filePath)
                return;
            constexpr int kBars = 300;
            std::vector<float> peaks(kBars);
            const size_t n = d.peaks.size();
            for (int b = 0; b < kBars; ++b)
            {
                const size_t i0 = static_cast<size_t>(
                    static_cast<double>(b) / kBars * static_cast<double>(n));
                const size_t i1 = std::max(i0 + 1, static_cast<size_t>(
                    static_cast<double>(b + 1) / kBars * static_cast<double>(n)));
                float m = 0.0f;
                for (size_t k = i0; k < i1 && k < n; ++k)
                    m = std::max(m, std::abs(d.peaks[k]));
                peaks[static_cast<size_t>(b)] = m;
            }
            if (bar->m_miniWaveform)
                bar->m_miniWaveform->setWaveformData(peaks);
        });
}

void NowPlayingBar::startHoverPreview(const juce::String& filePath)
{
    (void)filePath;
}

void NowPlayingBar::stopHoverPreview()
{
    if (m_hoverPreviewActive)
    {
        m_hoverPreviewActive = false;
        if (m_audioPlayer)
        {
            m_audioPlayer->pause();
            m_audioPlayer->setPosition(0.0);
        }
        m_playing = false;
        m_positionSec = 0.0;
        m_progress = 0.0;
        stopTimer();
        updateTimeDisplay();
        repaint();
    }
}

void NowPlayingBar::setTrackInfo(const juce::String& title, const juce::String& artist,
                                  double bpm, const juce::String& key)
{
    m_titleLabel->setText(title.isNotEmpty() ? title : juce::String("-"),
                          juce::dontSendNotification);
    m_artistLabel->setText(artist.isNotEmpty() ? artist : juce::String("-"),
                           juce::dontSendNotification);
    m_bpmText = (bpm > 0.0) ? (juce::String(bpm, 1) + " BPM") : juce::String("- BPM");
    m_keyText = key.isNotEmpty() ? key : juce::String("-");
    spdlog::info("[NowPlayingBar::setTrackInfo] title='{}' artist='{}' bpm={} key='{}'",
                 title.toStdString(), artist.toStdString(), bpm, key.toStdString());
    repaint();
}

void NowPlayingBar::setTrackInfo(const juce::String& title, const juce::String& artist)
{
    m_titleLabel->setText(title.isNotEmpty() ? title : juce::String("-"),
                          juce::dontSendNotification);
    m_artistLabel->setText(artist.isNotEmpty() ? artist : juce::String("-"),
                           juce::dontSendNotification);
    spdlog::info("[NowPlayingBar::setTrackInfo(2)] title='{}' artist='{}'",
                 title.toStdString(), artist.toStdString());
    repaint();
}

void NowPlayingBar::setPosition(double position)
{
    m_progress = position;
    m_miniWaveform->setPlayheadPosition(position);
    if (!m_seekDragging)
        m_seekSlider->setValue(position, juce::dontSendNotification);
    repaint();
}

void NowPlayingBar::setPlaying(bool playing)
{
    m_playing = playing;
    if (m_playing) startTimer(100); else stopTimer();
    repaint();
}

void NowPlayingBar::setEnergy(int energy)
{
    m_energy = juce::jlimit(0, 10, energy);
    m_energyText = juce::String(m_energy);
    repaint();
}

void NowPlayingBar::timerCallback()
{
    if (m_streamingPlayer && m_streamingPlayer->isPlaying())
    {
        m_positionSec = m_streamingPlayer->getPosition();
        m_durationSec = m_streamingPlayer->getDuration();

        if (m_durationSec > 0.0)
        {
            m_progress = m_positionSec / m_durationSec;
            if (!m_seekDragging && m_seekSlider)
                m_seekSlider->setValue(m_progress, juce::dontSendNotification);
            if (m_miniWaveform)
                m_miniWaveform->setPlayheadPosition(m_progress);
        }

        updateTimeDisplay();
        repaint();
        return;
    }

    if (m_audioPlayer && m_audioPlayer->isPlaying())
    {
        m_positionSec = m_audioPlayer->getPosition();
        m_durationSec = m_audioPlayer->getDuration();

        if (m_durationSec > 0.0)
        {
            m_progress = m_positionSec / m_durationSec;
            if (!m_seekDragging)
                m_seekSlider->setValue(m_progress, juce::dontSendNotification);
            m_miniWaveform->setPlayheadPosition(m_progress);
        }

        updateTimeDisplay();
        repaint();
    }
    else if (m_audioPlayer && m_audioPlayer->isStopped() && m_playing)
    {
        m_playing = false;
        stopTimer();

        if (m_audioPlayer->wasStoppedByLimit())
        {
            m_audioPlayer->clearStoppedByLimit();
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                BM_TJ("player.trialLimit"),
                BM_TJ("player.trialLimitMsg"),
                BM_TJ("common.ok"));
        }
        repaint();
    }
}

juce::String NowPlayingBar::formatTime(double seconds)
{
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    return juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
}

void NowPlayingBar::updateTimeDisplay()
{
    m_timePositionLabel->setText(formatTime(m_positionSec), juce::dontSendNotification);
    m_timeDurationLabel->setText("/ " + formatTime(m_durationSec), juce::dontSendNotification);
}

void NowPlayingBar::drawAlbumArtPlaceholder(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bounds = area.toFloat();
    g.setColour(Colors::bgElevated());
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    auto cx = bounds.getCentreX();
    auto cy = bounds.getCentreY();
    g.setColour(Colors::textMuted());
    juce::Path note;
    note.addEllipse(cx - 6.0f, cy + 2.0f, 10.0f, 7.0f);
    note.addRectangle(cx + 3.0f, cy - 10.0f, 2.0f, 14.0f);
    note.addRectangle(cx + 3.0f, cy - 10.0f, 7.0f, 2.0f);
    g.fillPath(note);
}

void NowPlayingBar::drawPlayPauseButton(juce::Graphics& g, juce::Rectangle<int> area, bool isPlaying)
{
    auto bounds = area.toFloat();
    auto cx = bounds.getCentreX();
    auto cy = bounds.getCentreY();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const float dim = m_durationSec > 0.0 ? 1.0f : 0.45f;

    g.setColour(Colors::primary().withAlpha(0.2f * dim));
    g.fillEllipse(cx - radius - 3, cy - radius - 3, (radius + 3) * 2, (radius + 3) * 2);
    g.setColour(Colors::primary().withAlpha(dim));
    g.fillEllipse(cx - radius, cy - radius, radius * 2, radius * 2);

    g.setColour(juce::Colours::white.withAlpha(dim));
    if (isPlaying)
    {
        float barW = 3.0f;
        float barH = radius * 0.8f;
        g.fillRoundedRectangle(cx - 5, cy - barH * 0.5f, barW, barH, 1.0f);
        g.fillRoundedRectangle(cx + 2, cy - barH * 0.5f, barW, barH, 1.0f);
    }
    else
    {
        juce::Path tri;
        float s = radius * 0.5f;
        tri.addTriangle(cx - s * 0.4f, cy - s, cx - s * 0.4f, cy + s, cx + s * 0.8f, cy);
        g.fillPath(tri);
    }
}

void NowPlayingBar::drawBadge(juce::Graphics& g, juce::Rectangle<int> area,
                               const juce::String& text, juce::Colour bgColour, juce::Colour textColour)
{
    auto bounds = area.toFloat();
    g.setColour(bgColour.withAlpha(0.15f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(bgColour.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    g.setColour(textColour);
    g.setFont(Fonts::monoFont(11.0f, Fonts::Weight::Medium));
    g.drawText(text, area, juce::Justification::centred);
}

namespace {
bool parseCamelot(const juce::String& key, int& number, bool& isMinor)
{
    auto t = key.trim().toUpperCase();
    if (t.length() < 2 || t.length() > 3)
        return false;
    auto last = t.getLastCharacter();
    if (last != 'A' && last != 'B')
        return false;
    auto digits = t.dropLastCharacters(1);
    if (!digits.containsOnly("0123456789"))
        return false;
    number = digits.getIntValue();
    isMinor = (last == 'A');
    return number >= 1 && number <= 12;
}
} // namespace

void NowPlayingBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto w = static_cast<float>(bounds.getWidth());

    g.setColour(Colors::bgSurface());
    g.fillRect(bounds);

    if (m_durationSec > 0.0)
    {
        ProDraw::energyThread(g, { 0.0f, 0.0f, w, 2.0f },
                              static_cast<float>(m_progress),
                              Colors::energy(juce::jlimit(0, 10, m_energy) / 10.0f));
    }
    else
    {
        g.setColour(Colors::border());
        g.drawHorizontalLine(0, 0.0f, w);
    }

    drawAlbumArtPlaceholder(g, m_albumArtArea);
    drawPlayPauseButton(g, m_playBtnArea, m_playing);

    int camelotNumber = 0;
    bool camelotMinor = false;
    const bool hasCamelot = parseCamelot(m_keyText, camelotNumber, camelotMinor);
    const float badgeDim = m_durationSec > 0.0 ? 1.0f : 0.45f;
    const juce::Colour keyColour = (hasCamelot ? Colors::camelot(camelotNumber, camelotMinor)
                                               : Colors::keyBadge()).withMultipliedAlpha(badgeDim);

    drawBadge(g, m_bpmBadgeArea, m_bpmText,
              Colors::bpmBadge().withMultipliedAlpha(badgeDim),
              Colors::bpmBadge().withMultipliedAlpha(badgeDim));
    drawBadge(g, m_keyBadgeArea, m_keyText, keyColour, keyColour);
    {
        auto keArea = m_energyBadgeArea.toFloat();
        const float d = juce::jmin(keArea.getWidth(), keArea.getHeight());
        juce::Rectangle<float> dot(keArea.getCentreX() - d * 0.5f,
                                   keArea.getCentreY() - d * 0.5f, d, d);
        const bool hasTrack = m_durationSec > 0.0;
        ProDraw::keDot(g, dot.reduced(1.0f),
                       hasTrack && hasCamelot ? camelotNumber : 0, camelotMinor,
                       hasTrack ? juce::jlimit(0, 10, m_energy) / 10.0f : 0.0f,
                       hasTrack ? m_energyText : juce::String("-"));
    }

    g.setColour(Colors::textMuted());
    g.setFont(Fonts::uiFont(11.0f));
    auto volBounds = m_volumeSlider->getBounds();
    g.drawText(BM_TJ("player.vol"), volBounds.getX() - 28, volBounds.getY(), 26, volBounds.getHeight(),
               juce::Justification::centredRight);
}

void NowPlayingBar::resized()
{
    auto bounds = getLocalBounds();
    int h = bounds.getHeight();
    int w = bounds.getWidth();
    int centerY = h / 2 - 2;

    m_playBtnArea = juce::Rectangle<int>(16, centerY - 20, 40, 40);
    m_albumArtArea = juce::Rectangle<int>(64, centerY - 26, 52, 52);

    m_titleLabel->setBounds(126, centerY - 20, 200, 20);
    m_artistLabel->setBounds(126, centerY + 2, 200, 16);

    int timeAreaX = 336;
    m_timePositionLabel->setBounds(timeAreaX, centerY - 8, 38, 16);
    m_seekSlider->setBounds(timeAreaX + 42, centerY - 12, 160, 24);
    m_timeDurationLabel->setBounds(timeAreaX + 206, centerY - 8, 50, 16);

    int waveformX = timeAreaX + 260;
    int volSliderW = 70;
    int volLabelW = 30;
    int badgesW = 210;
    int waveformMaxRight = w - badgesW - volSliderW - volLabelW - 32;
    int waveformW = juce::jmax(300, waveformMaxRight - waveformX);
    m_miniWaveform->setBounds(waveformX, centerY - 18, waveformW, 36);

    int badgeY = centerY - 13;
    int badgeH = 26;
    int rightMargin = 16;
    int rightX = w - rightMargin;
    m_energyBadgeArea = juce::Rectangle<int>(rightX - 52, badgeY, 52, badgeH);
    m_keyBadgeArea = juce::Rectangle<int>(rightX - 112, badgeY, 54, badgeH);
    m_bpmBadgeArea = juce::Rectangle<int>(rightX - 188, badgeY, 70, badgeH);

    int volRightEdge = rightX - 196;
    m_volumeSlider->setBounds(volRightEdge - 80, centerY - 12, 74, 24);
}

void NowPlayingBar::mouseDown(const juce::MouseEvent& e)
{
    if (m_playBtnArea.contains(e.getPosition()))
    {
        if (m_streamingPlayer && m_streamingPlayer->isPlaying())
        {
            m_streamingPlayer->pause();
            m_playing = false;
            stopTimer();
        }
        else if (m_streamingPlayer && m_streamingPlayer->getDuration() > 0.0)
        {
            m_streamingPlayer->play();
            m_playing = true;
            startTimer(100);
        }
        else if (m_audioPlayer)
        {
            if (m_audioPlayer->isPlaying())
            {
                m_audioPlayer->pause();
                m_playing = false;
                stopTimer();
            }
            else
            {
                m_audioPlayer->play();
                m_playing = true;
                startTimer(100);
            }
        }
        else
        {
            m_playing = !m_playing;
        }
        m_listeners.call(&Listener::playPauseClicked);
        repaint();
    }
}

} // namespace BeatMate::UI
