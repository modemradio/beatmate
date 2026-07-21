#include <algorithm>
#include "StarRatingWidget.h"
#include "../../styles/ColorPalette.h"
namespace BeatMate::UI {
StarRatingWidget::StarRatingWidget(){setSize(100,20);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
void StarRatingWidget::setRating(double rating){m_rating=juce::jlimit(0.0,5.0,rating);m_listeners.call([this](Listener&l){l.ratingChanged(m_rating);});repaint();}
double StarRatingWidget::ratingFromPos(int x) const{double starW=getWidth()/5.0;double r=(double)x/starW;return juce::jlimit(0.0,5.0,std::round(r*2.0)/2.0);}
void StarRatingWidget::paint(juce::Graphics& g){
    double displayRating=m_hoverRating>=0?m_hoverRating:m_rating;double starW=(double)getWidth()/5.0;
    for(int i=0;i<5;++i){
        double fill=juce::jlimit(0.0,1.0,displayRating-i);
        float cx=(float)(i*starW+starW/2),cy=(float)(getHeight()/2),r=(float)(std::min(starW-2.0,(double)getHeight()-2.0)/2.0);
        juce::Path star;
        for(int j=0;j<5;++j){
            float angle=(float)(-juce::MathConstants<double>::pi/2.0+j*juce::MathConstants<double>::pi*2.0/5.0);
            float innerAngle=angle+(float)(juce::MathConstants<double>::pi*2.0/10.0);
            if(j==0)star.startNewSubPath(cx+r*std::cos(angle),cy+r*std::sin(angle));
            else star.lineTo(cx+r*std::cos(angle),cy+r*std::sin(angle));
            star.lineTo(cx+r*0.4f*std::cos(innerAngle),cy+r*0.4f*std::sin(innerAngle));
        }
        star.closeSubPath();
        g.setColour(Colors::starEmpty());g.fillPath(star);
        g.setColour(juce::Colour(0xFF555555));g.strokePath(star,juce::PathStrokeType(1.0f));
        if(fill>0){
            g.saveState();g.reduceClipRegion(juce::Rectangle<int>((int)(i*starW),0,(int)(starW*fill),getHeight()));
            g.setColour(Colors::starFilled());g.fillPath(star);g.setColour(Colors::starBorder());g.strokePath(star,juce::PathStrokeType(1.0f));
            g.restoreState();
        }
    }
}
void StarRatingWidget::mouseDown(const juce::MouseEvent& e){if(!m_readOnly)setRating(ratingFromPos(e.x));}
void StarRatingWidget::mouseMoveImpl(const juce::MouseEvent& e){if(!m_readOnly){m_hoverRating=ratingFromPos(e.x);repaint();}}
void StarRatingWidget::mouseExit(const juce::MouseEvent&){m_hoverRating=-1.0;repaint();}
}
