#include "SliceListModel.h"

int SliceListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_slices.size());
}

QVariant SliceListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_slices.size()))
        return {};
    const SpikeSlice& s = m_slices[static_cast<size_t>(index.row())];
    switch (role) {
    case SliceIdRole: return s.id;
    case FreqMhzRole: return s.freqMhz;
    case ModeRole:    return s.mode;
    }
    return {};
}

QHash<int, QByteArray> SliceListModel::roleNames() const
{
    return {
        {SliceIdRole, "sliceId"},
        {FreqMhzRole, "freqMhz"},
        {ModeRole, "mode"},
    };
}

int SliceListModel::rowOf(int sliceId) const
{
    for (size_t i = 0; i < m_slices.size(); ++i)
        if (m_slices[i].id == sliceId)
            return static_cast<int>(i);
    return -1;
}

void SliceListModel::applyStatus(int sliceId, const QHash<QString, QString>& kvs)
{
    // Removal: "slice <id> in_use=0" tears the slice down.
    if (kvs.value("in_use") == "0") {
        const int row = rowOf(sliceId);
        if (row >= 0) {
            beginRemoveRows({}, row, row);
            m_slices.erase(m_slices.begin() + row);
            endRemoveRows();
        }
        return;
    }

    int row = rowOf(sliceId);
    if (row < 0) {
        SpikeSlice s;
        s.id = sliceId;
        beginInsertRows({}, static_cast<int>(m_slices.size()),
                        static_cast<int>(m_slices.size()));
        m_slices.push_back(s);
        endInsertRows();
        row = static_cast<int>(m_slices.size()) - 1;
    }

    SpikeSlice& s = m_slices[static_cast<size_t>(row)];
    if (kvs.contains("RF_frequency"))
        s.freqMhz = kvs.value("RF_frequency").toDouble();
    if (kvs.contains("mode"))
        s.mode = kvs.value("mode");
    if (kvs.contains("in_use"))
        s.inUse = kvs.value("in_use") == "1";

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void SliceListModel::clear()
{
    beginResetModel();
    m_slices.clear();
    endResetModel();
}
