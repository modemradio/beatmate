#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../models/SmartPlaylistRule.h"

namespace BeatMate::UI::Widgets {

class NestedSmartRuleGroupView : public juce::Component
{
public:
    using Group = Models::SmartPlaylistRuleGroup;
    using Rule  = Models::SmartPlaylistRule;

    explicit NestedSmartRuleGroupView(int depth = 0, bool isRoot = true);
    ~NestedSmartRuleGroupView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setGroup(const Group& group);
    Group getGroup() const;

    // Fires whenever structure changes (rule added/removed, group nested, etc.)
    std::function<void()> onChanged;

    // Called by root only: delete-me button invokes this on parent
    std::function<void(NestedSmartRuleGroupView*)> onDeleteRequested;

    int preferredHeight() const;

private:
    struct RuleRow {
        std::unique_ptr<juce::ComboBox>   fieldCombo;
        std::unique_ptr<juce::ComboBox>   opCombo;
        std::unique_ptr<juce::TextEditor> valueEdit;
        std::unique_ptr<juce::TextEditor> value2Edit;
        std::unique_ptr<juce::TextButton> removeBtn;
    };

    void addRuleRow(const Rule& rule);
    void removeRuleRow(int index);
    void addSubGroup(const Group& group);
    void removeSubGroup(NestedSmartRuleGroupView* child);
    void populateFieldCombo(juce::ComboBox& combo);
    void populateOpCombo(juce::ComboBox& combo);
    Rule readRuleFromRow(const RuleRow& row) const;
    void writeRuleToRow(RuleRow& row, const Rule& rule);
    void notifyChanged();
    void rebuildLayout();

    int m_depth;
    bool m_isRoot;

    std::unique_ptr<juce::Label>        m_headerLabel;
    std::unique_ptr<juce::ComboBox>     m_conjunctionCombo;
    std::unique_ptr<juce::TextButton>   m_addRuleBtn;
    std::unique_ptr<juce::TextButton>   m_addGroupBtn;
    std::unique_ptr<juce::TextButton>   m_deleteGroupBtn;

    std::vector<std::unique_ptr<RuleRow>> m_rules;
    std::vector<std::unique_ptr<NestedSmartRuleGroupView>> m_subGroups;
};

class NestedSmartRuleEditor : public juce::Component
{
public:
    NestedSmartRuleEditor();
    ~NestedSmartRuleEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setGroup(const Models::SmartPlaylistRuleGroup& g);
    Models::SmartPlaylistRuleGroup getGroup() const;

    std::function<void(const Models::SmartPlaylistRuleGroup&)> onApply;
    std::function<void()> onCancel;

    // Called continuously whenever the rule structure changes.
    std::function<juce::String(const Models::SmartPlaylistRuleGroup&)> onLivePreview;

private:
    std::unique_ptr<juce::Label>       m_titleLabel;
    std::unique_ptr<juce::Label>       m_matchCountLabel;
    std::unique_ptr<juce::Viewport>    m_viewport;
    std::unique_ptr<NestedSmartRuleGroupView> m_rootGroup;
    std::unique_ptr<juce::TextButton>  m_applyBtn;
    std::unique_ptr<juce::TextButton>  m_cancelBtn;
};

} // namespace BeatMate::UI::Widgets
