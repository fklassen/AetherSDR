#include "VitaStream.h"

#include <QAudioFormat>
#include <QMediaDevices>
#include <QNetworkDatagram>
#include <QtEndian>

namespace {
constexpr int kVitaHeaderBytes = 28;
constexpr quint16 kPccIfNarrow = 0x03E3;
constexpr quint16 kPccFft = 0x8003;
constexpr quint16 kPccMeter = 0x8002;
constexpr int kFftSubheaderBytes = 12;
constexpr int kSampleRate = 24000;
} // namespace

VitaStream::VitaStream(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &VitaStream::onReadyRead);

    m_primeTimer.setInterval(5000);
    connect(&m_primeTimer, &QTimer::timeout, this, &VitaStream::sendPrime);

    // Coalesce stats notifications; per-packet emits (~100/s) would thrash
    // the QML bindings.
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &VitaStream::statsChanged);
}

void VitaStream::openSocket(const QHostAddress& radioAddress)
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        return;
    m_radioAddress = radioAddress;
    m_packetsReceived = 0;
    m_bytesPlayed = 0;
    m_socket.bind(QHostAddress::AnyIPv4, kLocalPort,
                  QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    sendPrime();
    m_primeTimer.start();
    m_statsTimer.start();
    emit statsChanged();
}

void VitaStream::closeSocket()
{
    clearAudioStream();
    clearFftStream();
    m_primeTimer.stop();
    m_statsTimer.stop();
    m_socket.close();
}

void VitaStream::setAudioStream(quint32 streamId)
{
    clearAudioStream();
    m_audioStreamId = streamId;

    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);

    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
    m_sinkIo = m_audioSink->start();
    emit statsChanged();
}

void VitaStream::clearAudioStream()
{
    m_audioStreamId = 0;
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
        m_sinkIo = nullptr;
        emit statsChanged();
    }
}

void VitaStream::setFftStream(quint32 streamId)
{
    m_fftStreamId = streamId;
    m_frameBuf.clear();
    m_frameBinsReceived = 0;
}

void VitaStream::clearFftStream()
{
    m_fftStreamId = 0;
    m_frameBuf.clear();
    m_frameBinsReceived = 0;
}

void VitaStream::sendPrime()
{
    if (m_radioAddress.isNull())
        return;
    const QByteArray reg(1, '\x00');
    m_socket.writeDatagram(reg, m_radioAddress, 4992);
}

void VitaStream::onReadyRead()
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

        if (m_packetsReceived == 0)
            m_primeTimer.stop();

        if (pcc == kPccIfNarrow && streamId == m_audioStreamId) {
            ++m_packetsReceived;
            handleAudio(raw, data.size(), hasTrailer);
        } else if (pcc == kPccFft && streamId == m_fftStreamId) {
            ++m_packetsReceived;
            handleFft(raw, data.size(), hasTrailer);
        } else if (pcc == kPccMeter) {
            ++m_packetsReceived;
            handleMeter(raw, data.size(), hasTrailer);
        }
    }
}

void VitaStream::handleAudio(const uchar* raw, int size, bool hasTrailer)
{
    const int payloadBytes = size - kVitaHeaderBytes - (hasTrailer ? 4 : 0);
    const int numFloats = payloadBytes / 4;
    if (numFloats <= 0 || !m_sinkIo)
        return;

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

void VitaStream::handleMeter(const uchar* raw, int size, bool hasTrailer)
{
    // Payload: repeated (u16 meter id, s16 raw value), big-endian —
    // facts mirror PanadapterStream::decodeMeterData.
    const int payloadBytes = size - kVitaHeaderBytes - (hasTrailer ? 4 : 0);
    const int numMeters = payloadBytes / 4;
    if (numMeters <= 0)
        return;
    const uchar* payload = raw + kVitaHeaderBytes;
    QVector<quint16> ids(numMeters);
    QVector<qint16> values(numMeters);
    for (int i = 0; i < numMeters; ++i) {
        ids[i] = qFromBigEndian<quint16>(payload + i * 4);
        values[i] = qFromBigEndian<qint16>(payload + i * 4 + 2);
    }
    emit meterData(ids, values);
}

void VitaStream::handleFft(const uchar* raw, int size, bool hasTrailer)
{
    if (size < kVitaHeaderBytes + kFftSubheaderBytes)
        return;
    const uchar* sub = raw + kVitaHeaderBytes;
    const quint16 startBin = qFromBigEndian<quint16>(sub + 0);
    const quint16 numBins = qFromBigEndian<quint16>(sub + 2);
    const quint16 binSize = qFromBigEndian<quint16>(sub + 4);
    const quint16 totalBins = qFromBigEndian<quint16>(sub + 6);
    const quint32 frameIndex = qFromBigEndian<quint32>(sub + 8);

    if (numBins == 0 || binSize != 2 || totalBins == 0)
        return;

    const int binDataOffset = kVitaHeaderBytes + kFftSubheaderBytes;
    const int available = size - binDataOffset - (hasTrailer ? 4 : 0);
    if (available < numBins * 2)
        return;

    if (frameIndex != m_frameIndex || m_frameBuf.size() != totalBins) {
        m_frameIndex = frameIndex;
        m_frameBuf.fill(0, totalBins);
        m_frameBinsReceived = 0;
    }
    if (startBin + numBins > m_frameBuf.size())
        return;

    const uchar* binData = raw + binDataOffset;
    for (quint16 i = 0; i < numBins; ++i)
        m_frameBuf[startBin + i] = qFromBigEndian<quint16>(binData + i * 2);
    m_frameBinsReceived += numBins;

    if (m_frameBinsReceived < totalBins)
        return;

    QVector<int> bins(totalBins);
    for (int i = 0; i < totalBins; ++i)
        bins[i] = m_frameBuf[i];
    emit fftFrame(bins);
    m_frameBinsReceived = 0;
}
