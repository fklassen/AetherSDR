#pragma once

// Touch spectrum strip for the spike: paints the latest FFT frame as a
// filled trace. Bin values are y-pixels from the top at the pan's
// ypixels scale (yScale property), per PanadapterStream's FFT format.

#include <QQuickPaintedItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class SpectrumStrip : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int yScale MEMBER m_yScale NOTIFY yScaleChanged)

public:
    explicit SpectrumStrip(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

public slots:
    void setFrame(QVector<int> bins);

signals:
    void yScaleChanged();

private:
    QVector<int> m_bins;
    int m_yScale{200};
};
