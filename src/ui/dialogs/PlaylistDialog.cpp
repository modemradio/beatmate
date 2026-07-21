#include "PlaylistDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
PlaylistDialog::PlaylistDialog(){
    setSize(500,450);
    auto makeLbl=[this](const juce::String& text){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        addAndMakeVisible(*l);return l;
    };
    m_nameLabel=makeLbl(BM_TJ("dialog.playlist.name"));
    m_nameEdit=std::make_unique<juce::TextEditor>();
    m_nameEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_nameEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_nameEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_nameEdit->setTextToShowWhenEmpty(BM_TJ("dialog.playlist.namePlaceholder"),Colors::textDim());
    addAndMakeVisible(*m_nameEdit);

    m_descLabel=makeLbl(BM_TJ("dialog.playlist.description"));
    m_descEdit=std::make_unique<juce::TextEditor>();
    m_descEdit->setMultiLine(true,true);
    m_descEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_descEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_descEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_descEdit->setTextToShowWhenEmpty(BM_TJ("dialog.playlist.descPlaceholder"),Colors::textDim());
    addAndMakeVisible(*m_descEdit);

    m_colorLabel=makeLbl(BM_TJ("dialog.playlist.color"));
    m_colorBtn=std::make_unique<juce::TextButton>("");
    m_colorBtn->setColour(juce::TextButton::buttonColourId,m_selectedColor);
    m_colorBtn->onClick=[this]{
        auto chooser=std::make_unique<juce::ColourSelector>(juce::ColourSelector::showColourAtTop|juce::ColourSelector::showColourspace);
        chooser->setCurrentColour(m_selectedColor);
        chooser->setSize(300,400);
        chooser->addChangeListener(this);
        juce::CallOutBox::launchAsynchronously(std::move(chooser),m_colorBtn->getScreenBounds(),nullptr);
    };
    addAndMakeVisible(*m_colorBtn);

    m_smartCheck=std::make_unique<juce::ToggleButton>(BM_TJ("dialog.playlist.smart"));
    m_smartCheck->setColour(juce::ToggleButton::textColourId,Colors::textSecondary());
    m_smartCheck->setColour(juce::ToggleButton::tickColourId,Colors::primary());
    m_smartCheck->onClick=[this]{
        bool on=m_smartCheck->getToggleState();
        m_rulesTable->setVisible(on);m_addRuleBtn->setVisible(on);
        resized();
    };
    addAndMakeVisible(*m_smartCheck);

    m_rulesTable=std::make_unique<juce::TableListBox>("rules",this);
    m_rulesTable->setColour(juce::ListBox::backgroundColourId,Colors::bgDark());
    m_rulesTable->setColour(juce::ListBox::outlineColourId,Colors::border());
    m_rulesTable->getHeader().addColumn(BM_TJ("dialog.playlist.colField"),1,100);
    m_rulesTable->getHeader().addColumn(BM_TJ("dialog.playlist.colOp"),2,80);
    m_rulesTable->getHeader().addColumn(BM_TJ("dialog.playlist.colValue"),3,120);
    m_rulesTable->getHeader().addColumn(BM_TJ("dialog.playlist.colConj"),4,80);
    m_rulesTable->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId,Colors::bgMedium());
    m_rulesTable->getHeader().setColour(juce::TableHeaderComponent::textColourId,Colors::textMuted());
    m_rulesTable->setVisible(false);
    addAndMakeVisible(*m_rulesTable);

    m_addRuleBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.playlist.addRule"));
    m_addRuleBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_addRuleBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_addRuleBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_addRuleBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_addRuleBtn->onClick=[this]{m_ruleCount++;m_rulesTable->updateContent();};
    m_addRuleBtn->setVisible(false);
    addAndMakeVisible(*m_addRuleBtn);

    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.playlist.cancel"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_cancelBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_cancelBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);};
    addAndMakeVisible(*m_cancelBtn);

    m_okBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.playlist.create"));
    m_okBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_okBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_okBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_okBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_okBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_okBtn);
}
void PlaylistDialog::changeListenerCallback(juce::ChangeBroadcaster* source){
    if(auto* cs=dynamic_cast<juce::ColourSelector*>(source)){
        m_selectedColor=cs->getCurrentColour();
        m_colorBtn->setColour(juce::TextButton::buttonColourId,m_selectedColor);
    }
}
juce::String PlaylistDialog::playlistName() const{return m_nameEdit->getText();}
juce::String PlaylistDialog::description() const{return m_descEdit->getText();}
juce::Colour PlaylistDialog::color() const{return m_selectedColor;}
bool PlaylistDialog::isSmart() const{return m_smartCheck->getToggleState();}
int PlaylistDialog::getNumRows(){return m_ruleCount;}
void PlaylistDialog::paintRowBackground(juce::Graphics& g,int rowNumber,int,int,bool rowIsSelected){
    if(rowIsSelected)g.fillAll(Colors::primary().withAlpha(0.3f));
    else if(rowNumber%2)g.fillAll(Colors::bgDark());
}
void PlaylistDialog::paintCell(juce::Graphics& g,int,int,int width,int height,bool){
    g.setColour(Colors::textSecondary());g.setFont(12.0f);
    g.drawText("-",4,0,width-8,height,juce::Justification::centredLeft);
}
int PlaylistDialog::showDialog(juce::Component*){
    auto*content=new PlaylistDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.playlist.windowTitle");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    return opts.runModal();
}
void PlaylistDialog::paint(juce::Graphics& g){ProDraw::viewBackground(g, getWidth(), getHeight());}
void PlaylistDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    int rowH=28;
    auto r1=area.removeFromTop(rowH);m_nameLabel->setBounds(r1.removeFromLeft(90));m_nameEdit->setBounds(r1);
    area.removeFromTop(8);
    auto r2=area.removeFromTop(60);m_descLabel->setBounds(r2.removeFromLeft(90).removeFromTop(rowH));m_descEdit->setBounds(r2);
    area.removeFromTop(8);
    auto r3=area.removeFromTop(30);m_colorLabel->setBounds(r3.removeFromLeft(90));m_colorBtn->setBounds(r3.removeFromLeft(30));
    area.removeFromTop(8);
    m_smartCheck->setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    auto btnRow=area.removeFromBottom(30);
    m_okBtn->setBounds(btnRow.removeFromRight(80));btnRow.removeFromRight(8);
    m_cancelBtn->setBounds(btnRow.removeFromRight(80));
    area.removeFromBottom(8);
    if(m_smartCheck->getToggleState()){
        m_addRuleBtn->setBounds(area.removeFromBottom(28));area.removeFromBottom(4);
        m_rulesTable->setBounds(area);
    }
}
} // namespace BeatMate::UI
