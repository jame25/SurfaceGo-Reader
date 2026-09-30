#include "htmltext.h"

#include <QSet>

namespace {

const QHash<QString, char32_t> &namedEntities()
{
    static const QHash<QString, char32_t> map = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0xA0}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009},
        {"shy", 0xAD}, {"zwnj", 0x200C}, {"zwj", 0x200D},
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"minus", 0x2212},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E},
        {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsaquo", 0x2039}, {"rsaquo", 0x203A},
        {"bull", 0x2022}, {"middot", 0xB7}, {"prime", 0x2032}, {"Prime", 0x2033},
        {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"sect", 0xA7}, {"para", 0xB6},
        {"deg", 0xB0}, {"plusmn", 0xB1}, {"times", 0xD7}, {"divide", 0xF7},
        {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE},
        {"sup1", 0xB9}, {"sup2", 0xB2}, {"sup3", 0xB3},
        {"cent", 0xA2}, {"pound", 0xA3}, {"euro", 0x20AC}, {"yen", 0xA5}, {"curren", 0xA4},
        {"iexcl", 0xA1}, {"iquest", 0xBF}, {"dagger", 0x2020}, {"Dagger", 0x2021},
        {"agrave", 0xE0}, {"aacute", 0xE1}, {"acirc", 0xE2}, {"atilde", 0xE3}, {"auml", 0xE4}, {"aring", 0xE5}, {"aelig", 0xE6},
        {"ccedil", 0xE7}, {"egrave", 0xE8}, {"eacute", 0xE9}, {"ecirc", 0xEA}, {"euml", 0xEB},
        {"igrave", 0xEC}, {"iacute", 0xED}, {"icirc", 0xEE}, {"iuml", 0xEF}, {"eth", 0xF0}, {"ntilde", 0xF1},
        {"ograve", 0xF2}, {"oacute", 0xF3}, {"ocirc", 0xF4}, {"otilde", 0xF5}, {"ouml", 0xF6}, {"oslash", 0xF8},
        {"ugrave", 0xF9}, {"uacute", 0xFA}, {"ucirc", 0xFB}, {"uuml", 0xFC}, {"yacute", 0xFD}, {"yuml", 0xFF},
        {"szlig", 0xDF}, {"thorn", 0xFE}, {"oelig", 0x153}, {"OElig", 0x152},
        {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acirc", 0xC2}, {"Atilde", 0xC3}, {"Auml", 0xC4}, {"Aring", 0xC5}, {"AElig", 0xC6},
        {"Ccedil", 0xC7}, {"Egrave", 0xC8}, {"Eacute", 0xC9}, {"Ecirc", 0xCA}, {"Euml", 0xCB},
        {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icirc", 0xCE}, {"Iuml", 0xCF}, {"ETH", 0xD0}, {"Ntilde", 0xD1},
        {"Ograve", 0xD2}, {"Oacute", 0xD3}, {"Ocirc", 0xD4}, {"Otilde", 0xD5}, {"Ouml", 0xD6}, {"Oslash", 0xD8},
        {"Ugrave", 0xD9}, {"Uacute", 0xDA}, {"Ucirc", 0xDB}, {"Uuml", 0xDC}, {"Yacute", 0xDD}, {"THORN", 0xDE},
        {"alpha", 0x3B1}, {"beta", 0x3B2}, {"gamma", 0x3B3}, {"delta", 0x3B4}, {"pi", 0x3C0}, {"mu", 0x3BC},
        {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193},
    };
    return map;
}

const QSet<QString> &blockTags()
{
    static const QSet<QString> set = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol", "blockquote", "pre",
        "section", "article", "header", "footer", "aside", "nav", "table", "tr", "td", "th",
        "dt", "dd", "dl", "figure", "figcaption", "hr", "br", "body", "html", "main", "address",
        "caption", "center", "hgroup", "details", "summary", "tbody", "thead", "tfoot",
    };
    return set;
}

bool isHeadingTag(const QString &name)
{
    return name.size() == 2 && name.at(0) == 'h' && name.at(1) >= '1' && name.at(1) <= '6';
}

void appendCodepoint(QString &out, char32_t cp)
{
    if (cp == 0 || cp > 0x10FFFF)
        return;
    out.append(QString::fromUcs4(&cp, 1));
}

// Decodes the entity starting at html[i] == '&'. Returns chars consumed (0 = not an entity).
int decodeEntityAt(const QString &html, int i, QString &out)
{
    const int n = int(html.size());
    int j = i + 1;
    const int limit = qMin(n, i + 12);
    while (j < limit && html.at(j) != ';' && html.at(j) != '&' && !html.at(j).isSpace() && html.at(j) != '<')
        ++j;
    if (j >= limit || html.at(j) != ';')
        return 0;
    const QString name = html.mid(i + 1, j - i - 1);
    if (name.isEmpty())
        return 0;
    if (name.at(0) == '#') {
        bool ok = false;
        uint cp = 0;
        if (name.size() > 1 && (name.at(1) == 'x' || name.at(1) == 'X'))
            cp = name.mid(2).toUInt(&ok, 16);
        else
            cp = name.mid(1).toUInt(&ok, 10);
        if (!ok)
            return 0;
        appendCodepoint(out, cp);
        return j - i + 1;
    }
    auto it = namedEntities().constFind(name);
    if (it == namedEntities().constEnd()) {
        auto lower = namedEntities().constFind(name.toLower());
        if (lower == namedEntities().constEnd())
            return 0;
        appendCodepoint(out, lower.value());
    } else {
        appendCodepoint(out, it.value());
    }
    return j - i + 1;
}

} // namespace

QString decodeHtmlEntities(const QString &s)
{
    if (!s.contains(QLatin1Char('&')))
        return s;
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) == '&') {
            const int used = decodeEntityAt(s, i, out);
            if (used > 0) {
                i += used - 1;
                continue;
            }
        }
        out.append(s.at(i));
    }
    return out;
}

HtmlExtractResult extractHtmlText(const QString &html)
{
    HtmlExtractResult result;
    QString buf;
    QStringList pendingAnchors;
    int headingDepth = 0;
    int skipDepth = 0;   // inside <head>, <title>, <svg>...
    bool bufIsHeading = false;

    auto flush = [&]() {
        QString text = normalizeWhitespace(buf);
        buf.clear();
        const bool heading = bufIsHeading || headingDepth > 0;
        bufIsHeading = false;
        if (text.isEmpty())
            return;
        result.paragraphs.append({text, heading});
        const int idx = int(result.paragraphs.size()) - 1;
        for (const QString &a : std::as_const(pendingAnchors))
            result.anchors.insert(a, idx);
        pendingAnchors.clear();
    };

    const int n = int(html.size());
    int i = 0;
    while (i < n) {
        const QChar c = html.at(i);
        if (c == '<') {
            if (html.mid(i, 4) == QLatin1String("<!--")) {
                const int e = html.indexOf(QLatin1String("-->"), i + 4);
                i = e < 0 ? n : e + 3;
                continue;
            }
            if (html.mid(i, 9) == QLatin1String("<![CDATA[")) {
                const int e = html.indexOf(QLatin1String("]]>"), i + 9);
                const int end = e < 0 ? n : e;
                if (skipDepth == 0)
                    buf += html.mid(i + 9, end - i - 9);
                i = e < 0 ? n : e + 3;
                continue;
            }
            if (i + 1 < n && (html.at(i + 1) == '!' || html.at(i + 1) == '?')) {
                const int e = html.indexOf(QLatin1Char('>'), i);
                i = e < 0 ? n : e + 1;
                continue;
            }
            // Regular tag
            int j = i + 1;
            const bool closing = j < n && html.at(j) == '/';
            if (closing)
                ++j;
            const int nameStart = j;
            while (j < n && (html.at(j).isLetterOrNumber() || html.at(j) == ':' || html.at(j) == '-' || html.at(j) == '_'))
                ++j;
            if (j == nameStart) { // stray '<'
                if (skipDepth == 0)
                    buf.append(c);
                ++i;
                continue;
            }
            QString name = html.mid(nameStart, j - nameStart).toLower();
            const int colon = name.lastIndexOf(QLatin1Char(':'));
            if (colon >= 0)
                name = name.mid(colon + 1);

            // Scan attributes up to the closing '>' (respecting quotes)
            const int attrStart = j;
            QChar quote;
            while (j < n) {
                const QChar a = html.at(j);
                if (!quote.isNull()) {
                    if (a == quote)
                        quote = QChar();
                } else if (a == '"' || a == '\'') {
                    quote = a;
                } else if (a == '>') {
                    break;
                }
                ++j;
            }
            const int tagEnd = j; // index of '>' (or n)
            const bool selfClosing = tagEnd > attrStart && html.at(tagEnd - 1) == '/';
            i = tagEnd < n ? tagEnd + 1 : n;

            if (!closing && (name == QLatin1String("script") || name == QLatin1String("style"))) {
                const int e = html.indexOf(QLatin1String("</") + name, i, Qt::CaseInsensitive);
                if (e < 0) {
                    i = n;
                } else {
                    const int gt = html.indexOf(QLatin1Char('>'), e);
                    i = gt < 0 ? n : gt + 1;
                }
                continue;
            }

            if (!closing && skipDepth == 0) {
                // Record id / name anchors for table-of-contents targets
                const QString attrs = html.mid(attrStart, tagEnd - attrStart);
                for (const char *key : {"id", "name"}) {
                    const QString k = QString::fromLatin1(key);
                    int p = 0;
                    while ((p = attrs.indexOf(k, p)) >= 0) {
                        const bool boundaryBefore = p == 0 || attrs.at(p - 1).isSpace();
                        int q = p + int(k.size());
                        while (q < attrs.size() && attrs.at(q).isSpace())
                            ++q;
                        if (boundaryBefore && q < attrs.size() && attrs.at(q) == '=') {
                            ++q;
                            while (q < attrs.size() && attrs.at(q).isSpace())
                                ++q;
                            if (q < attrs.size() && (attrs.at(q) == '"' || attrs.at(q) == '\'')) {
                                const QChar qc = attrs.at(q);
                                const int e = attrs.indexOf(qc, q + 1);
                                if (e > q)
                                    pendingAnchors.append(attrs.mid(q + 1, e - q - 1));
                            }
                            break;
                        }
                        p = q;
                    }
                    if (name != QLatin1String("a"))
                        break; // "name" attribute is only an anchor on <a>
                }
            }

            const bool skipTag = name == QLatin1String("head") || name == QLatin1String("title")
                || name == QLatin1String("svg") || name == QLatin1String("math") || name == QLatin1String("rt")
                || name == QLatin1String("rp");
            if (skipTag) {
                if (selfClosing)
                    continue;
                if (closing)
                    skipDepth = qMax(0, skipDepth - 1);
                else
                    ++skipDepth;
                continue;
            }
            if (skipDepth > 0)
                continue;

            if (blockTags().contains(name)) {
                if (isHeadingTag(name)) {
                    if (closing) {
                        flush();
                        headingDepth = qMax(0, headingDepth - 1);
                    } else {
                        flush();
                        if (!selfClosing)
                            ++headingDepth;
                    }
                } else {
                    flush();
                }
            } else if (name == QLatin1String("img") && !closing) {
                // Images carry no readable text; ignore (alt text is often a file name).
            }
            continue;
        }

        if (skipDepth > 0) {
            ++i;
            continue;
        }
        if (c == '&') {
            const int used = decodeEntityAt(html, i, buf);
            if (used > 0) {
                i += used;
                continue;
            }
        }
        buf.append(c);
        if (headingDepth > 0 && !c.isSpace())
            bufIsHeading = true;
        ++i;
    }
    flush();

    for (const QString &a : std::as_const(pendingAnchors))
        result.anchors.insert(a, int(result.paragraphs.size()));
    return result;
}
