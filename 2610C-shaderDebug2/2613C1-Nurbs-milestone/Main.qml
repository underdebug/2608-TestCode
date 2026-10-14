import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    required property real interpolationError
    readonly property real cameraSpin: spin.value
    readonly property real cameraTilt: tilt.value
    readonly property real cameraDistance: distance.value
    readonly property bool showGrid: gridVisible.checked
    readonly property bool showPoints: pointsVisible.checked
    readonly property bool showControlPoints: controlPointsVisible.checked

    signal settingsChanged

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
            text: "100 input points · cubic\nWeights = 1\nGPU surface evaluation"
            color: "#b8c9db"
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

        Label {
            Layout.fillWidth: true
            text: "CPU interpolation error:\n" + panel.interpolationError.toExponential(3)
            color: "#b8c9db"
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
