#include "SpectrumStrip.h"

#include <QPainter>
#include <QPainterPath>

SpectrumStrip::SpectrumStrip(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
}

void SpectrumStrip::setFrame(QVector<int> bins)
{
    m_bins = std::move(bins);
    update();
}

void SpectrumStrip::paint(QPainter* painter)
{
    painter->fillRect(boundingRect(), QColor("#101418"));
    if (m_bins.isEmpty() || m_yScale <= 0)
        return;

    const qreal w = width();
    const qreal h = height();
    const int n = m_bins.size();

    QPainterPath path;
    path.moveTo(0, h);
    for (int i = 0; i < n; ++i) {
        const qreal x = (n == 1) ? 0 : (w * i) / (n - 1);
        const qreal y = h * qBound(0, m_bins[i], m_yScale) / m_yScale;
        path.lineTo(x, y);
    }
    path.lineTo(w, h);
    path.closeSubpath();

    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillPath(path, QColor(80, 170, 255, 90));
    painter->setPen(QPen(QColor(120, 200, 255), 1.5));
    painter->drawPath(path);
}
