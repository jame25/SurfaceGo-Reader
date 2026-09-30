#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <memory>

// A reflowable text representation of a document. Both EPUB and PDF are
// converted to a flat list of paragraphs; sentences and words are derived
// from them and addressed by character offsets inside the paragraph text.

struct Paragraph {
    QString text;          // whitespace-normalised, single spaces, trimmed
    bool heading = false;
};

struct Chapter {
    QString title;
    int paragraph = 0;
    int level = 0;
};

struct TextSpan {
    int start = 0;   // offset in paragraph text
    int length = 0;
    int end() const { return start + length; }
};

struct SentenceRef {
    int paragraph = 0;
    int start = 0;
    int length = 0;
};

class Book {
public:
    QString path;
    QString title;
    QString author;
    QString format;             // "epub" or "pdf"
    QList<Paragraph> paragraphs;
    QList<Chapter> chapters;

    // Derived data (not cached on disk; rebuilt quickly on load)
    QList<SentenceRef> sentences;
    QList<int> paraFirstSentence; // size = paragraphs.size() + 1

    void buildSentences();
    int sentenceCount() const { return int(sentences.size()); }
    int sentenceAt(int paragraph, int charPos) const;
    QString sentenceText(int sentence) const;
    QList<TextSpan> sentenceWords(int sentence) const; // offsets relative to the paragraph
    int chapterForParagraph(int paragraph) const;

    bool saveCache(const QString &file) const;
    bool loadCache(const QString &file);
};

using ProgressFn = std::function<void(int percent)>;

// Loads (and caches) a book. Returns nullptr on failure and fills *error.
std::shared_ptr<Book> loadBook(const QString &path, QString *error, const ProgressFn &progress = {});

// Stable key for per-book state (reading position, cache files).
QString bookKey(const QString &path);
QString bookCacheFile(const QString &path);

// Text utilities
QString normalizeWhitespace(const QString &s);
QList<TextSpan> splitSentences(const QString &text);
QList<TextSpan> splitWords(const QString &text, int start, int length);
