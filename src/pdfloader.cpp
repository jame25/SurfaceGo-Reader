#include "pdfloader.h"

#include <poppler-qt6.h>

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {

struct Line {
    QString text;
    QRectF rect;
    bool endsWithHyphen = false;
};

struct PagePara {
    QString text;
    double height = 0; // average line height, used for heading detection
    int lines = 0;
};

QString fixLigatures(QString s)
{
    static const QList<QPair<QChar, QString>> ligs = {
        {QChar(0xFB00), QStringLiteral("ff")}, {QChar(0xFB01), QStringLiteral("fi")},
        {QChar(0xFB02), QStringLiteral("fl")}, {QChar(0xFB03), QStringLiteral("ffi")},
        {QChar(0xFB04), QStringLiteral("ffl")}, {QChar(0xFB05), QStringLiteral("st")},
        {QChar(0xFB06), QStringLiteral("st")},
    };
    for (const auto &[c, r] : ligs)
        if (s.contains(c))
            s.replace(c, r);
    return s;
}

double median(QList<double> v, double fallback)
{
    if (v.isEmpty())
        return fallback;
    std::sort(v.begin(), v.end());
    return v.at(v.size() / 2);
}

bool endsSentence(const QString &s)
{
    for (int i = int(s.size()) - 1; i >= 0; --i) {
        const QChar c = s.at(i);
        if (c == '"' || c.unicode() == 0x201D || c.unicode() == 0x2019 || c == ')' || c == '\'')
            continue;
        return c == '.' || c == '!' || c == '?' || c == ':' || c.unicode() == 0x2026;
    }
    return false;
}

bool isPageNumberLike(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^(page\\s*)?([0-9]{1,4}|[ivxlcdm]{1,7})(\\s*(of|/)\\s*[0-9]+)?$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(s.trimmed()).hasMatch();
}

QList<Line> buildLines(const Poppler::Page &page)
{
    QList<Line> lines;
    const auto boxes = page.textList();
    Line cur;
    double lastRight = 0;
    bool open = false;
    bool lastSpace = false;
    for (const auto &box : boxes) {
        const QRectF r = box->boundingBox();
        const QString word = fixLigatures(box->text());
        if (word.isEmpty())
            continue;
        bool newLine = !open;
        if (open) {
            const double h = qMin(r.height(), cur.rect.height());
            const bool sameRow = qAbs(r.center().y() - cur.rect.center().y()) < h * 0.5;
            const bool goesBack = r.left() < lastRight - h;
            newLine = !sameRow || goesBack;
        }
        if (newLine) {
            if (open)
                lines.append(cur);
            cur = Line{word, r, false};
            open = true;
        } else {
            if (lastSpace || r.left() - lastRight > r.height() * 0.15)
                cur.text += QLatin1Char(' ');
            cur.text += word;
            cur.rect = cur.rect.united(r);
        }
        lastRight = r.right();
        lastSpace = box->hasSpaceAfter();
    }
    if (open)
        lines.append(cur);
    // Table-of-contents dot leaders: "Introduction . . . . . . 5"
    static const QRegularExpression leaders(QStringLiteral("(?:\\s*[.\u00B7\u2024\u2026]){4,}\\s*"));
    for (Line &l : lines) {
        l.text = normalizeWhitespace(l.text);
        if (l.text.contains(QLatin1Char('.')) || l.text.contains(QChar(0x2026)))
            l.text = normalizeWhitespace(l.text.replace(leaders, QStringLiteral(" \u2014 ")));
        l.endsWithHyphen = l.text.size() > 1 && (l.text.endsWith(QLatin1Char('-')) || l.text.endsWith(QChar(0x2010)))
            && l.text.at(l.text.size() - 2).isLetter();
    }
    return lines;
}

void appendLine(QString &para, const Line &prevLine, const QString &text)
{
    if (para.isEmpty()) {
        para = text;
        return;
    }
    if (prevLine.endsWithHyphen && !text.isEmpty() && text.at(0).isLower()) {
        para.chop(1); // join "exam-" + "ple"
        para += text;
    } else {
        para += QLatin1Char(' ') + text;
    }
}

bool isEdge(const Line &l, const QSizeF &size)
{
    return l.rect.top() < size.height() * 0.09 || l.rect.bottom() > size.height() * 0.91;
}

// Header/footer signature: text without digits, so "Chapter 3 · 41" matches "Chapter 3 · 42".
QString edgeSignature(const QString &text)
{
    QString sig;
    for (QChar c : text)
        if (!c.isDigit() && !c.isSpace())
            sig.append(c.toLower());
    return sig;
}

QList<PagePara> pageParagraphs(QList<Line> lines, const QSizeF &size, const QSet<QString> &runningHeaders)
{
    // Drop page numbers and running headers/footers at the top/bottom of the page.
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const Line &l) {
        return isEdge(l, size) && (isPageNumberLike(l.text) || runningHeaders.contains(edgeSignature(l.text)));
    }), lines.end());

    QList<PagePara> out;
    if (lines.isEmpty())
        return out;

    QList<double> heights, spacings, lefts, rights;
    for (int i = 0; i < lines.size(); ++i) {
        heights.append(lines[i].rect.height());
        lefts.append(lines[i].rect.left());
        rights.append(lines[i].rect.right());
        if (i > 0) {
            const double d = lines[i].rect.top() - lines[i - 1].rect.top();
            if (d > 0 && d < lines[i].rect.height() * 3)
                spacings.append(d);
        }
    }
    const double lineH = median(heights, 12);
    const double spacing = median(spacings, lineH * 1.2);
    std::sort(lefts.begin(), lefts.end());
    std::sort(rights.begin(), rights.end());
    const double leftMargin = lefts.at(lefts.size() / 5);
    const double rightMargin = rights.at(rights.size() * 4 / 5);

    PagePara cur;
    double heightSum = 0;
    auto finish = [&]() {
        if (!cur.text.isEmpty()) {
            cur.height = heightSum / qMax(1, cur.lines);
            out.append(cur);
        }
        cur = PagePara();
        heightSum = 0;
    };

    for (int i = 0; i < lines.size(); ++i) {
        const Line &l = lines[i];
        if (i > 0) {
            const Line &p = lines[i - 1];
            const double dy = l.rect.top() - p.rect.top();
            // A large vertical gap starts a paragraph; jumping up (next column)
            // only does when the previous line finished a sentence.
            const bool columnJump = dy < -spacing * 0.5;
            const bool bigGap = dy > spacing * 1.45 || (columnJump && endsSentence(p.text));
            const bool indented = l.rect.left() > leftMargin + lineH * 0.9 && l.rect.left() < leftMargin + lineH * 6
                && p.rect.left() <= leftMargin + lineH * 0.5 && endsSentence(p.text); // not hanging indents
            const bool shortPrev = p.rect.right() < rightMargin - lineH * 2.5 && endsSentence(p.text);
            const bool fontChange = qAbs(l.rect.height() - p.rect.height()) > qMax(l.rect.height(), p.rect.height()) * 0.25;
            if (bigGap || indented || shortPrev || fontChange)
                finish();
            else if (!cur.text.isEmpty()) {
                appendLine(cur.text, p, l.text);
                heightSum += l.rect.height();
                ++cur.lines;
                continue;
            }
        }
        cur.text = l.text;
        heightSum = l.rect.height();
        cur.lines = 1;
    }
    finish();

    // Heading detection by relative font size.
    for (PagePara &pp : out) {
        if (pp.height > lineH * 1.25 && pp.text.size() < 160)
            pp.lines = -1; // mark as heading
    }
    return out;
}

void flattenOutline(const QVector<Poppler::OutlineItem> &items, int level, QList<QPair<QString, QPair<int, int>>> &out)
{
    for (const Poppler::OutlineItem &item : items) {
        const auto dest = item.destination();
        if (dest && dest->pageNumber() > 0)
            out.append({item.name().simplified(), {dest->pageNumber() - 1, level}});
        if (item.hasChildren())
            flattenOutline(item.children(), level + 1, out);
    }
}

} // namespace

bool readPdfMetadata(const QString &path, QString *title, QString *author)
{
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(path);
    if (!doc || doc->isLocked())
        return false;
    if (title)
        *title = doc->info(QStringLiteral("Title")).simplified();
    if (author)
        *author = doc->info(QStringLiteral("Author")).simplified();
    return true;
}

bool loadPdf(const QString &path, Book &book, QString *error, const ProgressFn &progress)
{
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(path);
    if (!doc) {
        *error = QStringLiteral("Cannot open PDF");
        return false;
    }
    if (doc->isLocked()) {
        *error = QStringLiteral("This PDF is password protected");
        return false;
    }
    book.format = QStringLiteral("pdf");
    book.title = doc->info(QStringLiteral("Title")).simplified();
    book.author = doc->info(QStringLiteral("Author")).simplified();

    const int pages = doc->numPages();

    // Pass 1: text lines of every page (the expensive part).
    QList<QList<Line>> pageLines(pages);
    QList<QSizeF> pageSizes(pages);
    QHash<QString, int> edgeCounts;
    for (int i = 0; i < pages; ++i) {
        std::unique_ptr<Poppler::Page> page = doc->page(i);
        if (page) {
            pageLines[i] = buildLines(*page);
            pageSizes[i] = page->pageSizeF();
            QSet<QString> seen;
            for (const Line &l : std::as_const(pageLines[i]))
                if (isEdge(l, pageSizes[i]) && l.text.size() < 120)
                    seen.insert(edgeSignature(l.text));
            for (const QString &sig : std::as_const(seen))
                ++edgeCounts[sig];
        }
        if (progress)
            progress((i + 1) * 95 / qMax(1, pages));
    }
    QSet<QString> runningHeaders;
    if (pages >= 6) {
        for (auto it = edgeCounts.constBegin(); it != edgeCounts.constEnd(); ++it)
            if (!it.key().isEmpty() && it.value() >= 3)
                runningHeaders.insert(it.key());
    }

    // Pass 2: paragraphs
    QList<int> pageFirstPara;
    pageFirstPara.reserve(pages);
    for (int i = 0; i < pages; ++i) {
        pageFirstPara.append(int(book.paragraphs.size()));
        const QList<PagePara> paras = pageParagraphs(std::move(pageLines[i]), pageSizes[i], runningHeaders);
        for (int k = 0; k < paras.size(); ++k) {
            const PagePara &pp = paras.at(k);
            const bool heading = pp.lines < 0;
            // Continue a paragraph that was split by the page break.
            if (k == 0 && !heading && !book.paragraphs.isEmpty() && !book.paragraphs.last().heading
                && !endsSentence(book.paragraphs.last().text) && !pp.text.isEmpty() && pp.text.at(0).isLower()) {
                QString &prev = book.paragraphs.last().text;
                if (prev.endsWith(QLatin1Char('-')) && prev.size() > 1 && prev.at(prev.size() - 2).isLetter()) {
                    prev.chop(1);
                    prev += pp.text;
                } else {
                    prev += QLatin1Char(' ') + pp.text;
                }
                pageFirstPara.last() = int(book.paragraphs.size()) - 1;
                continue;
            }
            book.paragraphs.append({pp.text, heading});
        }
    }
    if (progress)
        progress(100);

    const int maxPara = qMax(0, int(book.paragraphs.size()) - 1);
    QList<QPair<QString, QPair<int, int>>> outline;
    flattenOutline(doc->outline(), 0, outline);
    for (const auto &[name, pl] : std::as_const(outline)) {
        if (pl.first < pageFirstPara.size() && !name.isEmpty())
            book.chapters.append({name, qMin(pageFirstPara.at(pl.first), maxPara), pl.second});
    }
    if (book.chapters.isEmpty()) {
        for (int i = 0; i < pages; ++i) {
            const int para = qMin(pageFirstPara.at(i), maxPara);
            book.chapters.append({QStringLiteral("Page %1").arg(i + 1), para, 0});
        }
    }
    return true;
}
