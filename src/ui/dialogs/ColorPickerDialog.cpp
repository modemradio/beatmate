#include "ColorPickerDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
namespace BeatMate::UI {
ColorPickerDialog::ColorPickerDialog(juce::Colour initial):m_color(initial){
    setSize(400,450);
    m_swatches={
        juce::Colour(0xFFFF0000),juce::Colour(0xFFFF8800),juce::Colour(0xFFFFFF00),juce::Colour(0xFF00FF00),
        juce::Colour(0xFF00FFFF),juce::Colour(0xFF0088FF),juce::Colour(0xFF8800FF),juce::Colour(0xFFFF00FF),
        juce::Colour(0xFFFF4444),juce::Colour(0xFFFFB800),juce::Colour(0xFFFFD700),juce::Colour(0xFF00FFA3),
        juce::Colour(0xFF00D9FF),juce::Colour(0xFF0078D4),juce::Colour(0xFFB048FF),juce::Colour(0xFFFF6B9D),
        juce::Colour(0xFFFFFFFF),juce::Colour(0xFFCCCCCC),juce::Colour(0xFF999999),juce::Colour(0xFF666666),
        juce::Colour(0xFF333333),juce::Colour(0xFF1A1A1A),juce::Colour(0xFF0A0A0A),juce::Colour(0xFF000000)
    };
    for(int i=0;i<m_swatches.size();++i){
        auto*btn=new juce::TextButton("");
        btn->setColour(juce::TextButton::buttonColourId,m_swatches[i]);
        btn->setColour(juce::TextButton::buttonOnColourId,m_swatches[i].brighter(0.2f));
        btn->setColour(juce::TextButton::textColourOffId,juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::textColourOnId,juce::Colours::transparentBlack);
        auto c=m_swatches[i];
        btn->onClick=[this,c]{
            m_color=c;
            m_hexEdit->setText(m_color.toDisplayString(false).toUpperCase(),false);
            updatePreview();
            if(onColorChanged)onColorChanged(c);
        };
        addAndMakeVisible(btn);
        m_swatchBtns.add(btn);
    }
    m_hexEdit=std::make_unique<juce::TextEditor>();
    m_hexEdit->setText(m_color.toDisplayString(false).toUpperCase());
    m_hexEdit->setColour(juce::TextEditor::backgroundColourId,Colors::bgLight());
    m_hexEdit->setColour(juce::TextEditor::textColourId,Colors::textPrimary());
    m_hexEdit->setColour(juce::TextEditor::outlineColourId,Colors::border());
    m_hexEdit->setInputRestrictions(7,"0123456789ABCDEFabcdef#");
    m_hexEdit->onTextChange=[this]{
        juce::String txt=m_hexEdit->getText();
        if(!txt.startsWith("#"))txt="#"+txt;
        juce::Colour c=juce::Colour::fromString("FF"+txt.substring(1));
        if(txt.length()==7){m_color=c;updatePreview();if(onColorChanged)onColorChanged(c);}
    };
    addAndMakeVisible(*m_hexEdit);

    m_cancelBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.color.cancel"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_cancelBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_cancelBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(0);};
    addAndMakeVisible(*m_cancelBtn);

    m_okBtn=std::make_unique<juce::TextButton>(BM_TJ("dialog.color.ok"));
    m_okBtn->setColour(juce::TextButton::buttonColourId,Colors::primary());
    m_okBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primaryHover());
    m_okBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_okBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_okBtn->onClick=[this]{if(auto*dw=findParentComponentOfClass<juce::DialogWindow>())dw->exitModalState(1);};
    addAndMakeVisible(*m_okBtn);
}
void ColorPickerDialog::updatePreview(){repaint();}
void ColorPickerDialog::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());
    float cx=m_wheelArea.getCentreX(),cy=m_wheelArea.getCentreY();
    float radius=juce::jmin(m_wheelArea.getWidth(),m_wheelArea.getHeight())*0.45f;
    for(int angle=0;angle<360;++angle){
        juce::Colour c=juce::Colour::fromHSV((float)angle/360.0f,1.0f,1.0f,1.0f);
        g.setColour(c);
        float rad=(float)angle*juce::MathConstants<float>::pi/180.0f;
        float x1=cx+(radius-20.0f)*std::cos(rad),y1=cy+(radius-20.0f)*std::sin(rad);
        float x2=cx+radius*std::cos(rad),y2=cy+radius*std::sin(rad);
        g.drawLine(x1,y1,x2,y2,3.0f);
    }
    g.setColour(m_color);
    auto previewRect=juce::Rectangle<float>((float)getWidth()-64.0f-24.0f,m_wheelArea.getBottom()+100.0f,40.0f,30.0f);
    g.fillRoundedRectangle(previewRect,4.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle(previewRect,4.0f,1.0f);
    g.setColour(Colors::textSecondary());g.setFont(12.0f);
    g.drawText(BM_TJ("dialog.color.hex"),24.0f,m_wheelArea.getBottom()+104.0f,40.0f,20.0f,juce::Justification::centredLeft);
}
void ColorPickerDialog::mouseDown(const juce::MouseEvent& e){
    if(m_wheelArea.contains(e.position)){
        float cx=m_wheelArea.getCentreX(),cy=m_wheelArea.getCentreY();
        float dx=e.position.x-cx,dy=e.position.y-cy;
        float angle=std::atan2(dy,dx);
        if(angle<0)angle+=juce::MathConstants<float>::twoPi;
        m_color=juce::Colour::fromHSV(angle/juce::MathConstants<float>::twoPi,1.0f,1.0f,1.0f);
        m_hexEdit->setText(m_color.toDisplayString(false).toUpperCase(),false);
        updatePreview();
        if(onColorChanged)onColorChanged(m_color);
    }
}
int ColorPickerDialog::showDialog(juce::Component*,juce::Colour initial,std::function<void(juce::Colour)> callback){
    auto*content=new ColorPickerDialog(initial);
    content->onColorChanged=callback;
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);opts.dialogTitle=BM_TJ("dialog.color.windowTitle");
    opts.dialogBackgroundColour=Colors::bgDarker();opts.resizable=false;opts.useNativeTitleBar=true;
    return opts.runModal();
}
void ColorPickerDialog::resized(){
    auto area=getLocalBounds().reduced(24);
    m_wheelArea=juce::Rectangle<float>((float)area.getCentreX()-100.0f,(float)area.getY(),200.0f,200.0f);
    area.removeFromTop(210);
    auto swatchArea=area.removeFromTop(100);
    int cols=8,size=30,gap=4;
    for(int i=0;i<m_swatchBtns.size();++i){
        int row=i/cols,col=i%cols;
        m_swatchBtns[i]->setBounds(swatchArea.getX()+col*(size+gap),swatchArea.getY()+row*(size+gap),size,size);
    }
    area.removeFromTop(8);
    auto hexRow=area.removeFromTop(30);
    hexRow.removeFromLeft(44); // "Hex:" label space
    m_hexEdit->setBounds(hexRow.removeFromLeft(200));
    area.removeFromTop(16);
    auto btnRow=area.removeFromBottom(30);
    m_okBtn->setBounds(btnRow.removeFromRight(80));btnRow.removeFromRight(8);
    m_cancelBtn->setBounds(btnRow.removeFromRight(80));
}
} // namespace BeatMate::UI
