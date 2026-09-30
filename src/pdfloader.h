#pragma once

#include "book.h"

// Extracts reflowable text from a PDF with poppler: words are grouped into
// lines, lines into paragraphs (by spacing / indentation / font size), and
// hyphenated line breaks are joined.
bool loadPdf(const QString &path, Book &book, QString *error, const ProgressFn &progress);

bool readPdfMetadata(const QString &path, QString *title, QString *author);
