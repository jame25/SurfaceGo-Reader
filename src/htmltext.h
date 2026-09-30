#pragma once

#include "book.h"

#include <QHash>
#include <QString>

// Lenient (X)HTML to paragraph converter. Tolerates malformed markup and
// undeclared entities, which are common in real-world EPUB files.
struct HtmlExtractResult {
    QList<Paragraph> paragraphs;
    QHash<QString, int> anchors; // element id -> index into paragraphs (first paragraph at/after it)
};

HtmlExtractResult extractHtmlText(const QString &html);
QString decodeHtmlEntities(const QString &s);
