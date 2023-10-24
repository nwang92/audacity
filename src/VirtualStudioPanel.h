/*!********************************************************************

   Audacity: A Digital Audio Editor

   @file VirtualStudioPanel.h

   @author Vitaly Sverchinsky

**********************************************************************/

#pragma once

#include <math.h>
#include <memory>

#include <wx/image.h>
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

#include "./widgets/MeterPanel.h"
#include "ThemedWrappers.h"
#include "Observer.h"

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

//! Notification of changes in individual participants of StudioParticipantMap, or of StudioParticipantMap's composition
struct ParticipantEvent
{
   enum Type {
      // Posted when volume is changed
      VOLUME_CHANGE,

      // Posted when participant is hidden
      HIDDEN,

      // Posted when participant is shown
      SHOWN,

      // Posted when a new participant is added
      ADDITION,

      //! Posted when the set of selected tracks changes.
      SELECTION_CHANGE,

      //! Posted when certain fields of a track change.
      TRACK_DATA_CHANGE,

      //! Posted when a track needs to be scrolled into view; leader track only
      TRACK_REQUEST_VISIBLE,

      //! Posted when tracks are reordered but otherwise unchanged.
      /*! mpTrack points to the moved track that is earliest in the New ordering. */
      PERMUTED,

      //! Posted when some track changed its height.
      RESIZING,

      //! Posted when a track has been deleted from a tracklist. Also posted when one track replaces another
      /*! mpTrack points to the removed track. It is expected, that track is valid during the event.
       *! mExtra is 1 if the track is being replaced by another track, 0 otherwise.
       */
      DELETION,
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
   bool IsHidden();
   float GetLeftVolume();
   float GetRightVolume();
   void UpdateVolume(float left, float right);
   void SetIndex(int idx);
   void SetShown(bool shown);
   void QueueEvent(ParticipantEvent event);

private:
   wxWindow* mParent;
   std::string mID;
   std::string mName;
   std::string mPicture;
   wxBitmap mBitmap;
   wxImage mImage;
   bool mShown;
   int mIndex;
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
   void UpdateParticipantVolume(std::string id, float left, float right);
   unsigned long GetParticipantsCount();
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

   std::weak_ptr<SampleTrack> mCurrentTrack;

   Observer::Subscription mTrackListChanged;
   Observer::Subscription mUndoSubscription;

   std::string mServerID;
   std::string mAccessToken;
   std::string mServerName;
   std::string mServerBanner;
   std::string mServerSessionID;
   std::string mServerOwnerID;
   std::string mServerStatus;
   int mServerBroadcast;
   double mServerSampleRate;
   bool mServerEnabled;

   WSSClient* mServerClient;
   boost::thread mServerThread;

   WSSClient* mSubscriptionsClient;
   boost::thread mSubscriptionsThread;

   WSSClient* mDevicesClient;
   boost::thread mDevicesThread;

   WSSClient* mMetersClient;
   boost::thread mMetersThread;

   StudioParticipantMap* mSubscriptionsMap{nullptr};
   std::map<std::string, std::string> mDeviceToOwnerMap;
   std::vector<std::shared_ptr<SampleTrack>> mPotentiallyRemovedTracks;

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

   void ShowPanel(std::string serverID, std::string accessToken, bool focus);
   void HidePanel();
   void DoClose();
   void OnJoin(const wxCommandEvent& event);
   StudioParticipantMap* GetSubscriptionsMap();

   /**
    * \brief Shows effects from the effect stack of the track
    * \param track Pointer to the existing track, or null
    */
   void SetStudio(std::string serverID, std::string accessToken);
   void ResetStudio();

   bool IsTopNavigationDomain(NavigationKind) const override { return true; }

   void SetFocus() override;

private:
   void OnCharHook(wxKeyEvent& evt);
   void UpdateServerName(std::string name);
   void UpdateServerStatus(std::string status);
   void UpdateServerBanner(std::string banner);
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
   static websocketpp::lib::shared_ptr<SslContext> OnTlsInit();
   void DisableLogging(WSSClient& client);
   void SetUrl(WSSClient& client, std::string url);
   void InitializeWebsockets();
   void StopWebsockets();
   void InitServerWebsocket();
   void InitSubscriptionsWebsocket();
   void InitDevicesWebsocket();
   void InitMetersWebsocket();
   void StopMetersWebsocket();
   void FetchOwner(std::string ownerID);
};
