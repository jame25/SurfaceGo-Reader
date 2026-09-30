#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>

// Minimal read-only ZIP reader (stored + deflate), enough for EPUB containers.
class ZipReader {
public:
    explicit ZipReader(const QString &path);
    bool isOpen() const { return m_ok; }
    QString errorString() const { return m_error; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    QByteArray read(const QString &name);
    QStringList names() const { return m_entries.keys(); }

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint32 localHeaderOffset = 0;
    };
    bool parseCentralDirectory();

    QFile m_file;
    QHash<QString, Entry> m_entries;
    bool m_ok = false;
    QString m_error;
};
