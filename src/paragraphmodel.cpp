#include "paragraphmodel.h"

ParagraphModel::ParagraphModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ParagraphModel::setBook(std::shared_ptr<const Book> book)
{
    beginResetModel();
    m_book = std::move(book);
    endResetModel();
}

int ParagraphModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_book ? 0 : int(m_book->paragraphs.size());
}

QVariant ParagraphModel::data(const QModelIndex &index, int role) const
{
    if (!m_book || !index.isValid() || index.row() >= m_book->paragraphs.size())
        return {};
    const Paragraph &p = m_book->paragraphs.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TextRole:
        return p.text;
    case HeadingRole:
        return p.heading;
    }
    return {};
}

QHash<int, QByteArray> ParagraphModel::roleNames() const
{
    return {{TextRole, "text"}, {HeadingRole, "heading"}};
}
