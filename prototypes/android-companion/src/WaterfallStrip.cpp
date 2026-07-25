#include "WaterfallStrip.h"

#include <QPainter>

namespace {
constexpr int kHistoryRows = 200;
} // namespace

WaterfallStrip::WaterfallStrip(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
}

QRgb WaterfallStrip::colorFor(float dbm) const
{
    const double range = m_maxDbm - m_minDbm;
    if (range <= 0)
        return qRgb(0, 0, 0);
    double t = (dbm - m_minDbm) / range;
    t = qBound(0.0, t, 1.0);
    // black → deep blue → yellow → white
    if (t < 0.4) {
        const double u = t / 0.4;
        return qRgb(0, 0, static_cast<int>(160 * u));
    }
    if (t < 0.8) {
        const double u = (t - 0.4) / 0.4;
        return qRgb(static_cast<int>(255 * u), static_cast<int>(215 * u),
                    static_cast<int>(160 * (1 - u)));
    }
    const double u = (t - 0.8) / 0.2;
    return qRgb(255, static_cast<int>(215 + 40 * u),
                static_cast<int>(255 * u));
}

void WaterfallStrip::addRow(QVector<float> dbmBins)
{
    if (dbmBins.isEmpty())
        return;
    if (m_history.width() != dbmBins.size()) {
        m_history = QImage(dbmBins.size(), kHistoryRows, QImage::Format_RGB32);
        m_history.fill(Qt::black);
    }

    // Scroll down one row, paint the new row at the top.
    std::memmove(m_history.scanLine(1), m_history.scanLine(0),
                 static_cast<size_t>(m_history.bytesPerLine())
                     * (kHistoryRows - 1));
    auto* top = reinterpret_cast<QRgb*>(m_history.scanLine(0));
    for (int i = 0; i < dbmBins.size(); ++i)
        top[i] = colorFor(dbmBins[i]);

    update();
}

void WaterfallStrip::paint(QPainter* painter)
{
    if (m_history.isNull()) {
        painter->fillRect(boundingRect(), QColor("#000814"));
        return;
    }
    painter->drawImage(boundingRect(), m_history);
}
