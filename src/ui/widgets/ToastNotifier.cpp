#include "ToastNotifier.h"

namespace BeatMate::UI::Widgets {

namespace {
    constexpr int kToastWidth = 320;
    constexpr int kToastHeight = 72;
    constexpr int kToastSpacing = 8;
    constexpr int kEdgeMargin = 16;
    constexpr int kAccentWidth = 4;
    constexpr int kCloseSize = 12;
    constexpr int kMaxVisible = 4;
    constexpr int kFadeMs = 200;
    constexpr int kTimerHz = 30;

    int64_t nowMs() { return juce::Time::currentTimeMillis(); }
}

ToastNotifier* ToastNotifier::s_instance = nullptr;

ToastNotifier::ToastNotifier()
{
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(false);
    startTimerHz(kTimerHz);
    m_lastTickMs = nowMs();
}

ToastNotifier::~ToastNotifier()
{
    stopTimer();
    if (s_instance == this) s_instance = nullptr;
}

ToastNotifier& ToastNotifier::getInstance()
{
    static ToastNotifier fallback;
    return s_instance != nullptr ? *s_instance : fallback;
}

void ToastNotifier::setInstance(ToastNotifier* instance) { s_instance = instance; }

int ToastNotifier::show(const juce::String& title, const juce::String& message, Kind kind, int autoCloseMs)
{
    ToastItem item;
    item.id = m_nextId++;
    item.title = title;
    item.message = message;
    item.kind = kind;
    item.autoCloseMs = autoCloseMs;
    item.createdMs = nowMs();
    item.alpha = 0.0f;
    item.dismissing = false;
    m_items.push_back(std::move(item));

    while (static_cast<int>(m_items.size()) > kMaxVisible) {
        for (auto& it : m_items) {
            if (!it.dismissing) { it.dismissing = true; break; }
        }
        bool anyActive = false;
        for (auto& it : m_items) if (!it.dismissing) { anyActive = true; break; }
        if (!anyActive) break;
        if (static_cast<int>(m_items.size()) > kMaxVisible) {
            m_items.erase(m_items.begin());
        }
    }

    layoutToasts();
    repaint();
    return m_items.back().id;
}

void ToastNotifier::update(int id, const juce::String& message, Kind kind)
{
    int idx = findById(id);
    if (idx < 0) return;
    m_items[static_cast<size_t>(idx)].message = message;
    m_items[static_cast<size_t>(idx)].kind = kind;
    m_items[static_cast<size_t>(idx)].createdMs = nowMs();
    repaint();
}

void ToastNotifier::dismiss(int id)
{
    int idx = findById(id);
    if (idx < 0) return;
    m_items[static_cast<size_t>(idx)].dismissing = true;
}

void ToastNotifier::setCancelCallback(int id, std::function<void()> cb)
{
    int idx = findById(id);
    if (idx < 0) return;
    m_items[static_cast<size_t>(idx)].cancelCb = std::move(cb);
}

int ToastNotifier::findById(int id) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return static_cast<int>(i);
    return -1;
}

juce::Colour ToastNotifier::accentFor(Kind k) const
{
    switch (k) {
        case Kind::Info:     return juce::Colour(0xFF3B82F6);
        case Kind::Success:  return juce::Colour(0xFF10B981);
        case Kind::Warning:  return juce::Colour(0xFFF59E0B);
        case Kind::Error:    return juce::Colour(0xFFEF4444);
        case Kind::Progress: return juce::Colour(0xFF8B5CF6);
    }
    return juce::Colour(0xFF3B82F6);
}

juce::Rectangle<int> ToastNotifier::getToastBounds(int index) const
{
    const int w = getWidth();
    const int h = getHeight();
    const int x = w - kToastWidth - kEdgeMargin;
    const int y = h - kEdgeMargin - kToastHeight - index * (kToastHeight + kToastSpacing);
    return { x, y, kToastWidth, kToastHeight };
}

juce::Rectangle<int> ToastNotifier::getCloseButtonBounds(const juce::Rectangle<int>& toast) const
{
    return juce::Rectangle<int>(toast.getRight() - kCloseSize - 8, toast.getY() + 8, kCloseSize, kCloseSize);
}

void ToastNotifier::layoutToasts() { repaint(); }

void ToastNotifier::resized() { layoutToasts(); }

bool ToastNotifier::hitTest(int x, int y)
{
    for (size_t i = 0; i < m_items.size() && i < static_cast<size_t>(kMaxVisible); ++i) {
        auto r = getToastBounds(static_cast<int>(i));
        if (r.contains(x, y)) return true;
    }
    return false;
}

void ToastNotifier::mouseDown(const juce::MouseEvent& e)
{
    for (size_t i = 0; i < m_items.size() && i < static_cast<size_t>(kMaxVisible); ++i) {
        auto r = getToastBounds(static_cast<int>(i));
        if (!r.contains(e.x, e.y)) continue;
        auto closeR = getCloseButtonBounds(r);
        if (closeR.expanded(4).contains(e.x, e.y)) {
            auto& it = m_items[i];
            if (it.cancelCb) {
                auto cb = it.cancelCb;
                cb();
            }
            it.dismissing = true;
            repaint();
        }
        return;
    }
}

void ToastNotifier::mouseMove(const juce::MouseEvent& e)
{
    int newHover = -1;
    for (size_t i = 0; i < m_items.size() && i < static_cast<size_t>(kMaxVisible); ++i) {
        auto r = getToastBounds(static_cast<int>(i));
        if (!r.contains(e.x, e.y)) continue;
        auto closeR = getCloseButtonBounds(r);
        if (closeR.expanded(4).contains(e.x, e.y)) { newHover = m_items[i].id; break; }
    }
    if (newHover != m_hoverCloseId) { m_hoverCloseId = newHover; repaint(); }
}

void ToastNotifier::mouseExit(const juce::MouseEvent&)
{
    if (m_hoverCloseId != -1) { m_hoverCloseId = -1; repaint(); }
}

void ToastNotifier::timerCallback()
{
    const int64_t now = nowMs();
    const int64_t dtMs = juce::jmax<int64_t>(1, now - m_lastTickMs);
    m_lastTickMs = now;

    const float fadeStep = static_cast<float>(dtMs) / static_cast<float>(kFadeMs);
    bool anyProgress = false;
    bool needRepaint = false;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        if (it->dismissing) {
            it->alpha -= fadeStep;
            needRepaint = true;
            if (it->alpha <= 0.0f) { it = m_items.erase(it); continue; }
        } else {
            if (it->alpha < 1.0f) { it->alpha = juce::jmin(1.0f, it->alpha + fadeStep); needRepaint = true; }
            if (it->autoCloseMs > 0 && it->kind != Kind::Progress) {
                if (now - it->createdMs >= it->autoCloseMs) it->dismissing = true;
            }
            if (it->kind == Kind::Progress) anyProgress = true;
        }
        ++it;
    }

    if (anyProgress) {
        const float rpm = 8.0f;
        const float radPerMs = (rpm * juce::MathConstants<float>::twoPi) / 60000.0f;
        m_spinnerAngle += radPerMs * static_cast<float>(dtMs);
        if (m_spinnerAngle > juce::MathConstants<float>::twoPi)
            m_spinnerAngle -= juce::MathConstants<float>::twoPi;
        needRepaint = true;
    }

    if (needRepaint) repaint();
}

void ToastNotifier::paint(juce::Graphics& g)
{
    const juce::Colour bg(0xF0101018);
    const juce::Colour border(0xFF1E293B);
    const juce::Colour titleColor(0xFFFFFFFF);
    const juce::Colour msgColor(0xFFCBD5E1);
    const juce::Colour closeHover(0xFFEF4444);
    const juce::Colour closeIdle(0xFF94A3B8);

    const juce::Font titleFont("Segoe UI", 13.0f, juce::Font::bold);
    const juce::Font msgFont("Segoe UI", 11.0f, juce::Font::plain);

    const int visible = juce::jmin(static_cast<int>(m_items.size()), kMaxVisible);
    for (int i = 0; i < visible; ++i) {
        auto& it = m_items[static_cast<size_t>(i)];
        auto r = getToastBounds(i).toFloat();
        const float a = juce::jlimit(0.0f, 1.0f, it.alpha);

        g.setColour(bg.withMultipliedAlpha(a));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(border.withMultipliedAlpha(a));
        g.drawRoundedRectangle(r, 10.0f, 1.0f);

        juce::Rectangle<float> accent(r.getX(), r.getY(), static_cast<float>(kAccentWidth), r.getHeight());
        juce::Path accentPath;
        accentPath.addRoundedRectangle(accent.getX(), accent.getY(), accent.getWidth(), accent.getHeight(),
                                       2.0f, 2.0f, true, false, true, false);
        g.setColour(accentFor(it.kind).withMultipliedAlpha(a));
        g.fillPath(accentPath);

        const float textX = r.getX() + kAccentWidth + 12.0f;
        const float textW = r.getWidth() - (kAccentWidth + 12.0f) - (kCloseSize + 16.0f);
        juce::Rectangle<float> titleR(textX, r.getY() + 10.0f, textW, 18.0f);
        juce::Rectangle<float> msgR(textX, r.getY() + 30.0f, textW, r.getHeight() - 38.0f);

        g.setFont(titleFont);
        g.setColour(titleColor.withMultipliedAlpha(a));
        g.drawText(it.title, titleR, juce::Justification::centredLeft, true);

        if (it.message.isNotEmpty()) {
            g.setFont(msgFont);
            g.setColour(msgColor.withMultipliedAlpha(a));
            g.drawFittedText(it.message, msgR.toNearestInt(), juce::Justification::topLeft, 2, 0.85f);
        }

        if (it.kind == Kind::Progress) {
            const float spinR = 7.0f;
            const float cx = r.getRight() - kCloseSize - 16.0f - spinR;
            const float cy = titleR.getCentreY();
            const juce::Colour spinCol = accentFor(Kind::Progress).withMultipliedAlpha(a);
            for (int s = 0; s < 8; ++s) {
                const float ang = m_spinnerAngle + s * (juce::MathConstants<float>::twoPi / 8.0f);
                const float x1 = cx + std::cos(ang) * (spinR * 0.55f);
                const float y1 = cy + std::sin(ang) * (spinR * 0.55f);
                const float x2 = cx + std::cos(ang) * spinR;
                const float y2 = cy + std::sin(ang) * spinR;
                const float segAlpha = static_cast<float>(s + 1) / 8.0f;
                g.setColour(spinCol.withMultipliedAlpha(segAlpha));
                g.drawLine(x1, y1, x2, y2, 1.6f);
            }
        }

        auto closeR = getCloseButtonBounds(getToastBounds(i)).toFloat();
        const bool hover = (m_hoverCloseId == it.id);
        g.setColour((hover ? closeHover : closeIdle).withMultipliedAlpha(a));
        const float pad = 2.0f;
        g.drawLine(closeR.getX() + pad, closeR.getY() + pad,
                   closeR.getRight() - pad, closeR.getBottom() - pad, 1.4f);
        g.drawLine(closeR.getRight() - pad, closeR.getY() + pad,
                   closeR.getX() + pad, closeR.getBottom() - pad, 1.4f);
    }
}

} // namespace BeatMate::UI::Widgets
