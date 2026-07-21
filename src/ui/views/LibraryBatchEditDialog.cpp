#include "LibraryBatchEditDialog.h"

#include "../styles/ColorPalette.h"
#include "../dialogs/ColorPickerDialog.h"

namespace BeatMate::UI {

LibraryBatchEditDialog::LibraryBatchEditDialog()
{
    headerLabel = std::make_unique<juce::Label>("h", "EDITION EN LOT");
    headerLabel->setFont(juce::Font(16.0f, juce::Font::bold));
    headerLabel->setColour(juce::Label::textColourId, Colors::primary());
    addAndMakeVisible(headerLabel.get());

    auto makeCb = [this](const juce::String& text) {
        auto cb = std::make_unique<juce::ToggleButton>(text);
        cb->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
        addAndMakeVisible(cb.get());
        return cb;
    };

    auto makeEd = [this]() {
        auto e = std::make_unique<juce::TextEditor>();
        e->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
        e->setColour(juce::TextEditor::textColourId,       Colors::textPrimary());
        addAndMakeVisible(e.get());
        return e;
    };

    enableTitle   = makeCb("Titre");       titleEdit   = makeEd();
    enableArtist  = makeCb("Artiste");     artistEdit  = makeEd();
    enableAlbum   = makeCb("Album");       albumEdit   = makeEd();
    enableGenre   = makeCb("Genre");       genreEdit   = makeEd();
    enableBPM     = makeCb("BPM");         bpmEdit     = makeEd();
    enableKey     = makeCb("Cle");         keyEdit     = makeEd();
    enableComment = makeCb("Commentaire"); commentEdit = makeEd();
    enableLabel   = makeCb("Label");       labelEdit   = makeEd();

    enableEnergy = makeCb("Energie");
    energySlider = std::make_unique<juce::Slider>(
        juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    energySlider->setRange(1, 10, 1);
    energySlider->setColour(juce::Slider::trackColourId, Colors::energyBadge());
    addAndMakeVisible(energySlider.get());

    enableRating = makeCb("Note");
    ratingSlider = std::make_unique<juce::Slider>(
        juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    ratingSlider->setRange(0, 5, 1);
    ratingSlider->setColour(juce::Slider::trackColourId, Colors::starFilled());
    addAndMakeVisible(ratingSlider.get());

    enableColor = makeCb("Couleur");
    colorBtn = std::make_unique<juce::TextButton>("Choisir...");
    colorBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    colorBtn->onClick = [this]() {
        ColorPickerDialog::showDialog(this,
            selectedColor.isTransparent() ? juce::Colours::white : selectedColor,
            [this](juce::Colour c) {
                selectedColor = c;
                if (colorBtn) {
                    colorBtn->setColour(juce::TextButton::buttonColourId, c);
                    colorBtn->setButtonText(c.toDisplayString(false));
                }
                if (enableColor) enableColor->setToggleState(true, juce::dontSendNotification);
            });
    };
    addAndMakeVisible(colorBtn.get());

    applyBtn = std::make_unique<juce::TextButton>("Appliquer");
    applyBtn->setColour(juce::TextButton::buttonColourId,     Colors::primary());
    applyBtn->setColour(juce::TextButton::textColourOffId,    Colors::textPrimary());
    applyBtn->onClick = [this]() { if (onApply) onApply(); };
    addAndMakeVisible(applyBtn.get());

    cancelBtn = std::make_unique<juce::TextButton>("Annuler");
    cancelBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    cancelBtn->onClick = [this]() { if (onCancel) onCancel(); };
    addAndMakeVisible(cancelBtn.get());

    closeBtn = std::make_unique<juce::TextButton>("X");
    closeBtn->setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFFB91C1C));
    closeBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    closeBtn->onClick = [this]() { if (onCancel) onCancel(); };
    addAndMakeVisible(closeBtn.get());
}

void LibraryBatchEditDialog::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(Colors::bgCard().withAlpha(0.97f));
    g.fillRoundedRectangle(b, 12.0f);
    g.setColour(Colors::primary().withAlpha(0.5f));
    g.drawRoundedRectangle(b, 12.0f, 1.5f);
}

void LibraryBatchEditDialog::resized()
{
    auto full = getLocalBounds();
    if (closeBtn) closeBtn->setBounds(full.getRight() - 40, 4, 32, 26);
    auto area = full.reduced(16);
    headerLabel->setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    auto row = [&](juce::ToggleButton* cb, juce::Component* editor) {
        auto r = area.removeFromTop(26);
        cb->setBounds(r.removeFromLeft(100));
        editor->setBounds(r);
        area.removeFromTop(4);
    };

    row(enableTitle.get(),   titleEdit.get());
    row(enableArtist.get(),  artistEdit.get());
    row(enableAlbum.get(),   albumEdit.get());
    row(enableGenre.get(),   genreEdit.get());
    row(enableBPM.get(),     bpmEdit.get());
    row(enableKey.get(),     keyEdit.get());
    row(enableEnergy.get(),  energySlider.get());
    row(enableRating.get(),  ratingSlider.get());
    row(enableColor.get(),   colorBtn.get());
    row(enableLabel.get(),   labelEdit.get());
    row(enableComment.get(), commentEdit.get());

    area.removeFromTop(12);
    auto btnRow = area.removeFromTop(32);
    applyBtn->setBounds(btnRow.removeFromLeft(btnRow.getWidth() / 2 - 4));
    btnRow.removeFromLeft(8);
    cancelBtn->setBounds(btnRow);
}

LibraryBatchEditDialog::EditResult LibraryBatchEditDialog::getResult() const
{
    EditResult r;
    if (enableTitle  ->getToggleState()) { r.hasTitle  = true; r.title  = titleEdit  ->getText(); }
    if (enableArtist ->getToggleState()) { r.hasArtist = true; r.artist = artistEdit ->getText(); }
    if (enableAlbum  ->getToggleState()) { r.hasAlbum  = true; r.album  = albumEdit  ->getText(); }
    if (enableGenre  ->getToggleState()) { r.hasGenre  = true; r.genre  = genreEdit  ->getText(); }
    if (enableBPM    ->getToggleState()) { r.hasBPM    = true; r.bpm    = bpmEdit    ->getText().getDoubleValue(); }
    if (enableKey    ->getToggleState()) { r.hasKey    = true; r.key    = keyEdit    ->getText(); }
    if (enableEnergy ->getToggleState()) { r.hasEnergy = true; r.energy = (float) energySlider->getValue(); }
    if (enableRating ->getToggleState()) { r.hasRating = true; r.rating = (int)   ratingSlider->getValue(); }
    if (enableColor  ->getToggleState()) {
        r.hasColor = true;
        r.color = selectedColor.isTransparent()
            ? juce::String("")
            : juce::String("#") + selectedColor.toDisplayString(false);
    }
    if (enableLabel  ->getToggleState()) { r.hasLabel   = true; r.label   = labelEdit  ->getText(); }
    if (enableComment->getToggleState()) { r.hasComment = true; r.comment = commentEdit->getText(); }
    return r;
}

} // namespace BeatMate::UI
