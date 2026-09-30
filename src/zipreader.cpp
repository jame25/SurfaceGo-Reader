#include "zipreader.h"

#include <QtEndian>
#include <zlib.h>

namespace {
quint16 rd16(const char *p) { return qFromLittleEndian<quint16>(p); }
quint32 rd32(const char *p) { return qFromLittleEndian<quint32>(p); }
}

ZipReader::ZipReader(const QString &path)
    : m_file(path)
{
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_error = m_file.errorString();
        return;
    }
    m_ok = parseCentralDirectory();
}

bool ZipReader::parseCentralDirectory()
{
    const qint64 fileSize = m_file.size();
    if (fileSize < 22) {
        m_error = QStringLiteral("Not a ZIP archive");
        return false;
    }
    // End of central directory record lives in the last 64 KiB + 22 bytes.
    const qint64 tailSize = qMin<qint64>(fileSize, 65535 + 22);
    m_file.seek(fileSize - tailSize);
    const QByteArray tail = m_file.read(tailSize);
    int eocd = -1;
    for (int i = int(tail.size()) - 22; i >= 0; --i) {
        if (rd32(tail.constData() + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        m_error = QStringLiteral("ZIP end-of-directory record not found");
        return false;
    }
    const char *e = tail.constData() + eocd;
    const quint16 count = rd16(e + 10);
    const quint32 cdSize = rd32(e + 12);
    const quint32 cdOffset = rd32(e + 16);
    if (qint64(cdOffset) + cdSize > fileSize) {
        m_error = QStringLiteral("Corrupt ZIP directory");
        return false;
    }
    m_file.seek(cdOffset);
    const QByteArray cd = m_file.read(cdSize);
    int pos = 0;
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > cd.size() || rd32(cd.constData() + pos) != 0x02014b50)
            break;
        const char *h = cd.constData() + pos;
        Entry entry;
        entry.method = rd16(h + 10);
        entry.compressedSize = rd32(h + 20);
        entry.size = rd32(h + 24);
        const quint16 nameLen = rd16(h + 28);
        const quint16 extraLen = rd16(h + 30);
        const quint16 commentLen = rd16(h + 32);
        entry.localHeaderOffset = rd32(h + 42);
        if (pos + 46 + nameLen > cd.size())
            break;
        const QString name = QString::fromUtf8(h + 46, nameLen);
        m_entries.insert(name, entry);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    if (m_entries.isEmpty()) {
        m_error = QStringLiteral("Empty or unsupported ZIP archive");
        return false;
    }
    return true;
}

QByteArray ZipReader::read(const QString &name)
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return {};
    const Entry &entry = it.value();
    if (!m_file.seek(entry.localHeaderOffset))
        return {};
    const QByteArray lh = m_file.read(30);
    if (lh.size() < 30 || rd32(lh.constData()) != 0x04034b50)
        return {};
    const quint16 nameLen = rd16(lh.constData() + 26);
    const quint16 extraLen = rd16(lh.constData() + 28);
    m_file.seek(qint64(entry.localHeaderOffset) + 30 + nameLen + extraLen);
    const QByteArray data = m_file.read(entry.compressedSize);
    if (entry.method == 0)
        return data;
    if (entry.method != 8)
        return {};

    QByteArray out;
    out.resize(qMax<quint32>(entry.size, 1));
    z_stream zs = {};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return {};
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    zs.avail_in = uInt(data.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    int ret = Z_OK;
    while (ret == Z_OK) {
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_OK && zs.avail_out == 0) { // size in header was wrong; grow
            const qsizetype done = out.size();
            out.resize(done * 2);
            zs.next_out = reinterpret_cast<Bytef *>(out.data() + done);
            zs.avail_out = uInt(out.size() - done);
        }
    }
    out.resize(qsizetype(zs.total_out));
    inflateEnd(&zs);
    return (ret == Z_STREAM_END || ret == Z_BUF_ERROR) ? out : QByteArray();
}
