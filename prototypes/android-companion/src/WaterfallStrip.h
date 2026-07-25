#pragma once

// Scrolling waterfall for the spike: each dBm row shifts the history
// image down one pixel and paints the new row at the top through a
// simple black→blue→yellow→white ramp over [minDbm, maxDbm].

#include <QImage>
#include <QQuickPaintedItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class WaterfallStrip : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double minDbm MEMBER m_minDbm NOTIFY rangeChanged)
    Q_PROPERTY(double maxDbm MEMBER m_maxDbm NOTIFY rangeChanged)

public:
    explicit WaterfallStrip(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

public slots:
    void addRow(QVector<float> dbmBins);

signals:
    void rangeChanged();

private:
    QRgb colorFor(float dbm) const;

    QImage m_history;
    double m_minDbm{-130.0};
    double m_maxDbm{-40.0};
};
