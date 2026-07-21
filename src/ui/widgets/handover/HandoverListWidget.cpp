#include "HandoverListWidget.h"

#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace BeatMate::UI::Widgets {

HandoverListWidget::Row::Row()
{
    auto smallFont = juce::Font(juce::FontOptions{}.withHeight(11.0f));
    auto editorFont = juce::Font(juce::FontOptions{}.withHeight(13.0f));

    for (auto* l : { &timeLabel, &fromLabel, &toLabel, &notesLabel })
    {
        l->setFont(smallFont);
        l->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(*l);
    }

    for (auto* e : { &timeEditor, &fromEditor, &toEditor, &notesEditor })
    {
        e->setFont(editorFont);
        e->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
        e->setColour(juce::TextEditor::textColourId,       juce::Colours::white);
        e->setColour(juce::TextEditor::outlineColourId,    juce::Colour(0xFF444444));
        e->onTextChange = [this] { if (onFieldChanged) onFieldChanged(); };
        e->onFocusLost  = [this] { if (onFieldChanged) onFieldChanged(); };
        addAndMakeVisible(*e);
    }

    timeEditor.setInputRestrictions(8, "0123456789.");

    removeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF552222));
    removeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    removeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    removeButton.onClick = [this] { if (onRemoveClicked) onRemoveClicked(); };
    addAndMakeVisible(removeButton);
}

void HandoverListWidget::Row::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xFF1E1E1E));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

    g.setColour(juce::Colour(0xFF3A3A3A));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void HandoverListWidget::Row::resized()
{
    auto area = getLocalBounds().reduced(6, 4);

    auto btnArea = area.removeFromRight(28);
    removeButton.setBounds(btnArea.withSizeKeepingCentre(22, 22));
    area.removeFromRight(6);

    const int timeW  = 70;
    const int fromW  = 110;
    const int toW    = 110;
    const int gap    = 6;

    auto placeCol = [&](juce::Label& lbl, juce::TextEditor& ed, int w)
    {
        auto col = area.removeFromLeft(w);
        lbl.setBounds(col.removeFromTop(14));
        ed .setBounds(col.removeFromTop(20));
        area.removeFromLeft(gap);
    };

    placeCol(timeLabel, timeEditor, timeW);
    placeCol(fromLabel, fromEditor, fromW);
    placeCol(toLabel,   toEditor,   toW);

    notesLabel.setBounds(area.removeFromTop(14));
    notesEditor.setBounds(area.removeFromTop(20));
}

HandoverListWidget::HandoverListWidget()
{
    addButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2C5F2D));
    addButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addButton.onClick = [this] { addHandover(); };
    addAndMakeVisible(addButton);
}

void HandoverListWidget::setHandovers(const std::vector<Models::HandoverPoint>& points)
{
    handovers = points;
    rebuildRows();
}

std::vector<Models::HandoverPoint> HandoverListWidget::getHandovers() const
{
    return handovers;
}

void HandoverListWidget::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF161616));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(15.0f).withStyle("Bold")));
    g.drawText("Handover points (multi-DJ)",
               getLocalBounds().removeFromTop(kHeaderHeight).reduced(8, 4),
               juce::Justification::centredLeft);

    if (handovers.empty())
    {
        auto area = getLocalBounds()
                        .withTrimmedTop(kHeaderHeight)
                        .withTrimmedBottom(kFooterHeight);
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Italic")));
        g.drawText("No handover configured. Click \"+ Add handover\" to create one.",
                   area, juce::Justification::centred);
    }
}

void HandoverListWidget::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(kHeaderHeight);

    auto footer = area.removeFromBottom(kFooterHeight).reduced(8, 6);
    addButton.setBounds(footer.removeFromLeft(160));

    area.reduce(6, 2);
    for (auto& row : rows)
    {
        row->setBounds(area.removeFromTop(kRowHeight));
        area.removeFromTop(kRowGap);
    }
}

void HandoverListWidget::rebuildRows()
{
    rows.clear();

    for (size_t i = 0; i < handovers.size(); ++i)
    {
        auto row = std::make_unique<Row>();
        const auto& hp = handovers[i];

        row->timeEditor .setText(juce::String(hp.atMinutes, 2), juce::dontSendNotification);
        row->fromEditor .setText(hp.fromDjName, juce::dontSendNotification);
        row->toEditor   .setText(hp.toDjName,   juce::dontSendNotification);
        row->notesEditor.setText(hp.notes,      juce::dontSendNotification);

        const int index = static_cast<int>(i);
        row->onFieldChanged  = [this, index] { pullFromRow(index); };
        row->onRemoveClicked = [this, index] { removeAt(index); };

        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    resized();
    repaint();
}

void HandoverListWidget::addHandover()
{
    Models::HandoverPoint hp;
    hp.id = makeSimpleId();
    hp.atMinutes = handovers.empty() ? 60.0
                                     : handovers.back().atMinutes + 60.0;
    hp.fromDjName = "";
    hp.toDjName   = "";
    hp.notes      = "";
    hp.colorARGB  = 0xFFFF8844;
    handovers.push_back(std::move(hp));

    rebuildRows();
    notifyChanged();
}

void HandoverListWidget::removeAt(int index)
{
    if (index < 0 || index >= static_cast<int>(handovers.size()))
        return;

    handovers.erase(handovers.begin() + index);
    rebuildRows();
    notifyChanged();
}

void HandoverListWidget::pullFromRow(int index)
{
    if (index < 0 || index >= static_cast<int>(rows.size()))
        return;
    if (index >= static_cast<int>(handovers.size()))
        return;

    auto& r = *rows[index];
    auto& h = handovers[index];

    h.atMinutes  = r.timeEditor.getText().getDoubleValue();
    h.fromDjName = r.fromEditor.getText().toStdString();
    h.toDjName   = r.toEditor  .getText().toStdString();
    h.notes      = r.notesEditor.getText().toStdString();

    notifyChanged();
}

void HandoverListWidget::notifyChanged()
{
    if (onChanged)
        onChanged();
}

std::string HandoverListWidget::makeSimpleId()
{
    static std::mt19937_64 rng{
        static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())
    };

    const uint64_t a = rng();
    const uint64_t b = rng();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << a << std::setw(16) << b;
    return oss.str();
}

} // namespace BeatMate::UI::Widgets
