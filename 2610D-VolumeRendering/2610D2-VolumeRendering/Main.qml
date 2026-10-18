import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    readonly property real cameraSpin: spin.value
    readonly property real cameraTilt: tilt.value
    readonly property real cameraDistance: distance.value
    property real cameraPanX: 0
    property real cameraPanY: 0
    property real cameraPanZ: 0
    property real coordinateX: 0
    property real coordinateY: 0
    property real coordinateZ: 0
    readonly property real windowCenter: center.value
    readonly property real windowWidth: widthSlider.value
    readonly property real volumeOpacity: density.value
    property bool vesselMode: true
    onVesselModeChanged: settingsChanged()
    onWindowCenterChanged: settingsChanged()
    onWindowWidthChanged: settingsChanged()
    onVolumeOpacityChanged: settingsChanged()

    signal settingsChanged

    function setCamera(spinValue, tiltValue, distanceValue) {
        spin.value = spinValue;
        tilt.value = tiltValue;
        distance.value = distanceValue;
    }

    onCameraSpinChanged: settingsChanged()
    onCameraTiltChanged: settingsChanged()
    onCameraDistanceChanged: settingsChanged()
    onCameraPanXChanged: settingsChanged()
    onCameraPanYChanged: settingsChanged()
    onCameraPanZChanged: settingsChanged()
    onCoordinateXChanged: settingsChanged()
    onCoordinateYChanged: settingsChanged()
    onCoordinateZChanged: settingsChanged()

    width: 280
    height: 800
    color: "#1c293c"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Label {
            text: "DICOM volume"
            color: "white"
            font.pixelSize: 22
        }

        Label { text: "CT · front-to-back ray casting"; color: "#b8c9db" }
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "Vessels"
                Layout.fillWidth: true
                onClicked: {
                    panel.vesselMode = true;
                    center.value = 275;
                    widthSlider.value = 350;
                    density.value = 1;
                }
            }
            Button {
                text: "General"
                Layout.fillWidth: true
                onClicked: {
                    panel.vesselMode = false;
                    center.value = 400;
                    widthSlider.value = 1800;
                    density.value = 1;
                }
            }
        }
        Label { text: "Window center: " + Math.round(center.value); color: "white" }
        Slider { id: center; Layout.fillWidth: true; from: -1000; to: 2000; value: 275 }
        Label { text: "Window width: " + Math.round(widthSlider.value); color: "white" }
        Slider { id: widthSlider; Layout.fillWidth: true; from: 1; to: 4000; value: 350 }
        Label { text: "Opacity"; color: "white" }
        Slider { id: density; Layout.fillWidth: true; from: 0.05; to: 5; value: 1 }

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
            from: 20
            to: 5000
            value: 450
        }

        Button {
            text: "Reset view"
            onClicked: {
                spin.value = 25;
                tilt.value = 55;
                distance.value = 450;
                panel.cameraPanX = 0;
                panel.cameraPanY = 0;
                panel.cameraPanZ = 0;
                panel.coordinateX = 0;
                panel.coordinateY = 0;
                panel.coordinateZ = 0;
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
