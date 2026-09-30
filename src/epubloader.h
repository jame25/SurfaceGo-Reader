#pragma once

#include "book.h"

bool loadEpub(const QString &path, Book &book, QString *error, const ProgressFn &progress);

// Fast metadata read for the library view (title/author only).
bool readEpubMetadata(const QString &path, QString *title, QString *author);
