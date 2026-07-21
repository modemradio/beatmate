#include "ErrorDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
ErrorDialog::ErrorDialog(const juce::String& message,const juce::String& details)
    :m_message(message),m_details(details){
    setSize(450,250);
    if(details.isNotEmpty()){
        m_detailsEdit=std::make_unique<juce::TextEditor>();
        m_detailsEdit->setMultiLine(true,true);
        m_detailsEdit->setReadOnly(true);
        m_detailsEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgDarkest());
        m_detailsEdit->setColour(juce::TextEditor::textColourId,Colors::textSecondary());
        m_detailsEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
        m_detailsEdit->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),10.0f,juce::Font::plain));
        m_detailsEdit->setText(details);
        addAndMakeVisible(*m_detailsEdit);

        m_copyBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.error.copyDetails"));
        m_copyBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
        m_copyBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
        m_copyBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
        m_copyBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
        m_copyBtn->onClick=[this]{juce::SystemClipboard::copyTextToClipboard(m_details);};
        addAndMakeVisible(*m_copyBtn);
    }
    m_okBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.error.ok"));
    m_okBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_okBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_okBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_okBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_okBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_okBtn);
}
void ErrorDialog::showError(const juce::String& message,const juce::String& details,juce::Component*){
    auto*content=new ErrorDialog(message,details);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.error.windowTitle");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    opts.runModal();
}
void ErrorDialog::paint(juce::Graphics& g){
    juce::ColourGradient bgGrad(juce::Colour(0xFF0D0D14),0.0f,0.0f,
        juce::Colour(0xFF0E0810),0.0f,static_cast<float>(getHeight()),false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    g.setColour(Colors::error().withAlpha(0.04f));
    g.fillEllipse(-40.0f,-40.0f,180.0f,180.0f);

    g.setColour(Colors::error().withAlpha(0.02f));
    g.fillEllipse(static_cast<float>(getWidth())-60.0f,
                  static_cast<float>(getHeight())-60.0f,120.0f,120.0f);

    auto area=getLocalBounds().reduced(24);

    auto headerPanel=juce::Rectangle<float>(16.0f,16.0f,
        static_cast<float>(getWidth())-32.0f,56.0f);
    g.setColour(Colors::error().withAlpha(0.05f));
    g.fillRoundedRectangle(headerPanel,8.0f);
    g.setColour(Colors::glassWhite());
    g.fillRoundedRectangle(headerPanel,8.0f);
    g.setColour(Colors::error().withAlpha(0.15f));
    g.drawRoundedRectangle(headerPanel,8.0f,0.5f);

    float iconX=static_cast<float>(area.getX())+12.0f;
    float iconY=static_cast<float>(area.getY())+10.0f;

    g.setColour(Colors::error().withAlpha(0.15f));
    g.fillEllipse(iconX-4.0f,iconY-4.0f,44.0f,44.0f);

    g.setColour(Colors::error().withAlpha(0.2f));
    g.fillEllipse(iconX+2.0f,iconY+2.0f,32.0f,32.0f);

    g.setColour(Colors::error());
    g.setFont(juce::Font(28.0f,juce::Font::bold));
    g.drawText("!",static_cast<int>(iconX)+2,static_cast<int>(iconY)+2,32,32,juce::Justification::centred);

    g.setColour(Colors::error().withAlpha(0.6f));
    g.fillRect(static_cast<float>(area.getX())+52.0f,static_cast<float>(area.getY())+6.0f,3.0f,16.0f);

    g.setColour(Colors::error());
    g.setFont(juce::Font(14.0f,juce::Font::bold));
    g.drawFittedText(m_message,area.getX()+62,area.getY(),area.getWidth()-62,50,juce::Justification::centredLeft,3);

    if(m_details.isNotEmpty()){
        float detailsY=static_cast<float>(area.getY())+60.0f;
        float detailsH=static_cast<float>(getHeight())-detailsY-54.0f;
        auto detailsPanel=juce::Rectangle<float>(16.0f,detailsY-4.0f,
            static_cast<float>(getWidth())-32.0f,detailsH+8.0f);
        g.setColour(Colors::glassWhite());
        g.fillRoundedRectangle(detailsPanel,6.0f);
        g.setColour(Colors::glassBorder());
        g.drawRoundedRectangle(detailsPanel,6.0f,0.5f);
    }

    float footerY=static_cast<float>(getHeight())-50.0f;
    g.setColour(Colors::error().withAlpha(0.06f));
    g.fillRect(0.0f,footerY,static_cast<float>(getWidth()),1.0f);

    g.setColour(Colors::error().withAlpha(0.08f));
    g.drawRect(getLocalBounds().toFloat(),0.5f);
}
void ErrorDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    area.removeFromTop(56); // header
    auto btnRow=area.removeFromBottom(30);
    m_okBtn->setBounds(btnRow.removeFromRight(80));
    if(m_copyBtn){btnRow.removeFromRight(8);m_copyBtn->setBounds(btnRow.removeFromRight(140));}
    area.removeFromBottom(8);
    if(m_detailsEdit)m_detailsEdit->setBounds(area);
}
} // namespace BeatMate::UI
