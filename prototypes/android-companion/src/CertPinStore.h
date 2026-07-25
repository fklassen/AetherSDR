#pragma once

// Trust-on-first-use certificate pin store for the spike's WAN path.
// Mirrors the desktop model in src/core/WanConnection.cpp
// (GHSA-wfx7-w6p8-4jr2): SHA-256 digest of the peer certificate, hex,
// keyed by host, with the pin timestamp kept alongside.
//
// Spike caveat (same as the rest of the spike's persistence): this uses
// QSettings, which AGENTS.md bans in the main tree. An in-tree version
// must move to the project's client-persistence layer.

#include <QString>
#include <QVector>

struct PinnedCert {
    QString host;
    QString fingerprintHex;
    QString pinnedAtIso;
};

namespace CertPinStore {

// Empty when the host has never been pinned.
QString load(const QString& host);
void store(const QString& host, const QString& fingerprintHex);

QVector<PinnedCert> list();
void forget(const QString& host);
void forgetAll();

} // namespace CertPinStore
