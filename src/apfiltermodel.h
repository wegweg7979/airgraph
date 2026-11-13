#pragma once

#include <QSortFilterProxyModel>

// The table shows one band at a time, like the graph above it, so the count in
// the header and the rows underneath cannot disagree.
class ApFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int gigahertz READ gigahertz WRITE setGigahertz NOTIFY gigahertzChanged)

public:
    explicit ApFilterModel(QObject *parent = nullptr);

    int count() const { return rowCount(); }
    int gigahertz() const { return m_gigahertz; }
    void setGigahertz(int gigahertz);

signals:
    void countChanged();
    void gigahertzChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_gigahertz = 2;
};
