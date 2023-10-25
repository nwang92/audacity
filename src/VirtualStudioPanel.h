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

//#include "./widgets/MeterPanel.h"
#include "Theme.h"
#include "ThemedWrappers.h"
#include "TempDirectory.h"
#include "Observer.h"
#include "Decibels.h"

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
class AudacityProject;

const int kMaxVSMeterBars = 2;

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

class StudioParticipant final
   : public Observer::Publisher<ParticipantEvent>
{
public:
   StudioParticipant(wxWindow* parent, std::string id, std::string name, std::string picture);
   ~StudioParticipant();

   std::string GetID();
   std::string GetName();
   std::string GetPicture();
   wxImage GetImage();
   std::string GetDeviceID();
   float GetCaptureVolume();
   bool GetMute();
   float GetLeftVolume();
   float GetRightVolume();
   void UpdateMeter(float left, float right);
   void SetCaptureVolume(int volume);
   bool SetDeviceID(std::string deviceID);
   bool SetMute(bool mute);
   void SyncDeviceAPI();

   std::string GetDownloadLocalDir();
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
   void AddParticipant(std::string id, std::string name, std::string picture);
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
   VirtualStudioParticipantListWindow* mParticipantsList{nullptr};
   wxWindow* mHeader{nullptr};
   wxWindow* mActions{nullptr};
   AudacityProject& mProject;

   std::string mServerID;
   std::string mAccessToken;
   std::string mServerName;
   std::string mServerBanner;
   std::string mServerSessionID;
   std::string mServerOwnerID;
   std::string mServerStatus;
   int mServerBroadcast = 0;
   double mServerSampleRate = 0;
   bool mServerEnabled = 0;
   bool mServerAdmin = 0;

   std::shared_ptr<WSSClient> mServerClient{nullptr};
   std::shared_ptr<boost::thread> mServerThread{nullptr};
   std::shared_ptr<WSSClient> mSubscriptionsClient{nullptr};
   std::shared_ptr<boost::thread> mSubscriptionsThread{nullptr};
   std::shared_ptr<WSSClient> mDevicesClient{nullptr};
   std::shared_ptr<boost::thread> mDevicesThread{nullptr};
   std::shared_ptr<WSSClient> mMetersClient{nullptr};
   std::shared_ptr<boost::thread> mMetersThread{nullptr};

   StudioParticipantMap* mSubscriptionsMap{nullptr};
   std::map<std::string, std::string> mDeviceToOwnerMap;

   // VirtualStudioPanel is wrapped using ThemedWindowWrapper,
   // so we cannot subscribe to Prefs directly
   struct PrefsListenerHelper;
   std::unique_ptr<PrefsListenerHelper> mPrefsListenerHelper;

public:
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

   void SyncDeviceAPI(std::string deviceID, bool mute, int captureVolume);
   void ShowPanel(std::string serverID, std::string accessToken, bool focus);
   void HidePanel();
   void DoClose();
   void OnJoin(const wxCommandEvent& event);
   StudioParticipantMap* GetSubscriptionsMap();
   void SetStudio(std::string serverID, std::string accessToken);
   void ResetStudio();

   bool IsTopNavigationDomain(NavigationKind) const override { return true; }

   void SetFocus() override;

private:
   void OnCharHook(wxKeyEvent& evt);
   void UpdateServerName(std::string name);
   void UpdateServerStatus(std::string status);
   void UpdateServerBanner(std::string banner);
   void UpdateServerAdmin(bool admin);
   void UpdateServerSessionID(std::string sessionID);
   void UpdateServerOwnerID(std::string ownerID);
   void UpdateServerEnabled(bool enabled);
   void UpdateServerSampleRate(double sampleRate);
   void UpdateServerBroadcast(int broadcast);

   void OnServerWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg);
   void OnSubscriptionWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg);
   void OnDeviceWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg);
   void OnMeterWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg);
   void OnWssOpen(ConnectionHdl hdl);
   void OnWssClose(ConnectionHdl hdl);
   static websocketpp::lib::shared_ptr<SslContext> OnTlsInit();
   void DisableLogging(const std::shared_ptr<WSSClient>& client);
   void Connect(const std::shared_ptr<WSSClient>& client, std::string url);
   void Disconnect(std::shared_ptr<WSSClient>& client, std::shared_ptr<boost::thread>& thread);
   void InitializeWebsockets();
   void StopWebsockets();
   void InitServerWebsocket();
   void InitSubscriptionsWebsocket();
   void InitDevicesWebsocket();
   void InitMetersWebsocket();
   void StopMetersWebsocket();
   void FetchOwner(std::string ownerID);
};
