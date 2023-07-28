import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels
import QtWebView

import Audacity
import Audacity.UiComponents

Rectangle {
   id: jtvs
   height: parent.height
   color: appConfig.backgroundColor2
   objectName: "JackTrip"

   property var accessToken: ""
   property var expanded: false

   WebView {
      id: webEngineView
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: parent.top
      anchors.bottom: expandButton.top
      //httpUserAgent: `Audacity/2.0.0`
      //url: `https://app.jacktrip.org/studios/${studioId}/live?accessToken=${accessToken}`
      url: `https://app.jacktrip.org/studios?accessToken=${accessToken}`
   }

   Rectangle {
      id: verticalSeparator
      x: parent.width
      width: 1
      height: parent.height
      color: appConfig.strokeColor1
   }

   FlatButton {
      id: expandButton
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: recordingsMenu.top
      height: 28
      radius: 0
      transparent: true
      textFont.pixelSize: 12
      text: expanded ? qsTr("Hide") : qsTr("+ Import Recordings")
      onClicked: expanded = !expanded

      Rectangle {
         id: separator1
         y: parent.height - 1
         width: parent.width
         height: 1
         color: appConfig.strokeColor1
      }

      Rectangle {
         id: separator2
         y: 0
         width: parent.width
         height: 1
         color: appConfig.strokeColor1
      }
   }

   Rectangle {
      id: recordingsMenu
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      height: expanded ? 220 : 0
      color: appConfig.backgroundColor2

      Text {
         id: todo
         anchors.fill: parent
         anchors.verticalCenter: parent.verticalCenter
         color: appConfig.fontColor1
         text: "TODO"

         font {
            family: appConfig.iconFont.family
            pixelSize: 16
         }
      }
   }
}
