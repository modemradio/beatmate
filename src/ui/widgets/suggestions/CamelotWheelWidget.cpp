#include "CamelotWheelWidget.h"

#include "../../styles/ColorPalette.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace BeatMate::UI::Widgets
{
    namespace
    {
        constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

        inline int densityIndex(int number1to12, bool isMajor)
        {
            return (number1to12 - 1) * 2 + (isMajor ? 1 : 0);
        }

        inline void wedgeAngles(int number1to12, bool isMajor,
                                float& startRad, float& endRad)
        {
            juce::ignoreUnused(isMajor);
            const float slotCenterDeg = ((number1to12 % 12) * 30.0f) - 90.0f;
            startRad = juce::degreesToRadians(slotCenterDeg - 15.0f);
            endRad   = juce::degreesToRadians(slotCenterDeg + 15.0f);
        }

        inline juce::Path buildWedge(juce::Point<float> c,
                                     float rInner, float rOuter,
                                     float a0, float a1)
        {
            juce::Path p;
            const int segs = juce::jmax(6, (int) std::ceil((a1 - a0) / 0.05f));
            for (int i = 0; i <= segs; ++i)
            {
                const float t = (float) i / (float) segs;
                const float a = a0 + (a1 - a0) * t;
                const juce::Point<float> pt{ c.x + std::cos(a) * rOuter,
                                             c.y + std::sin(a) * rOuter };
                if (i == 0) p.startNewSubPath(pt);
                else        p.lineTo(pt);
            }
            for (int i = segs; i >= 0; --i)
            {
                const float t = (float) i / (float) segs;
                const float a = a0 + (a1 - a0) * t;
                p.lineTo(c.x + std::cos(a) * rInner,
                         c.y + std::sin(a) * rInner);
            }
            p.closeSubPath();
            return p;
        }
    } // namespace

    CamelotWheelWidget::CamelotWheelWidget()
    {
        setOpaque(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        density_.fill(0);
    }

    void CamelotWheelWidget::setCurrentKey(const std::string& camelot)
    {
        int n = 0; bool maj = false;
        if (parseCamelot(camelot, n, maj))
        {
            currentNumber_  = n;
            currentIsMajor_ = maj;
        }
        else
        {
            currentNumber_ = 0;
        }
        repaint();
    }

    void CamelotWheelWidget::setKeyDensity(const std::map<std::string, int>& densityByKey)
    {
        density_.fill(0);
        maxDensity_ = 0;
        for (const auto& kv : densityByKey)
        {
            int n = 0; bool maj = false;
            if (!parseCamelot(kv.first, n, maj)) continue;
            const int idx = densityIndex(n, maj);
            density_[(size_t) idx] = juce::jmax(0, kv.second);
            maxDensity_ = juce::jmax(maxDensity_, density_[(size_t) idx]);
        }
        repaint();
    }

    void CamelotWheelWidget::paint(juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced(6.0f);
        const juce::Point<float> center{ bounds.getCentreX(), bounds.getCentreY() };
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

        const float rHole  = radius * 0.34f;
        const float rMid   = radius * 0.64f;
        const float rOuter = radius * 0.98f;

        const float scale        = juce::jlimit(0.7f, 1.3f, radius / 100.0f);
        const float labelHeight  = juce::jlimit(9.0f, 13.0f, 10.5f * scale);
        const float centerHeight = juce::jlimit(18.0f, 40.0f, 30.0f * scale);
        const float gap          = juce::degreesToRadians(1.6f);

        const bool hasSel = currentNumber_ > 0;

        for (int num = 1; num <= 12; ++num)
        {
            for (int side = 0; side < 2; ++side)
            {
                const bool isMajor = (side == 1);
                float a0, a1;
                wedgeAngles(num, isMajor, a0, a1);
                a0 += gap;
                a1 -= gap;

                const float rLo = isMajor ? rMid + 1.0f : rHole;
                const float rHi = isMajor ? rOuter      : rMid - 1.0f;

                auto path = buildWedge(center, rLo, rHi, a0, a1);

                const auto rel = relationTo(num, isMajor);
                const bool compatible = (rel == Relation::Current
                                         || rel == Relation::Adjacent
                                         || rel == Relation::Relative
                                         || rel == Relation::EnergyBoost);
                const juce::Colour keyCol = Colors::camelot(num, !isMajor);

                juce::Colour fill;
                if (!hasSel)
                    fill = keyCol.withMultipliedSaturation(0.85f).withAlpha(0.60f);
                else if (rel == Relation::Current)
                    fill = keyCol.withAlpha(0.96f);
                else if (compatible)
                    fill = keyCol.withAlpha(0.74f);
                else
                    fill = keyCol.withMultipliedSaturation(0.25f)
                                 .withMultipliedBrightness(0.55f)
                                 .withAlpha(0.30f);

                if (hovered_.valid && hovered_.number == num
                    && hovered_.isMajor == isMajor)
                    fill = fill.brighter(0.16f);

                g.setColour(fill);
                g.fillPath(path);

                g.setColour(Colors::bgSurface().withAlpha(0.85f));
                g.strokePath(path, juce::PathStrokeType(1.4f));

                if (rel == Relation::Current)
                {
                    g.setColour(Colors::textPrimary());
                    g.strokePath(path, juce::PathStrokeType(2.2f));
                }

                const float aMid = 0.5f * (a0 + a1);
                const float rLbl = 0.5f * (rLo + rHi);
                const juce::Point<float> lp{ center.x + std::cos(aMid) * rLbl,
                                             center.y + std::sin(aMid) * rLbl };

                juce::Colour txtCol;
                if (!hasSel)
                    txtCol = Colors::textPrimary().withAlpha(0.90f);
                else if (rel == Relation::Current)
                    txtCol = Colors::bg();
                else if (compatible)
                    txtCol = Colors::textPrimary();
                else
                    txtCol = Colors::textPrimary().withAlpha(0.32f);

                g.setColour(txtCol);
                g.setFont(Fonts::uiFont(labelHeight,
                    rel == Relation::Current ? Fonts::Weight::Bold
                                             : Fonts::Weight::Medium));
                const juce::String code(codeOf(num, isMajor));
                const int tw = (int) std::ceil(labelHeight * 2.6f);
                const int th = (int) std::ceil(labelHeight * 1.5f);
                g.drawText(code,
                           juce::Rectangle<int>((int) lp.x - tw / 2,
                                                (int) lp.y - th / 2, tw, th),
                           juce::Justification::centred, false);
            }
        }

        {
            const float discR = rHole - 3.0f;
            auto disc = juce::Rectangle<float>(discR * 2.0f, discR * 2.0f)
                            .withCentre(center);
            g.setColour(Colors::bgSurface());
            g.fillEllipse(disc);
            g.setColour(Colors::borderLight().withAlpha(0.55f));
            g.drawEllipse(disc, 1.0f);

            juce::String bigText;
            juce::String subText;
            if (hovered_.valid)
            {
                bigText = juce::String(codeOf(hovered_.number, hovered_.isMajor));
                subText = hovered_.isMajor ? "Majeur" : "Mineur";
            }
            else if (hasSel)
            {
                bigText = juce::String(codeOf(currentNumber_, currentIsMajor_));
                subText = "En cours";
            }

            if (bigText.isNotEmpty())
            {
                g.setColour(Colors::textPrimary());
                g.setFont(Fonts::uiFont(centerHeight, Fonts::Weight::Bold));
                g.drawText(bigText, disc, juce::Justification::centred, false);

                g.setColour(Colors::textSecondary());
                g.setFont(Fonts::uiFont(juce::jmax(9.0f, centerHeight * 0.30f),
                                        Fonts::Weight::Medium));
                auto subBox = disc.translated(0.0f, centerHeight * 0.52f);
                g.drawText(subText, subBox, juce::Justification::centredTop, false);
            }
        }
    }

    void CamelotWheelWidget::mouseDown(const juce::MouseEvent& e)
    {
        const auto hit = hitTest(e.position);
        if (hit.valid && onKeyClicked)
            onKeyClicked(codeOf(hit.number, hit.isMajor));
    }

    void CamelotWheelWidget::mouseMove(const juce::MouseEvent& e)
    {
        const auto hit = hitTest(e.position);
        const bool changed = (hit.valid != hovered_.valid)
                          || (hit.valid && (hit.number != hovered_.number
                                            || hit.isMajor != hovered_.isMajor));
        hovered_ = hit;
        if (changed)
        {
            if (onKeyHovered)
                onKeyHovered(hit.valid ? codeOf(hit.number, hit.isMajor) : std::string{});
            repaint();
        }
    }

    void CamelotWheelWidget::mouseExit(const juce::MouseEvent&)
    {
        if (hovered_.valid)
        {
            hovered_ = {};
            if (onKeyHovered) onKeyHovered(std::string{});
            repaint();
        }
    }

    CamelotWheelWidget::Hit
    CamelotWheelWidget::hitTest(juce::Point<float> p) const
    {
        Hit h;
        const auto bounds = getLocalBounds().toFloat().reduced(6.0f);
        const juce::Point<float> c{ bounds.getCentreX(), bounds.getCentreY() };
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

        const float rInnerCenter = radius * 0.34f;
        const float rMid         = radius * 0.64f;
        const float rOuter       = radius * 0.98f;

        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        const float r  = std::sqrt(dx * dx + dy * dy);
        if (r < rInnerCenter || r > rOuter) return h;

        const bool isMajor = (r >= rMid);

        // angle in our math space: 0 = +x, CCW positive
        float ang = std::atan2(dy, dx);

        // Convert to "degrees from -90 (top)" for slot math
        float degFromTop = juce::radiansToDegrees(ang) + 90.0f;
        degFromTop = std::fmod(degFromTop + 360.0f, 360.0f);

        // Each number's slot is 30deg wide, but our B-half is -7.5..+7.5
        for (int n = 1; n <= 12; ++n)
        {
            float a0, a1;
            wedgeAngles(n, isMajor, a0, a1);
            float aN = ang;
            while (aN < a0)         aN += kTwoPi;
            while (aN >= a0 + kTwoPi) aN -= kTwoPi;
            if (aN >= a0 && aN <= a1)
            {
                h.number  = n;
                h.isMajor = isMajor;
                h.valid   = true;
                return h;
            }
        }
        return h;
    }

    std::string CamelotWheelWidget::codeOf(int number, bool isMajor)
    {
        std::string s = std::to_string(number);
        s += (isMajor ? 'B' : 'A');
        return s;
    }

    bool CamelotWheelWidget::parseCamelot(const std::string& s,
                                          int& number, bool& isMajor)
    {
        if (s.size() < 2 || s.size() > 3) return false;
        const char last = (char) std::toupper((unsigned char) s.back());
        if (last != 'A' && last != 'B') return false;
        try
        {
            int n = std::stoi(s.substr(0, s.size() - 1));
            if (n < 1 || n > 12) return false;
            number  = n;
            isMajor = (last == 'B');
            return true;
        }
        catch (...) { return false; }
    }

    CamelotWheelWidget::Relation
    CamelotWheelWidget::relationTo(int number, bool isMajor) const
    {
        if (currentNumber_ <= 0) return Relation::None;
        if (number == currentNumber_ && isMajor == currentIsMajor_)
            return Relation::Current;

        // same letter, adjacent number (+/- 1 mod 12) = harmonic neighbour
        auto wrap = [] (int n) { return ((n - 1 + 12) % 12) + 1; };
        if (isMajor == currentIsMajor_)
        {
            if (number == wrap(currentNumber_ + 1)
                || number == wrap(currentNumber_ - 1))
                return Relation::Adjacent;
            if (number == wrap(currentNumber_ + 3))
                return Relation::EnergyBoost;
            if (number == wrap(currentNumber_ + 6)
                || number == wrap(currentNumber_ - 6))
                return Relation::Tritone;
        }
        else
        {
            // relative minor/major: same number, different letter
            if (number == currentNumber_)
                return Relation::Relative;
        }

        return Relation::None;
    }

} // namespace BeatMate::UI::Widgets
