#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>

class AppSettings;

// Books (EPUB / PDF) found in the library folder, including sub-folders.
class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, AuthorRole, FormatRole, ProgressRole, FileNameRole, KeyRole };

    explicit LibraryModel(AppSettings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    bool scanning() const { return m_scanning; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool removeBook(const QString &path);   // moves the file to the trash
    Q_INVOKABLE void updateProgress(const QString &path);

Q_SIGNALS:
    void countChanged();
    void scanningChanged();
    void message(const QString &text);

private:
    struct Item {
        QString path;
        QString key;
        QString fileName;
        QString title;
        QString author;
        QString format;
        double progress = 0;
        qint64 lastOpened = 0;
    };
    void sortItems();
    void fetchMetadata();
    void watchFolders();

    AppSettings *m_settings;
    QList<Item> m_items;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    bool m_scanning = false;
    int m_scanToken = 0;
};
