#include "NestedSmartRuleEditor.h"

#include <algorithm>

namespace BeatMate::UI::Widgets {

using Models::SmartPlaylistField;
using Models::SmartPlaylistOperator;
using Models::RuleConjunction;

namespace {

constexpr int kRowHeight       = 30;
constexpr int kHeaderHeight    = 28;
constexpr int kButtonRowHeight = 28;
constexpr int kPadding         = 8;
constexpr int kIndent          = 18;

struct FieldEntry { SmartPlaylistField field; const char* label; };
struct OpEntry    { SmartPlaylistOperator op;  const char* label; };

const FieldEntry kFields[] = {
    { SmartPlaylistField::BPM,          "BPM" },
    { SmartPlaylistField::Key,          "Key" },
    { SmartPlaylistField::Genre,        "Genre" },
    { SmartPlaylistField::Energy,       "Energy" },
    { SmartPlaylistField::Rating,       "Rating" },
    { SmartPlaylistField::Year,         "Year" },
    { SmartPlaylistField::Artist,       "Artist" },
    { SmartPlaylistField::Title,        "Title" },
    { SmartPlaylistField::Album,        "Album" },
    { SmartPlaylistField::Duration,     "Duration" },
    { SmartPlaylistField::PlayCount,    "Play Count" },
    { SmartPlaylistField::DateAdded,    "Date Added" },
    { SmartPlaylistField::LastPlayed,   "Last Played" },
    { SmartPlaylistField::Comment,      "Comment" },
    { SmartPlaylistField::Label,        "Label" },
    { SmartPlaylistField::Mood,         "Mood" },
    { SmartPlaylistField::Danceability, "Danceability" },
    { SmartPlaylistField::Color,        "Color" },
    { SmartPlaylistField::Source,       "Source" },
    { SmartPlaylistField::FileFormat,   "File Format" },
    { SmartPlaylistField::BitRate,      "Bit Rate" },
    { SmartPlaylistField::SampleRate,   "Sample Rate" },
    { SmartPlaylistField::Grouping,     "Grouping" },
    { SmartPlaylistField::CamelotKey,   "Camelot Key" },
    { SmartPlaylistField::OpenKey,      "Open Key" }
};

const OpEntry kOps[] = {
    { SmartPlaylistOperator::Equals,         "=" },
    { SmartPlaylistOperator::NotEquals,      "!=" },
    { SmartPlaylistOperator::Contains,       "contains" },
    { SmartPlaylistOperator::NotContains,    "!contains" },
    { SmartPlaylistOperator::StartsWith,     "starts with" },
    { SmartPlaylistOperator::EndsWith,       "ends with" },
    { SmartPlaylistOperator::GreaterThan,    ">" },
    { SmartPlaylistOperator::LessThan,       "<" },
    { SmartPlaylistOperator::GreaterOrEqual, ">=" },
    { SmartPlaylistOperator::LessOrEqual,    "<=" },
    { SmartPlaylistOperator::Between,        "between" },
    { SmartPlaylistOperator::IsEmpty,        "is empty" },
    { SmartPlaylistOperator::IsNotEmpty,     "is not empty" },
    { SmartPlaylistOperator::InLast,         "in last N days" },
    { SmartPlaylistOperator::NotInLast,      "not in last N days" }
};

juce::Colour depthTint(int depth) {
    const juce::Colour palette[] = {
        juce::Colour::fromRGB(40, 42, 54),
        juce::Colour::fromRGB(48, 50, 68),
        juce::Colour::fromRGB(56, 58, 82),
        juce::Colour::fromRGB(64, 66, 96),
        juce::Colour::fromRGB(72, 74, 110)
    };
    int idx = std::min(depth, (int)(sizeof(palette) / sizeof(palette[0])) - 1);
    return palette[idx];
}

} // anonymous namespace


NestedSmartRuleGroupView::NestedSmartRuleGroupView(int depth, bool isRoot)
    : m_depth(depth), m_isRoot(isRoot)
{
    m_headerLabel = std::make_unique<juce::Label>("header",
        isRoot ? "Smart Rules" : ("Group (level " + juce::String(depth) + ")"));
    m_headerLabel->setFont(juce::Font(14.0f, juce::Font::bold));
    m_headerLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(*m_headerLabel);

    m_conjunctionCombo = std::make_unique<juce::ComboBox>("conj");
    m_conjunctionCombo->addItem("Match ALL (AND)", 1);
    m_conjunctionCombo->addItem("Match ANY (OR)",  2);
    m_conjunctionCombo->setSelectedId(1, juce::dontSendNotification);
    m_conjunctionCombo->onChange = [this] { notifyChanged(); };
    addAndMakeVisible(*m_conjunctionCombo);

    m_addRuleBtn = std::make_unique<juce::TextButton>("+ Rule");
    m_addRuleBtn->onClick = [this] {
        addRuleRow(Rule{});
        rebuildLayout();
        notifyChanged();
    };
    addAndMakeVisible(*m_addRuleBtn);

    m_addGroupBtn = std::make_unique<juce::TextButton>("+ Group");
    m_addGroupBtn->onClick = [this] {
        addSubGroup(Group{});
        rebuildLayout();
        notifyChanged();
    };
    addAndMakeVisible(*m_addGroupBtn);

    if (!isRoot) {
        m_deleteGroupBtn = std::make_unique<juce::TextButton>("Delete group");
        m_deleteGroupBtn->onClick = [this] {
            if (onDeleteRequested) onDeleteRequested(this);
        };
        addAndMakeVisible(*m_deleteGroupBtn);
    }
}

void NestedSmartRuleGroupView::paint(juce::Graphics& g) {
    g.fillAll(depthTint(m_depth));
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);
}

void NestedSmartRuleGroupView::resized() {
    auto bounds = getLocalBounds().reduced(kPadding);

    auto headerRow = bounds.removeFromTop(kHeaderHeight);
    m_headerLabel->setBounds(headerRow.removeFromLeft(160));
    if (m_deleteGroupBtn)
        m_deleteGroupBtn->setBounds(headerRow.removeFromRight(110));
    headerRow.removeFromLeft(8);
    m_conjunctionCombo->setBounds(headerRow.removeFromLeft(170));

    bounds.removeFromTop(4);

    for (auto& row : m_rules) {
        auto r = bounds.removeFromTop(kRowHeight);
        row->fieldCombo->setBounds(r.removeFromLeft(140));
        r.removeFromLeft(4);
        row->opCombo->setBounds(r.removeFromLeft(130));
        r.removeFromLeft(4);
        auto remove = r.removeFromRight(30);
        row->removeBtn->setBounds(remove);
        r.removeFromRight(4);
        auto valW = r.getWidth();
        row->valueEdit->setBounds(r.removeFromLeft(valW / 2 - 2));
        r.removeFromLeft(4);
        row->value2Edit->setBounds(r);
        bounds.removeFromTop(2);
    }

    for (auto& sub : m_subGroups) {
        auto subArea = bounds.removeFromTop(sub->preferredHeight());
        subArea.removeFromLeft(kIndent);
        sub->setBounds(subArea);
        bounds.removeFromTop(4);
    }

    auto btnRow = bounds.removeFromTop(kButtonRowHeight);
    m_addRuleBtn->setBounds(btnRow.removeFromLeft(90));
    btnRow.removeFromLeft(6);
    m_addGroupBtn->setBounds(btnRow.removeFromLeft(90));
}

void NestedSmartRuleGroupView::setGroup(const Group& g) {
    m_rules.clear();
    m_subGroups.clear();
    removeAllChildren();

    addAndMakeVisible(*m_headerLabel);
    addAndMakeVisible(*m_conjunctionCombo);
    addAndMakeVisible(*m_addRuleBtn);
    addAndMakeVisible(*m_addGroupBtn);
    if (m_deleteGroupBtn) addAndMakeVisible(*m_deleteGroupBtn);

    m_conjunctionCombo->setSelectedId(
        g.conjunction == RuleConjunction::AND ? 1 : 2,
        juce::dontSendNotification);

    for (const auto& r : g.rules) addRuleRow(r);
    for (const auto& sg : g.subGroups) addSubGroup(sg);

    rebuildLayout();
}

NestedSmartRuleGroupView::Group NestedSmartRuleGroupView::getGroup() const {
    Group g;
    g.conjunction = (m_conjunctionCombo->getSelectedId() == 2)
        ? RuleConjunction::OR : RuleConjunction::AND;
    for (const auto& row : m_rules) g.rules.push_back(readRuleFromRow(*row));
    for (const auto& sub : m_subGroups) g.subGroups.push_back(sub->getGroup());
    return g;
}

int NestedSmartRuleGroupView::preferredHeight() const {
    int h = kPadding * 2 + kHeaderHeight + 4 + kButtonRowHeight;
    h += (int)m_rules.size() * (kRowHeight + 2);
    for (const auto& sub : m_subGroups) h += sub->preferredHeight() + 4;
    return h;
}

void NestedSmartRuleGroupView::addRuleRow(const Rule& rule) {
    auto row = std::make_unique<RuleRow>();

    row->fieldCombo = std::make_unique<juce::ComboBox>();
    populateFieldCombo(*row->fieldCombo);
    row->fieldCombo->onChange = [this] { notifyChanged(); };
    addAndMakeVisible(*row->fieldCombo);

    row->opCombo = std::make_unique<juce::ComboBox>();
    populateOpCombo(*row->opCombo);
    row->opCombo->onChange = [this] { notifyChanged(); };
    addAndMakeVisible(*row->opCombo);

    row->valueEdit = std::make_unique<juce::TextEditor>();
    row->valueEdit->setTextToShowWhenEmpty("value", juce::Colours::grey);
    row->valueEdit->onTextChange = [this] { notifyChanged(); };
    addAndMakeVisible(*row->valueEdit);

    row->value2Edit = std::make_unique<juce::TextEditor>();
    row->value2Edit->setTextToShowWhenEmpty("value2 (between)", juce::Colours::grey);
    row->value2Edit->onTextChange = [this] { notifyChanged(); };
    addAndMakeVisible(*row->value2Edit);

    auto* rowPtr = row.get();
    row->removeBtn = std::make_unique<juce::TextButton>("x");
    row->removeBtn->onClick = [this, rowPtr] {
        for (size_t i = 0; i < m_rules.size(); ++i) {
            if (m_rules[i].get() == rowPtr) {
                removeRuleRow((int)i);
                return;
            }
        }
    };
    addAndMakeVisible(*row->removeBtn);

    writeRuleToRow(*row, rule);
    m_rules.push_back(std::move(row));
}

void NestedSmartRuleGroupView::removeRuleRow(int index) {
    if (index < 0 || index >= (int)m_rules.size()) return;
    auto& row = m_rules[index];
    removeChildComponent(row->fieldCombo.get());
    removeChildComponent(row->opCombo.get());
    removeChildComponent(row->valueEdit.get());
    removeChildComponent(row->value2Edit.get());
    removeChildComponent(row->removeBtn.get());
    m_rules.erase(m_rules.begin() + index);
    rebuildLayout();
    notifyChanged();
}

void NestedSmartRuleGroupView::addSubGroup(const Group& g) {
    auto sub = std::make_unique<NestedSmartRuleGroupView>(m_depth + 1, false);
    sub->onChanged = [this] { notifyChanged(); };
    sub->onDeleteRequested = [this](NestedSmartRuleGroupView* v) {
        removeSubGroup(v);
    };
    sub->setGroup(g);
    addAndMakeVisible(*sub);
    m_subGroups.push_back(std::move(sub));
}

void NestedSmartRuleGroupView::removeSubGroup(NestedSmartRuleGroupView* child) {
    for (size_t i = 0; i < m_subGroups.size(); ++i) {
        if (m_subGroups[i].get() == child) {
            removeChildComponent(m_subGroups[i].get());
            m_subGroups.erase(m_subGroups.begin() + i);
            rebuildLayout();
            notifyChanged();
            return;
        }
    }
}

void NestedSmartRuleGroupView::populateFieldCombo(juce::ComboBox& combo) {
    int id = 1;
    for (const auto& f : kFields) combo.addItem(f.label, id++);
    combo.setSelectedId(1, juce::dontSendNotification);
}

void NestedSmartRuleGroupView::populateOpCombo(juce::ComboBox& combo) {
    int id = 1;
    for (const auto& o : kOps) combo.addItem(o.label, id++);
    combo.setSelectedId(1, juce::dontSendNotification);
}

NestedSmartRuleGroupView::Rule NestedSmartRuleGroupView::readRuleFromRow(const RuleRow& row) const {
    Rule r;
    int fIdx = row.fieldCombo->getSelectedId() - 1;
    int oIdx = row.opCombo->getSelectedId() - 1;
    if (fIdx >= 0 && fIdx < (int)(sizeof(kFields) / sizeof(kFields[0])))
        r.field = kFields[fIdx].field;
    if (oIdx >= 0 && oIdx < (int)(sizeof(kOps) / sizeof(kOps[0])))
        r.operator_ = kOps[oIdx].op;
    r.value  = row.valueEdit->getText().toStdString();
    r.value2 = row.value2Edit->getText().toStdString();
    return r;
}

void NestedSmartRuleGroupView::writeRuleToRow(RuleRow& row, const Rule& rule) {
    int fIdx = 0;
    for (int i = 0; i < (int)(sizeof(kFields) / sizeof(kFields[0])); ++i)
        if (kFields[i].field == rule.field) { fIdx = i; break; }
    int oIdx = 0;
    for (int i = 0; i < (int)(sizeof(kOps) / sizeof(kOps[0])); ++i)
        if (kOps[i].op == rule.operator_) { oIdx = i; break; }
    row.fieldCombo->setSelectedId(fIdx + 1, juce::dontSendNotification);
    row.opCombo->setSelectedId(oIdx + 1, juce::dontSendNotification);
    row.valueEdit->setText(rule.value, juce::dontSendNotification);
    row.value2Edit->setText(rule.value2, juce::dontSendNotification);
}

void NestedSmartRuleGroupView::notifyChanged() {
    if (onChanged) onChanged();
}

void NestedSmartRuleGroupView::rebuildLayout() {
    setSize(getWidth() > 0 ? getWidth() : 600, preferredHeight());
    resized();
    if (auto* parent = getParentComponent()) parent->resized();
    repaint();
}


NestedSmartRuleEditor::NestedSmartRuleEditor() {
    m_titleLabel = std::make_unique<juce::Label>("title", "Smart Playlist Rules (nested)");
    m_titleLabel->setFont(juce::Font(16.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(*m_titleLabel);

    m_matchCountLabel = std::make_unique<juce::Label>("match", "");
    m_matchCountLabel->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(*m_matchCountLabel);

    m_rootGroup = std::make_unique<NestedSmartRuleGroupView>(0, true);
    m_rootGroup->onChanged = [this] {
        if (onLivePreview) {
            auto txt = onLivePreview(m_rootGroup->getGroup());
            m_matchCountLabel->setText(txt, juce::dontSendNotification);
        }
        resized();
    };

    m_viewport = std::make_unique<juce::Viewport>();
    m_viewport->setViewedComponent(m_rootGroup.get(), false);
    m_viewport->setScrollBarsShown(true, false);
    addAndMakeVisible(*m_viewport);

    m_applyBtn = std::make_unique<juce::TextButton>("Apply");
    m_applyBtn->onClick = [this] {
        if (onApply) onApply(m_rootGroup->getGroup());
    };
    addAndMakeVisible(*m_applyBtn);

    m_cancelBtn = std::make_unique<juce::TextButton>("Cancel");
    m_cancelBtn->onClick = [this] {
        if (onCancel) onCancel();
    };
    addAndMakeVisible(*m_cancelBtn);
}

void NestedSmartRuleEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(30, 32, 44));
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 6.0f, 1.5f);
}

void NestedSmartRuleEditor::resized() {
    auto bounds = getLocalBounds().reduced(10);

    auto top = bounds.removeFromTop(28);
    m_titleLabel->setBounds(top.removeFromLeft(260));
    m_matchCountLabel->setBounds(top);

    auto bottom = bounds.removeFromBottom(36);
    m_cancelBtn->setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(6);
    m_applyBtn->setBounds(bottom.removeFromRight(90));

    bounds.removeFromTop(6);
    m_viewport->setBounds(bounds);

    const int contentWidth = bounds.getWidth() - 16;
    const int contentHeight = std::max(m_rootGroup->preferredHeight(), bounds.getHeight());
    m_rootGroup->setSize(contentWidth, contentHeight);
}

void NestedSmartRuleEditor::setGroup(const Models::SmartPlaylistRuleGroup& g) {
    m_rootGroup->setGroup(g);
    resized();
}

Models::SmartPlaylistRuleGroup NestedSmartRuleEditor::getGroup() const {
    return m_rootGroup->getGroup();
}

} // namespace BeatMate::UI::Widgets
