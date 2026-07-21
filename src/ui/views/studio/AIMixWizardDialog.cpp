#include "AIMixWizardDialog.h"

#include <algorithm>
#include <cmath>

namespace BeatMate::UI::Studio {

namespace {
    constexpr int kCardWidth  = 200;
    constexpr int kCardHeight = 140;
    constexpr int kCardGap    = 16;

    juce::Colour cardAccent(BeatMate::Services::AI::MashupType t)
    {
        using T = BeatMate::Services::AI::MashupType;
        switch (t) {
            case T::Mashup:  return juce::Colour(0xFF3B82F6);
            case T::Megamix: return juce::Colour(0xFF8B5CF6);
            case T::Medley:  return juce::Colour(0xFF06B6D4);
            case T::Remix:   return juce::Colour(0xFFEC4899);
        }
        return juce::Colour(0xFF3B82F6);
    }
}

class AIMixWizardDialog::TypeCard : public juce::Component {
public:
    using MashupType = BeatMate::Services::AI::MashupType;

    TypeCard(MashupType type, juce::String title, juce::String subtitle)
        : m_type(type), m_title(std::move(title)), m_subtitle(std::move(subtitle)) {}

    void setSelected(bool s) { m_selected = s; repaint(); }
    bool isSelected() const  { return m_selected; }
    MashupType getType() const { return m_type; }

    std::function<void(MashupType)> onClicked;

    void mouseEnter(const juce::MouseEvent&) override { m_hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { m_hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClicked) onClicked(m_type);
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        const float radius = 12.0f;

        auto accent = cardAccent(m_type);

        g.setColour(juce::Colour(0xFF1E1E34));
        g.fillRoundedRectangle(r, radius);

        if (m_hover && !m_selected) {
            g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.05f));
            g.fillRoundedRectangle(r, radius);
        }

        if (m_selected) {
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(r, radius);
        }

        g.setColour(m_selected ? accent : juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.08f));
        g.drawRoundedRectangle(r, radius, m_selected ? 2.0f : 1.0f);

        auto iconArea = juce::Rectangle<float>(r.getX() + 16.0f, r.getY() + 16.0f, 56.0f, 56.0f);
        drawIcon(g, iconArea, accent);

        auto textArea = r.reduced(16.0f);
        textArea.removeFromTop(72.0f);

        g.setFont(juce::Font("Segoe UI", 16.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFF1F5F9));
        g.drawText(m_title,
                   textArea.removeFromTop(22.0f),
                   juce::Justification::centredLeft, false);

        g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
        g.setColour(juce::Colour(0xFF94A3B8));
        g.drawFittedText(m_subtitle,
                         textArea.toNearestInt(),
                         juce::Justification::topLeft, 2);
    }

private:
    void drawIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent)
    {
        using T = BeatMate::Services::AI::MashupType;
        g.setColour(accent);

        switch (m_type) {
            case T::Mashup: {
                const float r = area.getHeight() * 0.42f;
                auto cx = area.getCentreX();
                auto cy = area.getCentreY();
                g.fillEllipse(cx - r * 1.2f, cy - r, r * 2.0f, r * 2.0f);
                g.setColour(accent.withAlpha(0.55f));
                g.fillEllipse(cx - r * 0.2f, cy - r, r * 2.0f, r * 2.0f);
                break;
            }
            case T::Megamix: {
                const float bh = 6.0f;
                const float gap = 4.0f;
                float y = area.getY() + 8.0f;
                for (int i = 0; i < 5; ++i) {
                    float w = area.getWidth() * (0.4f + (float) i * 0.12f);
                    g.setColour(accent.withAlpha(0.5f + (float) i * 0.1f));
                    g.fillRoundedRectangle(area.getX(), y, w, bh, bh * 0.5f);
                    y += bh + gap;
                }
                break;
            }
            case T::Medley: {
                juce::Path tri;
                tri.addTriangle(area.getX(), area.getBottom(),
                                area.getCentreX(), area.getY() + 6.0f,
                                area.getRight(), area.getBottom());
                g.fillPath(tri);
                g.setColour(accent.withAlpha(0.55f));
                g.fillRoundedRectangle(area.getX() + 6.0f,
                                       area.getBottom() - 8.0f,
                                       area.getWidth() - 12.0f, 6.0f, 3.0f);
                break;
            }
            case T::Remix: {
                const float ringT = 5.0f;
                g.drawEllipse(area.reduced(4.0f), ringT);
                auto inner = area.reduced(area.getWidth() * 0.30f);
                g.setColour(accent.withAlpha(0.6f));
                g.fillRect(inner);
                break;
            }
        }
    }

    MashupType m_type;
    juce::String m_title;
    juce::String m_subtitle;
    bool m_selected { false };
    bool m_hover { false };
};

class AIMixWizardDialog::TrackListModel : public juce::ListBoxModel {
public:
    TrackListModel(std::function<int()> count,
                   std::function<juce::String(int)> textFor)
        : m_count(std::move(count)), m_textFor(std::move(textFor)) {}

    int getNumRows() override { return m_count ? m_count() : 0; }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected) {
            g.fillAll(juce::Colour(0xFF3B82F6).withAlpha(0.25f));
        } else if ((row % 2) == 0) {
            g.fillAll(juce::Colour(0xFF1A1A2E));
        } else {
            g.fillAll(juce::Colour(0xFF1E1E34));
        }
        g.setColour(juce::Colour(0xFFF1F5F9));
        g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
        if (m_textFor) {
            g.drawText(m_textFor(row),
                       6, 0, w - 12, h,
                       juce::Justification::centredLeft, true);
        }
    }

private:
    std::function<int()> m_count;
    std::function<juce::String(int)> m_textFor;
};

AIMixWizardDialog::AIMixWizardDialog()
{
    using namespace BeatMate::Services::AI;

    setSize(700, 500);

    m_modeToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xFFF1F5F9));
    m_modeToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFF3B82F6));
    m_modeToggle.setColour(juce::ToggleButton::tickDisabledColourId,
                           juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.25f));
    m_modeToggle.onClick = [this] {
        switchMode(m_modeToggle.getToggleState() ? SkillMode::Expert : SkillMode::Novice);
    };
    addAndMakeVisible(m_modeToggle);

    m_generateBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3B82F6));
    m_generateBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF60A5FA));
    m_generateBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFF1F5F9));
    m_generateBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFF1F5F9));
    m_generateBtn.onClick = [this] { onGenerateClicked(); };
    addAndMakeVisible(m_generateBtn);

    m_cancelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2E2E44));
    m_cancelBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFF1F5F9));
    m_cancelBtn.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(m_cancelBtn);

    m_statusLabel.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF94A3B8));
    m_statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_statusLabel);

    buildNoviceUI();
    buildExpertUI();

    m_orchestrator = std::make_unique<AIMashupOrchestrator>();

    switchMode(SkillMode::Novice);
    updateGenerateEnabled();
}

AIMixWizardDialog::~AIMixWizardDialog()
{
    if (m_orchestrator) m_orchestrator->cancel();
}

void AIMixWizardDialog::setLibrary(std::vector<Models::Track> library)
{
    m_library = std::move(library);
    if (m_libraryList) m_libraryList->updateContent();
}

void AIMixWizardDialog::buildNoviceUI()
{
    using T = BeatMate::Services::AI::MashupType;

    m_noviceContent = std::make_unique<juce::Component>();
    addChildComponent(*m_noviceContent);

    struct CardSpec { T type; const char* title; const char* subtitle; };
    const CardSpec specs[] = {
        { T::Mashup,  "Mashup",  "Combine 2-3 tracks" },
        { T::Megamix, "Megamix", "10-30 tracks enchainees" },
        { T::Medley,  "Medley",  "5-8 tracks longues sections" },
        { T::Remix,   "Remix",   "1 track stems reorganisees" },
    };

    for (const auto& s : specs) {
        auto card = std::make_unique<TypeCard>(s.type, s.title, s.subtitle);
        card->onClicked = [this](T t) {
            m_selectedType = t;
            m_typeChosen = true;
            for (auto& c : m_noviceCards)
                c->setSelected(c->getType() == t);
            updateGenerateEnabled();
        };
        m_noviceContent->addAndMakeVisible(*card);
        m_noviceCards.push_back(std::move(card));
    }
}

void AIMixWizardDialog::buildExpertUI()
{
    using namespace BeatMate::Services::AI;

    m_expertContent = std::make_unique<juce::Component>();
    addChildComponent(*m_expertContent);

    m_tabs.setOutline(0);
    m_tabs.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xFF141420));
    m_tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0x00000000));
    m_expertContent->addAndMakeVisible(m_tabs);

    m_expertTabTracks = std::make_unique<juce::Component>();

    m_libraryModel = std::make_unique<TrackListModel>(
        [this] { return (int) m_library.size(); },
        [this](int row) -> juce::String {
            if (row < 0 || row >= (int) m_library.size()) return {};
            const auto& t = m_library[(size_t) row];
            juce::String title = juce::String(t.title.empty() ? t.filePath : t.title);
            juce::String artist = juce::String(t.artist);
            juce::String bpm = (t.bpm > 0.0) ? juce::String(t.bpm, 1) + " BPM" : juce::String();
            juce::String key = juce::String(t.camelotKey.empty() ? t.key : t.camelotKey);
            juce::String suffix;
            if (bpm.isNotEmpty()) suffix += "  [" + bpm + "]";
            if (key.isNotEmpty()) suffix += "  [" + key + "]";
            return artist.isNotEmpty() ? (artist + " - " + title + suffix)
                                        : (title + suffix);
        });

    m_selectedModel = std::make_unique<TrackListModel>(
        [this] { return (int) m_selectedIndices.size(); },
        [this](int row) -> juce::String {
            if (row < 0 || row >= (int) m_selectedIndices.size()) return {};
            int libIdx = m_selectedIndices[(size_t) row];
            if (libIdx < 0 || libIdx >= (int) m_library.size()) return {};
            const auto& t = m_library[(size_t) libIdx];
            juce::String title = juce::String(t.title.empty() ? t.filePath : t.title);
            juce::String artist = juce::String(t.artist);
            return artist.isNotEmpty() ? (artist + " - " + title) : title;
        });

    m_libraryList = std::make_unique<juce::ListBox>("library", m_libraryModel.get());
    m_libraryList->setMultipleSelectionEnabled(true);
    m_libraryList->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF141420));
    m_libraryList->setRowHeight(24);
    m_expertTabTracks->addAndMakeVisible(*m_libraryList);

    m_selectedList = std::make_unique<juce::ListBox>("selected", m_selectedModel.get());
    m_selectedList->setMultipleSelectionEnabled(true);
    m_selectedList->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF1A1A2E));
    m_selectedList->setRowHeight(24);
    m_expertTabTracks->addAndMakeVisible(*m_selectedList);

    m_addBtn = std::make_unique<juce::TextButton>("Ajouter >");
    m_addBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3B82F6));
    m_addBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFF1F5F9));
    m_addBtn->onClick = [this] {
        auto sel = m_libraryList->getSelectedRows();
        for (int i = 0; i < sel.size(); ++i) {
            int row = sel[i];
            if (std::find(m_selectedIndices.begin(), m_selectedIndices.end(), row)
                == m_selectedIndices.end()) {
                m_selectedIndices.push_back(row);
            }
        }
        m_selectedList->updateContent();
        updateGenerateEnabled();
    };
    m_expertTabTracks->addAndMakeVisible(*m_addBtn);

    m_removeBtn = std::make_unique<juce::TextButton>("< Retirer");
    m_removeBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2E2E44));
    m_removeBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFF1F5F9));
    m_removeBtn->onClick = [this] {
        auto sel = m_selectedList->getSelectedRows();
        std::vector<int> toRemove;
        for (int i = 0; i < sel.size(); ++i) toRemove.push_back(sel[i]);
        std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());
        for (int idx : toRemove) {
            if (idx >= 0 && idx < (int) m_selectedIndices.size())
                m_selectedIndices.erase(m_selectedIndices.begin() + idx);
        }
        m_selectedList->updateContent();
        updateGenerateEnabled();
    };
    m_expertTabTracks->addAndMakeVisible(*m_removeBtn);

    m_expertTabParams = std::make_unique<juce::Component>();

    auto styleSlider = [](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        s.setColour(juce::Slider::thumbColourId, juce::Colour(0xFF3B82F6));
        s.setColour(juce::Slider::trackColourId, juce::Colour(0xFF3B82F6));
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF2E2E44));
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFF1F5F9));
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF1E1E34));
    };

    m_bpmLabel = std::make_unique<juce::Label>("", "Target BPM");
    m_bpmLabel->setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    m_bpmLabel->setColour(juce::Label::textColourId, juce::Colour(0xFF94A3B8));
    m_expertTabParams->addAndMakeVisible(*m_bpmLabel);

    m_bpmSlider = std::make_unique<juce::Slider>();
    m_bpmSlider->setRange(60.0, 180.0, 1.0);
    m_bpmSlider->setValue(125.0, juce::dontSendNotification);
    styleSlider(*m_bpmSlider);
    m_expertTabParams->addAndMakeVisible(*m_bpmSlider);

    m_durationLabel = std::make_unique<juce::Label>("", "Duree (s)");
    m_durationLabel->setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    m_durationLabel->setColour(juce::Label::textColourId, juce::Colour(0xFF94A3B8));
    m_expertTabParams->addAndMakeVisible(*m_durationLabel);

    m_durationSlider = std::make_unique<juce::Slider>();
    m_durationSlider->setRange(30.0, 600.0, 1.0);
    m_durationSlider->setValue(180.0, juce::dontSendNotification);
    styleSlider(*m_durationSlider);
    m_expertTabParams->addAndMakeVisible(*m_durationSlider);

    m_useStemsToggle = std::make_unique<juce::ToggleButton>("Utiliser stems");
    m_useStemsToggle->setColour(juce::ToggleButton::textColourId, juce::Colour(0xFFF1F5F9));
    m_useStemsToggle->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFF3B82F6));
    m_useStemsToggle->setToggleState(true, juce::dontSendNotification);
    m_expertTabParams->addAndMakeVisible(*m_useStemsToggle);

    m_harmonicOnlyToggle = std::make_unique<juce::ToggleButton>("Harmonique uniquement");
    m_harmonicOnlyToggle->setColour(juce::ToggleButton::textColourId, juce::Colour(0xFFF1F5F9));
    m_harmonicOnlyToggle->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFF3B82F6));
    m_harmonicOnlyToggle->setToggleState(true, juce::dontSendNotification);
    m_expertTabParams->addAndMakeVisible(*m_harmonicOnlyToggle);

    m_keyLabel = std::make_unique<juce::Label>("", "Tonalite cible");
    m_keyLabel->setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
    m_keyLabel->setColour(juce::Label::textColourId, juce::Colour(0xFF94A3B8));
    m_expertTabParams->addAndMakeVisible(*m_keyLabel);

    m_keyCombo = std::make_unique<juce::ComboBox>();
    m_keyCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1E1E34));
    m_keyCombo->setColour(juce::ComboBox::textColourId, juce::Colour(0xFFF1F5F9));
    m_keyCombo->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF2E2E44));
    m_keyCombo->setColour(juce::ComboBox::arrowColourId, juce::Colour(0xFF94A3B8));
    m_keyCombo->addItem("Auto", 1);
    int idCounter = 2;
    const char* camelot[] = {
        "1A","1B","2A","2B","3A","3B","4A","4B","5A","5B","6A","6B",
        "7A","7B","8A","8B","9A","9B","10A","10B","11A","11B","12A","12B"
    };
    for (auto* k : camelot) m_keyCombo->addItem(k, idCounter++);
    m_keyCombo->setSelectedId(1, juce::dontSendNotification);
    m_expertTabParams->addAndMakeVisible(*m_keyCombo);

    m_expertTabType = std::make_unique<juce::Component>();

    auto makeRadio = [&](std::unique_ptr<juce::ToggleButton>& btn,
                         const juce::String& name,
                         MashupType t) {
        btn = std::make_unique<juce::ToggleButton>(name);
        btn->setRadioGroupId(1234);
        btn->setColour(juce::ToggleButton::textColourId, juce::Colour(0xFFF1F5F9));
        btn->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFF3B82F6));
        btn->onClick = [this, t, raw = btn.get()] {
            if (raw->getToggleState()) {
                m_selectedType = t;
                m_typeChosen = true;
                updateGenerateEnabled();
            }
        };
        m_expertTabType->addAndMakeVisible(*btn);
    };

    makeRadio(m_typeMashup,  "Mashup",  MashupType::Mashup);
    makeRadio(m_typeMegamix, "Megamix", MashupType::Megamix);
    makeRadio(m_typeMedley,  "Medley",  MashupType::Medley);
    makeRadio(m_typeRemix,   "Remix",   MashupType::Remix);
    m_typeMegamix->setToggleState(true, juce::sendNotification);

    m_expertTabPreview = std::make_unique<juce::Component>();
    m_previewLabel = std::make_unique<juce::Label>(
        "preview", "Apercu disponible apres generation");
    m_previewLabel->setFont(juce::Font("Segoe UI", 13.0f, juce::Font::plain));
    m_previewLabel->setColour(juce::Label::textColourId, juce::Colour(0xFF94A3B8));
    m_previewLabel->setJustificationType(juce::Justification::centred);
    m_expertTabPreview->addAndMakeVisible(*m_previewLabel);

    const auto tabBg = juce::Colour(0xFF12121A);
    m_tabs.addTab("Tracks",      tabBg, m_expertTabTracks.get(),  false);
    m_tabs.addTab("Parametres",  tabBg, m_expertTabParams.get(),  false);
    m_tabs.addTab("Type",        tabBg, m_expertTabType.get(),    false);
    m_tabs.addTab("Apercu",      tabBg, m_expertTabPreview.get(), false);
    m_tabs.setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colour(0x00000000));
    m_tabs.setColour(juce::TabbedButtonBar::tabTextColourId,    juce::Colour(0xFF94A3B8));
    m_tabs.setColour(juce::TabbedButtonBar::frontTextColourId,  juce::Colour(0xFFF1F5F9));
    m_tabs.setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colour(0xFF3B82F6));
}

void AIMixWizardDialog::switchMode(BeatMate::Services::AI::SkillMode mode)
{
    m_mode = mode;
    m_modeToggle.setToggleState(mode == BeatMate::Services::AI::SkillMode::Expert,
                                juce::dontSendNotification);
    if (m_noviceContent) m_noviceContent->setVisible(mode == BeatMate::Services::AI::SkillMode::Novice);
    if (m_expertContent) m_expertContent->setVisible(mode == BeatMate::Services::AI::SkillMode::Expert);
    updateGenerateEnabled();
    resized();
}

void AIMixWizardDialog::updateGenerateEnabled()
{
    using SM = BeatMate::Services::AI::SkillMode;
    bool ok = !m_generating;
    if (m_mode == SM::Novice) {
        ok = ok && m_typeChosen;
    } else {
        ok = ok && m_typeChosen;
    }
    m_generateBtn.setEnabled(ok);
}

void AIMixWizardDialog::setStatusText(const juce::String& text)
{
    m_statusLabel.setText(text, juce::dontSendNotification);
}

void AIMixWizardDialog::onGenerateClicked()
{
    using namespace BeatMate::Services::AI;

    if (m_generating || !m_orchestrator) return;

    MashupRequest req;
    req.type  = m_selectedType;
    req.skill = m_mode;
    req.libraryPool = m_library;

    if (m_mode == SkillMode::Expert) {
        for (int idx : m_selectedIndices) {
            if (idx >= 0 && idx < (int) m_library.size())
                req.sourceTracks.push_back(m_library[(size_t) idx]);
        }
        if (m_bpmSlider)       req.targetBpm = (int) m_bpmSlider->getValue();
        if (m_durationSlider)  req.targetDurationSec = (int) m_durationSlider->getValue();
        if (m_useStemsToggle)  req.useStems = m_useStemsToggle->getToggleState();
        if (m_harmonicOnlyToggle) req.harmonicOnly = m_harmonicOnlyToggle->getToggleState();
        if (m_keyCombo && m_keyCombo->getSelectedId() > 1)
            req.targetKey = m_keyCombo->getText();
    }

    m_generating = true;
    updateGenerateEnabled();
    setStatusText("Generation en cours... 0 %");

    auto onProgress = [safeThis = juce::Component::SafePointer<AIMixWizardDialog>(this)]
                      (float pct, const juce::String& phase) {
        juce::MessageManager::callAsync([safeThis, pct, phase] {
            if (auto* self = safeThis.getComponent()) {
                int pctI = juce::jlimit(0, 100, (int) std::round(pct * 100.0f));
                juce::String msg = "Generation en cours... " + juce::String(pctI) + " %";
                if (phase.isNotEmpty()) msg += "  -  " + phase;
                self->setStatusText(msg);
            }
        });
    };

    auto onDone = [safeThis = juce::Component::SafePointer<AIMixWizardDialog>(this)]
                  (MashupResult result) {
        juce::MessageManager::callAsync([safeThis, result = std::move(result)]() mutable {
            if (auto* self = safeThis.getComponent()) {
                self->m_generating = false;
                if (result.ok) {
                    self->setStatusText("Termine. " + juce::String(result.clips.size())
                                        + " clips generes.");
                } else {
                    self->setStatusText("Echec: " + result.errorMessage);
                }
                if (self->m_onGenerated) self->m_onGenerated(result);
                if (auto* dw = self->findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(result.ok ? 1 : 0);
            }
        });
    };

    m_orchestrator->generateAsync(req, std::move(onProgress), std::move(onDone));
}

void AIMixWizardDialog::showDialog(std::vector<Models::Track> library, ResultCb onGenerated)
{
    auto* dlg = new AIMixWizardDialog();
    dlg->setLibrary(std::move(library));
    dlg->setOnGenerated(std::move(onGenerated));

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(dlg);
    opts.dialogTitle = "AI Mix Wizard";
    opts.dialogBackgroundColour = juce::Colour(0xFF12121A);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void AIMixWizardDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF12121A));

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f));
    g.drawHorizontalLine(56, 0.0f, (float) getWidth());

    g.drawHorizontalLine(getHeight() - 56, 0.0f, (float) getWidth());
}

void AIMixWizardDialog::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(56).reduced(16, 12);
    m_modeToggle.setBounds(header.removeFromRight(140));

    auto footer = area.removeFromBottom(56).reduced(16, 12);
    m_cancelBtn.setBounds(footer.removeFromRight(110));
    footer.removeFromRight(8);
    m_generateBtn.setBounds(footer.removeFromRight(140));
    footer.removeFromRight(12);
    m_statusLabel.setBounds(footer);

    auto body = area.reduced(16, 12);

    if (m_noviceContent) m_noviceContent->setBounds(body);
    if (m_expertContent) m_expertContent->setBounds(body);

    if (m_noviceContent && m_noviceContent->isVisible() && m_noviceCards.size() == 4) {
        const int totalW = kCardWidth * 2 + kCardGap;
        const int totalH = kCardHeight * 2 + kCardGap;
        int xOrigin = (body.getWidth() - totalW) / 2;
        int yOrigin = (body.getHeight() - totalH) / 2;

        for (int i = 0; i < 4; ++i) {
            int row = i / 2;
            int col = i % 2;
            int x = xOrigin + col * (kCardWidth + kCardGap);
            int y = yOrigin + row * (kCardHeight + kCardGap);
            m_noviceCards[(size_t) i]->setBounds(x, y, kCardWidth, kCardHeight);
        }
    }

    if (m_expertContent && m_expertContent->isVisible()) {
        m_tabs.setBounds(m_expertContent->getLocalBounds());
    }

    if (m_expertTabTracks) {
        auto tab = m_expertTabTracks->getLocalBounds().reduced(12);
        auto top = tab.removeFromTop(tab.getHeight() / 2 - 8);
        if (m_libraryList) m_libraryList->setBounds(top);

        tab.removeFromTop(8);
        auto btnRow = tab.removeFromTop(28);
        if (m_addBtn)    m_addBtn->setBounds(btnRow.removeFromLeft(110));
        btnRow.removeFromLeft(8);
        if (m_removeBtn) m_removeBtn->setBounds(btnRow.removeFromLeft(110));

        tab.removeFromTop(8);
        if (m_selectedList) m_selectedList->setBounds(tab);
    }

    if (m_expertTabParams) {
        auto tab = m_expertTabParams->getLocalBounds().reduced(16);
        const int rowH = 28;
        const int gap  = 12;
        const int labelW = 140;

        auto row = tab.removeFromTop(rowH);
        if (m_bpmLabel)  m_bpmLabel->setBounds(row.removeFromLeft(labelW));
        if (m_bpmSlider) m_bpmSlider->setBounds(row);
        tab.removeFromTop(gap);

        row = tab.removeFromTop(rowH);
        if (m_durationLabel)  m_durationLabel->setBounds(row.removeFromLeft(labelW));
        if (m_durationSlider) m_durationSlider->setBounds(row);
        tab.removeFromTop(gap);

        row = tab.removeFromTop(rowH);
        if (m_useStemsToggle) m_useStemsToggle->setBounds(row);
        tab.removeFromTop(gap);

        row = tab.removeFromTop(rowH);
        if (m_harmonicOnlyToggle) m_harmonicOnlyToggle->setBounds(row);
        tab.removeFromTop(gap);

        row = tab.removeFromTop(rowH);
        if (m_keyLabel) m_keyLabel->setBounds(row.removeFromLeft(labelW));
        if (m_keyCombo) m_keyCombo->setBounds(row.removeFromLeft(180));
    }

    if (m_expertTabType) {
        auto tab = m_expertTabType->getLocalBounds().reduced(20);
        const int rowH = 30;
        const int gap  = 8;
        if (m_typeMashup)  { m_typeMashup->setBounds(tab.removeFromTop(rowH));  tab.removeFromTop(gap); }
        if (m_typeMegamix) { m_typeMegamix->setBounds(tab.removeFromTop(rowH)); tab.removeFromTop(gap); }
        if (m_typeMedley)  { m_typeMedley->setBounds(tab.removeFromTop(rowH));  tab.removeFromTop(gap); }
        if (m_typeRemix)   { m_typeRemix->setBounds(tab.removeFromTop(rowH));   tab.removeFromTop(gap); }
    }

    if (m_expertTabPreview && m_previewLabel) {
        m_previewLabel->setBounds(m_expertTabPreview->getLocalBounds());
    }
}

}
