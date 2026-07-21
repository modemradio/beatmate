#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class StarRatingWidget : public juce::Component {
public:
    StarRatingWidget(); ~StarRatingWidget() override=default;
    double rating() const{return m_rating;} void setRating(double rating);
    void setReadOnly(bool readOnly){m_readOnly=readOnly;}
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;void mouseMoveImpl(const juce::MouseEvent& e);void mouseMove(const juce::MouseEvent& e) override{mouseMoveImpl(e);}
    void mouseExit(const juce::MouseEvent&) override;
    class Listener{public:virtual ~Listener()=default;virtual void ratingChanged(double){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    double ratingFromPos(int x) const;
    double m_rating=0.0,m_hoverRating=-1.0;bool m_readOnly=false;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StarRatingWidget)
};
} // namespace BeatMate::UI
