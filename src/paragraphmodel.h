#pragma once

#include "book.h"

#include <QAbstractListModel>
#include <memory>

// Paragraphs of the open book, for the virtualised reader ListView.
class ParagraphModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TextRole = Qt::UserRole + 1, HeadingRole };

    explicit ParagraphModel(QObject *parent = nullptr);

    void setBook(std::shared_ptr<const Book> book);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::shared_ptr<const Book> m_book;
};
