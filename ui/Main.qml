// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.15
import QtQuick.Controls 2.15
import net.asivery.AppLoad 1.0

Rectangle {
    id: root
    anchors.fill: parent
    color: "#f7f7f4"
    signal close()
    property string game: ""
    property string status: "Connecting to melonDS…"
    property bool connected: false
    property bool paused: false
    property bool closing: false
    property bool docked: false
    property bool inkTouch: false
    property bool mouseTouch: false
    property var heldButtons: ({})
    property real unit: Math.min(width / 12, height / 15)

    function command(type, data) {
        endpoint.sendMessage(type, data === undefined ? "1" : data)
    }
    function unloading() {
        closing = true
        endpoint.terminate()
    }
    function setButton(mask, held) {
        if (!game || paused || closing)
            return
        heldButtons[mask] = held
        let bits = 0
        for (let key in heldButtons) {
            if (heldButtons[key])
                bits |= Number(key)
        }
        command(3, String(bits))
    }
    function releaseInputs() {
        heldButtons = ({})
        inkTouch = mouseTouch = false
        if (game && !closing) {
            command(3, "0")
            command(4, "0 0 0")
        }
    }
    function touchAt(px, py, down) {
        if (!game || paused || closing)
            return
        const x = Math.max(0, Math.min(255, Math.floor(px * 256 / digitizer.width)))
        const y = Math.max(0, Math.min(191, Math.floor(py * 192 / digitizer.height)))
        command(4, (down ? "1 " : "0 ") + x + " " + y)
    }
    onVisibleChanged: if (!visible) releaseInputs()

    AppLoad {
        id: endpoint
        applicationID: "melon-move"
        onMessageReceived: (type, contents) => {
            switch (type) {
            case 100:
                roms.model = JSON.parse(contents)
                root.connected = true
                root.status = roms.count ? "" : "Copy .nds files to /home/root/melon-move/roms."
                break
            case 101:
                if (root.game)
                    screens.source = contents
                break
            case 102:
                root.status = contents
                break
            case 103:
                root.releaseInputs()
                root.game = contents
                root.paused = false
                root.status = ""
                screens.source = ""
                if (!contents)
                    root.command(1)
                break
            case 104:
                root.paused = contents === "1"
                break
            }
        }
    }

    Timer {
        running: !root.connected && !root.closing
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: root.command(1)
    }

    Item {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: root.unit * 0.25 }
        height: root.unit * 0.84

        Text {
            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: root.unit * 0.22
                      right: actions.left; rightMargin: root.unit * 0.2 }
            text: root.game || "melonDS"
            elide: Text.ElideRight
            font.pixelSize: root.unit * 0.36
            font.bold: true
            color: "#202328"
            textFormat: Text.PlainText
        }

        Row {
            id: actions
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: root.unit * 0.12
            Button {
                visible: !!root.game
                text: root.paused ? "Resume" : "Pause"
                onClicked: { root.releaseInputs(); root.command(6) }
            }
            Button {
                enabled: root.connected
                text: root.game ? "Library" : "Refresh"
                onClicked: {
                    root.status = ""
                    if (root.game) {
                        root.releaseInputs()
                        root.command(5)
                    } else root.command(1)
                }
            }
            Button {
                visible: !!root.game
                text: root.docked ? "Overlay" : "Dock"
                onClicked: { root.releaseInputs(); root.docked = !root.docked }
            }
            Button { text: "Close"; onClicked: root.close() }
        }
    }

    Text {
        id: statusLine
        anchors { left: header.left; right: header.right; top: header.bottom }
        height: visible ? implicitHeight + root.unit * 0.15 : 0
        visible: text.length > 0
        text: root.status
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        color: "#343840"
        font.pixelSize: root.unit * 0.29
    }

    ListView {
        id: roms
        anchors { top: statusLine.bottom; left: header.left; right: header.right; bottom: parent.bottom
                  topMargin: root.unit * 0.24 }
        visible: !root.game
        clip: true
        ScrollBar.vertical: ScrollBar {}
        delegate: Button {
            width: ListView.view.width
            height: root.unit
            text: modelData
            contentItem: Text {
                text: modelData
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: root.unit * 0.22
                font.pixelSize: root.unit * 0.32
                textFormat: Text.PlainText
            }
            onClicked: {
                root.status = "Loading…"
                root.command(2, modelData)
            }
        }
    }

    Item {
        id: playfield
        visible: !!root.game
        anchors { top: statusLine.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        property real reserved: root.docked ? controls.height + root.unit * 0.35 : 0
        property real gameWidth: Math.max(1, Math.min(width * 0.98, (height - reserved) * 256 / 384))
        Image {
            id: screens
            anchors.horizontalCenter: parent.horizontalCenter
            y: (playfield.height - playfield.reserved - height) / 2
            width: playfield.gameWidth
            height: width * 384 / 256
            cache: false
            asynchronous: false
            smooth: false
        }
        Item {
            id: digitizer
            x: screens.x
            y: screens.y + screens.height / 2
            width: screens.width
            height: screens.height / 2
            enabled: !root.paused

            PointHandler {
                id: marker
                acceptedDevices: PointerDevice.Stylus
                acceptedButtons: Qt.LeftButton
                onActiveChanged: {
                    if (active && root.mouseTouch) {
                        root.mouseTouch = false
                        root.touchAt(0, 0, false)
                    }
                    root.inkTouch = active
                    root.touchAt(point.position.x, point.position.y, active)
                }
                onPointChanged: if (active) root.touchAt(point.position.x, point.position.y, true)
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                onPressed: (event) => {
                    if (!root.inkTouch) {
                        root.mouseTouch = true
                        root.touchAt(event.x, event.y, true)
                    }
                }
                onPositionChanged: (event) => {
                    if (root.mouseTouch && !root.inkTouch) root.touchAt(event.x, event.y, true)
                }
                onReleased: {
                    if (root.mouseTouch) root.touchAt(0, 0, false)
                    root.mouseTouch = false
                }
                onCanceled: {
                    if (root.mouseTouch) root.touchAt(0, 0, false)
                    root.mouseTouch = false
                }
            }
        }

        Item {
            id: controls
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                      margins: root.unit * 0.24 }
            height: root.unit * 3.9
            enabled: !root.paused
            property real step: root.unit * 0.89

            Repeater {
                // Bit numbers follow the Nintendo DS keypad register.
                model: [
                    { label: "↑", mask: 64, column: 1, row: 1, right: false },
                    { label: "←", mask: 32, column: 0, row: 2, right: false },
                    { label: "→", mask: 16, column: 2, row: 2, right: false },
                    { label: "↓", mask: 128, column: 1, row: 3, right: false },
                    { label: "L", mask: 512, column: 0, row: 0, right: false },
                    { label: "X", mask: 1024, column: 1, row: 1, right: true },
                    { label: "Y", mask: 2048, column: 2, row: 2, right: true },
                    { label: "A", mask: 1, column: 0, row: 2, right: true },
                    { label: "B", mask: 2, column: 1, row: 3, right: true },
                    { label: "R", mask: 256, column: 0, row: 0, right: true }
                ]
                delegate: PadButton {
                    size: controls.step * 0.94
                    x: modelData.right ? controls.width - size - modelData.column * controls.step
                                       : modelData.column * controls.step
                    y: modelData.row * controls.step
                    label: modelData.label
                    onHeldChanged: root.setButton(modelData.mask, held)
                }
            }
            Row {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                spacing: root.unit * 0.18
                PadButton {
                    size: controls.step * 0.94
                    label: "SEL"
                    onHeldChanged: root.setButton(4, held)
                }
                PadButton {
                    size: controls.step * 0.94
                    label: "START"
                    onHeldChanged: root.setButton(8, held)
                }
            }
        }
    }
}
