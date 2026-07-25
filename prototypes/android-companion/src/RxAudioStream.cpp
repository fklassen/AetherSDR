#include "RxAudioStream.h"

#include <QAudioFormat>
#include <QMediaDevices>
#include <QNetworkDatagram>
#include <QtEndian>

namespace {
constexpr int kVitaHeaderBytes = 28;
constexpr quint16 kPccIfNarrow = 0x03E3;
constexpr int kSampleRate = 24000;
} // namespace

RxAudioStream::RxAudioStream(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &RxAudioStream::onReadyRead);

    m_primeTimer.setInterval(5000);
    connect(&m_primeTimer, &QTimer::timeout, this, &RxAudioStream::sendPrime);

    // Coalesce stats notifications; per-packet emits (~100/s) would thrash
    // the QML bindings.
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &RxAudioStream::statsChanged);
}

void RxAudioStream::start(quint32 streamId, const QHostAddress& radioAddress)
{
    stop();

    m_streamId = streamId;
    m_radioAddress = radioAddress;
    m_packetsReceived = 0;
    m_bytesPlayed = 0;

    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);

    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
    m_sinkIo = m_sink->start();

    m_socket.bind(QHostAddress::AnyIPv4, kLocalPort,
                  QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    m_active = true;
    sendPrime();
    m_primeTimer.start();
    m_statsTimer.start();
    emit statsChanged();
}

void RxAudioStream::stop()
{
    m_primeTimer.stop();
    m_statsTimer.stop();
    m_socket.close();
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
        m_sinkIo = nullptr;
    }
    if (m_active) {
        m_active = false;
        emit statsChanged();
    }
}

void RxAudioStream::sendPrime()
{
    if (m_radioAddress.isNull())
        return;
    const QByteArray reg(1, '\x00');
    m_socket.writeDatagram(reg, m_radioAddress, 4992);
}

void RxAudioStream::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        const QByteArray data = m_socket.receiveDatagram().data();
        if (data.size() <= kVitaHeaderBytes)
            continue;
        const auto* raw = reinterpret_cast<const uchar*>(data.constData());

        const quint32 word0 = qFromBigEndian<quint32>(raw);
        const quint32 streamId = qFromBigEndian<quint32>(raw + 4);
        const quint16 pcc =
            static_cast<quint16>(qFromBigEndian<quint32>(raw + 12) & 0xFFFFu);
        const bool hasTrailer = (word0 & 0x04000000u) != 0;

        if (pcc != kPccIfNarrow || streamId != m_streamId)
            continue;

        if (m_packetsReceived == 0)
            m_primeTimer.stop();
        ++m_packetsReceived;

        const int payloadBytes =
            data.size() - kVitaHeaderBytes - (hasTrailer ? 4 : 0);
        const int numFloats = payloadBytes / 4;
        if (numFloats <= 0 || !m_sinkIo)
            continue;

        // Big-endian float32 → native for the sink.
        QByteArray pcm(numFloats * static_cast<int>(sizeof(float)), Qt::Uninitialized);
        auto* dst = reinterpret_cast<float*>(pcm.data());
        const uchar* src = raw + kVitaHeaderBytes;
        for (int i = 0; i < numFloats; ++i) {
            const quint32 u = qFromBigEndian<quint32>(src + i * 4);
            std::memcpy(&dst[i], &u, 4);
        }
        m_bytesPlayed += m_sinkIo->write(pcm);
    }
}
