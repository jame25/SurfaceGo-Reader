#include "epubloader.h"
#include "htmltext.h"
#include "zipreader.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

struct ManifestItem {
    QString href;       // full path inside the zip
    QString mediaType;
    QString properties;
};

struct TocEntry {
    QString href;       // full path inside the zip, may contain #fragment
    QString title;
    int level = 0;
};

struct OpfInfo {
    QString title;
    QString author;
    QHash<QString, ManifestItem> manifest;
    QStringList spine;  // manifest ids
    QString ncxId;
};

QString dirOf(const QString &zipPath)
{
    const int slash = zipPath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : zipPath.left(slash + 1);
}

// Resolves an href (relative to baseDir, URL-encoded) to a normalised zip path.
QString resolveHref(const QString &baseDir, const QString &href)
{
    QString h = href;
    QString fragment;
    const int hash = h.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        fragment = h.mid(hash + 1);
        h = h.left(hash);
    }
    h = QUrl::fromPercentEncoding(h.toUtf8());
    QString full = h.isEmpty() ? QString() : QDir::cleanPath(baseDir + h);
    if (full.startsWith(QLatin1String("./")))
        full = full.mid(2);
    if (!fragment.isEmpty())
        full += QLatin1Char('#') + fragment;
    return full;
}

QString decodeDocument(const QByteArray &data)
{
    QStringDecoder dec = QStringDecoder::decoderForHtml(data);
    if (!dec.isValid())
        return QString::fromUtf8(data);
    return dec.decode(data);
}

QString findRootfile(ZipReader &zip)
{
    const QByteArray container = zip.read(QStringLiteral("META-INF/container.xml"));
    QXmlStreamReader xml(container);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("rootfile")) {
            const QString p = xml.attributes().value(QLatin1String("full-path")).toString();
            if (!p.isEmpty())
                return p;
        }
    }
    // Fallback: first .opf in the archive
    for (const QString &n : zip.names())
        if (n.endsWith(QLatin1String(".opf"), Qt::CaseInsensitive))
            return n;
    return {};
}

bool parseOpf(ZipReader &zip, const QString &opfPath, OpfInfo &info)
{
    const QByteArray data = zip.read(opfPath);
    if (data.isEmpty())
        return false;
    const QString base = dirOf(opfPath);
    QXmlStreamReader xml(data);
    bool inMetadata = false;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == QLatin1String("metadata")) {
                inMetadata = true;
            } else if (inMetadata && name == QLatin1String("title") && info.title.isEmpty()) {
                info.title = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            } else if (inMetadata && name == QLatin1String("creator") && info.author.isEmpty()) {
                info.author = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            } else if (name == QLatin1String("item")) {
                const auto a = xml.attributes();
                ManifestItem item;
                item.href = resolveHref(base, a.value(QLatin1String("href")).toString());
                item.mediaType = a.value(QLatin1String("media-type")).toString();
                item.properties = a.value(QLatin1String("properties")).toString();
                info.manifest.insert(a.value(QLatin1String("id")).toString(), item);
            } else if (name == QLatin1String("spine")) {
                info.ncxId = xml.attributes().value(QLatin1String("toc")).toString();
            } else if (name == QLatin1String("itemref")) {
                const auto a = xml.attributes();
                if (a.value(QLatin1String("linear")) != QLatin1String("no"))
                    info.spine.append(a.value(QLatin1String("idref")).toString());
            }
        } else if (tok == QXmlStreamReader::EndElement && xml.name() == QLatin1String("metadata")) {
            inMetadata = false;
        }
    }
    // Recover from minor XML errors as long as we got a spine.
    return !info.spine.isEmpty();
}

QString stripTags(const QString &s)
{
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString t = s;
    t.remove(tag);
    return normalizeWhitespace(decodeHtmlEntities(t));
}

QList<TocEntry> parseNav(ZipReader &zip, const QString &navPath)
{
    QList<TocEntry> out;
    const QString html = decodeDocument(zip.read(navPath));
    const QString base = dirOf(navPath);
    static const QRegularExpression navRe(QStringLiteral("<nav\\b[^>]*type\\s*=\\s*[\"'][^\"']*\\btoc\\b"),
                                          QRegularExpression::CaseInsensitiveOption);
    int start = int(html.indexOf(navRe));
    if (start < 0)
        start = int(html.indexOf(QLatin1String("<nav"), 0, Qt::CaseInsensitive));
    if (start < 0)
        return out;
    int end = int(html.indexOf(QLatin1String("</nav"), start, Qt::CaseInsensitive));
    if (end < 0)
        end = int(html.size());
    const QString section = html.mid(start, end - start);

    static const QRegularExpression tagRe(QStringLiteral("<(/?)(ol|a)\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefRe(QStringLiteral("href\\s*=\\s*[\"']([^\"']*)[\"']"), QRegularExpression::CaseInsensitiveOption);
    int depth = 0;
    auto it = tagRe.globalMatch(section);
    while (it.hasNext()) {
        const auto m = it.next();
        const bool closing = !m.captured(1).isEmpty();
        const QString tag = m.captured(2).toLower();
        if (tag == QLatin1String("ol")) {
            depth += closing ? -1 : 1;
            continue;
        }
        if (closing)
            continue;
        const auto hm = hrefRe.match(m.captured(3));
        if (!hm.hasMatch())
            continue;
        const int textStart = int(m.capturedEnd());
        const int close = int(section.indexOf(QLatin1String("</a"), textStart, Qt::CaseInsensitive));
        const QString title = stripTags(section.mid(textStart, close < 0 ? -1 : close - textStart));
        if (title.isEmpty())
            continue;
        out.append({resolveHref(base, hm.captured(1)), title, qMax(0, depth - 1)});
    }
    return out;
}

QList<TocEntry> parseNcx(ZipReader &zip, const QString &ncxPath)
{
    QList<TocEntry> out;
    const QString base = dirOf(ncxPath);
    QXmlStreamReader xml(zip.read(ncxPath));
    int depth = 0;
    QString pendingTitle;
    bool inNavMap = false;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == QLatin1String("navMap")) {
                inNavMap = true;
            } else if (!inNavMap) {
                continue;
            } else if (name == QLatin1String("navPoint")) {
                ++depth;
                pendingTitle.clear();
            } else if (name == QLatin1String("text") && pendingTitle.isEmpty()) {
                pendingTitle = xml.readElementText().simplified();
            } else if (name == QLatin1String("content")) {
                const QString src = xml.attributes().value(QLatin1String("src")).toString();
                if (!pendingTitle.isEmpty() && !src.isEmpty())
                    out.append({resolveHref(base, src), pendingTitle, qMax(0, depth - 1)});
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("navPoint"))
                --depth;
            else if (xml.name() == QLatin1String("navMap"))
                inNavMap = false;
        }
    }
    return out;
}

} // namespace

bool readEpubMetadata(const QString &path, QString *title, QString *author)
{
    ZipReader zip(path);
    if (!zip.isOpen())
        return false;
    const QString opf = findRootfile(zip);
    OpfInfo info;
    if (opf.isEmpty() || !parseOpf(zip, opf, info))
        return false;
    if (title)
        *title = info.title;
    if (author)
        *author = info.author;
    return true;
}

bool loadEpub(const QString &path, Book &book, QString *error, const ProgressFn &progress)
{
    ZipReader zip(path);
    if (!zip.isOpen()) {
        *error = QStringLiteral("Cannot open EPUB: %1").arg(zip.errorString());
        return false;
    }
    const QString opfPath = findRootfile(zip);
    OpfInfo info;
    if (opfPath.isEmpty() || !parseOpf(zip, opfPath, info)) {
        *error = QStringLiteral("Invalid EPUB: package document not found");
        return false;
    }
    book.format = QStringLiteral("epub");
    book.title = info.title;
    book.author = info.author;

    QHash<QString, int> fileStart;   // zip path -> first paragraph index
    QHash<QString, int> anchorIndex; // zip path#id -> paragraph index
    QList<QPair<QString, int>> spineStarts;

    const int total = int(info.spine.size());
    for (int s = 0; s < total; ++s) {
        const auto it = info.manifest.constFind(info.spine.at(s));
        if (it == info.manifest.constEnd())
            continue;
        const ManifestItem &item = it.value();
        if (!item.mediaType.contains(QLatin1String("html")) && !item.href.endsWith(QLatin1String("html"), Qt::CaseInsensitive)
            && !item.href.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive))
            continue;
        const int base = int(book.paragraphs.size());
        fileStart.insert(item.href, base);
        spineStarts.append({item.href, base});
        const HtmlExtractResult r = extractHtmlText(decodeDocument(zip.read(item.href)));
        book.paragraphs.append(r.paragraphs);
        for (auto a = r.anchors.constBegin(); a != r.anchors.constEnd(); ++a)
            anchorIndex.insert(item.href + QLatin1Char('#') + a.key(), base + a.value());
        if (progress)
            progress(total > 0 ? (s + 1) * 100 / total : 100);
    }

    // Table of contents: EPUB3 nav first, then EPUB2 NCX.
    QList<TocEntry> toc;
    for (const ManifestItem &item : std::as_const(info.manifest)) {
        if (item.properties.split(QLatin1Char(' ')).contains(QLatin1String("nav"))) {
            toc = parseNav(zip, item.href);
            break;
        }
    }
    if (toc.isEmpty()) {
        QString ncx = info.manifest.value(info.ncxId).href;
        if (ncx.isEmpty()) {
            for (const ManifestItem &item : std::as_const(info.manifest))
                if (item.mediaType == QLatin1String("application/x-dtbncx+xml"))
                    ncx = item.href;
        }
        if (!ncx.isEmpty())
            toc = parseNcx(zip, ncx);
    }

    const int maxPara = qMax(0, int(book.paragraphs.size()) - 1);
    for (const TocEntry &e : std::as_const(toc)) {
        const int hash = e.href.indexOf(QLatin1Char('#'));
        const QString file = hash >= 0 ? e.href.left(hash) : e.href;
        int para = -1;
        if (hash >= 0)
            para = anchorIndex.value(e.href, -1);
        if (para < 0)
            para = fileStart.value(file, -1);
        if (para < 0)
            continue;
        book.chapters.append({e.title, qMin(para, maxPara), e.level});
    }
    if (book.chapters.isEmpty()) {
        int n = 0;
        for (const auto &[file, start] : std::as_const(spineStarts)) {
            if (start > maxPara || (n > 0 && book.chapters.last().paragraph == start))
                continue;
            ++n;
            QString title;
            if (book.paragraphs.value(start).heading)
                title = book.paragraphs.at(start).text.left(80);
            else
                title = QStringLiteral("Section %1").arg(n);
            book.chapters.append({title, start, 0});
        }
    }
    return true;
}
