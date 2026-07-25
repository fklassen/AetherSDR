import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

            footer: Label {
                visible: connection.state.startsWith("error")
                         || connection.state === "connecting"
                text: connection.state
                padding: 12
                color: connection.state === "connecting" ? "#666" : "#c62828"
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
                }
            }

            ListView {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                model: connection.slices

                delegate: Frame {
                    id: sliceCard
                    width: ListView.view.width

                    required property int sliceId
                    required property double freqMhz
                    required property string mode

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Label {
                                text: "Slice " + String.fromCharCode(65 + sliceCard.sliceId)
                                font.pixelSize: 14
                                opacity: 0.7
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
