#include "AgendaExport.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace BeatMate::Services::Preparation {

namespace {

juce::String pad2(int v) { return juce::String(v).paddedLeft('0', 2); }

const char* jourFr(int dowSun0)
{
    static const char* j[] = { "Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam" };
    return j[(dowSun0 % 7 + 7) % 7];
}

const char* moisFr(int m0)
{
    static const char* m[] = { "janvier", "f\xc3\xa9vrier", "mars", "avril", "mai", "juin",
                               "juillet", "ao\xc3\xbbt", "septembre", "octobre", "novembre", "d\xc3\xa9""cembre" };
    return (m0 >= 0 && m0 < 12) ? m[m0] : "";
}

juce::String frDate(int64_t unixSeconds)
{
    if (unixSeconds <= 0) return "-";
    juce::Time t(unixSeconds * (int64_t) 1000);
    return juce::String(jourFr(t.getDayOfWeek())) + " " + pad2(t.getDayOfMonth()) + "/"
         + pad2(t.getMonth() + 1) + "/" + juce::String(t.getYear());
}

juce::String frTime(int64_t unixSeconds)
{
    if (unixSeconds <= 0) return "";
    juce::Time t(unixSeconds * (int64_t) 1000);
    return pad2(t.getHours()) + ":" + pad2(t.getMinutes());
}

juce::String statusFr(const std::string& s)
{
    if (s == "cancelled") return juce::String::fromUTF8("Annul\xc3\xa9");
    if (s == "planned")   return juce::String::fromUTF8("\xc3\x80 confirmer");
    if (s == "completed") return juce::String::fromUTF8("Pass\xc3\xa9");
    return juce::String::fromUTF8("Confirm\xc3\xa9");
}

const char* statusHex(const std::string& s)
{
    if (s == "cancelled") return "#9ca3af";
    if (s == "planned")   return "#f59e0b";
    if (s == "completed") return "#6b7280";
    return "#10b981";
}

juce::String htmlEscape(const juce::String& s)
{
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;");
}

std::vector<EventPlan> sortedByDate(const std::vector<EventPlan>& in)
{
    std::vector<EventPlan> v = in;
    std::sort(v.begin(), v.end(), [](const EventPlan& a, const EventPlan& b) { return a.startTime < b.startTime; });
    return v;
}

} // namespace

std::string buildAgendaHtml(const std::vector<EventPlan>& events)
{
    const auto v = sortedByDate(events);
    double total = 0.0; int confirmes = 0;
    for (const auto& e : v) { total += e.fee; if (e.status == "confirmed") ++confirmes; }

    juce::Time now = juce::Time::getCurrentTime();
    const juce::String genLe = juce::String(now.getDayOfMonth()) + " " + juce::String::fromUTF8(moisFr(now.getMonth()))
                             + " " + juce::String(now.getYear());

    juce::String h;
    h << "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\">\n"
      << "<title>Agenda DJ \xe2\x80\x94 BeatMate</title>\n<style>\n"
      << "*{box-sizing:border-box}body{font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;color:#1f2937;margin:0;padding:32px;background:#fff}\n"
      << "h1{font-size:26px;margin:0 0 4px}.sub{color:#6b7280;font-size:13px;margin-bottom:20px}\n"
      << ".cards{display:flex;gap:14px;margin-bottom:22px;flex-wrap:wrap}\n"
      << ".card{background:#f3f4f6;border-radius:12px;padding:12px 18px;min-width:130px}\n"
      << ".card .n{font-size:22px;font-weight:700}.card .l{font-size:12px;color:#6b7280}\n"
      << "table{width:100%;border-collapse:collapse;font-size:13px}\n"
      << "th{text-align:left;padding:10px 12px;background:#111827;color:#fff;font-weight:600}\n"
      << "th:first-child{border-radius:8px 0 0 0}th:last-child{border-radius:0 8px 0 0}\n"
      << "td{padding:10px 12px;border-bottom:1px solid #e5e7eb}\n"
      << "tr:hover td{background:#f9fafb}\n"
      << ".pill{display:inline-block;padding:3px 10px;border-radius:999px;color:#fff;font-size:11px;font-weight:700}\n"
      << ".fee{font-weight:700;white-space:nowrap}\n"
      << "@media print{body{padding:0}th{background:#111827!important;-webkit-print-color-adjust:exact;print-color-adjust:exact}}\n"
      << "</style></head><body>\n"
      << "<h1>Agenda DJ</h1>\n<div class=\"sub\">G\xc3\xa9n\xc3\xa9r\xc3\xa9 le " << genLe
      << " \xc2\xb7 BeatMate</div>\n";

    h << "<div class=\"cards\">"
      << "<div class=\"card\"><div class=\"n\">" << juce::String((int) v.size()) << "</div><div class=\"l\">Dates</div></div>"
      << "<div class=\"card\"><div class=\"n\">" << juce::String(confirmes) << "</div><div class=\"l\">Confirm\xc3\xa9""es</div></div>"
      << "<div class=\"card\"><div class=\"n\">" << juce::String(total, 0) << " \xe2\x82\xac</div><div class=\"l\">Cachet total</div></div>"
      << "</div>\n";

    h << "<table><thead><tr><th>Date</th><th>Horaire</th><th>Prestation</th><th>Lieu</th><th>Ville</th><th>Cachet</th><th>Statut</th></tr></thead><tbody>\n";
    for (const auto& e : v)
    {
        juce::String horaire = frTime(e.startTime);
        if (e.endTime > e.startTime) horaire << " \xe2\x80\x93 " << frTime(e.endTime);
        juce::String fee = e.fee > 0.0 ? juce::String(e.fee, 0) + " " + juce::String::fromUTF8(e.currency.c_str()) : "\xe2\x80\x94";
        h << "<tr><td>" << htmlEscape(frDate(e.startTime)) << "</td>"
          << "<td>" << htmlEscape(horaire) << "</td>"
          << "<td><b>" << htmlEscape(juce::String::fromUTF8(e.name.c_str())) << "</b>";
        if (! e.style.empty()) h << "<br><span style=\"color:#6b7280;font-size:11px\">" << htmlEscape(juce::String::fromUTF8(e.style.c_str())) << "</span>";
        h << "</td>"
          << "<td>" << htmlEscape(juce::String::fromUTF8(e.venue.c_str())) << "</td>"
          << "<td>" << htmlEscape(juce::String::fromUTF8(e.city.c_str())) << "</td>"
          << "<td class=\"fee\">" << htmlEscape(fee) << "</td>"
          << "<td><span class=\"pill\" style=\"background:" << statusHex(e.status) << "\">"
          << htmlEscape(statusFr(e.status)) << "</span></td></tr>\n";
    }
    h << "</tbody></table>\n</body></html>\n";
    return h.toStdString();
}

std::string buildAgendaCsv(const std::vector<EventPlan>& events)
{
    const auto v = sortedByDate(events);
    auto field = [](const juce::String& s) -> juce::String {
        if (s.containsAnyOf(";\"\n\r")) return "\"" + s.replace("\"", "\"\"") + "\"";
        return s;
    };
    juce::String c;
    c << juce::String::fromUTF8("Date;D\xc3\xa9""but;Fin;Prestation;Style;Lieu;Ville;Cachet;Devise;Statut;Notes\r\n");
    for (const auto& e : v)
    {
        c << field(frDate(e.startTime)) << ";"
          << field(frTime(e.startTime)) << ";"
          << field(frTime(e.endTime)) << ";"
          << field(juce::String::fromUTF8(e.name.c_str())) << ";"
          << field(juce::String::fromUTF8(e.style.c_str())) << ";"
          << field(juce::String::fromUTF8(e.venue.c_str())) << ";"
          << field(juce::String::fromUTF8(e.city.c_str())) << ";"
          << field(e.fee > 0.0 ? juce::String(e.fee, 2) : juce::String()) << ";"
          << field(juce::String::fromUTF8(e.currency.c_str())) << ";"
          << field(statusFr(e.status)) << ";"
          << field(juce::String::fromUTF8(e.notes.c_str()).replace("\n", " ")) << "\r\n";
    }
    return std::string("\xEF\xBB\xBF") + c.toStdString();
}

namespace {

std::string pdfStr(const juce::String& s)
{
    std::string out;
    const int n = s.length();
    for (int i = 0; i < n; ++i)
    {
        const juce::juce_wchar ch = s[i];
        unsigned char b;
        if (ch == 0x2013 || ch == 0x2014) b = '-';          // tirets
        else if (ch == 0x2019 || ch == 0x2018) b = '\'';    // apostrophes typographiques
        else if (ch == 0x20AC) { out += "EUR"; continue; }  // euro
        else if (ch == 0x00B7 || ch == 0x2022) b = '-';     // puce
        else if (ch < 0x100) b = (unsigned char) ch;
        else b = '?';
        if (b == '(' || b == ')' || b == '\\') out += '\\';
        out += (char) b;
    }
    return out;
}

std::string inum(double v) { char buf[32]; std::snprintf(buf, sizeof buf, "%d", (int) (v + (v < 0 ? -0.5 : 0.5))); return buf; }

} // namespace

std::string buildAgendaPdf(const std::vector<EventPlan>& events)
{
    const auto v = sortedByDate(events);
    double total = 0.0;
    for (const auto& e : v) total += e.fee;

    juce::Time now = juce::Time::getCurrentTime();
    const juce::String subtitle = juce::String::fromUTF8("G\xc3\xa9n\xc3\xa9r\xc3\xa9 le ") + juce::String(now.getDayOfMonth())
        + " " + juce::String::fromUTF8(moisFr(now.getMonth())) + " " + juce::String(now.getYear())
        + juce::String::fromUTF8("  \xc2\xb7  ") + juce::String((int) v.size()) + " dates"
        + (total > 0 ? juce::String::fromUTF8("  \xc2\xb7  Cachet total ") + juce::String(total, 0) + juce::String::fromUTF8(" \xe2\x82\xac") : juce::String());

    const double top = 795, bottom = 55, leftX = 50, leading = 16;

    std::vector<std::string> pages;
    std::string cur;
    double y = 0;
    bool open = false;

    auto beginPage = [&](bool first)
    {
        cur.clear();
        cur += "BT\n";
        y = top;
        if (first)
        {
            cur += "/F2 18 Tf\n1 0 0 1 " + inum(leftX) + " " + inum(y) + " Tm\n(" + pdfStr("Agenda DJ - BeatMate") + ") Tj\n";
            y -= 24;
            cur += "/F1 10 Tf\n1 0 0 1 " + inum(leftX) + " " + inum(y) + " Tm\n(" + pdfStr(subtitle) + ") Tj\n";
            y -= 26;
        }
        cur += "/F1 11 Tf\n1 0 0 1 " + inum(leftX) + " " + inum(y) + " Tm\n" + inum(leading) + " TL\n";
        open = true;
    };
    auto endPage = [&]() { if (open) { cur += "ET\n"; pages.push_back(cur); open = false; } };

    beginPage(true);
    for (const auto& e : v)
    {
        if (y < bottom) { endPage(); beginPage(false); }
        juce::String line = frDate(e.startTime) + "   " + frTime(e.startTime);
        if (e.endTime > e.startTime) line << "-" << frTime(e.endTime);
        line << "   " << juce::String::fromUTF8(e.name.c_str());
        if (! e.venue.empty()) line << "  |  " << juce::String::fromUTF8(e.venue.c_str());
        if (! e.city.empty())  line << ", " << juce::String::fromUTF8(e.city.c_str());
        if (e.fee > 0.0) line << "  |  " << juce::String(e.fee, 0) << " " << juce::String::fromUTF8(e.currency.c_str());
        line << "   [" << statusFr(e.status) << "]";
        if (line.length() > 98) line = line.substring(0, 96) + juce::String::fromUTF8("\xe2\x80\xa6");
        cur += "(" + pdfStr(line) + ") Tj T*\n";
        y -= leading;
    }
    if (v.empty())
        cur += "(" + pdfStr(juce::String::fromUTF8("Aucune date enregistr\xc3\xa9""e.")) + ") Tj T*\n";
    endPage();

    std::vector<std::string> objs;
    objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");

    std::string kids;
    for (size_t k = 0; k < pages.size(); ++k)
        kids += (kids.empty() ? "" : " ") + std::to_string(5 + 2 * (int) k) + " 0 R";
    objs.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size())
                   + " /MediaBox [0 0 595 842] >>");
    objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");

    for (size_t k = 0; k < pages.size(); ++k)
    {
        const int contentNum = 6 + 2 * (int) k;
        objs.push_back("<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents "
                       + std::to_string(contentNum) + " 0 R >>");
        objs.push_back("<< /Length " + std::to_string(pages[k].size()) + " >>\nstream\n" + pages[k] + "\nendstream");
    }

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets(objs.size() + 1, 0);
    for (size_t i = 0; i < objs.size(); ++i)
    {
        offsets[i + 1] = pdf.size();
        pdf += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    const size_t xrefOff = pdf.size();
    pdf += "xref\n0 " + std::to_string(objs.size() + 1) + "\n";
    pdf += "0000000000 65535 f\r\n";
    for (size_t i = 1; i <= objs.size(); ++i)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n\r\n", offsets[i]);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
         + std::to_string(xrefOff) + "\n%%EOF\n";
    return pdf;
}

namespace {

juce::String xmlEscape(const juce::String& s)
{
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace("\"", "&quot;").replace("'", "&apos;");
}

juce::String docxCell(const juce::String& text, bool bold, const char* fillHex, const char* colorHex)
{
    juce::String rpr;
    if (bold)     rpr << "<w:b/>";
    if (colorHex) rpr << "<w:color w:val=\"" << colorHex << "\"/>";
    juce::String tcpr = "<w:tcPr><w:tcMar><w:top w:w=\"40\" w:type=\"dxa\"/><w:left w:w=\"80\" w:type=\"dxa\"/>"
                        "<w:bottom w:w=\"40\" w:type=\"dxa\"/><w:right w:w=\"80\" w:type=\"dxa\"/></w:tcMar>";
    if (fillHex) tcpr << "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" << fillHex << "\"/>";
    tcpr << "</w:tcPr>";
    return "<w:tc>" + tcpr + "<w:p><w:pPr><w:spacing w:after=\"0\"/></w:pPr><w:r><w:rPr>" + rpr
         + "</w:rPr><w:t xml:space=\"preserve\">" + xmlEscape(text) + "</w:t></w:r></w:p></w:tc>";
}

} // namespace

std::string buildAgendaDocx(const std::vector<EventPlan>& events)
{
    const auto v = sortedByDate(events);
    double total = 0.0;
    for (const auto& e : v) total += e.fee;

    juce::Time now = juce::Time::getCurrentTime();
    const juce::String genLe = juce::String(now.getDayOfMonth()) + " " + juce::String::fromUTF8(moisFr(now.getMonth()))
                             + " " + juce::String(now.getYear());

    juce::String body;
    body << "<w:p><w:pPr><w:spacing w:after=\"40\"/></w:pPr>"
         << "<w:r><w:rPr><w:b/><w:sz w:val=\"40\"/></w:rPr><w:t>Agenda DJ</w:t></w:r></w:p>";
    juce::String sub = juce::String::fromUTF8("G\xc3\xa9n\xc3\xa9r\xc3\xa9 le ") + genLe
                     + juce::String::fromUTF8("  \xe2\x80\xa2  ") + juce::String((int) v.size()) + " dates";
    if (total > 0) sub << juce::String::fromUTF8("  \xe2\x80\xa2  Cachet total ") << juce::String(total, 0) << " EUR";
    body << "<w:p><w:pPr><w:spacing w:after=\"200\"/></w:pPr>"
         << "<w:r><w:rPr><w:color w:val=\"6B7280\"/><w:sz w:val=\"18\"/></w:rPr><w:t xml:space=\"preserve\">"
         << xmlEscape(sub) << "</w:t></w:r></w:p>";

    juce::String tbl = "<w:tbl><w:tblPr><w:tblW w:w=\"5000\" w:type=\"pct\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9D9D9\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9D9D9\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9D9D9\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9D9D9\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9D9D9\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9D9D9\"/>"
        "</w:tblBorders></w:tblPr>";

    tbl << "<w:tr>"
        << docxCell(juce::String::fromUTF8("Date"), true, "111827", "FFFFFF")
        << docxCell(juce::String::fromUTF8("Horaire"), true, "111827", "FFFFFF")
        << docxCell(juce::String::fromUTF8("Prestation"), true, "111827", "FFFFFF")
        << docxCell(juce::String::fromUTF8("Lieu"), true, "111827", "FFFFFF")
        << docxCell(juce::String::fromUTF8("Ville"), true, "111827", "FFFFFF")
        << docxCell(juce::String::fromUTF8("Cachet"), true, "111827", "FFFFFF")
        << docxCell(juce::String::fromUTF8("Statut"), true, "111827", "FFFFFF")
        << "</w:tr>";

    for (const auto& e : v)
    {
        juce::String horaire = frTime(e.startTime);
        if (e.endTime > e.startTime) horaire << " - " << frTime(e.endTime);
        juce::String fee = e.fee > 0.0 ? juce::String(e.fee, 0) + " " + juce::String::fromUTF8(e.currency.c_str()) : juce::String();
        juce::String nom = juce::String::fromUTF8(e.name.c_str());
        if (! e.style.empty()) nom << " (" << juce::String::fromUTF8(e.style.c_str()) << ")";
        const char* stc = e.status == "cancelled" ? "9CA3AF" : (e.status == "planned" ? "B45309"
                        : (e.status == "completed" ? "6B7280" : "059669"));
        tbl << "<w:tr>"
            << docxCell(frDate(e.startTime), false, nullptr, nullptr)
            << docxCell(horaire, false, nullptr, nullptr)
            << docxCell(nom, true, nullptr, nullptr)
            << docxCell(juce::String::fromUTF8(e.venue.c_str()), false, nullptr, nullptr)
            << docxCell(juce::String::fromUTF8(e.city.c_str()), false, nullptr, nullptr)
            << docxCell(fee, false, nullptr, nullptr)
            << docxCell(statusFr(e.status), true, nullptr, stc)
            << "</w:tr>";
    }
    tbl << "</w:tbl>";

    juce::String document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        + body + tbl +
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"720\" w:right=\"720\" w:bottom=\"720\" w:left=\"720\" w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/>"
        "</w:sectPr></w:body></w:document>";

    const juce::String contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";

    const juce::String rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";

    auto addPart = [](juce::ZipFile::Builder& b, const juce::String& path, const juce::String& xml)
    {
        auto utf8 = xml.toRawUTF8();
        const size_t len = std::strlen(utf8);
        b.addEntry(new juce::MemoryInputStream(utf8, len, true), 9, path, juce::Time::getCurrentTime());
    };

    juce::ZipFile::Builder builder;
    addPart(builder, "[Content_Types].xml", contentTypes);
    addPart(builder, "_rels/.rels", rels);
    addPart(builder, "word/document.xml", document);

    juce::MemoryOutputStream out;
    builder.writeToStream(out, nullptr);
    return std::string(static_cast<const char*>(out.getData()), out.getDataSize());
}

} // namespace BeatMate::Services::Preparation
