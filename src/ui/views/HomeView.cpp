#include "HomeView.h"
#include "../styles/ColorPalette.h"
#include "analysis/AnalysisColumns.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/djsoftware/DJSoftwareDetector.h"
#include "../../services/djsoftware/CollectionSyncService.h"
#include "../../core/audio/AudioEngine.h"
#include "../../app/ServiceLocator.h"
#include "../../services/config/I18n.h"
#include <cmath>
#include <ctime>
#include <filesystem>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI {

BeatMate::ServiceLocator* HomeView::serviceLocator() noexcept
{
    return BeatMate::g_serviceLocator;
}

namespace
{
    inline juce::Font makeFont(float h, int styleFlags = 0)
    {
        return juce::Font(juce::FontOptions{}.withHeight(h).withStyle(
            styleFlags & juce::Font::bold   ? "Bold"
          : styleFlags & juce::Font::italic ? "Italic"
          : "Regular"));
    }
    inline juce::Font makeFont(const juce::String& name, float h, int styleFlags = 0)
    {
        return juce::Font(juce::FontOptions{}.withName(name).withHeight(h).withStyle(
            styleFlags & juce::Font::bold   ? "Bold"
          : styleFlags & juce::Font::italic ? "Italic"
          : "Regular"));
    }
}

void HomeView::BentoCard::paintCardBackground(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(Colors::bgMedium().withAlpha(0.45f));
    g.fillRoundedRectangle(b, 12.0f);

    juce::ColourGradient glassGrad(juce::Colours::white.withAlpha(0.06f), 0, 0,
                                   juce::Colours::white.withAlpha(0.01f), 0, b.getHeight(), false);
    g.setGradientFill(glassGrad);
    g.fillRoundedRectangle(b, 12.0f);

    g.setColour(Colors::border().withAlpha(0.25f));
    g.drawRoundedRectangle(b.reduced(0.5f), 12.0f, 1.0f);

    g.setColour(juce::Colour(0xFF0A0A12).withAlpha(0.4f));
    g.drawLine(b.getX() + 12, b.getBottom() - 0.5f, b.getRight() - 12, b.getBottom() - 0.5f, 1.0f);

    if (accentColor != juce::Colour())
    {
        juce::Path topLine;
        topLine.addRoundedRectangle(b.getX() + 16, b.getY() + 1, b.getWidth() - 32, 2.0f, 1.0f);
        g.setColour(accentColor.withAlpha(0.5f));
        g.fillPath(topLine);
    }

    if (accentColor != juce::Colour())
    {
        juce::ColourGradient leftBar(accentColor.withAlpha(0.7f), b.getX() + 1.0f, b.getCentreY() - 20.0f,
                                     accentColor.withAlpha(0.15f), b.getX() + 1.0f, b.getCentreY() + 20.0f, false);
        g.setGradientFill(leftBar);
        g.fillRoundedRectangle(b.getX() + 1.0f, b.getY() + 14.0f, 3.0f, b.getHeight() - 28.0f, 1.5f);
    }

    if (cardTitle.isNotEmpty())
    {
        g.setFont(makeFont("Segoe UI", 11.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xFF64748B));
        g.drawText(cardTitle, 16.0f, 12.0f, b.getWidth() - 32, 16.0f, juce::Justification::centredLeft);
    }
}

void HomeView::CollectionCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);
    auto b = getLocalBounds().toFloat();
    const float w = b.getWidth();
    const float h = b.getHeight();

    g.setFont(makeFont(46.0f, juce::Font::bold));
    g.setColour(Colors::primary());
    g.drawText(juce::String(totalTracks), 20, 34, static_cast<int>(w * 0.5f), 52,
               juce::Justification::centredLeft);

    g.setFont(makeFont(13.0f));
    g.setColour(Colors::textSecondary());
    g.drawText(BM_TJ("home.stats.totalTracks"), 20, 86, 220, 18, juce::Justification::centredLeft);

    {
        float bx = 20.0f;
        const float by = 114.0f;
        auto chip = [&](const juce::String& text, juce::Colour colour) {
            const float cw = juce::GlyphArrangement::getStringWidth(Type::label(), text) + 28.0f;
            ProDraw::badge(g, text, bx, by, cw, 22.0f, colour);
            bx += cw + 8.0f;
        };
        chip(juce::String(analyzedCount) + " " + BM_TJ("home.stats.analyzed").toUpperCase(), Colors::success());
        chip(juce::String(genreCount) + " " + BM_TJ("home.stats.genres").toUpperCase(), Colors::warning());
        if (lastSync.isNotEmpty())
            chip(BM_TJ("home.stats.lastSync").toUpperCase() + " : " + lastSync, Colors::accent());
    }

    ProDraw::scoreCircle(g, w - 76.0f, 78.0f, 44.0f,
                         juce::jlimit(0, 100, analyzedPercent) / 100.0f,
                         BM_TJ("home.stats.analyzed").toUpperCase());

    if (!bpmHistogram.empty())
    {
        const float histTop = h - 66.0f;
        g.setFont(makeFont("Segoe UI", 9.0f, juce::Font::bold));
        g.setColour(Colors::textDim());
        g.drawText(BM_TJ("home.bpmDist"), 20, static_cast<int>(histTop) - 14, 200, 12,
                   juce::Justification::centredLeft);

        int maxCount = 1;
        for (int c : bpmHistogram)
            maxCount = juce::jmax(maxCount, c);

        const float histX = 20.0f;
        const float histW = w - 40.0f;
        const float histH = 40.0f;
        const int n = static_cast<int>(bpmHistogram.size());
        const float barW = histW / static_cast<float>(n);
        for (int i = 0; i < n; ++i)
        {
            const float frac = static_cast<float>(bpmHistogram[static_cast<size_t>(i)])
                / static_cast<float>(maxCount);
            const float bh = juce::jmax(2.0f, histH * frac);
            const float t = static_cast<float>(i) / static_cast<float>(n - 1);
            g.setColour(Colors::primary().interpolatedWith(Colors::secondary(), t)
                            .withAlpha(0.35f + 0.55f * frac));
            g.fillRoundedRectangle(histX + static_cast<float>(i) * barW,
                                   histTop + histH - bh,
                                   juce::jmax(2.0f, barW - 2.0f), bh, 1.5f);
        }

        g.setFont(makeFont("Consolas", 9.0f));
        g.setColour(Colors::textDim());
        g.drawText("60", static_cast<int>(histX), static_cast<int>(histTop + histH + 2), 40, 10,
                   juce::Justification::centredLeft);
        g.drawText("130", static_cast<int>(histX + histW * 0.5f - 20), static_cast<int>(histTop + histH + 2),
                   40, 10, juce::Justification::centred);
        g.drawText("200", static_cast<int>(histX + histW - 40), static_cast<int>(histTop + histH + 2),
                   40, 10, juce::Justification::centredRight);
    }
}

void HomeView::RecentAddedCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);

    if (tracks.empty())
    {
        ProDraw::emptyState(g, getLocalBounds().withTrimmedTop(20),
                            juce::CharPointer_UTF8("\xe2\x99\xaa"),
                            BM_TJ("home.card.emptyTitle"),
                            BM_TJ("home.card.emptyBody"),
                            accentColor);
        return;
    }

    const int n = visibleRows();
    float y = static_cast<float>(kListTopY);
    for (int i = 0; i < n; ++i)
    {
        if (i == hoveredIndex) {
            g.setColour(Colors::primary().withAlpha(0.12f));
            g.fillRoundedRectangle(8.0f, y - 2.0f, getWidth() - 16.0f, 32.0f, 5.0f);
        }
        const auto& t = tracks[static_cast<size_t>(i)];

        g.setFont(makeFont(11.5f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(t.title, 16, static_cast<int>(y), getWidth() - 122, 14, juce::Justification::centredLeft);

        g.setFont(makeFont(10.0f));
        g.setColour(Colors::textMuted());
        g.drawText(t.artist, 16, static_cast<int>(y) + 14, getWidth() - 122, 12, juce::Justification::centredLeft);

        const float bx = getWidth() - 102.0f;
        ProDraw::badge(g, t.bpm > 0.0f ? juce::String(t.bpm, 0) : juce::String::fromUTF8("\xe2\x80\x94"),
                       bx, y + 4.0f, 44.0f, 18.0f, Colors::bpmBadge());
        if (t.key.isNotEmpty())
            ProDraw::badge(g, t.key, bx + 48.0f, y + 4.0f, 40.0f, 18.0f,
                           AnalysisColumns::camelotColour(t.key));

        g.setColour(Colors::glassBorder());
        g.drawHorizontalLine(static_cast<int>(y + 30), 16.0f, getWidth() - 16.0f);

        y += static_cast<float>(kRowHeight);
    }
}

void HomeView::RecentPlayedCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);

    if (tracks.empty())
    {
        ProDraw::emptyState(g, getLocalBounds().withTrimmedTop(20),
                            juce::CharPointer_UTF8("\xe2\x99\xaa"),
                            BM_TJ("home.card.emptyTitle"),
                            BM_TJ("home.card.emptyPlayedBody"),
                            accentColor);
        return;
    }

    const int maxRows = juce::jmax(0, (getHeight() - 34 - 6) / 34);
    float y = 34.0f;
    for (size_t i = 0; i < tracks.size() && static_cast<int>(i) < maxRows; ++i)
    {
        auto& t = tracks[i];

        g.setFont(makeFont(11.5f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(t.title, 16, static_cast<int>(y), getWidth() - 110, 14, juce::Justification::centredLeft);

        g.setFont(makeFont(10.0f));
        g.setColour(Colors::textMuted());
        g.drawText(t.artist, 16, static_cast<int>(y) + 14, getWidth() - 110, 12, juce::Justification::centredLeft);

        g.setColour(Colors::textDim());
        g.setFont(makeFont("Consolas", 9.5f));
        g.drawText(t.timestamp, getWidth() - 100, static_cast<int>(y) + 6, 84, 14, juce::Justification::centredRight);

        g.setColour(Colors::glassBorder());
        g.drawHorizontalLine(static_cast<int>(y + 30), 16.0f, getWidth() - 16.0f);

        y += 34.0f;
    }
}

void HomeView::SuggestionsCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);

    if (suggestions.empty())
    {
        ProDraw::emptyState(g, getLocalBounds().withTrimmedTop(20),
                            juce::CharPointer_UTF8("\xe2\x9c\xa8"),
                            BM_TJ("home.card.emptyTitle"),
                            BM_TJ("home.card.emptySuggestBody"),
                            accentColor);
        return;
    }

    const int n = visibleRows();
    float y = static_cast<float>(kListTopY);
    for (int i = 0; i < n; ++i)
    {
        const auto& s = suggestions[static_cast<size_t>(i)];

        juce::Colour scoreCol = s.score >= 80 ? Colors::success()
                              : s.score >= 60 ? Colors::warning()
                              : Colors::accent();
        g.setColour(scoreCol.withAlpha(0.14f));
        g.fillRoundedRectangle(16.0f, y, 34.0f, 34.0f, 9.0f);
        g.setColour(scoreCol.withAlpha(0.35f));
        g.drawRoundedRectangle(16.0f, y, 34.0f, 34.0f, 9.0f, 0.8f);
        g.setColour(scoreCol);
        g.setFont(makeFont(12.5f, juce::Font::bold));
        g.drawText(s.score > 0 ? juce::String(s.score) : juce::String::fromUTF8("\xe2\x99\xaa"),
                   16, static_cast<int>(y), 34, 34, juce::Justification::centred);

        g.setFont(makeFont(11.5f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(s.title + " - " + s.artist, 58, static_cast<int>(y) + 2, getWidth() - 74, 14,
                   juce::Justification::centredLeft);

        if (s.reason.isNotEmpty())
        {
            const float tagW = juce::jmin(static_cast<float>(getWidth() - 66),
                juce::GlyphArrangement::getStringWidth(Type::caption(), s.reason) + 18.0f);
            ProDraw::badge(g, s.reason, 58.0f, y + 19.0f, tagW, 15.0f, scoreCol);
        }

        y += static_cast<float>(kRowHeight);
    }
}

void HomeView::DJSoftwareCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);

    float y = 32.0f;
    for (size_t i = 0; i < platforms.size(); ++i)
    {
        auto& p = platforms[i];

        float ledX = 20.0f;
        float ledY = y + 4.0f;
        float ledSz = 10.0f;

        juce::Colour ledCol = p.connected ? Colors::success() : Colors::error();

        if (p.connected)
        {
            g.setColour(ledCol.withAlpha(0.2f));
            g.fillEllipse(ledX - 3, ledY - 3, ledSz + 6, ledSz + 6);
        }
        g.setColour(ledCol);
        g.fillEllipse(ledX, ledY, ledSz, ledSz);

        g.setFont(makeFont(12.0f));
        g.setColour(p.connected ? Colors::textPrimary() : Colors::textMuted());
        g.drawText(p.name, (int)(ledX + ledSz + 10), (int)y, getWidth() - 60, 18, juce::Justification::centredLeft);

        g.setFont(makeFont(9.0f));
        g.setColour(ledCol);
        g.drawText(p.connected ? BM_TJ("home.connected") : BM_TJ("home.notDetected"),
                   getWidth() - 90, (int)y, 74, 18, juce::Justification::centredRight);

        y += 26.0f;
    }
}

void HomeView::NextEventCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);

    if (!hasEvent)
    {
        auto b = getLocalBounds().toFloat().reduced(16);

        g.setFont(makeFont(11.0f));
        g.setColour(Colors::textDim());
        g.drawText(BM_TJ("home.noEvent"), b.removeFromTop(b.getHeight() * 0.45f),
                    juce::Justification::centredBottom);

        auto btnArea = b.reduced(20, 4).removeFromTop(26);
        g.setColour(Colors::primary().withAlpha(0.15f));
        g.fillRoundedRectangle(btnArea, 6.0f);
        g.setColour(Colors::primary());
        g.drawRoundedRectangle(btnArea, 6.0f, 1.0f);
        g.setFont(makeFont(11.0f, juce::Font::bold));
        g.drawText(BM_TJ("home.createEventBtn"), btnArea, juce::Justification::centred);

        createEventBtnArea = btnArea.toNearestInt();
        return;
    }

    g.setColour(accentColor.withAlpha(0.15f));
    g.fillEllipse(16, 32, 36, 36);
    g.setColour(accentColor);
    g.setFont(makeFont(18.0f, juce::Font::bold));
    g.drawText("E", 16, 32, 36, 36, juce::Justification::centred);

    g.setFont(makeFont(14.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(eventName, 60, 32, getWidth() - 76, 18, juce::Justification::centredLeft);

    g.setFont(makeFont(10.0f));
    g.setColour(Colors::textSecondary());
    g.drawText(venue + " | " + date, 60, 50, getWidth() - 76, 14, juce::Justification::centredLeft);

    g.setFont(makeFont(9.0f));
    g.setColour(Colors::textDim());
    g.drawText(juce::String(phasesCount) + " " + BM_TJ("home.phasesPlanned"),
               60, 64, getWidth() - 76, 12, juce::Justification::centredLeft);
}

void HomeView::PerformanceCard::paint(juce::Graphics& g)
{
    paintCardBackground(g);

    const bool horizontal = getWidth() > getHeight() * 3;

    float totalRAM = ramTotalMB > 0.0f
                       ? ramTotalMB
                       : static_cast<float>(juce::SystemStats::getMemorySizeInMegabytes());
    if (totalRAM <= 0.0f) totalRAM = 2048.0f;
    const float diskMax = diskTotalGB > 0.0f ? diskTotalGB : 1000.0f;

    struct Gauge { juce::String label, value; float norm; };
    const Gauge gauges[4] = {
        { "CPU", juce::String(cpuPercent, 1) + "%", cpuPercent / 100.0f },
        { "RAM", juce::String(ramMB / 1024.0f, 1) + " / " + juce::String(totalRAM / 1024.0f, 1) + " GB",
          ramMB / totalRAM },
        { BM_TJ("home.gauge.latency"), juce::String(latencyMs, 1) + " ms", latencyMs / 50.0f },
        { BM_TJ("home.gauge.disk"), juce::String(diskUsedGB, 0) + " / " + juce::String(diskMax, 0) + " GB",
          diskUsedGB / diskMax },
    };

    auto gaugeColour = [](float norm) {
        return norm < 0.5f ? Colors::success()
             : norm < 0.8f ? Colors::warning()
                           : Colors::error();
    };

    if (horizontal)
    {
        const int tileGap = 10;
        const int tilesX = 16;
        const int tileY = 30;
        const int tileH = getHeight() - tileY - 12;
        const int tileW = (getWidth() - tilesX * 2 - tileGap * 3) / 4;
        for (int i = 0; i < 4; ++i)
        {
            const auto& ga = gauges[i];
            const float norm = juce::jlimit(0.0f, 1.0f, ga.norm);
            const int tx = tilesX + i * (tileW + tileGap);
            g.setColour(Colors::glassWhite());
            g.fillRoundedRectangle(static_cast<float>(tx), static_cast<float>(tileY),
                                   static_cast<float>(tileW), static_cast<float>(tileH), 8.0f);
            g.setColour(Colors::glassBorder());
            g.drawRoundedRectangle(static_cast<float>(tx), static_cast<float>(tileY),
                                   static_cast<float>(tileW), static_cast<float>(tileH), 8.0f, 0.8f);
            g.setFont(makeFont("Segoe UI", 9.0f, juce::Font::bold));
            g.setColour(Colors::textDim());
            g.drawText(ga.label.toUpperCase(), tx + 12, tileY + 8, tileW - 24, 12,
                       juce::Justification::centredLeft);
            g.setFont(makeFont("Consolas", 15.0f, juce::Font::bold));
            g.setColour(gaugeColour(norm));
            g.drawText(ga.value, tx + 12, tileY + 22, tileW - 24, 18, juce::Justification::centredLeft);

            const float barY = static_cast<float>(tileY + tileH - 14);
            const float barW = static_cast<float>(tileW - 24);
            g.setColour(Colors::bgElevated());
            g.fillRoundedRectangle(static_cast<float>(tx + 12), barY, barW, 5.0f, 2.5f);
            g.setColour(gaugeColour(norm));
            g.fillRoundedRectangle(static_cast<float>(tx + 12), barY, barW * norm, 5.0f, 2.5f);
        }
        return;
    }

    float y = 32.0f;
    float gaugeH = (getHeight() - 44.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        const auto& ga = gauges[i];
        const float norm = juce::jlimit(0.0f, 1.0f, ga.norm);
        const float gy = y + gaugeH * static_cast<float>(i);
        g.setFont(makeFont(9.0f));
        g.setColour(Colors::textDim());
        g.drawText(ga.label, 16, static_cast<int>(gy), 60, 12, juce::Justification::centredLeft);
        g.setFont(makeFont(13.0f, juce::Font::bold));
        g.setColour(gaugeColour(norm));
        g.drawText(ga.value, 16, static_cast<int>(gy + 11), 130, 16, juce::Justification::centredLeft);

        const float barX = 150.0f;
        const float barW = getWidth() - barX - 20.0f;
        g.setColour(Colors::bgElevated());
        g.fillRoundedRectangle(barX, gy + 16.0f, barW, 6.0f, 3.0f);
        g.setColour(gaugeColour(norm));
        g.fillRoundedRectangle(barX, gy + 16.0f, barW * norm, 6.0f, 3.0f);
    }
}

HomeView::HomeView()
    : m_provider(nullptr)
    , m_aliveFlag(std::make_shared<std::atomic<bool>>(true))
{
    setupUI();
    refreshStats();
    if (m_performanceCard)
        startTimerHz(2);

    retranslateUi();
}

HomeView::HomeView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
    , m_aliveFlag(std::make_shared<std::atomic<bool>>(true))
{
    setupUI();
    refreshStats();
    if (m_performanceCard)
        startTimerHz(2);

    m_refreshDebouncer = std::make_unique<DataChangeDebouncer>();
    m_refreshDebouncer->onFire = [this]() { refreshStats(); };

    if (m_provider)
    {
        juce::Component::SafePointer<HomeView> self(this);
        std::weak_ptr<std::atomic<bool>> aliveWeak = m_aliveFlag;
        m_provider->onDataChanged([self, aliveWeak] {
            auto alive = aliveWeak.lock();
            if (!alive || !alive->load()) return;
            juce::MessageManager::callAsync([self, aliveWeak] {
                auto a = aliveWeak.lock();
                if (!a || !a->load()) return;
                if (!self) return;
                if (self->m_refreshDebouncer)
                    self->m_refreshDebouncer->startTimer(500);
            });
        });
    }

    retranslateUi();
}

HomeView::~HomeView()
{
    stopTimer();
    if (m_aliveFlag)
        m_aliveFlag->store(false);
    if (m_refreshDebouncer)
        m_refreshDebouncer->stopTimer();
}

void HomeView::setRecentAddedTrackClickHandler(TrackClickedFn fn)
{
    if (m_recentAddedCard)
        m_recentAddedCard->onTrackClicked = std::move(fn);
}

void HomeView::setSuggestionClickHandler(SuggestionClickedFn fn)
{
    if (m_suggestionsCard)
        m_suggestionsCard->onTrackClicked = std::move(fn);
}

void HomeView::timerCallback()
{
    m_pulsePhase += 0.12f;
    if (m_pulsePhase > 6.2831853f) m_pulsePhase -= 6.2831853f;
    if (m_titleLabel)
    {
        auto titleArea = m_titleLabel->getBounds().expanded(40, 16);
        repaint(titleArea);
    }

    if (!m_performanceCard)
        return;

    {
        auto* sl     = serviceLocator();
        auto* engine = sl ? sl->tryGet<Core::AudioEngine>() : nullptr;
        m_performanceCard->latencyMs = engine ? static_cast<float>(engine->getLatencyMs()) : 0.0f;
    }

#ifdef _WIN32
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user))
    {
        auto ftToULL = [](const FILETIME& ft) -> uint64_t {
            return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        const uint64_t curIdle   = ftToULL(idle);
        const uint64_t curKernel = ftToULL(kernel);
        const uint64_t curUser   = ftToULL(user);
        const uint64_t dIdle   = curIdle   - m_prevIdleFt;
        const uint64_t dKernel = curKernel - m_prevKernelFt;
        const uint64_t dUser   = curUser   - m_prevUserFt;
        const uint64_t total   = dKernel + dUser;
        if (total > 0 && m_prevIdleFt != 0)
            m_performanceCard->cpuPercent = juce::jlimit(0.0f, 100.0f,
                static_cast<float>((total - dIdle) * 100.0 / static_cast<double>(total)));
        m_prevIdleFt   = curIdle;
        m_prevKernelFt = curKernel;
        m_prevUserFt   = curUser;
    }

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus))
    {
        constexpr double kBytesPerMB = 1024.0 * 1024.0;
        m_performanceCard->ramTotalMB = static_cast<float>(memStatus.ullTotalPhys / kBytesPerMB);
        m_performanceCard->ramMB = static_cast<float>(
            (memStatus.ullTotalPhys - memStatus.ullAvailPhys) / kBytesPerMB);
    }

    ULARGE_INTEGER freeBytes, totalBytes;
    if (GetDiskFreeSpaceExA("C:\\", &freeBytes, &totalBytes, nullptr))
    {
        constexpr double kBytesPerGB = 1024.0 * 1024.0 * 1024.0;
        const double total = static_cast<double>(totalBytes.QuadPart);
        const double free  = static_cast<double>(freeBytes.QuadPart);
        m_performanceCard->diskTotalGB = static_cast<float>(total / kBytesPerGB);
        m_performanceCard->diskUsedGB  = static_cast<float>((total - free) / kBytesPerGB);
    }
#else
    m_performanceCard->cpuPercent  = 0.0f;
    m_performanceCard->ramMB       = 0.0f;
    m_performanceCard->ramTotalMB  = 0.0f;
    m_performanceCard->diskUsedGB  = 0.0f;
    m_performanceCard->diskTotalGB = 0.0f;
#endif

    m_performanceCard->repaint();
}

void HomeView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("title", BM_TJ("home.title"));
    m_titleLabel->setFont(makeFont(22.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_collectionCard = std::make_unique<CollectionCard>();
    m_collectionCard->cardTitle = BM_TJ("home.card.collection");
    m_collectionCard->accentColor = Colors::primary();
    addAndMakeVisible(*m_collectionCard);

    m_recentAddedCard = std::make_unique<RecentAddedCard>();
    m_recentAddedCard->cardTitle = BM_TJ("home.card.recentAdded");
    m_recentAddedCard->accentColor = Colors::success();
    addAndMakeVisible(*m_recentAddedCard);

    m_recentPlayedCard = std::make_unique<RecentPlayedCard>();
    m_recentPlayedCard->cardTitle = BM_TJ("home.card.recentPlayed");
    m_recentPlayedCard->accentColor = Colors::accent();
    addAndMakeVisible(*m_recentPlayedCard);

    m_suggestionsCard = std::make_unique<SuggestionsCard>();
    m_suggestionsCard->cardTitle = BM_TJ("home.card.suggestions");
    m_suggestionsCard->accentColor = Colors::warning();
    addAndMakeVisible(*m_suggestionsCard);

    m_djSoftwareCard = std::make_unique<DJSoftwareCard>();
    m_djSoftwareCard->cardTitle = BM_TJ("home.card.djSoftware");
    m_djSoftwareCard->accentColor = juce::Colour(0xFFFF6B9D);
    addAndMakeVisible(*m_djSoftwareCard);

    m_nextEventCard = std::make_unique<NextEventCard>();
    m_nextEventCard->cardTitle = BM_TJ("home.card.nextEvent");
    m_nextEventCard->accentColor = juce::Colour(0xFFB048FF);
    m_nextEventCard->onCreateEvent = [this] {
        if (onNavigateToModule)
            onNavigateToModule(kModuleIndexPreparationSoiree);
    };
    addAndMakeVisible(*m_nextEventCard);

    m_performanceCard = std::make_unique<PerformanceCard>();
    m_performanceCard->cardTitle = BM_TJ("home.card.performance");
    m_performanceCard->accentColor = Colors::success();
    addAndMakeVisible(*m_performanceCard);

    m_viewAllBtn = std::make_unique<juce::TextButton>(BM_TJ("home.viewAll"));
    m_viewAllBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_viewAllBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_viewAllBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_viewAllBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_viewAllBtn->onClick = [this] { spdlog::info("[HomeView] viewAllBtn: navigate to Library"); m_listeners.call(&Listener::navigateToLibrary); };
    addAndMakeVisible(*m_viewAllBtn);

    m_connectBtn = std::make_unique<juce::TextButton>(BM_TJ("home.searchDj"));
    m_connectBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_connectBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_connectBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_connectBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_connectBtn->onClick = [this] {
        spdlog::info("[HomeView] connectBtn: scanning DJ software and syncing collections");
        auto* sl = serviceLocator();

        try {
            Services::DJSoftware::DJSoftwareDetector detector;
            auto found = detector.detect();
            spdlog::info("[HomeView] connectBtn: detected {} DJ software entries", found.size());
            if (m_djSoftwareCard) {
                m_djSoftwareCard->platforms.clear();
                for (const auto& info : found) {
                    HomeView::DJSoftwareCard::Platform p;
                    p.name = juce::String(info.name);
                    p.connected = info.isInstalled;
                    m_djSoftwareCard->platforms.push_back(std::move(p));
                }
                m_djSoftwareCard->repaint();
            }
        } catch (const std::exception& e) {
            spdlog::error("[HomeView] connectBtn: DJSoftwareDetector failed: {}", e.what());
        }

        if (sl) {
            auto* sync = sl->tryGet<Services::DJSoftware::CollectionSyncService>();
            if (sync) {
                spdlog::info("[HomeView] connectBtn: triggering CollectionSyncService::syncAll() + syncAllPlaylists()");
                sync->syncAll();
                int added = sync->syncAllPlaylists();
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    BM_TJ("home.searchDj"),
                    BM_TJ("home.syncDone").replace("{n}", juce::String(added)),
                    "OK");
            } else {
                spdlog::warn("[HomeView] connectBtn: CollectionSyncService not available in ServiceLocator");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    BM_TJ("home.searchDj"),
                    BM_TJ("home.syncUnavailable"),
                    "OK");
            }
        }

        m_listeners.call(&Listener::navigateToLibrary);
    };
    addAndMakeVisible(*m_connectBtn);
}

void HomeView::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("home.title"), juce::dontSendNotification);

    if (m_collectionCard)
    {
        m_collectionCard->cardTitle = BM_TJ("home.card.collection");
        m_collectionCard->repaint();
    }
    if (m_recentAddedCard)
    {
        m_recentAddedCard->cardTitle = BM_TJ("home.card.recentAdded");
        m_recentAddedCard->repaint();
    }
    if (m_recentPlayedCard)
    {
        m_recentPlayedCard->cardTitle = BM_TJ("home.card.recentPlayed");
        m_recentPlayedCard->repaint();
    }
    if (m_suggestionsCard)
    {
        m_suggestionsCard->cardTitle = BM_TJ("home.card.suggestions");
        m_suggestionsCard->repaint();
    }
    if (m_djSoftwareCard)
    {
        m_djSoftwareCard->cardTitle = BM_TJ("home.card.djSoftware");
        m_djSoftwareCard->repaint();
    }
    if (m_nextEventCard)
    {
        m_nextEventCard->cardTitle = BM_TJ("home.card.nextEvent");
        m_nextEventCard->repaint();
    }
    if (m_performanceCard)
    {
        m_performanceCard->cardTitle = BM_TJ("home.card.performance");
        m_performanceCard->repaint();
    }

    if (m_viewAllBtn)
        m_viewAllBtn->setButtonText(BM_TJ("home.viewAll"));
    if (m_connectBtn)
        m_connectBtn->setButtonText(BM_TJ("home.searchDj"));

    repaint();
}

void HomeView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    if (!m_titleLabel) return;
    float pulseAlpha = 0.7f + 0.3f * std::sin(m_pulsePhase);
    auto titleBounds = m_titleLabel->getBounds().toFloat();
    juce::ColourGradient titleGlow(Colors::primary().withAlpha(0.08f * pulseAlpha),
                                   titleBounds.getCentreX(), titleBounds.getCentreY(),
                                   juce::Colours::transparentBlack,
                                   titleBounds.getCentreX() + 120.0f, titleBounds.getCentreY(), true);
    g.setGradientFill(titleGlow);
    g.fillEllipse(titleBounds.expanded(40.0f, 16.0f));

    g.setFont(Type::subheading());
    g.setColour(Colors::textSecondary());
    g.drawText(juce::Time::getCurrentTime().formatted("%d/%m/%Y"),
               getWidth() - 224, static_cast<int>(titleBounds.getY()), 200,
               static_cast<int>(titleBounds.getHeight()), juce::Justification::centredRight);

    ProDraw::vignette(g, static_cast<float>(getWidth()), static_cast<float>(getHeight()));
}

void HomeView::resized()
{
    const int margin = 24;
    const int gap = 14;
    const int w = getWidth();

    m_titleLabel->setBounds(margin, 16, 400, 32);
    m_titleLabel->setFont(Type::display());

    const int contentY = 62;
    const bool twoColumns = (w > 980);

    if (twoColumns)
    {
        const int availW = w - margin * 2;
        const int availH = juce::jmax(700, getHeight()) - contentY - 16;

        const int heroH = juce::jmax(226, availH * 33 / 100);
        const int perfH = juce::jmax(96, availH * 14 / 100);
        const int midH  = availH - heroH - perfH - gap * 2;

        const int heroLeftW = availW * 58 / 100;
        const int heroRightX = margin + heroLeftW + gap;
        const int heroRightW = availW - heroLeftW - gap;

        m_collectionCard->setBounds(margin, contentY, heroLeftW, heroH);
        const int djH = (heroH - gap) * 56 / 100;
        m_djSoftwareCard->setBounds(heroRightX, contentY, heroRightW, djH);
        m_nextEventCard->setBounds(heroRightX, contentY + djH + gap, heroRightW, heroH - djH - gap);

        const int midY = contentY + heroH + gap;
        const int cardW = (availW - gap * 2) / 3;
        m_recentAddedCard->setBounds(margin, midY, cardW, midH);
        m_suggestionsCard->setBounds(margin + cardW + gap, midY, cardW, midH);
        m_recentPlayedCard->setBounds(margin + (cardW + gap) * 2, midY, availW - (cardW + gap) * 2, midH);

        m_performanceCard->setBounds(margin, midY + midH + gap, availW, perfH);

        m_viewAllBtn->setBounds(margin + heroLeftW - 96, contentY + 8, 82, 24);
        m_connectBtn->setBounds(heroRightX + heroRightW - 176, contentY + djH - 34, 162, 26);

        const int totalH = midY + midH + gap + perfH + 16;
        if (getHeight() < totalH)
            setSize(w, totalH);
    }
    else
    {
        const int colX = margin;
        const int colW = w - margin * 2;
        int y = contentY;

        m_collectionCard->setBounds(colX, y, colW, 230); y += 230 + gap;
        m_viewAllBtn->setBounds(colX + colW - 96, contentY + 8, 82, 24);

        m_recentAddedCard->setBounds(colX, y, colW, 210); y += 210 + gap;
        m_suggestionsCard->setBounds(colX, y, colW, 190); y += 190 + gap;
        m_recentPlayedCard->setBounds(colX, y, colW, 190); y += 190 + gap;

        m_djSoftwareCard->setBounds(colX, y, colW, 160);
        m_connectBtn->setBounds(colX + colW - 176, y + 160 - 34, 162, 26);
        y += 160 + gap;

        m_nextEventCard->setBounds(colX, y, colW, 120); y += 120 + gap;
        m_performanceCard->setBounds(colX, y, colW, 170); y += 170 + 16;

        if (getHeight() < y)
            setSize(w, y);
    }
}

void HomeView::refreshStats()
{
    if (m_provider)
    {
        int total = m_provider->getTotalTracks();
        int analyzed = m_provider->getAnalyzedTracks();
        auto genres = m_provider->getGenreDistribution();

        if (m_collectionCard)
        {
            m_collectionCard->totalTracks = total;
            m_collectionCard->analyzedCount = analyzed;
            m_collectionCard->analyzedPercent = total > 0 ? (analyzed * 100 / total) : 0;
            m_collectionCard->genreCount = static_cast<int>(genres.size());
            m_collectionCard->lastSync = BM_TJ("home.ago.now");

            m_collectionCard->bpmHistogram.clear();
            constexpr int kBuckets = 14;
            constexpr double kBpmMin = 60.0;
            constexpr double kBpmMax = 200.0;
            std::vector<int> histogram(kBuckets, 0);
            int withBpm = 0;
            for (const auto& t : m_provider->getAllTracks())
            {
                if (t.bpm <= 0.0)
                    continue;
                const double clamped = juce::jlimit(kBpmMin, kBpmMax, t.bpm);
                const int bucket = juce::jlimit(0, kBuckets - 1,
                    static_cast<int>((clamped - kBpmMin) / (kBpmMax - kBpmMin) * kBuckets));
                ++histogram[static_cast<size_t>(bucket)];
                ++withBpm;
            }
            if (withBpm > 0)
                m_collectionCard->bpmHistogram = std::move(histogram);
            m_collectionCard->repaint();
        }

        auto recentAdded = m_provider->getRecentlyAdded(8);
        if (m_recentAddedCard)
        {
            m_recentAddedCard->tracks.clear();
            for (auto& t : recentAdded)
            {
                m_recentAddedCard->tracks.push_back({
                    juce::String(t.title),
                    juce::String(t.artist),
                    juce::String(t.filePath),
                    static_cast<float>(t.bpm),
                    juce::String(t.camelotKey.empty() ? t.key : t.camelotKey),
                    t.id
                });
            }
            m_recentAddedCard->repaint();
        }

        auto recentPlayed = m_provider->getRecentlyPlayed(8);
        if (m_recentPlayedCard) {
        m_recentPlayedCard->tracks.clear();
        for (auto& t : recentPlayed)
        {
            juce::String timestamp;
            if (t.lastPlayed > 0)
            {
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                auto diff = now - t.lastPlayed;
                if (diff < 3600)
                    timestamp = BM_TJ("home.ago.minutesFmt").replace("{n}", juce::String(static_cast<int>(diff / 60)));
                else if (diff < 86400)
                    timestamp = BM_TJ("home.ago.hoursFmt").replace("{n}", juce::String(static_cast<int>(diff / 3600)));
                else
                    timestamp = BM_TJ("home.ago.daysFmt").replace("{n}", juce::String(static_cast<int>(diff / 86400)));
            }
            m_recentPlayedCard->tracks.push_back({
                juce::String(t.title),
                juce::String(t.artist),
                timestamp
            });
        }
        m_recentPlayedCard->repaint();
        }

        if (m_suggestionsCard) {
        m_suggestionsCard->suggestions.clear();
        {
            auto lastPlayedList = m_provider->getRecentlyPlayed(1);
            bool usedCompatible = false;
            if (!lastPlayedList.empty())
            {
                auto& lastTrack = lastPlayedList.front();
                if (lastTrack.bpm > 0.0 && !lastTrack.key.empty())
                {
                    auto compatible = m_provider->getCompatibleTracks(
                        static_cast<float>(lastTrack.bpm), lastTrack.key, 5);
                    if (!compatible.empty())
                    {
                        usedCompatible = true;
                        for (auto& t : compatible)
                        {
                            juce::String reason;
                            float bpmDiff = std::abs(static_cast<float>(t.bpm - lastTrack.bpm));
                            if (bpmDiff < 2.0f && t.key == lastTrack.key)
                                reason = "BPM + " + BM_TJ("home.reason.keyCompat");
                            else if (t.key == lastTrack.key)
                                reason = BM_TJ("home.reason.keyCompat");
                            else if (bpmDiff < 5.0f)
                                reason = BM_TJ("home.reason.bpmClose");
                            else
                                reason = juce::String(t.genre.empty() ? BM_T("home.reason.compat") : t.genre);

                            m_suggestionsCard->suggestions.push_back({
                                juce::String(t.title),
                                juce::String(t.artist),
                                reason,
                                juce::jlimit(0, 100, t.playCount * 10 + static_cast<int>(t.rating) * 15)
                            });
                        }
                    }
                }
            }
            if (!usedCompatible)
            {
                auto mostPlayed = m_provider->getMostPlayed(5);
                for (auto& t : mostPlayed)
                {
                    m_suggestionsCard->suggestions.push_back({
                        juce::String(t.title),
                        juce::String(t.artist),
                        juce::String(t.genre.empty() ? BM_T("home.popular") : t.genre),
                        juce::jlimit(0, 100, t.playCount * 10 + static_cast<int>(t.rating) * 15)
                    });
                }
            }
        }
        m_suggestionsCard->repaint();
        }
    }
    else
    {
        if (m_collectionCard)
        {
            m_collectionCard->totalTracks = 0;
            m_collectionCard->analyzedPercent = 0;
            m_collectionCard->genreCount = 0;
            m_collectionCard->lastSync = "-";
            m_collectionCard->repaint();
        }

        if (m_recentAddedCard) {
            m_recentAddedCard->tracks.clear();
            m_recentAddedCard->repaint();
        }

        if (m_recentPlayedCard) {
            m_recentPlayedCard->tracks.clear();
            m_recentPlayedCard->repaint();
        }

        if (m_suggestionsCard) {
            m_suggestionsCard->suggestions.clear();
            m_suggestionsCard->repaint();
        }
    }

    if (m_djSoftwareCard)
    {
        auto* sl = serviceLocator();
        m_djSoftwareCard->platforms.clear();
        if (sl)
        {
            auto* detector = sl->tryGet<Services::DJSoftware::DJSoftwareDetector>();
            if (detector)
            {
                auto results = detector->detect();
                for (auto& info : results)
                    m_djSoftwareCard->platforms.push_back({juce::String(info.name), info.isInstalled});
            }
        }
        if (m_djSoftwareCard->platforms.empty())
        {
            m_djSoftwareCard->platforms = {
                {"Rekordbox", false},
                {"Traktor", false},
                {"Serato", false},
                {"VirtualDJ", false},
            };
        }
        m_djSoftwareCard->repaint();
    }

    if (m_nextEventCard)
    {
        m_nextEventCard->hasEvent = false;
        m_nextEventCard->eventName = BM_TJ("home.createEvent");
        m_nextEventCard->venue = "";
        m_nextEventCard->date = "";
        m_nextEventCard->phasesCount = 0;
        m_nextEventCard->repaint();
    }

    if (m_performanceCard)
    {
        m_performanceCard->cpuPercent = 0.0f;
        m_performanceCard->ramMB = 0.0f;
        m_performanceCard->latencyMs = 0.0f;
    }
}

} // namespace BeatMate::UI
