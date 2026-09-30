#include "librarymodel.h"
#include "appsettings.h"
#include "book.h"
#include "epubloader.h"
#include "pdfloader.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace {
struct Meta {
    QString path;
    QString title;
    QString author;
};

QString prettyFileName(const QString &path)
{
    QString s = QFileInfo(path).completeBaseName();
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    return s.simplified();
}
}

LibraryModel::LibraryModel(AppSettings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(800);
    connect(&m_rescanTimer, &QTimer::timeout, this, &LibraryModel::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_rescanTimer, qOverload<>(&QTimer::start));
    connect(m_settings, &AppSettings::libraryFolderChanged, this, &LibraryModel::refresh);
    connect(m_settings, &AppSettings::sortByRecentChanged, this, [this] {
        beginResetModel();
        sortItems();
        endResetModel();
    });
    refresh();
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &it = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return it.title.isEmpty() ? prettyFileName(it.path) : it.title;
    case PathRole: return it.path;
    case AuthorRole: return it.author;
    case FormatRole: return it.format;
    case ProgressRole: return it.progress;
    case FileNameRole: return it.fileName;
    case KeyRole: return it.key;
    }
    return {};
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {{PathRole, "path"}, {TitleRole, "title"}, {AuthorRole, "author"}, {FormatRole, "format"},
            {ProgressRole, "progress"}, {FileNameRole, "fileName"}, {KeyRole, "bookKey"}};
}

void LibraryModel::sortItems()
{
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    const bool recent = m_settings->sortByRecent();
    std::stable_sort(m_items.begin(), m_items.end(), [&](const Item &a, const Item &b) {
        if (recent && a.lastOpened != b.lastOpened)
            return a.lastOpened > b.lastOpened;
        const QString ta = a.title.isEmpty() ? prettyFileName(a.path) : a.title;
        const QString tb = b.title.isEmpty() ? prettyFileName(b.path) : b.title;
        return coll.compare(ta, tb) < 0;
    });
}

void LibraryModel::watchFolders()
{
    const QStringList old = m_watcher.directories();
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    const QString root = m_settings->libraryFolder();
    QStringList dirs{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && dirs.size() < 200)
        dirs.append(it.next());
    m_watcher.addPaths(dirs);
}

void LibraryModel::refresh()
{
    const QString root = m_settings->libraryFolder();
    QDir().mkpath(root);
    QList<Item> items;
    QDirIterator it(root, {QStringLiteral("*.epub"), QStringLiteral("*.pdf"), QStringLiteral("*.EPUB"), QStringLiteral("*.PDF")},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        Item item;
        item.path = path;
        item.fileName = fi.fileName();
        item.key = bookKey(path);
        item.format = fi.suffix().toLower();
        const QVariantMap st = m_settings->bookState(item.key);
        item.title = st.value(QStringLiteral("title")).toString();
        item.author = st.value(QStringLiteral("author")).toString();
        item.lastOpened = st.value(QStringLiteral("lastOpened")).toLongLong();
        const int total = st.value(QStringLiteral("total")).toInt();
        item.progress = total > 1 ? qBound(0.0, st.value(QStringLiteral("sentence")).toDouble() / (total - 1), 1.0) : 0.0;
        items.append(item);
    }
    beginResetModel();
    m_items = items;
    sortItems();
    endResetModel();
    Q_EMIT countChanged();
    watchFolders();
    fetchMetadata();
}

void LibraryModel::fetchMetadata()
{
    QStringList missing;
    for (const Item &it : std::as_const(m_items))
        if (it.title.isEmpty() && !m_settings->bookState(it.key).contains(QStringLiteral("metaChecked")))
            missing.append(it.path);
    if (missing.isEmpty())
        return;
    const int token = ++m_scanToken;
    m_scanning = true;
    Q_EMIT scanningChanged();
    auto *watcher = new QFutureWatcher<QList<Meta>>(this);
    connect(watcher, &QFutureWatcher<QList<Meta>>::finished, this, [this, watcher, token] {
        watcher->deleteLater();
        if (token != m_scanToken)
            return;
        const QList<Meta> metas = watcher->result();
        for (const Meta &m : metas) {
            const QString key = bookKey(m.path);
            m_settings->setBookState(key, {{QStringLiteral("title"), m.title},
                                           {QStringLiteral("author"), m.author},
                                           {QStringLiteral("metaChecked"), true}});
            for (int i = 0; i < m_items.size(); ++i) {
                if (m_items[i].path == m.path) {
                    m_items[i].title = m.title;
                    m_items[i].author = m.author;
                }
            }
        }
        beginResetModel();
        sortItems();
        endResetModel();
        m_scanning = false;
        Q_EMIT scanningChanged();
    });
    watcher->setFuture(QtConcurrent::run([missing] {
        QList<Meta> out;
        for (const QString &p : missing) {
            Meta m{p, {}, {}};
            if (p.endsWith(QLatin1String("epub"), Qt::CaseInsensitive))
                readEpubMetadata(p, &m.title, &m.author);
            else
                readPdfMetadata(p, &m.title, &m.author);
            // Many PDFs carry junk titles like "Microsoft Word - foo.doc"
            if (m.title.startsWith(QLatin1String("Microsoft Word")) || m.title.size() < 2)
                m.title.clear();
            out.append(m);
        }
        return out;
    }));
}

bool LibraryModel::removeBook(const QString &path)
{
    const QString key = bookKey(path);
    const QString cache = bookCacheFile(path);
    if (!QFile::moveToTrash(path)) {
        Q_EMIT message(tr("Could not move \"%1\" to the trash").arg(QFileInfo(path).fileName()));
        return false;
    }
    QFile::remove(cache);
    m_settings->removeBookState(key);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].path == path) {
            beginRemoveRows({}, i, i);
            m_items.removeAt(i);
            endRemoveRows();
            Q_EMIT countChanged();
            break;
        }
    }
    Q_EMIT message(tr("Moved to trash"));
    return true;
}

void LibraryModel::updateProgress(const QString &path)
{
    for (int i = 0; i < m_items.size(); ++i) {
        Item &it = m_items[i];
        if (it.path != path)
            continue;
        const QVariantMap st = m_settings->bookState(it.key);
        const int total = st.value(QStringLiteral("total")).toInt();
        it.progress = total > 1 ? qBound(0.0, st.value(QStringLiteral("sentence")).toDouble() / (total - 1), 1.0) : 0.0;
        it.lastOpened = st.value(QStringLiteral("lastOpened")).toLongLong();
        if (!st.value(QStringLiteral("title")).toString().isEmpty())
            it.title = st.value(QStringLiteral("title")).toString();
        it.author = st.value(QStringLiteral("author")).toString();
        break;
    }
    beginResetModel();
    sortItems();
    endResetModel();
}
