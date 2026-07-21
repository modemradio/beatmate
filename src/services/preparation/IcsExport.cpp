#include "IcsExport.h"

namespace BeatMate::Services::Preparation {

namespace {

juce::String pad2(int v) { return juce::String(v).paddedLeft('0', 2); }

juce::String icsLocalStamp(int64_t unixSeconds)
{
    juce::Time t(unixSeconds * (int64_t) 1000);
    return juce::String(t.getYear())
         + pad2(t.getMonth() + 1) + pad2(t.getDayOfMonth())
         + "T" + pad2(t.getHours()) + pad2(t.getMinutes()) + pad2(t.getSeconds());
}

juce::String icsEscape(const juce::String& s)
{
    return s.replace("\\", "\\\\")
            .replace(";", "\\;")
            .replace(",", "\\,")
            .replace("\n", "\\n")
            .replace("\r", "");
}

juce::String statusToIcs(const std::string& st)
{
    if (st == "cancelled") return "CANCELLED";
    if (st == "planned")   return "TENTATIVE";
    return "CONFIRMED";
}

}

juce::String buildIcsCalendar(const std::vector<EventPlan>& events)
{
    juce::String out;
    out << "BEGIN:VCALENDAR\r\n"
        << "VERSION:2.0\r\n"
        << "PRODID:-//BeatMate//Agenda DJ//FR\r\n"
        << "CALSCALE:GREGORIAN\r\n"
        << "METHOD:PUBLISH\r\n"
        << "X-WR-CALNAME:Agenda DJ BeatMate\r\n";

    const juce::String nowStamp = icsLocalStamp((int64_t) (juce::Time::getCurrentTime().toMilliseconds() / 1000));

    for (const auto& e : events)
    {
        juce::String loc = juce::String::fromUTF8(e.venue.c_str());
        if (! e.city.empty()) loc << (loc.isNotEmpty() ? ", " : "") << juce::String::fromUTF8(e.city.c_str());
        if (! e.address.empty()) loc << (loc.isNotEmpty() ? ", " : "") << juce::String::fromUTF8(e.address.c_str());

        juce::String desc = juce::String::fromUTF8(e.notes.c_str());
        if (e.fee > 0.0)
            desc << (desc.isNotEmpty() ? "\n" : "")
                 << "Cachet : " << juce::String(e.fee, 2) << " " << juce::String::fromUTF8(e.currency.c_str());
        if (! e.style.empty())
            desc << (desc.isNotEmpty() ? "\n" : "") << "Style : " << juce::String::fromUTF8(e.style.c_str());

        const int64_t end = e.endTime > e.startTime ? e.endTime : e.startTime + 3600;

        out << "BEGIN:VEVENT\r\n"
            << "UID:beatmate-" << juce::String(e.id) << "@beatmate.fr\r\n"
            << "DTSTAMP:" << nowStamp << "\r\n"
            << "DTSTART:" << icsLocalStamp(e.startTime) << "\r\n"
            << "DTEND:" << icsLocalStamp(end) << "\r\n"
            << "SUMMARY:" << icsEscape(juce::String::fromUTF8(e.name.c_str())) << "\r\n";
        if (loc.isNotEmpty())  out << "LOCATION:" << icsEscape(loc) << "\r\n";
        if (desc.isNotEmpty()) out << "DESCRIPTION:" << icsEscape(desc) << "\r\n";
        out << "STATUS:" << statusToIcs(e.status) << "\r\n"
            << "END:VEVENT\r\n";
    }

    out << "END:VCALENDAR\r\n";
    return out;
}

juce::String googleCalendarUrl(const EventPlan& e)
{
    juce::String loc = juce::String::fromUTF8(e.venue.c_str());
    if (! e.city.empty()) loc << (loc.isNotEmpty() ? ", " : "") << juce::String::fromUTF8(e.city.c_str());

    const int64_t end = e.endTime > e.startTime ? e.endTime : e.startTime + 3600;

    juce::URL url("https://calendar.google.com/calendar/render");
    url = url.withParameter("action", "TEMPLATE")
             .withParameter("text", juce::String::fromUTF8(e.name.c_str()))
             .withParameter("dates", icsLocalStamp(e.startTime) + "/" + icsLocalStamp(end))
             .withParameter("location", loc)
             .withParameter("details", juce::String::fromUTF8(e.notes.c_str()));
    return url.toString(true);
}

std::vector<EventPlan> parseIcsCalendar(const juce::String& icsText)
{
    std::vector<EventPlan> out;
    auto lines = juce::StringArray::fromLines(icsText);

    auto parseStamp = [](const juce::String& v) -> int64_t {
        juce::String s = v.fromLastOccurrenceOf(":", false, false).trim();
        if (s.length() < 15) return 0;
        const int y = s.substring(0, 4).getIntValue();
        const int mo = s.substring(4, 6).getIntValue();
        const int d = s.substring(6, 8).getIntValue();
        const int h = s.substring(9, 11).getIntValue();
        const int mi = s.substring(11, 13).getIntValue();
        const int se = s.substring(13, 15).getIntValue();
        if (y < 1970) return 0;
        juce::Time t(y, mo - 1, d, h, mi, se, 0, ! s.endsWithChar('Z'));
        return (int64_t) (t.toMilliseconds() / 1000);
    };
    auto unescape = [](const juce::String& v) {
        return v.replace("\\n", "\n").replace("\\,", ",").replace("\\;", ";").replace("\\\\", "\\");
    };

    EventPlan cur;
    bool inEvent = false;
    for (auto& raw : lines)
    {
        const juce::String line = raw.trimEnd();
        if (line.startsWith("BEGIN:VEVENT")) { cur = EventPlan(); inEvent = true; }
        else if (line.startsWith("END:VEVENT")) { if (inEvent && ! cur.name.empty()) out.push_back(cur); inEvent = false; }
        else if (! inEvent) continue;
        else if (line.startsWith("SUMMARY:"))  cur.name  = unescape(line.fromFirstOccurrenceOf(":", false, false)).toStdString();
        else if (line.startsWith("LOCATION:")) cur.venue = unescape(line.fromFirstOccurrenceOf(":", false, false)).toStdString();
        else if (line.startsWith("DESCRIPTION:")) cur.notes = unescape(line.fromFirstOccurrenceOf(":", false, false)).toStdString();
        else if (line.startsWith("DTSTART")) cur.startTime = parseStamp(line);
        else if (line.startsWith("DTEND"))   cur.endTime   = parseStamp(line);
        else if (line.startsWith("STATUS:")) {
            const auto s = line.fromFirstOccurrenceOf(":", false, false).trim();
            cur.status = s == "CANCELLED" ? "cancelled" : (s == "TENTATIVE" ? "planned" : "confirmed");
        }
    }
    return out;
}

}
