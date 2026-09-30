#include "book.h"
#include "epubloader.h"
#include "pdfloader.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {

constexpr quint32 kCacheMagic = 0x53475242; // "SGRB"
constexpr quint32 kCacheVersion = 6;

bool isTerminator(QChar c)
{
    switch (c.unicode()) {
    case '.': case '!': case '?':
    case 0x2026: // …
    case 0x3002: case 0xFF01: case 0xFF1F: // 。！？
        return true;
    default:
        return false;
    }
}

bool isCloser(QChar c)
{
    switch (c.unicode()) {
    case '"': case '\'': case ')': case ']': case '}':
    case 0x201D: case 0x2019: case 0x00BB: case 0x203A: // ” ’ » ›
        return true;
    default:
        return false;
    }
}

bool isOpener(QChar c)
{
    switch (c.unicode()) {
    case '"': case '\'': case '(': case '[': case '{':
    case 0x201C: case 0x2018: case 0x00AB: case 0x2039: // “ ‘ « ‹
    case 0x2014: case 0x2013: case '-': case 0x00BF: case 0x00A1: // — – - ¿ ¡
        return true;
    default:
        return false;
    }
}

const QSet<QString> &abbreviations()
{
    static const QSet<QString> set = {
        "mr", "mrs", "ms", "dr", "prof", "sr", "jr", "st", "vs", "etc", "e.g", "i.e", "mt",
        "fig", "gen", "col", "lt", "sgt", "capt", "rev", "hon", "inc", "ltd", "co", "messrs",
        "mme", "mlle", "esq", "cf", "approx", "dept", "est", "al", "gov", "sen", "rep", "univ",
        "viz", "ave", "blvd", "jan", "feb", "mar", "apr", "jun", "jul", "aug", "sep", "sept",
        "oct", "nov", "dec", "u.s", "u.k", "a.m", "p.m", "ph.d", "b.c", "a.d", "cap", "vol",
    };
    return set;
}

const QSet<QString> &numberAbbreviations()
{
    // Only abbreviations when followed by a number ("No. 5", "p. 12").
    static const QSet<QString> set = {"no", "nos", "p", "pp", "ch", "chap", "art", "sec", "op", "vol", "fig"};
    return set;
}

// Word immediately preceding position `pos` (exclusive), without leading punctuation.
QString wordBefore(const QString &text, int pos)
{
    int b = pos;
    while (b > 0 && !text.at(b - 1).isSpace())
        --b;
    QString w = text.mid(b, pos - b);
    while (!w.isEmpty() && !w.at(0).isLetterOrNumber())
        w.remove(0, 1);
    return w;
}

void appendTrimmed(QList<TextSpan> &out, const QString &text, int s, int e)
{
    while (s < e && text.at(s).isSpace())
        ++s;
    while (e > s && text.at(e - 1).isSpace())
        --e;
    if (e > s)
        out.append({s, e - s});
}

// Long sentences are split at clause punctuation so that synthesis latency and
// highlight granularity stay reasonable.
void subdivide(QList<TextSpan> &out, const QString &text, TextSpan span)
{
    constexpr int kMax = 280;
    if (span.length <= kMax) {
        out.append(span);
        return;
    }
    int pieceStart = span.start;
    for (int p = span.start; p < span.end() - 1; ++p) {
        const QChar c = text.at(p);
        const bool clause = (c == ';' || c == ':' || c.unicode() == 0x2014) && text.at(p + 1).isSpace();
        const bool comma = c == ',' && text.at(p + 1).isSpace();
        const int len = p + 1 - pieceStart;
        // Never leave a tiny tail piece behind.
        if (((clause && len >= 60) || (comma && len >= 180)) && span.end() - (p + 1) >= 40) {
            appendTrimmed(out, text, pieceStart, p + 1);
            pieceStart = p + 1;
        }
    }
    appendTrimmed(out, text, pieceStart, span.end());
}

} // namespace

QString normalizeWhitespace(const QString &s)
{
    QString out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (QChar c : s) {
        const char16_t u = c.unicode();
        if (u == 0x00AD || u == 0x200B || u == 0xFEFF || u == 0x200C || u == 0x200D || u == 0x2060)
            continue; // soft hyphen / zero width characters
        if (c.isSpace() || u == 0x00A0 || (u >= 0x2000 && u <= 0x200A) || u == 0x202F || u == 0x3000) {
            pendingSpace = !out.isEmpty();
            continue;
        }
        if (c.category() == QChar::Other_Control)
            continue;
        if (pendingSpace) {
            out.append(QLatin1Char(' '));
            pendingSpace = false;
        }
        out.append(c);
    }
    return out;
}

QList<TextSpan> splitSentences(const QString &text)
{
    QList<TextSpan> raw;
    const int n = int(text.size());
    int start = 0;
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (!isTerminator(c)) {
            ++i;
            continue;
        }
        int j = i + 1;
        while (j < n && isTerminator(text.at(j)))
            ++j;
        const bool singleDot = (c == '.' && j == i + 1);
        while (j < n && isCloser(text.at(j)))
            ++j;
        if (j >= n)
            break;
        if (!text.at(j).isSpace()) {
            i = j;
            continue;
        }
        int k = j;
        while (k < n && text.at(k).isSpace())
            ++k;
        if (k >= n)
            break;
        const QChar next = text.at(k);
        bool boundary = !next.isLower();
        if (boundary && singleDot) {
            const QString w = wordBefore(text, i);
            const QString lw = w.toLower();
            if (abbreviations().contains(lw))
                boundary = false;
            else if (w.size() == 1 && w.at(0).isUpper() && w != QLatin1String("I"))
                boundary = false; // initials: "J. R. R. Tolkien"
            else if (next.isDigit() && numberAbbreviations().contains(lw))
                boundary = false;
        }
        if (boundary) {
            appendTrimmed(raw, text, start, j);
            start = k;
            i = k;
        } else {
            i = j;
        }
    }
    appendTrimmed(raw, text, start, n);

    QList<TextSpan> pieces;
    for (const TextSpan &s : raw)
        subdivide(pieces, text, s);

    // Pieces without any letters or digits ("* * *", stray punctuation) are
    // merged into a neighbour instead of becoming silent sentences.
    auto speakable = [&](const TextSpan &s) {
        for (int i = s.start; i < s.end(); ++i)
            if (text.at(i).isLetterOrNumber())
                return true;
        return false;
    };
    QList<TextSpan> out;
    bool pendingMerge = false;
    for (const TextSpan &s : std::as_const(pieces)) {
        if (!speakable(s)) {
            if (!out.isEmpty())
                out.last().length = s.end() - out.last().start;
            else
                pendingMerge = true;
            continue;
        }
        if (pendingMerge) {
            out.append({pieces.first().start, s.end() - pieces.first().start});
            pendingMerge = false;
        } else {
            out.append(s);
        }
    }
    if (out.isEmpty() && !pieces.isEmpty())
        out.append({pieces.first().start, pieces.last().end() - pieces.first().start});
    return out;
}

QList<TextSpan> splitWords(const QString &text, int start, int length)
{
    QList<TextSpan> words;
    const int end = start + length;
    int i = start;
    while (i < end) {
        while (i < end && text.at(i).isSpace())
            ++i;
        const int ws = i;
        while (i < end && !text.at(i).isSpace())
            ++i;
        if (i > ws)
            words.append({ws, i - ws});
    }
    return words;
}

void Book::buildSentences()
{
    sentences.clear();
    paraFirstSentence.clear();
    paraFirstSentence.reserve(paragraphs.size() + 1);
    for (int p = 0; p < paragraphs.size(); ++p) {
        paraFirstSentence.append(int(sentences.size()));
        for (const TextSpan &s : splitSentences(paragraphs.at(p).text))
            sentences.append({p, s.start, s.length});
    }
    paraFirstSentence.append(int(sentences.size()));
}

int Book::sentenceAt(int paragraph, int charPos) const
{
    if (paragraph < 0 || paragraph >= paragraphs.size())
        return -1;
    const int first = paraFirstSentence.at(paragraph);
    const int last = paraFirstSentence.at(paragraph + 1);
    if (first == last) {
        // Empty paragraph: next sentence after it, if any.
        return first < sentences.size() ? first : int(sentences.size()) - 1;
    }
    int found = first;
    for (int s = first; s < last; ++s) {
        if (sentences.at(s).start <= charPos)
            found = s;
        else
            break;
    }
    return found;
}

QString Book::sentenceText(int sentence) const
{
    if (sentence < 0 || sentence >= sentences.size())
        return {};
    const SentenceRef &s = sentences.at(sentence);
    return paragraphs.at(s.paragraph).text.mid(s.start, s.length);
}

QList<TextSpan> Book::sentenceWords(int sentence) const
{
    if (sentence < 0 || sentence >= sentences.size())
        return {};
    const SentenceRef &s = sentences.at(sentence);
    return splitWords(paragraphs.at(s.paragraph).text, s.start, s.length);
}

int Book::chapterForParagraph(int paragraph) const
{
    int found = -1;
    for (int i = 0; i < chapters.size(); ++i) {
        if (chapters.at(i).paragraph <= paragraph)
            found = i;
        else if (found >= 0)
            break;
    }
    return found;
}

bool Book::saveCache(const QString &file) const
{
    QByteArray raw;
    {
        QDataStream ds(&raw, QIODevice::WriteOnly);
        ds << title << author << format << qint32(paragraphs.size());
        for (const Paragraph &p : paragraphs)
            ds << p.text << p.heading;
        ds << qint32(chapters.size());
        for (const Chapter &c : chapters)
            ds << c.title << qint32(c.paragraph) << qint32(c.level);
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&f);
    out << kCacheMagic << kCacheVersion << qCompress(raw, 3);
    return f.commit();
}

bool Book::loadCache(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&f);
    quint32 magic = 0, version = 0;
    QByteArray compressed;
    in >> magic >> version;
    if (magic != kCacheMagic || version != kCacheVersion)
        return false;
    in >> compressed;
    const QByteArray raw = qUncompress(compressed);
    if (raw.isEmpty())
        return false;
    QDataStream ds(raw);
    qint32 np = 0, nc = 0;
    ds >> title >> author >> format >> np;
    if (np < 0 || np > 50'000'000)
        return false;
    paragraphs.clear();
    paragraphs.reserve(np);
    for (int i = 0; i < np; ++i) {
        Paragraph p;
        ds >> p.text >> p.heading;
        paragraphs.append(std::move(p));
    }
    ds >> nc;
    chapters.clear();
    for (int i = 0; i < nc && ds.status() == QDataStream::Ok; ++i) {
        Chapter c;
        qint32 para = 0, level = 0;
        ds >> c.title >> para >> level;
        c.paragraph = para;
        c.level = level;
        chapters.append(c);
    }
    return ds.status() == QDataStream::Ok;
}

QString bookKey(const QString &path)
{
    // File name + size: survives moving the file around inside the library.
    const QFileInfo fi(path);
    const QByteArray id = fi.fileName().toUtf8() + '|' + QByteArray::number(fi.size());
    return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(20));
}

QString bookCacheFile(const QString &path)
{
    const QFileInfo fi(path);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/books");
    return dir + QLatin1Char('/') + bookKey(path) + QLatin1Char('-')
        + QString::number(fi.lastModified().toSecsSinceEpoch()) + QStringLiteral(".bin");
}

std::shared_ptr<Book> loadBook(const QString &path, QString *error, const ProgressFn &progress)
{
    auto book = std::make_shared<Book>();
    const QString cache = bookCacheFile(path);
    if (book->loadCache(cache)) {
        book->path = path;
        book->buildSentences();
        return book;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    bool ok = false;
    QString err;
    if (suffix == QLatin1String("epub"))
        ok = loadEpub(path, *book, &err, progress);
    else if (suffix == QLatin1String("pdf"))
        ok = loadPdf(path, *book, &err, progress);
    else
        err = QStringLiteral("Unsupported file type: %1").arg(suffix);

    if (ok && book->paragraphs.isEmpty()) {
        ok = false;
        err = QStringLiteral("No readable text found in this document (scanned PDFs need OCR first).");
    }
    if (!ok) {
        if (error)
            *error = err;
        return nullptr;
    }
    book->path = path;
    if (book->title.trimmed().isEmpty())
        book->title = QFileInfo(path).completeBaseName();

    // Remove stale cache entries for this book, then write the new one.
    const QFileInfo cfi(cache);
    const QString prefix = bookKey(path) + QLatin1Char('-');
    for (const QFileInfo &old : QDir(cfi.absolutePath()).entryInfoList({prefix + QLatin1Char('*')}, QDir::Files))
        QFile::remove(old.absoluteFilePath());
    book->saveCache(cache);

    book->buildSentences();
    return book;
}
