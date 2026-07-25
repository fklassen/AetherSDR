#pragma once

// Slice list for the spike's connected screen. Rows track the minimal
// tune-relevant subset of SmartSDR slice status (see src/models/SliceModel).

#include <QAbstractListModel>

#include <vector>

struct SpikeSlice {
    int id{-1};
    double freqMhz{0.0};
    QString mode;
    bool inUse{false};
    bool muted{false};
    double sMeterDbm{-150.0};
};

class SliceListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SliceIdRole = Qt::UserRole + 1,
        FreqMhzRole,
        ModeRole,
        SMeterDbmRole,
        MutedRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Apply one "slice <id> key=value ..." status body.
    void applyStatus(int sliceId, const QHash<QString, QString>& kvs);
    void setSMeter(int sliceId, double dbm);
    void clear();

private:
    int rowOf(int sliceId) const;

    std::vector<SpikeSlice> m_slices;
};
