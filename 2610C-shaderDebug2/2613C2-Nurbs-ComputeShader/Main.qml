import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    readonly property real cameraSpin: spin.value
    readonly property real cameraTilt: tilt.value
    readonly property real cameraDistance: distance.value
    readonly property bool showGrid: gridVisible.checked
    readonly property bool showPoints: pointsVisible.checked
    readonly property bool showControlPoints: controlPointsVisible.checked

    property int surfaceStyle: 0
    readonly property int inputRows: [10, 13, 16][surfaceStyle]
    readonly property int inputCols: [11, 9, 12][surfaceStyle]
    readonly property var surfaceStyleNames: ["Waves", "Bowl", "Saddle"]

    signal settingsChanged

    onSurfaceStyleChanged: settingsChanged()
    onCameraSpinChanged: settingsChanged()
    onCameraTiltChanged: settingsChanged()
    onCameraDistanceChanged: settingsChanged()
    onShowGridChanged: settingsChanged()
    onShowPointsChanged: settingsChanged()
    onShowControlPointsChanged: settingsChanged()

    width: 280
    height: 800
    color: "#1c293c"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Label {
            text: "NURBS surface"
            color: "white"
            font.pixelSize: 22
        }

        Label {
            text: panel.inputRows + " × " + panel.inputCols + " input grid · cubic\nWeights = 1"
            color: "#b8c9db"
        }

        Label {
            text: "Shape: " + panel.surfaceStyleNames[panel.surfaceStyle]
            color: "#b8c9db"
        }

        Button {
            text: "Change surface style"
            Layout.fillWidth: true
            onClicked: panel.surfaceStyle = (panel.surfaceStyle + 1) % panel.surfaceStyleNames.length
        }

        Label {
            text: "Rotation"
            color: "white"
        }

        Slider {
            id: spin

            Layout.fillWidth: true
            from: -180
            to: 180
            value: 25
        }

        Label {
            text: "Tilt"
            color: "white"
        }

        Slider {
            id: tilt

            Layout.fillWidth: true
            from: -90
            to: 90
            value: 55
        }

        Label {
            text: "Camera distance"
            color: "white"
        }

        Slider {
            id: distance

            Layout.fillWidth: true
            from: 220
            to: 900
            value: 450
        }

        CheckBox {
            id: gridVisible

            text: "Surface grid"
            checked: true
            palette.windowText: "white"
        }

        CheckBox {
            id: pointsVisible

            text: "Input points"
            checked: true
            palette.windowText: "white"
        }

        CheckBox {
            id: controlPointsVisible

            text: "Control points (green)"
            checked: true
            palette.windowText: "white"
        }

        Button {
            text: "Reset view"
            onClicked: {
                spin.value = 25;
                tilt.value = 55;
                distance.value = 450;
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
