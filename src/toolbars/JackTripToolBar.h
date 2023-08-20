/**********************************************************************

  Audacity: A Digital Audio Editor

  JackTripToolBar.h

  K. Soze

  **********************************************************************/

#ifndef __AUDACITY_JACKTRIP_TOOLBAR__
#define __AUDACITY_JACKTRIP_TOOLBAR__

#include <cstdio>
#include <iostream>
#include <regex>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <optional>
#include <vector>
#include <fstream>

#ifdef HAS_NETWORKING
#include "NetworkManager.h"
#include "IResponse.h"
#include "Request.h"
#endif
#include <rapidjson/document.h>


#include <wx/menu.h>
#include <wx/dialog.h>
#include "ToolBar.h"
#include "BasicUI.h"
#include "ProjectFileManager.h"
#include "TempDirectory.h"
#include "Observer.h"
#include "MemoryX.h"
#include "../widgets/MeterPanel.h"
#include "AudacityMessageBox.h"
#include "../widgets/FileHistory.h"

const std::string kAuthAuthorizeUrl = "https://auth.jacktrip.org/authorize";
const std::string kAuthTokenUrl = "https://auth.jacktrip.org/oauth/token";
const std::string kAuthAudience = "https://api.jacktrip.org";
const std::string kAuthClientId = "cROUJag0UVKDaJ6jRAKRzlVjKVFNU39I";
const std::string kAuthHost = "auth.jacktrip.org";

enum class DeviceChangeMessage : char;

class wxMenu;
class wxString;
class VirtualStudioAuthDialog;
struct DeviceSourceMap;

class JackTripToolBar final : public ToolBar {
   static constexpr int kAudioSettings = 15800;

 public:
   static Identifier ID();

   explicit JackTripToolBar( AudacityProject &project );
   virtual ~JackTripToolBar();

   static JackTripToolBar &Get( AudacityProject &project );
   static const JackTripToolBar &Get( const AudacityProject &project );

   void Create(wxWindow * parent) override;

   void UpdatePrefs() override;
   void UpdateSelectedPrefs( int ) override;

   void DeinitChildren();
   void Populate() override;
   void Repaint(wxDC* dc) override;
   void EnableDisableButtons() override;
   void ReCreateButtons() override;
   void OnFocus(wxFocusEvent &event);
   void OnAudioSetup(wxCommandEvent &event);

 private:
   void OnRescannedDevices(DeviceChangeMessage);
   void OnRecording(std::string serverID, int id);
   void OnAuth(wxCommandEvent& event);
   std::string ExecCommand(const char* cmd);
   bool JackTripExists();
   void GetUserInfo();
   void GetRecordingDownloadURL(std::string serverID, std::string recordingID);
   std::string GetDownloadLocalDir(std::string recordingID);
   std::string GetDownloadFilenameFromUrl(std::string url);
   std::string GetRecordingIDFromUrl(std::string url);
   void DownloadRecording(std::string url);
   void ExtractRecording(std::string recordingID);
   void ImportRecordingFiles(std::string recordingID);
   void JackTripListDevices(std::regex rex, std::vector<std::string>& devices);
   void JackTripListInputDevices(std::vector<std::string>& devices);
   void JackTripListOutputDevices(std::vector<std::string>& devices);
   void RunJackTrip();
   void StopJackTrip();

   class Choices;
   void RepopulateMenus();
   void FillRecordings();
   void RegenerateTooltips() override;

   void MakeJackTripButton();
   void ArrangeButtons();

   using JackTripCallback = void (JackTripToolBar::*)(std::string serverID, int id);
   // Append submenu with one radio item group
   // Bind menu items to lambdas that invoke callback,
   // with successive ids from 0
   // Check the item with given index, or disable the submenu when that is < 0
   static void AppendSubMenu(JackTripToolBar &toolbar, wxMenu& menu,
      const wxArrayString &labels, int checkedItem,
      std::string serverID, JackTripCallback callback, const wxString& title);

   enum {
      ID_JACKTRIP_BUTTON = 15000,
      BUTTON_COUNT,
   };

   AButton *mJackTrip{};
   wxBoxSizer *mSizer{};
   FILE *mExec;
   std::vector<std::string> mInputDevices{};
   std::vector<std::string> mOutputDevices{};
   MeterPanel *mStudioMeter{nullptr};

   class JackTripChoices {
   public:
      void Clear() { mStrings.Clear(); mIds.Clear(); mIndex = -1; }
      [[nodiscard]] bool Empty() const { return mStrings.empty(); }
      std::optional<wxString> Get() const {
         if (mIndex < 0 || mIndex >= mStrings.size())
            return {};
         return { mStrings[mIndex] };
      }
      wxString GetFirst() const {
         if (!Empty())
            return mStrings[0];
         return {};
      }
      int GetSmallIntegerId() const {
         return mIndex;
      }
      wxString GetID(int id) const {
         if (id < 0 || id >= mStrings.size())
            return "";
         return mIds[id];
      }
      int Find(const wxString &name) const {
         return make_iterator_range(mStrings).index(name);
      }
      bool Set(const wxString &name) {
         auto index = make_iterator_range(mStrings).index(name);
         if (index != -1) {
            mIndex = index;
            return true;
         }
         // else no state change
         return false;
      }
      void Set(wxArrayString &&names, wxArrayString &&ids) {
         mStrings.swap(names);
         mIds.swap(ids);
         mIndex = mStrings.empty() ? -1 : 0;
      }
      // id is just a small-integer index into the string array
      bool Set(int id) {
         if (id < 0 || id >= mStrings.size())
            return false; // no change of state then
         mIndex = id;
         return true;
      }
      void AppendSubMenu(JackTripToolBar &toolBar,
         wxMenu &menu, std::string serverID, JackTripCallback callback, const wxString &title);

   private:
      wxArrayStringEx mStrings;
      wxArrayStringEx mIds;
      int mIndex{ -1 };
   };

   // Jacktrip-specific options
   std::string mAccessToken;
   std::string mUserID;
   std::unique_ptr<BasicUI::ProgressDialog> mProgressDialog;
   std::map<std::string, std::string> mServerIdToName;
   std::map<std::string, std::string> mRecordingIdToName;
   std::map<std::string, JackTripChoices> mServerIdToRecordings;
   std::string mDownloadFile;
   std::ofstream mDownloadOutput;

   Observer::Subscription mSubscription;

 public:

   DECLARE_CLASS(JackTripToolBar)
   DECLARE_EVENT_TABLE()
};

class VirtualStudioAuthDialog final : public wxDialogWrapper
{
 public:
   VirtualStudioAuthDialog(wxWindow* parent, std::string* parentAccessTokenPtr);
   ~VirtualStudioAuthDialog();

   float Get();

 private:
   void DoLayout();
   void UpdateLayout();

   void InitDeviceAuthorizationCodeFlow(std::string* parentAccessTokenPtr);
   void StartPolling(std::string* parentAccessTokenPtr);
   void CheckForToken(std::string* parentAccessTokenPtr);

   void OpenVerificationUri();

   void OnSlider();
   void OnTextChange();

   std::string* mParentAccessTokenPtr;
   std::string mIDToken;
   std::string mAccessToken;
   std::string mRefreshToken;
   std::string mDeviceCode;
   std::string mUserCode;
   std::string mVerificationUri;
   std::string mVerificationUriComplete;
   int mPollingInterval;
   int mExpiresIn;

   bool mError;
   bool mSuccess;
   int mStyle;
   float mValue;

 public:
   DECLARE_EVENT_TABLE()
};

#endif

