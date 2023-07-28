import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Audacity
import Audacity.UiComponents

ApplicationWindow {
   id: root
   width: 1024
   height: 480
   visible: true
   title: qsTr("Audacity")
   minimumWidth: 480
   minimumHeight: 540

   required property ApplicationConfiguration appConfig
   property alias workspaceMode: toolsToolbar.workspaceMode
   property alias enableVolumeTester: toolsToolbar.enableVolumeTester
   property alias enableJacktrip: toolsToolbar.enableJacktrip
   property string language: "en"

   QtObject {
      id: testers
      property bool volumeVisible: false
   }

   TranslationManager {
      id: translationManager
   }

   menuBar: MenuBar {
      Menu {
         title: qsTr("File")
         MenuItem {
            text: qsTr("Exit")
            onTriggered: Qt.quit()
         }
      }

      Menu {
         title: qsTr("Workspace")
         MenuItem {
            text: qsTr("Classic")
            autoExclusive: true
            checkable: true
            checked: root.workspaceMode === Workspace.Mode.Classic
            onTriggered: {
               workspaceMode = Workspace.Mode.Classic
            }
         }

         MenuItem {
            text: qsTr("Simple recording")
            autoExclusive: true
            checkable: true
            checked: root.workspaceMode === Workspace.Mode.SimpleRecording
            onTriggered: {
               workspaceMode = Workspace.Mode.SimpleRecording
            }
         }

         MenuItem {
            text: qsTr("Audio editing")
            autoExclusive: true
            checkable: true
            checked: root.workspaceMode === Workspace.Mode.AudioEditing
            onTriggered: {
               workspaceMode = Workspace.Mode.AudioEditing
            }
         }

         MenuItem {
            text: qsTr("Spectral editing")
            autoExclusive: true
            checkable: true
            checked: root.workspaceMode === Workspace.Mode.SpectralEditing
            onTriggered: {
               workspaceMode = Workspace.Mode.SpectralEditing
            }
         }
      }

      Menu {
         title: qsTr("Language")
         MenuItem {
            text: "English"
            autoExclusive: true
            checkable: true
            checked: language === "en"
            onTriggered:{
               language = "en"
               translationManager.ChangeLanguage(language)
            }
         }

         MenuItem {
            text: "Deutsch"
            autoExclusive: true
            checkable: true
            checked: language === "de"
            onTriggered: {
               language = "de"
               translationManager.ChangeLanguage(language)
            }
         }
      }

      Menu {
         title: qsTr("Testers")
         MenuItem {
            text: qsTr("VolumeControl")
            checkable: true
            checked: enableVolumeTester
            onTriggered: {
               enableVolumeTester = !enableVolumeTester
            }
         }
      }
   }

   header: ToolsToolbar {
      id: toolsToolbar

      onPlaybackStarted: timelineRuler.start()
      onPlaybackStopped: timelineRuler.stop()
      onPlaybackPaused: timelineRuler.pause()

      onUpdateStatusBar: function(status) {
         statusBar.text = status
         timer.restart()
      }
   }

   Rectangle {
      id: trackCanvas
      x: sidebar.width
      anchors.left: sidebar.right
      anchors.right: jacktrip.left
      color: appConfig.backgroundColor3
      anchors.top: timelineRuler.bottom
      anchors.bottom: footerId.top

      WaveClipView {
         id: waveClipView
         x: 13
         y: 0
         height: 84
         name: "Audio 1"

         onOptionsClicked: {
            statusBar.text = waveClipView.name + " options clicked"
            timer.restart()
         }
      }
   }

   Sidebar {
      id: sidebar
      anchors.top: toolsToolbar.bottom
      anchors.bottom: footerId.top
      onUpdateStatusBar: function(status) {
         statusBar.text = status
         timer.restart()
      }
   }

   JackTrip {
      id: jacktrip
      anchors.top: toolsToolbar.bottom
      anchors.bottom: footerId.top
      anchors.right: parent.right
      width: enableJacktrip * 360
      visible: enableJacktrip > 0
   }

   Timer {
      id: timer
      interval: 1000
      repeat: false
      onTriggered: statusBar.text = ""
   }

   TimelineRuler {
      id: timelineRuler
      x: sidebar.width + 1
      width: root.width - sidebar.width - 1
      height: 28
      playheadCursorHeight: height / 2 + trackCanvas.height
   }

   footer: Rectangle {
      id: footerId
      width: parent.width
      height: 30
      color: appConfig.backgroundColor1

      Text {
         id: statusBar
         anchors.centerIn: parent
      }

      Rectangle {
         anchors.bottom: parent.top
         height: 1
         width: parent.width
         color: appConfig.strokeColor1
      }
   }
}
