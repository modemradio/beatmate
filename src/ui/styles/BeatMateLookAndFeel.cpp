#include "BeatMateLookAndFeel.h"
#include "ColorPalette.h"
#include "FontLibrary.h"
#include <cmath>

namespace BeatMate::UI {

BeatMateLookAndFeel::BeatMateLookAndFeel()
{
    refreshColours();
    ThemeEngine::instance().addListener(this);
}

BeatMateLookAndFeel::~BeatMateLookAndFeel()
{
    ThemeEngine::instance().removeListener(this);
}

void BeatMateLookAndFeel::refreshColours()
{
    setColour(juce::ResizableWindow::backgroundColourId, Colors::bg());
    setColour(juce::DocumentWindow::textColourId, Colors::textPrimary());

    setColour(juce::Label::textColourId, Colors::textPrimary());
    setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    setColour(juce::TextEditor::backgroundColourId, Colors::bgSurface());
    setColour(juce::TextEditor::outlineColourId, Colors::border());
    setColour(juce::TextEditor::focusedOutlineColourId, Colors::borderFocus());
    setColour(juce::CaretComponent::caretColourId, Colors::primary());

    setColour(juce::TextButton::buttonColourId, Colors::bgCard());
    setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    setColour(juce::ScrollBar::thumbColourId, Colors::borderLight());
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour(juce::ListBox::backgroundColourId, Colors::bg());
    setColour(juce::ListBox::textColourId, Colors::textPrimary());
    setColour(juce::ListBox::outlineColourId, Colors::border());

    setColour(juce::TableHeaderComponent::backgroundColourId, Colors::bgSurface());
    setColour(juce::TableHeaderComponent::textColourId, Colors::textSecondary());
    setColour(juce::TableHeaderComponent::outlineColourId, Colors::border());

    setColour(juce::ComboBox::backgroundColourId, Colors::bgSurface());
    setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    setColour(juce::ComboBox::outlineColourId, Colors::border());

    setColour(juce::Slider::backgroundColourId, Colors::border());
    setColour(juce::Slider::thumbColourId, Colors::primary());
    setColour(juce::Slider::trackColourId, Colors::primary());
    setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
    setColour(juce::Slider::textBoxBackgroundColourId, Colors::bgSurface());
    setColour(juce::Slider::textBoxOutlineColourId, Colors::border());

    setColour(juce::ProgressBar::backgroundColourId, Colors::bgSurface());
    setColour(juce::ProgressBar::foregroundColourId, Colors::primary());

    setColour(juce::ToggleButton::textColourId, Colors::textPrimary());
    setColour(juce::ToggleButton::tickColourId, Colors::primary());
    setColour(juce::ToggleButton::tickDisabledColourId, Colors::borderLight());

    setColour(juce::PopupMenu::backgroundColourId, Colors::bgSurface());
    setColour(juce::PopupMenu::textColourId, Colors::textPrimary());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colors::primary());
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour(juce::AlertWindow::backgroundColourId, Colors::bgSidebar());
    setColour(juce::AlertWindow::textColourId, Colors::textPrimary());
    setColour(juce::AlertWindow::outlineColourId, Colors::border());

    setColour(juce::TooltipWindow::backgroundColourId, Colors::bgElevated());
    setColour(juce::TooltipWindow::textColourId, Colors::textPrimary());
    setColour(juce::TooltipWindow::outlineColourId, Colors::border());

    setColour(juce::TabbedComponent::backgroundColourId, Colors::bg());
    setColour(juce::TabbedButtonBar::tabOutlineColourId, Colors::border());
    setColour(juce::TabbedButtonBar::tabTextColourId, Colors::textSecondary());
    setColour(juce::TabbedButtonBar::frontTextColourId, Colors::textPrimary());
}

void BeatMateLookAndFeel::themeChanged(ThemeEngine::Theme theme)
{
    juce::ignoreUnused(theme);
    refreshColours();
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
    {
        if (auto* c = desktop.getComponent(i))
        {
            c->sendLookAndFeelChange();
            c->repaint();
        }
    }
}

juce::Typeface::Ptr BeatMateLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    const auto name = font.getTypefaceName();
    if (name == "Segoe UI" || name == "Inter"
        || name == juce::Font::getDefaultSansSerifFontName())
    {
        return Fonts::ui(font.isBold() ? Fonts::Weight::Bold : Fonts::Weight::Regular);
    }
    if (name == "Consolas" || name == "JetBrains Mono"
        || name == juce::Font::getDefaultMonospacedFontName())
    {
        return Fonts::mono(font.isBold() ? Fonts::Weight::Medium : Fonts::Weight::Regular);
    }
    return juce::LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font BeatMateLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    auto size = juce::jmin(13.0f, static_cast<float>(buttonHeight) * 0.6f);
    return Fonts::uiFont(size, Fonts::Weight::Medium);
}

void BeatMateLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto cornerSize = 8.0f;

    if (shouldDrawButtonAsDown)
    {
        auto haloBounds = bounds.expanded(4.0f);
        g.setColour(backgroundColour.withAlpha(0.55f));
        g.fillRoundedRectangle(haloBounds, cornerSize + 4.0f);

        g.setColour(backgroundColour);
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.6f);
    }
    else if (shouldDrawButtonAsHighlighted)
    {
        auto gradient = juce::ColourGradient(
            backgroundColour.brighter(0.15f), bounds.getX(), bounds.getY(),
            backgroundColour.brighter(0.05f), bounds.getX(), bounds.getBottom(),
            false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(Colors::borderLight());
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }
    else
    {
        g.setColour(backgroundColour);
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(Colors::border());
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }
}

void BeatMateLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar,
    int x, int y, int width, int height,
    bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
    bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused(scrollbar);
    // Ascenseurs BLANCS bien visibles (demande utilisateur) : piste sombre
    juce::Colour thumbColour;
    if (isMouseDown)
        thumbColour = juce::Colours::white;
    else if (isMouseOver)
        thumbColour = juce::Colours::white.withAlpha(0.85f);
    else
        thumbColour = juce::Colours::white.withAlpha(0.55f);

    if (isScrollbarVertical)
    {
        const float tw = juce::jmin(10.0f, static_cast<float>(width));
        const float trackX = static_cast<float>(x + width) - tw;
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(trackX, static_cast<float>(y), tw, static_cast<float>(height), tw * 0.5f);
        g.setColour(thumbColour);
        g.fillRoundedRectangle(trackX + 1.0f, static_cast<float>(y + thumbStartPosition),
            juce::jmax(2.0f, tw - 2.0f), static_cast<float>(thumbSize), juce::jmax(1.0f, tw * 0.5f - 1.0f));
    }
    else
    {
        const float th = juce::jmin(10.0f, static_cast<float>(height));
        const float trackY = static_cast<float>(y + height) - th;
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(static_cast<float>(x), trackY, static_cast<float>(width), th, th * 0.5f);
        g.setColour(thumbColour);
        g.fillRoundedRectangle(static_cast<float>(x + thumbStartPosition), trackY + 1.0f,
            static_cast<float>(thumbSize), juce::jmax(2.0f, th - 2.0f), juce::jmax(1.0f, th * 0.5f - 1.0f));
    }
}

void BeatMateLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    auto bounds = header.getLocalBounds();
    g.setColour(Colors::bgSurface());
    g.fillRect(bounds);

    g.setColour(Colors::border());
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));
}

void BeatMateLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos, style, slider);

    auto trackY = static_cast<float>(y + height / 2);
    auto trackLeft = static_cast<float>(x);
    auto trackRight = static_cast<float>(x + width);

    g.setColour(Colors::border());
    g.fillRoundedRectangle(trackLeft, trackY - 1.5f, trackRight - trackLeft, 3.0f, 1.5f);

    auto gradient = juce::ColourGradient(
        Colors::primary(), trackLeft, trackY,
        Colors::secondary(), sliderPos, trackY, false);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(trackLeft, trackY - 1.5f, sliderPos - trackLeft, 3.0f, 1.5f);

    g.setColour(Colors::primary().withAlpha(0.3f));
    g.fillEllipse(sliderPos - 8.0f, trackY - 8.0f, 16.0f, 16.0f);

    g.setColour(Colors::primary());
    g.fillEllipse(sliderPos - 5.0f, trackY - 5.0f, 10.0f, 10.0f);

    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.fillEllipse(sliderPos - 3.0f, trackY - 3.0f, 4.0f, 4.0f);
}

void BeatMateLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
    bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH,
    juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH, box);
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    auto cornerSize = 8.0f;

    g.setColour(Colors::bgSurface());
    g.fillRoundedRectangle(bounds, cornerSize);

    auto borderColour = isButtonDown ? Colors::borderFocus() : Colors::border();
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);

    auto arrowZone = juce::Rectangle<float>(static_cast<float>(width - 24), 0.0f, 20.0f, static_cast<float>(height));
    juce::Path arrow;
    auto arrowCentre = arrowZone.getCentre();
    arrow.addTriangle(arrowCentre.x - 4.0f, arrowCentre.y - 2.0f,
                      arrowCentre.x + 4.0f, arrowCentre.y - 2.0f,
                      arrowCentre.x, arrowCentre.y + 3.0f);
    g.setColour(Colors::textSecondary());
    g.fillPath(arrow);
}

void BeatMateLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar,
    int width, int height, double progress,
    const juce::String& textToShow)
{
    juce::ignoreUnused(bar);
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    auto cornerSize = 4.0f;

    g.setColour(Colors::bgSurface());
    g.fillRoundedRectangle(bounds, cornerSize);

    if (progress >= 0.0 && progress <= 1.0)
    {
        auto fillWidth = static_cast<float>(width) * static_cast<float>(progress);
        auto fillBounds = juce::Rectangle<float>(0, 0, fillWidth, static_cast<float>(height));

        auto gradient = juce::ColourGradient(
            Colors::primary(), 0, 0,
            Colors::secondary(), static_cast<float>(width), 0, false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(fillBounds, cornerSize);
    }

    if (textToShow.isNotEmpty())
    {
        g.setColour(Colors::textPrimary());
        g.setFont(Fonts::uiFont(11.0f));
        g.drawText(textToShow, bounds.toNearestInt(), juce::Justification::centred);
    }
}

void BeatMateLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
    bool isMouseOver, bool isMouseDown)
{
    auto area = button.getActiveArea();
    auto isSelected = button.isFrontTab();

    if (isSelected)
    {
        juce::ColourGradient selGrad(Colors::primary().withAlpha(0.08f),
            static_cast<float>(area.getX()), static_cast<float>(area.getY()),
            juce::Colours::transparentBlack,
            static_cast<float>(area.getX()), static_cast<float>(area.getBottom()), false);
        g.setGradientFill(selGrad);
        g.fillRect(area);
        g.setColour(Colors::primary().withAlpha(0.3f));
        g.fillRect(area.getX(), area.getBottom() - 4, area.getWidth(), 4);
        g.setColour(Colors::primary());
        g.fillRect(area.getX(), area.getBottom() - 2, area.getWidth(), 2);
        g.setColour(Colors::textPrimary());
    }
    else if (isMouseOver || isMouseDown)
    {
        g.setColour(Colors::bgElevated().withAlpha(0.5f));
        g.fillRect(area);
        g.setColour(Colors::textSecondary().brighter(0.3f));
    }
    else
    {
        g.setColour(Colors::textSecondary());
    }

    g.setFont(Fonts::uiFont(13.0f, isSelected ? Fonts::Weight::SemiBold : Fonts::Weight::Regular));
    g.drawText(button.getButtonText(), area, juce::Justification::centred);
}

void BeatMateLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(Colors::bgSurface().withAlpha(0.97f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(Colors::borderLight().withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(1.5f), 7.0f, 0.5f);
}

void BeatMateLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
    bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
    bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
    const juce::Drawable* icon, const juce::Colour* textColour)
{
    juce::ignoreUnused(icon, textColour, hasSubMenu);

    if (isSeparator)
    {
        auto sepArea = area.reduced(8, 0);
        g.setColour(Colors::border());
        g.fillRect(sepArea.getX(), area.getCentreY(), sepArea.getWidth(), 1);
        return;
    }

    if (isHighlighted && isActive)
    {
        juce::ColourGradient hlGrad(Colors::primary().withAlpha(0.2f),
            static_cast<float>(area.getX()), static_cast<float>(area.getY()),
            Colors::primary().withAlpha(0.05f),
            static_cast<float>(area.getRight()), static_cast<float>(area.getY()), false);
        g.setGradientFill(hlGrad);
        g.fillRoundedRectangle(area.toFloat().reduced(4.0f, 1.0f), 6.0f);
    }

    auto textArea = area.reduced(12, 0);
    g.setFont(Fonts::uiFont(13.0f));

    if (isTicked)
    {
        g.setColour(Colors::primary());
        auto tickArea = juce::Rectangle<int>(area.getX() + 6, area.getCentreY() - 6, 12, 12);
        juce::Path tick;
        tick.startNewSubPath(static_cast<float>(tickArea.getX()), static_cast<float>(tickArea.getCentreY()));
        tick.lineTo(static_cast<float>(tickArea.getCentreX()), static_cast<float>(tickArea.getBottom()));
        tick.lineTo(static_cast<float>(tickArea.getRight()), static_cast<float>(tickArea.getY()));
        g.strokePath(tick, juce::PathStrokeType(2.0f));
    }

    g.setColour(isActive ? Colors::textPrimary() : Colors::textMuted());
    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(Colors::textMuted());
        g.setFont(Fonts::monoFont(11.0f));
        g.drawFittedText(shortcutKeyText, textArea, juce::Justification::centredRight, 1);
    }
}

void BeatMateLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsDown);
    auto bounds = button.getLocalBounds();
    auto tickSize = 18.0f;
    auto tickBounds = juce::Rectangle<float>(4.0f, (bounds.getHeight() - tickSize) * 0.5f, tickSize, tickSize);

    if (button.getToggleState())
    {
        g.setColour(Colors::primary().withAlpha(0.2f));
        g.fillRoundedRectangle(tickBounds.expanded(2.0f), 6.0f);
        juce::ColourGradient fillGrad(Colors::primary(), tickBounds.getX(), tickBounds.getY(),
            Colors::secondary(), tickBounds.getRight(), tickBounds.getBottom(), false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(tickBounds, 5.0f);
        g.setColour(juce::Colours::white);
        juce::Path tick;
        tick.startNewSubPath(tickBounds.getX() + 4.5f, tickBounds.getCentreY());
        tick.lineTo(tickBounds.getCentreX() - 0.5f, tickBounds.getBottom() - 5.0f);
        tick.lineTo(tickBounds.getRight() - 4.0f, tickBounds.getY() + 5.0f);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour(Colors::border());
        g.fillRoundedRectangle(tickBounds, 5.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? Colors::borderLight() : Colors::border());
        g.drawRoundedRectangle(tickBounds, 5.0f, 1.5f);
    }

    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(Fonts::uiFont(13.0f));
    auto textBounds = bounds.toFloat().withTrimmedLeft(tickSize + 10.0f);
    g.drawFittedText(button.getButtonText(), textBounds.toNearestInt(),
        juce::Justification::centredLeft, 2);
}

void BeatMateLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    juce::ignoreUnused(slider);
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centreX = bounds.getCentreX();
    auto centreY = bounds.getCentreY();
    auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Path trackArc;
    trackArc.addCentredArc(centreX, centreY, radius - 2.0f, radius - 2.0f,
        0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(Colors::border());
    g.strokePath(trackArc, juce::PathStrokeType(3.0f));

    juce::Path valueArc;
    valueArc.addCentredArc(centreX, centreY, radius - 2.0f, radius - 2.0f,
        0.0f, rotaryStartAngle, angle, true);
    juce::ColourGradient arcGrad(Colors::primary(), centreX - radius, centreY,
        Colors::secondary(), centreX + radius, centreY, false);
    g.setGradientFill(arcGrad);
    g.strokePath(valueArc, juce::PathStrokeType(3.0f));

    auto knobRadius = radius * 0.65f;
    g.setColour(Colors::bgCard());
    g.fillEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
    g.setColour(Colors::borderLight());
    g.drawEllipse(centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

    juce::Path pointer;
    auto pointerLength = knobRadius * 0.8f;
    auto pointerThickness = 2.5f;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.6f, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(Colors::primary());
    g.fillPath(pointer);

    auto tipX = centreX + std::sin(angle) * (knobRadius * 0.65f);
    auto tipY = centreY - std::cos(angle) * (knobRadius * 0.65f);
    g.setColour(Colors::primary().withAlpha(0.4f));
    g.fillEllipse(tipX - 4.0f, tipY - 4.0f, 8.0f, 8.0f);
}

void BeatMateLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(Colors::bgElevated().withAlpha(0.97f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(Colors::textPrimary());
    g.setFont(Fonts::uiFont(12.0f));
    g.drawFittedText(text, bounds.reduced(6.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 3);
}

void BeatMateLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
    const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds().toFloat();

    juce::ColourGradient bgGrad(Colors::bgSurface(), 0.0f, 0.0f,
        Colors::bg(), 0.0f, bounds.getHeight(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, 12.0f);

    g.setColour(Colors::primary().withAlpha(0.04f));
    g.fillEllipse(-30.0f, -30.0f, 150.0f, 150.0f);

    g.setColour(Colors::border());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

    g.setColour(Colors::borderLight().withAlpha(0.6f));
    g.drawRoundedRectangle(bounds.reduced(1.5f), 11.0f, 0.5f);

    auto titleBounds = textArea.toFloat();
    g.setColour(Colors::primary().withAlpha(0.5f));
    g.fillRect(titleBounds.getX(), titleBounds.getY() - 4.0f, 3.0f, 18.0f);

    textLayout.draw(g, textArea.toFloat());
}

void BeatMateLookAndFeel::drawGroupComponentOutline(juce::Graphics& g, int width, int height,
    const juce::String& text, const juce::Justification& position,
    juce::GroupComponent& group)
{
    juce::ignoreUnused(position);
    auto bounds = juce::Rectangle<float>(0, 12, static_cast<float>(width), static_cast<float>(height) - 12);

    g.setColour(juce::Colour(0x0AFFFFFF));
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(Colors::border());
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    if (text.isNotEmpty())
    {
        g.setFont(Fonts::uiFont(12.0f, Fonts::Weight::SemiBold));
        auto textW = g.getCurrentFont().getStringWidthFloat(text) + 12.0f;
        auto textBounds = juce::Rectangle<float>(12.0f, 4.0f, textW, 16.0f);

        g.setColour(Colors::bg());
        g.fillRoundedRectangle(textBounds, 4.0f);

        g.setColour(group.findColour(juce::GroupComponent::textColourId, true)
            .isTransparent() ? Colors::textSecondary() : group.findColour(juce::GroupComponent::textColourId));
        g.drawText(text, textBounds.toNearestInt(), juce::Justification::centred);
    }
}

void BeatMateLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g,
    const juce::Path& path, juce::Image& cachedImage)
{
    juce::ignoreUnused(box, cachedImage);
    g.setColour(juce::Colour(0x66000000));
    g.fillPath(path, juce::AffineTransform::translation(0.0f, 2.0f));
    g.setColour(Colors::bgElevated().withAlpha(0.98f));
    g.fillPath(path);
    g.setColour(Colors::border());
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

int BeatMateLookAndFeel::getDefaultScrollbarWidth() { return 8; }
int BeatMateLookAndFeel::getMinimumScrollbarThumbSize(juce::ScrollBar&) { return 30; }

}
