#include "TransitionEditorWidget.h"

#include <algorithm>
#include <cmath>

namespace BeatMate::UI::Widgets {

namespace {
constexpr float kBarHeight       = 56.0f;
constexpr float kHandleWidth     = 10.0f;
constexpr float kLabelAreaHeight = 22.0f;
constexpr float kPadding         = 12.0f;
constexpr double kMinViewWindow  = 32.0; // seconds minimum for x-axis

juce::String formatSeconds(double s) {
    if (s < 0.0) s = 0.0;
    const int total = static_cast<int>(std::round(s));
    const int mm = total / 60;
    const int ss = total % 60;
    return juce::String::formatted("%d:%02d", mm, ss);
}
}

TransitionEditorWidget::TransitionEditorWidget() {
    setInterceptsMouseClicks(true, true);

    crossfadeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    crossfadeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    crossfadeSlider_.setRange(0.0, 1.0, 0.01);
    crossfadeSlider_.setValue(plan_.crossfadeCurve, juce::dontSendNotification);
    crossfadeSlider_.onValueChange = [this]() {
        plan_.crossfadeCurve = static_cast<float>(crossfadeSlider_.getValue());
        notifyChange();
        repaint();
    };
    addAndMakeVisible(crossfadeSlider_);
}

void TransitionEditorWidget::setPlan(const Services::Preparation::TransitionPlan& plan) {
    plan_ = plan;
    crossfadeSlider_.setValue(plan_.crossfadeCurve, juce::dontSendNotification);
    repaint();
}

Services::Preparation::TransitionPlan TransitionEditorWidget::getPlan() const {
    return plan_;
}

double TransitionEditorWidget::viewSecondsA() const {
    if (trackADuration_ > 0.0) return trackADuration_;
    // At least show mixOutStart + overlap with comfortable margin
    return std::max(kMinViewWindow, plan_.mixOutStart + plan_.overlapSec * 2.0);
}

double TransitionEditorWidget::viewSecondsB() const {
    if (trackBDuration_ > 0.0) return trackBDuration_;
    return std::max(kMinViewWindow, plan_.mixInStart + plan_.overlapSec * 2.0);
}

juce::Rectangle<float> TransitionEditorWidget::getBarAArea() const {
    auto r = getLocalBounds().toFloat().reduced(kPadding);
    return { r.getX(), r.getY() + kLabelAreaHeight, r.getWidth(), kBarHeight };
}

juce::Rectangle<float> TransitionEditorWidget::getBarBArea() const {
    auto r = getLocalBounds().toFloat().reduced(kPadding);
    const float y = r.getY() + kLabelAreaHeight + kBarHeight + 28.0f;
    return { r.getX(), y, r.getWidth(), kBarHeight };
}

float TransitionEditorWidget::timeToXA(double seconds, juce::Rectangle<float> bar) const {
    const double total = std::max(1.0, viewSecondsA());
    const double t = juce::jlimit(0.0, total, seconds);
    return bar.getX() + static_cast<float>((t / total) * bar.getWidth());
}

float TransitionEditorWidget::timeToXB(double seconds, juce::Rectangle<float> bar) const {
    const double total = std::max(1.0, viewSecondsB());
    const double t = juce::jlimit(0.0, total, seconds);
    return bar.getX() + static_cast<float>((t / total) * bar.getWidth());
}

double TransitionEditorWidget::xToTimeA(float x, juce::Rectangle<float> bar) const {
    const double total = std::max(1.0, viewSecondsA());
    const float rel = juce::jlimit(0.0f, bar.getWidth(), x - bar.getX());
    return (static_cast<double>(rel) / bar.getWidth()) * total;
}

double TransitionEditorWidget::xToTimeB(float x, juce::Rectangle<float> bar) const {
    const double total = std::max(1.0, viewSecondsB());
    const float rel = juce::jlimit(0.0f, bar.getWidth(), x - bar.getX());
    return (static_cast<double>(rel) / bar.getWidth()) * total;
}

juce::Rectangle<float> TransitionEditorWidget::getMixOutHandleArea() const {
    auto bar = getBarAArea();
    const float x = timeToXA(plan_.mixOutStart, bar);
    return { x - kHandleWidth * 0.5f, bar.getY() - 4.0f, kHandleWidth, bar.getHeight() + 8.0f };
}

juce::Rectangle<float> TransitionEditorWidget::getMixInHandleArea() const {
    auto bar = getBarBArea();
    const float x = timeToXB(plan_.mixInStart, bar);
    return { x - kHandleWidth * 0.5f, bar.getY() - 4.0f, kHandleWidth, bar.getHeight() + 8.0f };
}

void TransitionEditorWidget::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1d22));

    auto barA = getBarAArea();
    auto barB = getBarBArea();

    const juce::Font labelFont(juce::FontOptions{}.withHeight(12.0f));
    const juce::Font bigFont(juce::FontOptions{}.withHeight(13.0f).withStyle("Bold"));

    g.setColour(juce::Colour(0xff2a2f36));
    g.fillRoundedRectangle(barA, 4.0f);

    g.setColour(juce::Colour(0xff4a90e2).withAlpha(0.55f));
    {
        const int steps = juce::jmax(20, static_cast<int>(barA.getWidth() / 3.0f));
        for (int i = 0; i < steps; ++i) {
            const float fx = barA.getX() + (i / static_cast<float>(steps)) * barA.getWidth();
            const float amp = 0.25f + 0.6f * (0.5f + 0.5f * std::sin(i * 0.37f + 1.1f));
            const float h = barA.getHeight() * amp;
            g.fillRect(fx, barA.getCentreY() - h * 0.5f, 2.0f, h);
        }
    }

    g.setColour(juce::Colour(0xff2a2f36));
    g.fillRoundedRectangle(barB, 4.0f);

    g.setColour(juce::Colour(0xffe27a4a).withAlpha(0.55f));
    {
        const int steps = juce::jmax(20, static_cast<int>(barB.getWidth() / 3.0f));
        for (int i = 0; i < steps; ++i) {
            const float fx = barB.getX() + (i / static_cast<float>(steps)) * barB.getWidth();
            const float amp = 0.25f + 0.6f * (0.5f + 0.5f * std::sin(i * 0.29f + 2.3f));
            const float h = barB.getHeight() * amp;
            g.fillRect(fx, barB.getCentreY() - h * 0.5f, 2.0f, h);
        }
    }

    const float aStartX = timeToXA(plan_.mixOutStart, barA);
    const float aEndX   = timeToXA(plan_.mixOutStart + plan_.overlapSec, barA);
    const float bStartX = timeToXB(plan_.mixInStart, barB);
    const float bEndX   = timeToXB(plan_.mixInStart + plan_.overlapSec, barB);

    juce::ColourGradient gradA(juce::Colour(0xff4a90e2).withAlpha(0.75f), aStartX, 0.0f,
                               juce::Colour(0xff4a90e2).withAlpha(0.0f), aEndX, 0.0f, false);
    g.setGradientFill(gradA);
    g.fillRect(juce::Rectangle<float>(aStartX, barA.getY(), std::max(1.0f, aEndX - aStartX), barA.getHeight()));

    juce::ColourGradient gradB(juce::Colour(0xffe27a4a).withAlpha(0.0f), bStartX, 0.0f,
                               juce::Colour(0xffe27a4a).withAlpha(0.75f), bEndX, 0.0f, false);
    g.setGradientFill(gradB);
    g.fillRect(juce::Rectangle<float>(bStartX, barB.getY(), std::max(1.0f, bEndX - bStartX), barB.getHeight()));

    juce::Path overlapPath;
    overlapPath.startNewSubPath(aStartX, barA.getBottom());
    overlapPath.lineTo(aEndX,   barA.getBottom());
    overlapPath.lineTo(bEndX,   barB.getY());
    overlapPath.lineTo(bStartX, barB.getY());
    overlapPath.closeSubPath();
    g.setColour(juce::Colour(0xff9b59b6).withAlpha(0.25f));
    g.fillPath(overlapPath);

    g.setColour(juce::Colour(0xffffffff));
    g.fillRoundedRectangle(getMixOutHandleArea(), 2.0f);
    g.fillRoundedRectangle(getMixInHandleArea(), 2.0f);

    g.setColour(juce::Colours::white);
    g.setFont(bigFont);
    auto r = getLocalBounds().toFloat().reduced(kPadding);
    g.drawText("Track A (Mix Out)", juce::Rectangle<float>(r.getX(), r.getY(), r.getWidth(), kLabelAreaHeight),
               juce::Justification::centredLeft);
    g.drawText("Track B (Mix In)",
               juce::Rectangle<float>(r.getX(), barB.getY() - kLabelAreaHeight, r.getWidth(), kLabelAreaHeight),
               juce::Justification::centredLeft);

    g.setFont(labelFont);
    g.setColour(juce::Colour(0xffc0c4cc));
    const juce::String info = "Out@" + formatSeconds(plan_.mixOutStart)
                            + "  In@" + formatSeconds(plan_.mixInStart)
                            + "  Overlap " + juce::String(plan_.overlapSec, 1) + "s"
                            + "  Curve " + juce::String(plan_.crossfadeCurve, 2);
    g.drawText(info, juce::Rectangle<float>(r.getX(), r.getY(), r.getWidth(), kLabelAreaHeight),
               juce::Justification::centredRight);
}

void TransitionEditorWidget::resized() {
    auto r = getLocalBounds().reduced(static_cast<int>(kPadding));
    const int sliderH = 22;
    crossfadeSlider_.setBounds(r.getX() + r.getWidth() / 2,
                               r.getBottom() - sliderH,
                               r.getWidth() / 2,
                               sliderH);
}

void TransitionEditorWidget::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.position;
    if (getMixOutHandleArea().expanded(6.0f, 4.0f).contains(p)) {
        dragTarget_ = DragTarget::MixOutHandle;
    } else if (getMixInHandleArea().expanded(6.0f, 4.0f).contains(p)) {
        dragTarget_ = DragTarget::MixInHandle;
    } else if (getBarAArea().contains(p)) {
        plan_.mixOutStart = xToTimeA(p.x, getBarAArea());
        dragTarget_ = DragTarget::MixOutHandle;
        notifyChange();
        repaint();
    } else if (getBarBArea().contains(p)) {
        plan_.mixInStart = xToTimeB(p.x, getBarBArea());
        dragTarget_ = DragTarget::MixInHandle;
        notifyChange();
        repaint();
    } else {
        dragTarget_ = DragTarget::None;
    }
}

void TransitionEditorWidget::mouseDrag(const juce::MouseEvent& e) {
    if (dragTarget_ == DragTarget::None) return;

    if (dragTarget_ == DragTarget::MixOutHandle) {
        const auto bar = getBarAArea();
        double t = xToTimeA(e.position.x, bar);
        t = juce::jlimit(0.0, std::max(0.0, viewSecondsA() - 0.1), t);
        plan_.mixOutStart = t;
    } else if (dragTarget_ == DragTarget::MixInHandle) {
        const auto bar = getBarBArea();
        double t = xToTimeB(e.position.x, bar);
        t = juce::jlimit(0.0, std::max(0.0, viewSecondsB() - 0.1), t);
        plan_.mixInStart = t;
    }

    notifyChange();
    repaint();
}

void TransitionEditorWidget::notifyChange() {
    if (onPlanChanged) onPlanChanged(plan_);
}

} // namespace BeatMate::UI::Widgets
