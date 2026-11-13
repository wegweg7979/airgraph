#include "apfiltermodel.h"

#include "apmodel.h"

ApFilterModel::ApFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    setSortRole(ApModel::SignalRole);

    // The row count is a header line, so it has to follow every structural
    // change the proxy makes.
    connect(this, &QAbstractItemModel::rowsInserted, this, &ApFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ApFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ApFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ApFilterModel::countChanged);
}

void ApFilterModel::setGigahertz(int gigahertz) {
    if (m_gigahertz == gigahertz)
        return;
    m_gigahertz = gigahertz;
    invalidateFilter();
    emit gigahertzChanged();
    emit countChanged();
}

bool ApFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;
    return index.data(ApModel::BandRole).toInt() == m_gigahertz;
}
