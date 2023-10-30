/*!********************************************************************

   Audacity: A Digital Audio Editor

   @file VirtualStudioPanel.h

   @author Vitaly Sverchinsky

**********************************************************************/

#pragma once

#include <math.h>
#include <memory>
#include <fstream>

#include <wx/image.h>
#include <wx/bitmap.h>
#include <wx/scrolwin.h>
#include <wx/weakref.h>
#include <wx/arrstr.h>
#include <wx/timer.h>

#include "sha256.h"
#include <boost/thread/thread.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

#ifdef HAS_NETWORKING
#include "NetworkManager.h"
#include "IResponse.h"
#include "Request.h"
#endif
#include <rapidjson/document.h>
#include <rapidjson/writer.h>

//#include "./widgets/MeterPanel.h"
#include "Theme.h"
#include "ThemedWrappers.h"
#include "TempDirectory.h"
#include "Observer.h"
#include "Decibels.h"

// used to import files
#include "ProjectFileManager.h"
#include "./import/Import.h"
#include "./import/ImportPlugin.h"
#include "Tags.h"


const std::string kApiHost = "app.jacktrip.org";
const std::string kApiBaseUrl = "https://" + kApiHost;

using WSSClient = websocketpp::client<websocketpp::config::asio_tls_client>;
using ConnectionHdl = websocketpp::connection_hdl;
using SslContext = websocketpp::lib::asio::ssl::context;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

class SampleTrack;

class wxButton;
class wxStaticText;
class wxBitmapButton;

class VirtualStudioParticipantListWindow;
class VirtualStudioPanel;
class ConnectionMetadata;
class WebsocketEndpoint;
class AudacityProject;

const int kMaxVSMeterBars = 2;
const int kVSMeterBarsGap = 2;

struct VSMeterBar {
   bool   vert;
   wxRect b;         // Bevel around bar
   wxRect r;         // True bar drawing area
   float  peak;
   float  rms;
   float  peakHold;
   double peakHoldTime;
   wxRect rClip;
   bool   clipping;
   bool   isclipping; //ANSWER-ME: What's the diff between these bools?! "clipping" vs "isclipping" is not clear.
   int    tailPeakCount;
   float  peakPeakHold;
};

//! Notification of changes in individual participants of StudioParticipantMap, or of StudioParticipantMap's composition
struct ParticipantEvent
{
   enum Type {
      // Posted when volume is changed
      METER_CHANGE,
      // Posted when volume is changed
      VOLUME_CHANGE,
      // Posted when mute is changed
      MUTE_CHANGE,
      // Posted when modify access is changed
      MODIFY_CHANGE,
      // Posted when participant is hidden
      HIDE,
      // Posted when participant is shown
      SHOW,
      // Posted when a new participant is added or removed
      REFRESH,
   };

   ParticipantEvent( Type type, std::string uid = "")
      : mType{ type }
      , mUid{ uid }
   {}

   ParticipantEvent( const ParticipantEvent& ) = default;

   const Type mType;
   //const std::weak_ptr<StudioParticipant> mParticipant;
   const std::string mUid;
};

//! Notification of changes in websocket connection
struct WebsocketEvent
{
   enum Type {
      // Posted when volume is changed
      RECONNECT,
   };

   WebsocketEvent( Type type )
      : mType{ type }
   {}

   WebsocketEvent( const WebsocketEvent& ) = default;

   const Type mType;
};

class StudioParticipant final
   : public Observer::Publisher<ParticipantEvent>
{
public:
   StudioParticipant(wxWindow* parent, const std::string &id, const std::string &name, const std::string &picture, bool canModify);
   ~StudioParticipant();

   std::string GetID();
   std::string GetName();
   std::string GetPicture();
   wxImage GetImage();
   std::string GetDeviceID();
   bool CanModify();
   float GetCaptureVolume();
   bool GetMute();
   float GetLeftVolume();
   float GetRightVolume();
   void UpdateMeter(float left, float right);
   void SetCaptureVolume(int volume);
   bool SetDeviceID(std::string deviceID);
   bool SetMute(bool mute);
   void SetModify(bool mod);
   void SyncDeviceAPI();
   void FetchImage();
   void LoadImage();
   void QueueEvent(ParticipantEvent event);

private:
   wxWindow* mParent;
   std::string mID;
   std::string mName;
   std::string mPicture;
   std::string mDeviceID;
   int mCaptureVolume;
   bool mMute;
   bool mModify;
   wxBitmap mBitmap;
   wxImage mImage;
   std::string mImageFile;
   std::ofstream mDownloadOutput;

   float mLeftVolume;
   float mRightVolume;
};

class StudioParticipantMap final
   : public Observer::Publisher<ParticipantEvent>
{
public:
   StudioParticipantMap(wxWindow* parent);
   ~StudioParticipantMap();

   std::map<std::string, std::shared_ptr<StudioParticipant>> GetMap();
   std::shared_ptr<StudioParticipant> GetParticipantByID(std::string id);
   void AddParticipant(const std::string &id, const std::string &name, const std::string &picture, bool canModify);
   void UpdateParticipantMeter(std::string id, float left, float right);
   void UpdateParticipantDevice(std::string id, std::string device);
   void UpdateParticipantCaptureVolume(std::string id, int volume);
   void UpdateParticipantMute(std::string, bool mute);
   unsigned long GetParticipantsCount();
   void Clear();
   void QueueEvent(ParticipantEvent event);
   void Print();
   wxImage* DownloadImage(std::string url);

private:
   wxWindow* mParent;
   std::map<std::string, std::shared_ptr<StudioParticipant>> mMap;
};

enum WSSType {
   SERVER,
   SUBSCRIPTIONS,
   DEVICES,
   METERS,
};

class ConnectionMetadata final
   : public Observer::Publisher<WebsocketEvent> {
public:
   typedef websocketpp::lib::shared_ptr<ConnectionMetadata> ptr;
   ConnectionMetadata(VirtualStudioPanel* panel, int id, ConnectionHdl hdl, const std::string &uri, WSSType type);
   void OnOpen(WSSClient* client, ConnectionHdl hdl);
   void OnFail(WSSClient* client, ConnectionHdl hdl);
   void OnClose(WSSClient* client, ConnectionHdl hdl);
   void OnMessage(ConnectionHdl hdl, WSSClient::message_ptr msg);
   void HandleServerMessage(const rapidjson::Document &document);
   void HandleSubscriptionsMessage(const rapidjson::Document &document);
   void HandleDevicesMessage(const rapidjson::Document &document);
   void HandleMetersMessage(const rapidjson::Document &document);
   void QueueEvent(WebsocketEvent event);
   const websocketpp::connection_hdl GetHdl() { return mHdl; }
   const int GetID() { return mID; }
   const std::string GetStatus() { return mStatus; }

private:
   VirtualStudioPanel* mPanel;
   int mID;
   ConnectionHdl mHdl;
   std::string mStatus;
   std::string mUri;
   WSSType mType;
   std::string mErrorReason;
   std::vector<std::string> mMessages;
};

class WebsocketEndpoint {
public:
   WebsocketEndpoint(VirtualStudioPanel* panel, const std::string &uri, WSSType type);
   ~WebsocketEndpoint();
   static websocketpp::lib::shared_ptr<SslContext> OnTlsInit();
   void DisableLogging();
   void SetReconnect(bool reconnect);
   int Connect();
   void Close(int id, websocketpp::close::status::value code, std::string reason);
   const ConnectionMetadata::ptr GetMetadata(int id);

private:
   typedef std::map<int, ConnectionMetadata::ptr> con_list;

   VirtualStudioPanel* mPanel;
   WSSClient mClient;
   std::string mUri;
   WSSType mType;
   websocketpp::lib::shared_ptr<boost::thread> mThread;
   Observer::Subscription mConnectionSubscription;
   con_list mConnectionList;
   int mNextID;
   bool mReconnect;
};

// Thread-safe queue of recording segments
class RecordingSegmentQueue
{
 public:
   RecordingSegmentQueue();
   ~RecordingSegmentQueue();

   bool Put(const std::string &filename);
   bool Get(std::string &filename);
   void Clear();

 private:
   std::queue<std::string> mQueue;
   mutable std::mutex mMutex;
};

/**
 * \brief UI Panel that displays realtime effects from the effect stack of
 * an individual track, provides controls for accessing effect settings,
 * stack manipulation (reorder, add, remove)
 */
class VirtualStudioPanel : public wxPanel
{
   AButton* mStudioOnline{nullptr};
   wxStaticText* mStudioTitle {nullptr};
   wxStaticText* mStudioStatus {nullptr};
   AButton* mJoinStudio{nullptr};
   AButton* mRecButton{nullptr};
   VirtualStudioParticipantListWindow* mParticipantsList{nullptr};
   wxWindow* mHeader{nullptr};
   wxWindow* mActions{nullptr};
   AudacityProject& mProject;

   bool mSetupDone = false;
   wxString mTrackName;
   WaveTrackArray mRecTracks;
   std::map<std::string, bool> mDownloadedMediaFiles;
   std::unique_ptr<RecordingSegmentQueue> mQueue{nullptr};
   wxTimer mRecordingTimer;

   std::string mServerID;
   std::string mUserID;
   std::string mAccessToken;
   std::string mServerName;
   std::string mServerBanner;
   std::string mServerSessionID;
   std::string mServerOwnerID;
   std::string mServerStatus;
   std::string mServerStreamID;
   int mServerBroadcast = 0;
   double mServerSampleRate = 0;
   bool mServerEnabled = false;
   bool mServerAdmin = false;

   std::shared_ptr<WebsocketEndpoint> mServerClient{nullptr};
   std::shared_ptr<WebsocketEndpoint> mSubscriptionsClient{nullptr};
   std::shared_ptr<WebsocketEndpoint> mDevicesClient{nullptr};
   std::shared_ptr<WebsocketEndpoint> mMetersClient{nullptr};
   std::shared_ptr<boost::thread> mActiveParticipantsThread{nullptr};
   std::shared_ptr<boost::thread> mRecordingThread{nullptr};

   StudioParticipantMap* mSubscriptionsMap{nullptr};
   std::map<std::string, std::string> mDeviceToOwnerMap;
   std::map<std::string, bool> mWebrtcUsers;

   // VirtualStudioPanel is wrapped using ThemedWindowWrapper,
   // so we cannot subscribe to Prefs directly
   struct PrefsListenerHelper;
   std::unique_ptr<PrefsListenerHelper> mPrefsListenerHelper;

public:
   static bool IsWebrtcDevice(const std::string &device);
   static std::string GetDownloadLocalDir(const std::string &uniqueID);
   static VirtualStudioPanel &Get(AudacityProject &project);
   static const VirtualStudioPanel &Get(const AudacityProject &project);

   VirtualStudioPanel(
      AudacityProject& project, wxWindow* parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxPanelNameStr);

   ~VirtualStudioPanel() override;

   void SyncDeviceAPI(const std::string &deviceID, bool mute, int captureVolume);
   void ShowPanel(const std::string &serverID, const std::string &userID, const std::string &accessToken, bool focus);
   void HidePanel();
   void DoClose();
   StudioParticipantMap* GetSubscriptionsMap();
   std::map<std::string, std::string>* GetDeviceToOwnerMap();
   void SetStudio();
   void ResetStudio();
   void UpdateServerName(std::string name);
   void UpdateServerStatus(std::string status);
   void UpdateServerBanner(std::string banner);
   void UpdateServerAdmin(bool admin);
   void UpdateServerSessionID(std::string sessionID);
   void UpdateServerOwnerID(std::string ownerID);
   void UpdateServerEnabled(bool enabled);
   void UpdateServerSampleRate(double sampleRate);
   void UpdateServerBroadcast(int broadcast);
   void UpdateServerStreamID(std::string streamID);
   void AddParticipant(const std::string &userID, const std::string &name, const std::string &picture);
   bool ServerIsReady();
   bool IsTopNavigationDomain(NavigationKind) const override { return true; }

   void SetFocus() override;

private:
   void OnJoin(const wxCommandEvent& event);
   void OnRecord(const wxCommandEvent& event);
   void OnNewRecordingSegment(const wxTimerEvent& event);
   void OnCharHook(wxKeyEvent& evt);
   void Disconnect(std::shared_ptr<WebsocketEndpoint>& endpoint);
   void InitializeWebsockets();
   void StopWebsockets();
   void InitServerWebsocket();
   void InitSubscriptionsWebsocket();
   void InitDevicesWebsocket();
   void InitMetersWebsocket();
   void InitActiveParticipants();
   void InitRecording();
   void StopMetersWebsocket();
   void StopActiveParticipants();
   void StopRecording();
   void PopulatePanel();
   void FetchOwner(const std::string &ownerID);
   void FetchServer();
   void FetchActiveServerParticipants();
   void FetchFullMixMediaSegments();
   void FetchMediaSegment(const std::string &filename);
   void LoadSegment(const std::string &filepath);
};
