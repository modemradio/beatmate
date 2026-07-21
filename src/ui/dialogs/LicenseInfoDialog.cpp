#include "LicenseInfoDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
LicenseInfoDialog::LicenseInfoDialog(){
    setSize(450,400);
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("dialog.licenseInfo.title"));
    m_titleLabel->setFont(juce::Font(18.0f,juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeLbl=[this](const juce::String& text,juce::Colour col=Colors::textSecondary()){
        auto l=std::make_unique<juce::Label>("",text);
        l->setFont(juce::Font(12.0f));l->setColour(juce::Label::textColourId,col);
        addAndMakeVisible(*l);return l;
    };

    m_typeLbl=makeLbl(BM_TJ("dialog.licenseInfo.type"));
    m_typeVal=makeLbl(BM_TJ("dialog.licenseInfo.trial"),Colors::warning());
    m_typeVal->setFont(juce::Font(12.0f,juce::Font::bold));

    m_activationLbl=makeLbl(BM_TJ("dialog.licenseInfo.activation"));
    m_activationVal=makeLbl("-");

    m_expirationLbl=makeLbl(BM_TJ("dialog.licenseInfo.expiration"));
    m_expirationVal=makeLbl("-");

    m_machineIdLbl=makeLbl(BM_TJ("dialog.licenseInfo.machineId"));
    m_machineIdVal=makeLbl("-");

    m_featuresLabel=std::make_unique<juce::Label>("f",BM_TJ("dialog.licenseInfo.features"));
    m_featuresLabel->setFont(juce::Font(12.0f));
    m_featuresLabel->setColour(juce::Label::textColourId,Colors::success());
    m_featuresLabel->setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(*m_featuresLabel);

    m_closeBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.licenseInfo.close"));
    m_closeBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_closeBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_closeBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_closeBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_closeBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_closeBtn);
}
void LicenseInfoDialog::setLicenseType(const juce::String& type){m_typeVal->setText(type,juce::dontSendNotification);}
void LicenseInfoDialog::setActivationDate(const juce::String& date){m_activationVal->setText(date,juce::dontSendNotification);}
void LicenseInfoDialog::setExpirationDate(const juce::String& date){m_expirationVal->setText(date,juce::dontSendNotification);}
void LicenseInfoDialog::setMachineId(const juce::String& id){m_machineIdVal->setText(id,juce::dontSendNotification);}
void LicenseInfoDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    float y1=54.0f;
    g.setColour(Colors::bgMedium());g.fillRoundedRectangle(24.0f,y1,(float)getWidth()-48.0f,110.0f,8.0f);
    g.setColour(Colors::border());g.drawRoundedRectangle(24.0f,y1,(float)getWidth()-48.0f,110.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(13.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.licenseInfo.details"),34,(int)y1-8,100,16,juce::Justification::centredLeft);
    float y2=y1+126.0f;
    g.setColour(Colors::bgMedium());g.fillRoundedRectangle(24.0f,y2,(float)getWidth()-48.0f,120.0f,8.0f);
    g.setColour(Colors::border());g.drawRoundedRectangle(24.0f,y2,(float)getWidth()-48.0f,120.0f,8.0f,1.0f);
    g.setColour(Colors::textPrimary());g.setFont(juce::Font(13.0f,juce::Font::bold));
    g.drawText(BM_TJ("dialog.licenseInfo.featuresSection"),34,(int)y2-8,150,16,juce::Justification::centredLeft);
}
void LicenseInfoDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26));area.removeFromTop(8);
    auto detArea=area.removeFromTop(110).reduced(12);
    int rowH=22;
    auto r1=detArea.removeFromTop(rowH);m_typeLbl->setBounds(r1.removeFromLeft(130));m_typeVal->setBounds(r1);
    auto r2=detArea.removeFromTop(rowH);m_activationLbl->setBounds(r2.removeFromLeft(130));m_activationVal->setBounds(r2);
    auto r3=detArea.removeFromTop(rowH);m_expirationLbl->setBounds(r3.removeFromLeft(130));m_expirationVal->setBounds(r3);
    auto r4=detArea.removeFromTop(rowH);m_machineIdLbl->setBounds(r4.removeFromLeft(130));m_machineIdVal->setBounds(r4);
    area.removeFromTop(16);
    m_featuresLabel->setBounds(area.removeFromTop(120).reduced(12));
    auto btnRow=area.removeFromBottom(30);
    m_closeBtn->setBounds(btnRow.removeFromRight(100));
}
}
