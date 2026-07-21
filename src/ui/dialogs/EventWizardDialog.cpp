#include "EventWizardDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
EventWizardDialog::EventWizardDialog(){
    setSize(600,450);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.event.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_subtitleLabel=std::make_unique<juce::Label>("st","");
    m_subtitleLabel->setFont(juce::Font(12.0f));
    m_subtitleLabel->setColour(juce::Label::textColourId,Colors::textMuted());
    addAndMakeVisible(*m_subtitleLabel);

    auto* p0=new juce::Component();
    auto makeFormRow=[&](const juce::String& label,std::unique_ptr<juce::Label>& lbl,std::unique_ptr<juce::TextEditor>& edit){
        lbl=std::make_unique<juce::Label>("",label);
        lbl->setFont(juce::Font(12.0f));lbl->setColour(juce::Label::textColourId,Colors::textSecondary());
        p0->addAndMakeVisible(*lbl);
        edit=std::make_unique<juce::TextEditor>();
        edit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
        edit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
        edit->setColour(juce::TextEditor::outlineColourId,Colors::border());
        p0->addAndMakeVisible(*edit);
    };
    makeFormRow(BM_TJ("dialog.event.name"),m_nameLabel,m_nameEdit);
    makeFormRow(BM_TJ("dialog.event.venue"),m_venueLabel,m_venueEdit);
    makeFormRow(BM_TJ("dialog.event.date"),m_dateLabel,m_dateEdit);
    makeFormRow(BM_TJ("dialog.event.start"),m_startLabel,m_startEdit);
    m_startEdit->setText("22:00");
    makeFormRow(BM_TJ("dialog.event.end"),m_endLabel,m_endEdit);
    m_endEdit->setText("04:00");
    m_pages.add(p0);addChildComponent(p0);

    juce::StringArray pageTexts={
        BM_TJ("dialog.event.page1"),
        BM_TJ("dialog.event.page2"),
        BM_TJ("dialog.event.page3")
    };
    for(int i=0;i<3;++i){
        auto*p=new juce::Component();
        auto*l=new juce::Label("",pageTexts[i]);
        l->setFont(juce::Font(14.0f));l->setColour(juce::Label::textColourId,Colors::textSecondary());
        l->setJustificationType(juce::Justification::topLeft);
        p->addAndMakeVisible(l);
        m_pages.add(p);addChildComponent(p);
    }

    m_prevBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.event.prev"));
    m_prevBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_prevBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_prevBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_prevBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_prevBtn->onClick=[this]{if(m_currentPage>0)showPage(m_currentPage-1);};
    addAndMakeVisible(*m_prevBtn);

    m_nextBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.event.next"));
    m_nextBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_nextBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_nextBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_nextBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_nextBtn->onClick=[this]{
        if(m_currentPage<m_pageCount-1)showPage(m_currentPage+1);
        else if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);
    };
    addAndMakeVisible(*m_nextBtn);

    showPage(0);
}
void EventWizardDialog::showPage(int index){
    for(int i=0;i<m_pages.size();++i)m_pages[i]->setVisible(i==index);
    m_currentPage=index;
    juce::StringArray titles={BM_TJ("dialog.event.pageInfo"),BM_TJ("dialog.event.pageSections"),BM_TJ("dialog.event.pageConstraints"),BM_TJ("dialog.event.pageSummary")};
    m_subtitleLabel->setText(titles[index],juce::dontSendNotification);
    m_prevBtn->setEnabled(index>0);
    m_nextBtn->setButtonText(index<m_pageCount-1?BM_TJ("dialog.event.next"):BM_TJ("dialog.event.finish"));
    resized();
}
int EventWizardDialog::showDialog(juce::Component*){
    auto*content=new EventWizardDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.event.title");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    return opts.runModal();
}
void EventWizardDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    g.setColour(Colors::border());
    g.drawHorizontalLine(60,0.0f,(float)getWidth());
    g.drawHorizontalLine(getHeight()-50,0.0f,(float)getWidth());
}
void EventWizardDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(24));
    m_subtitleLabel->setBounds(area.removeFromTop(20));area.removeFromTop(20);
    auto btnRow=area.removeFromBottom(30);
    m_nextBtn->setBounds(btnRow.removeFromRight(100));btnRow.removeFromRight(8);
    m_prevBtn->setBounds(btnRow.removeFromRight(100));
    area.removeFromBottom(12);

    for(auto*p:m_pages)p->setBounds(area);

    if(m_currentPage==0&&m_pages.size()>0){
        auto formArea=area.reduced(8);
        int rowH=30,gap=8;
        auto layoutRow=[&](juce::Label* lbl,juce::TextEditor* ed){
            auto row=formArea.removeFromTop(rowH);
            lbl->setBounds(row.removeFromLeft(80));
            ed->setBounds(row);
            formArea.removeFromTop(gap);
        };
        layoutRow(m_nameLabel.get(),m_nameEdit.get());
        layoutRow(m_venueLabel.get(),m_venueEdit.get());
        layoutRow(m_dateLabel.get(),m_dateEdit.get());
        layoutRow(m_startLabel.get(),m_startEdit.get());
        layoutRow(m_endLabel.get(),m_endEdit.get());
    }
    for(int i=1;i<m_pages.size();++i){
        if(m_pages[i]->getNumChildComponents()>0)
            m_pages[i]->getChildComponent(0)->setBounds(m_pages[i]->getLocalBounds().reduced(8));
    }
}
} // namespace BeatMate::UI
