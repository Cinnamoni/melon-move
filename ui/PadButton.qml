// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.15

Rectangle {
    id: button
    property string label
    property bool held: mouse.pressed || finger.pressed
    property real size: 56
    width: size
    height: size
    radius: size / 4
    color: held ? "#dd202328" : "#99202328"
    border.color: "#99ffffff"
    border.width: 1
    Text {
        anchors.centerIn: parent
        text: button.label
        color: "white"
        font.pixelSize: button.size * 0.34
        font.bold: true
    }
    MultiPointTouchArea {
        anchors.fill: parent
        mouseEnabled: false
        maximumTouchPoints: 1
        touchPoints: TouchPoint { id: finger }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        preventStealing: true
    }
}
