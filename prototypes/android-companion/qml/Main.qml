import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherCompanion

ApplicationWindow {
    id: root
    visible: true
    title: "Aether Companion (spike)"

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: discoveryPage
    }

    Connections {
        target: connection
        function onStateChanged() {
            if (connection.state === "connected"
                    && stack.currentItem !== slicePage)
                stack.push(slicePage)
            else if (connection.state !== "connected"
                     && connection.state !== "connecting"
                     && stack.depth > 1)
                stack.pop(null)
        }
    }

    Component {
        id: discoveryPage
        Page {
            header: ToolBar {
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    verticalAlignment: Text.AlignVCenter
                    text: discoveryModel.listening
                          ? "Listening for radios on UDP 4992…"
                          : "Discovery socket failed to bind"
                    font.pixelSize: 16
                }
            }

            ListView {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                model: discoveryModel

                delegate: Frame {
                    id: card
                    width: ListView.view.width

                    required property string radioModel
                    required property string nickname
                    required property string callsign
                    required property string version
                    required property string status
                    required property string address
                    required property int port

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 2
                        Label {
                            text: card.radioModel
                                  + (card.nickname ? "  ·  " + card.nickname : "")
                            font.pixelSize: 20
                            font.bold: true
                        }
                        Label {
                            text: (card.callsign ? card.callsign + "  ·  " : "")
                                  + card.address + "  ·  v" + card.version
                            font.pixelSize: 14
                            opacity: 0.7
                        }
                        Label {
                            text: card.status
                            font.pixelSize: 14
                            color: card.status === "Available" ? "#2e7d32" : "#c62828"
                        }
                    }

                    TapHandler {
                        onTapped: connection.connectToRadio(
                                      card.address, card.port,
                                      card.radioModel + (card.nickname
                                          ? " · " + card.nickname : ""))
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: parent.count === 0
                    text: "No radios found yet.\nPhone must be on the same WiFi as the radio."
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.6
                    font.pixelSize: 16
                }
            }

            footer: ColumnLayout {
                spacing: 0

                // SmartLink (WAN) — login + remote radio list.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: !smartLink.loggedIn
                        TextField {
                            id: slEmail
                            Layout.fillWidth: true
                            placeholderText: "SmartLink email (or harness host:port)"
                            inputMethodHints: Qt.ImhEmailCharactersOnly
                        }
                        TextField {
                            id: slPassword
                            Layout.fillWidth: true
                            placeholderText: "Password"
                            echoMode: TextInput.Password
                        }
                        Button {
                            text: "Login"
                            enabled: slEmail.text.length > 0
                            // Harness mode: "host:port" in the email field,
                            // empty password → local TLS broker, no Auth0.
                            onClicked: slEmail.text.indexOf(":") >= 0
                                       ? smartLink.login("", "", slEmail.text)
                                       : smartLink.login(slEmail.text,
                                                         slPassword.text)
                        }
                    }

                    RowLayout {
                        visible: smartLink.loggedIn
                        Layout.fillWidth: true
                        Label {
                            text: "SmartLink: " + smartLink.authState
                            font.pixelSize: 13
                            opacity: 0.7
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Logout"
                            onClicked: smartLink.logout()
                        }
                    }

                    Label {
                        visible: smartLink.authState.indexOf("failed") >= 0
                                 || smartLink.authState.indexOf("error") >= 0
                        text: smartLink.authState
                        color: "#c62828"
                        font.pixelSize: 13
                    }

                    Repeater {
                        model: smartLink
                        delegate: Frame {
                            id: wanCard
                            Layout.fillWidth: true

                            required property int index
                            required property string radioModel
                            required property string nickname
                            required property string callsign
                            required property string status
                            required property string serial

                            RowLayout {
                                anchors.fill: parent
                                ColumnLayout {
                                    spacing: 2
                                    Layout.fillWidth: true
                                    Label {
                                        text: wanCard.radioModel
                                              + (wanCard.nickname
                                                 ? "  ·  " + wanCard.nickname : "")
                                              + "   (SmartLink)"
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                    Label {
                                        text: (wanCard.callsign
                                               ? wanCard.callsign + "  ·  " : "")
                                              + wanCard.status
                                        font.pixelSize: 13
                                        opacity: 0.7
                                    }
                                }
                                Button {
                                    text: "Connect"
                                    onClicked: smartLink.requestConnect(
                                                   wanCard.index, 14993)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 8
                    TextField {
                        id: manualIp
                        Layout.fillWidth: true
                        placeholderText: "Radio IP (manual connect)"
                        inputMethodHints: Qt.ImhPreferNumbers
                    }
                    Button {
                        text: "Connect"
                        enabled: manualIp.text.length > 0
                        onClicked: connection.connectToRadio(
                                       manualIp.text, 4992,
                                       manualIp.text)
                    }
                }

                Label {
                    visible: connection.state.startsWith("error")
                             || connection.state.startsWith("connecting")
                    text: connection.state
                    padding: 12
                    color: connection.state.startsWith("connecting") ? "#666" : "#c62828"
                }
            }
        }
    }

    Component {
        id: slicePage
        Page {
            header: ToolBar {
                RowLayout {
                    anchors.fill: parent
                    ToolButton {
                        text: "‹"
                        font.pixelSize: 24
                        onClicked: connection.disconnectFromRadio()
                    }
                    Label {
                        text: connection.radioLabel
                        font.pixelSize: 16
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        text: connection.spectrumActive ? "📈" : "📉"
                        font.pixelSize: 20
                        onClicked: connection.spectrumActive
                                   ? connection.stopSpectrum()
                                   : connection.startSpectrum()
                    }
                    ToolButton {
                        visible: connection.vita.opusCapable
                        enabled: !connection.vita.audioActive
                        text: connection.opusEnabled ? "OPUS" : "PCM"
                        font.pixelSize: 13
                        onClicked: connection.opusEnabled = !connection.opusEnabled
                    }
                    ToolButton {
                        text: connection.vita.audioActive ? "🔊" : "🔇"
                        font.pixelSize: 20
                        onClicked: connection.vita.audioActive
                                   ? connection.stopRxAudio()
                                   : connection.startRxAudio()
                    }
                }
            }

            footer: Label {
                visible: connection.vita.audioActive
                padding: 8
                font.pixelSize: 13
                opacity: 0.7
                text: "RX audio: " + connection.vita.packetsReceived
                      + " pkts · " + (connection.vita.bytesPlayed / 1024).toFixed(0)
                      + " KiB played"
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: connection.spectrumActive ? 160 : 0
                    visible: connection.spectrumActive
                    clip: true

                    SpectrumStrip {
                        id: strip
                        anchors.fill: parent
                        yScale: 200

                        Connections {
                            target: connection.vita
                            function onFftFrame(bins) { strip.setFrame(bins) }
                        }

                        // Tap or drag anywhere on the strip tunes slice 0
                        // (spike scope: first slice).
                        function freqAt(x) {
                            return connection.panCenterMhz
                                   - connection.panBandwidthMhz / 2
                                   + (x / width) * connection.panBandwidthMhz
                        }

                        TapHandler {
                            onTapped: (eventPoint) =>
                                connection.tune(0, strip.freqAt(eventPoint.position.x))
                        }
                        DragHandler {
                            target: null
                            onCentroidChanged: {
                                if (active)
                                    connection.tune(0, strip.freqAt(centroid.position.x))
                            }
                        }
                    }

                    Label {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: 6
                        text: connection.panCenterMhz.toFixed(3) + " MHz ± "
                              + (connection.panBandwidthMhz * 500).toFixed(0) + " kHz"
                        color: "#9bd"
                        font.pixelSize: 12
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12
                    model: connection.slices

                delegate: Frame {
                    id: sliceCard
                    width: ListView.view.width

                    required property int sliceId
                    required property double freqMhz
                    required property string mode
                    required property double sMeterDbm

                    // S-units: S9 = -73 dBm, 6 dB per unit below, dB-over above.
                    function sUnits(dbm) {
                        if (dbm <= -140) return "—"
                        var s = 9 + (dbm + 73) / 6
                        if (s <= 9)
                            return "S" + Math.max(0, Math.round(s))
                        return "S9+" + Math.round(dbm + 73)
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Label {
                                text: "Slice " + String.fromCharCode(65 + sliceCard.sliceId)
                                font.pixelSize: 14
                                opacity: 0.7
                            }
                            Label {
                                text: sliceCard.sUnits(sliceCard.sMeterDbm)
                                      + "  (" + sliceCard.sMeterDbm.toFixed(0) + " dBm)"
                                font.pixelSize: 14
                                font.bold: true
                                color: "#2e7d32"
                            }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: modeBox
                                model: ["LSB", "USB", "CW", "AM", "FM", "DIGU", "DIGL"]
                                currentIndex: Math.max(0, model.indexOf(sliceCard.mode))
                                onActivated: connection.setMode(
                                                 sliceCard.sliceId, currentText)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: sliceCard.freqMhz.toFixed(6) + " MHz"
                            font.pixelSize: 34
                            font.bold: true
                            font.family: "monospace"
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            Repeater {
                                model: [
                                    { label: "-10k", step: -0.010 },
                                    { label: "-1k",  step: -0.001 },
                                    { label: "+1k",  step:  0.001 },
                                    { label: "+10k", step:  0.010 },
                                ]
                                Button {
                                    required property var modelData
                                    text: modelData.label
                                    font.pixelSize: 18
                                    onClicked: connection.tune(
                                                   sliceCard.sliceId,
                                                   sliceCard.freqMhz + modelData.step)
                                }
                            }
                        }
                    }
                }

                    Label {
                        anchors.centerIn: parent
                        visible: parent.count === 0
                        text: "Connected — waiting for slice status…"
                        opacity: 0.6
                        font.pixelSize: 16
                    }
                }
            }
        }
    }
}
