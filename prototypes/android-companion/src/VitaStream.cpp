#include "VitaStream.h"

#include <QAudioFormat>
#include <QMediaDevices>
#include <QNetworkDatagram>
#include <QtEndian>

#ifdef HAVE_OPUS
#include <opus/opus.h>
#endif

namespace {
constexpr int kVitaHeaderBytes = 28;
constexpr quint16 kPccIfNarrow = 0x03E3;
constexpr quint16 kPccOpus = 0x8005;
constexpr quint16 kPccFft = 0x8003;
constexpr quint16 kPccWaterfall = 0x8004;
constexpr quint16 kPccMeter = 0x8002;
constexpr int kTileSubheaderBytes = 36;
constexpr int kFftSubheaderBytes = 12;
constexpr int kSampleRate = 24000;
// One Opus frame per VITA packet, 10 ms at 24 kHz (OpusCodec.h facts).
constexpr int kOpusFrameSamples = 240;
} // namespace

bool VitaStream::opusCapable()
{
#ifdef HAVE_OPUS
    return true;
#else
    return false;
#endif
}

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
#ifdef HAVE_OPUS
    if (m_opusDecoder) {
        opus_decoder_destroy(m_opusDecoder);
        m_opusDecoder = nullptr;
    }
#endif
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
        m_sinkIo = nullptr;
        emit statsChanged();
    }
}

void VitaStream::setWaterfallStream(quint32 streamId)
{
    m_wfStreamId = streamId;
    m_wfBuf.clear();
    m_wfBinsReceived = 0;
}

void VitaStream::clearWaterfallStream()
{
    m_wfStreamId = 0;
    m_wfBuf.clear();
    m_wfBinsReceived = 0;
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
        } else if (pcc == kPccOpus && streamId == m_audioStreamId) {
            ++m_packetsReceived;
            handleOpusAudio(raw, data.size(), hasTrailer);
        } else if (pcc == kPccFft && streamId == m_fftStreamId) {
            ++m_packetsReceived;
            handleFft(raw, data.size(), hasTrailer);
        } else if (pcc == kPccWaterfall && streamId == m_wfStreamId) {
            ++m_packetsReceived;
            handleWaterfall(raw, data.size(), hasTrailer);
        } else if (pcc == kPccMeter) {
            ++m_packetsReceived;
            handleMeter(raw, data.size(), hasTrailer);
        }
    }
}

void VitaStream::writePcmFloats(const float* samples, int count)
{
    if (!m_sinkIo || count <= 0)
        return;
    m_bytesPlayed += m_sinkIo->write(
        reinterpret_cast<const char*>(samples),
        count * static_cast<qint64>(sizeof(float)));
}

void VitaStream::handleAudio(const uchar* raw, int size, bool hasTrailer)
{
    const int payloadBytes = size - kVitaHeaderBytes - (hasTrailer ? 4 : 0);
    const int numFloats = payloadBytes / 4;
    if (numFloats <= 0 || !m_sinkIo)
        return;

    // Big-endian float32 → native for the sink.
    QVector<float> pcm(numFloats);
    const uchar* src = raw + kVitaHeaderBytes;
    for (int i = 0; i < numFloats; ++i) {
        const quint32 u = qFromBigEndian<quint32>(src + i * 4);
        std::memcpy(&pcm[i], &u, 4);
    }
    writePcmFloats(pcm.constData(), numFloats);
}

void VitaStream::handleOpusAudio(const uchar* raw, int size, bool hasTrailer)
{
#ifdef HAVE_OPUS
    const int payloadBytes = size - kVitaHeaderBytes - (hasTrailer ? 4 : 0);
    if (payloadBytes <= 0 || !m_sinkIo)
        return;

    if (!m_opusDecoder) {
        int err = 0;
        m_opusDecoder = opus_decoder_create(kSampleRate, 2, &err);
        if (err != OPUS_OK || !m_opusDecoder) {
            m_opusDecoder = nullptr;
            return;
        }
    }

    // Payload = one Opus frame (PanadapterStream::decodeOpusAudio).
    // Decode to int16 stereo, convert to float for the sink.
    int16_t frame[kOpusFrameSamples * 2];
    const int samples = opus_decode(
        m_opusDecoder, raw + kVitaHeaderBytes, payloadBytes,
        frame, kOpusFrameSamples, 0);
    if (samples <= 0)
        return;

    QVector<float> pcm(samples * 2);
    for (int i = 0; i < samples * 2; ++i)
        pcm[i] = frame[i] / 32768.0f;
    writePcmFloats(pcm.constData(), samples * 2);
#else
    Q_UNUSED(raw);
    Q_UNUSED(size);
    Q_UNUSED(hasTrailer);
#endif
}

void VitaStream::handleWaterfall(const uchar* raw, int size, bool hasTrailer)
{
    // Tile: 36-byte subheader — lowFreq i64 (Hz × 2^20), binBw i64,
    // [16..19 reserved], tileWidth u16 @20, tileHeight u16 @22,
    // timecode u32 @24, autoBlack u32 @28, totalBinsInFrame u16 @32,
    // firstBinIndex u16 @34; payload u16 BE bins, dBm = int16/128.
    // Facts + frame-assembly rules per PanadapterStream::decodeWaterfallTile
    // (including the GHSA-7gvg-x594-pprq reset-on-totalBins-change guard).
    if (size < kVitaHeaderBytes + kTileSubheaderBytes)
        return;
    const uchar* sub = raw + kVitaHeaderBytes;
    const quint16 tileWidth = qFromBigEndian<quint16>(sub + 20);
    const quint32 timecode = qFromBigEndian<quint32>(sub + 24);
    const quint16 totalBins = qFromBigEndian<quint16>(sub + 32);
    const quint16 firstBin = qFromBigEndian<quint16>(sub + 34);
    if (tileWidth == 0 || totalBins == 0)
        return;

    const int payloadOffset = kVitaHeaderBytes + kTileSubheaderBytes;
    const int payloadBytes = size - payloadOffset - (hasTrailer ? 4 : 0);
    if (payloadBytes < tileWidth * 2)
        return;

    if (timecode != m_wfTimecode || m_wfBuf.size() != totalBins) {
        m_wfTimecode = timecode;
        m_wfBuf.fill(-999.0f, totalBins);
        m_wfBinsReceived = 0;
    }

    const int binsToRead =
        qMin<int>(tileWidth, static_cast<int>(totalBins) - firstBin);
    if (binsToRead <= 0 || firstBin + binsToRead > m_wfBuf.size())
        return;

    const uchar* payload = raw + payloadOffset;
    for (int i = 0; i < binsToRead; ++i) {
        const auto raw16 =
            static_cast<qint16>(qFromBigEndian<quint16>(payload + i * 2));
        m_wfBuf[firstBin + i] = static_cast<float>(raw16) / 128.0f;
    }
    m_wfBinsReceived += binsToRead;

    if (m_wfBinsReceived >= totalBins) {
        emit waterfallRow(m_wfBuf);
        m_wfBinsReceived = 0;
    }
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
