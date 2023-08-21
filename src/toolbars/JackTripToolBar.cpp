/**********************************************************************

  Audacity: A Digital Audio Editor

  JackTripToolBar.cpp

*******************************************************************//*!

\class JackTripToolBar
\brief A toolbar to allow easier changing of input and output devices .

*//*******************************************************************/

#include "JackTripToolBar.h"
#include "ToolManager.h"

#include <thread>
#include <chrono>

#include <wx/app.h>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/tooltip.h>
#include <wx/filename.h>
#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/dialog.h>
#include <wx/statline.h>

#include "wxPanelWrapper.h"

#include "../ActiveProject.h"

#include "ShuttleGui.h"

#include "AColor.h"
#include "AllThemeResources.h"
#include "AudioIOBase.h"
#include "DeviceToolBar.h"
#include "../KeyboardCapture.h"
#include "Project.h"
#include "../ProjectWindows.h"
#include "DeviceManager.h"
#include "../prefs/PrefsDialog.h"
#include "../widgets/AButton.h"
#include "../widgets/BasicMenu.h"
#include "wxWidgetsWindowPlacement.h"

namespace {
   class ViewDeviceSettingsDialog final : public PrefsDialog
   {
   public:
      ViewDeviceSettingsDialog(wxWindow* parent, AudacityProject& project,
         const TranslatableString& title, PrefsPanel::Factories& factories,
         int page)
         : PrefsDialog(parent, &project, title, factories)
         , mPage(page)
      {
      }

      long GetPreferredPage() override
      {
         return mPage;
      }

      void SavePreferredPage() override
      {
      }

   private:
      const int mPage;
   };
}

IMPLEMENT_CLASS(JackTripToolBar, ToolBar);

////////////////////////////////////////////////////////////
/// Methods for JackTripToolBar
////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(JackTripToolBar, ToolBar)
   EVT_BUTTON(ID_JACKTRIP_BUTTON, JackTripToolBar::OnAudioSetup)
END_EVENT_TABLE()

Identifier JackTripToolBar::ID()
{
   return wxT("JackTrip");
}

//Standard constructor
JackTripToolBar::JackTripToolBar( AudacityProject &project )
: ToolBar( project, XO("JackTrip"), ID() )
{
   mSubscription = DeviceManager::Instance()->Subscribe(
      *this, &JackTripToolBar::OnRescannedDevices );
}

JackTripToolBar::~JackTripToolBar()
{
}

JackTripToolBar &JackTripToolBar::Get( AudacityProject &project )
{
   auto &toolManager = ToolManager::Get( project );
   return *static_cast<JackTripToolBar*>(toolManager.GetToolBar(ID()));
}

const JackTripToolBar &JackTripToolBar::Get( const AudacityProject &project )
{
   return Get( const_cast<AudacityProject&>( project )) ;
}

void JackTripToolBar::Create(wxWindow *parent)
{
   ToolBar::Create(parent);

   std::cout << "Hello World!" << std::endl;
   std::cout << "Hello World!" << std::endl;
   std::cout << "Hello World!" << std::endl;

   GetUserInfo();
   /*
   if (JackTripExists()) {
      // parse for input devices
      JackTripListInputDevices(mInputDevices);
      for (int i = 0; i < mInputDevices.size(); i++) {
         std::cout << mInputDevices[i] << std::endl;
      }

      // parse for output devices
      JackTripListOutputDevices(mOutputDevices);
      for (int i = 0; i < mOutputDevices.size(); i++) {
         std::cout << mOutputDevices[i] << std::endl;
      }
   }
   */

   // Simulate a size event to set initial meter placement/size
   wxSizeEvent event(GetSize(), GetId());
   event.SetEventObject(this);
   GetEventHandler()->ProcessEvent(event);
}

void JackTripToolBar::DeinitChildren()
{
   mServerIdToName.clear();
   mRecordingIdToName.clear();
   mServerIdToRecordings.clear();
}

void JackTripToolBar::Populate()
{
   MakeButtonBackgroundsSmall();
   SetBackgroundColour( theTheme.Colour( clrMedium  ) );
   MakeJackTripButton();

   DeinitChildren();

#if wxUSE_TOOLTIPS
   RegenerateTooltips();
   wxToolTip::Enable(true);
   wxToolTip::SetDelay(1000);
#endif

   // Set default order and mode
   ArrangeButtons();

   RepopulateMenus();

   // mStudioMeter = safenew MeterPanel( &mProject,
   //                              this,
   //                              wxID_ANY,
   //                              true,
   //                              wxDefaultPosition,
   //                              wxSize( 260, toolbarSingle) );
   // /* i18n-hint: (noun) The meter that shows the loudness of the audio being recorded.*/
   // mStudioMeter->SetName( XO("Studio Level"));
   // /* i18n-hint: (noun) The meter that shows the loudness of the audio being recorded.
   //    This is the name used in screen reader software, where having 'Meter' first
   //    apparently is helpful to partially sighted people.  */
   // mStudioMeter->SetLabel( XO("Meter-Studio") );
}

void JackTripToolBar::Repaint(wxDC* dc)
{
#ifndef USE_AQUA_THEME
   wxSize s = mSizer->GetSize();
   wxPoint p = mSizer->GetPosition();

   wxRect bevelRect(p.x, p.y, s.GetWidth() - 1, s.GetHeight() - 1);
   AColor::Bevel(*dc, true, bevelRect);
#endif
}

void JackTripToolBar::MakeJackTripButton()
{
   mJackTrip = safenew AButton(this, ID_JACKTRIP_BUTTON);
   //i18n-hint: Audio setup button text, keep as short as possible
   mJackTrip->SetLabel(XO("JackTrip"));
   mJackTrip->SetButtonType(AButton::FrameButton);
   mJackTrip->SetImages(
      theTheme.Image(bmpRecoloredUpSmall),
      theTheme.Image(bmpRecoloredUpHiliteSmall),
      theTheme.Image(bmpRecoloredDownSmall),
      theTheme.Image(bmpRecoloredHiliteSmall),
      theTheme.Image(bmpRecoloredUpSmall));
   mJackTrip->SetIcon(theTheme.Image(bmpSetup));
   mJackTrip->SetForegroundColour(theTheme.Colour(clrTrackPanelText));
}

void JackTripToolBar::ArrangeButtons()
{
   int flags = wxALIGN_CENTER | wxRIGHT;

   // (Re)allocate the button sizer
   if (mSizer)
   {
      Detach(mSizer);
      std::unique_ptr < wxSizer > {mSizer}; // DELETE it
   }

   Add((mSizer = safenew wxBoxSizer(wxHORIZONTAL)), 1, wxEXPAND);
   mSizer->Add(mJackTrip, 1, wxEXPAND);

   // Layout the sizer
   mSizer->Layout();

   // Layout the toolbar
   Layout();

   SetMinSize(GetSizer()->GetMinSize());
}

void JackTripToolBar::ReCreateButtons()
{
   bool isJackTripDown = false;

   // ToolBar::ReCreateButtons() will get rid of its sizer and
   // since we've attached our sizer to it, ours will get deleted too
   // so clean ours up first.
   if (mSizer)
   {
      isJackTripDown = mJackTrip->IsDown();
      Detach(mSizer);

      std::unique_ptr < wxSizer > {mSizer}; // DELETE it
      mSizer = nullptr;
   }

   ToolBar::ReCreateButtons();

   if (isJackTripDown)
   {
      mJackTrip->PushDown();
   }

   EnableDisableButtons();

   RegenerateTooltips();
}

void JackTripToolBar::OnFocus(wxFocusEvent &event)
{
   KeyboardCapture::OnFocus( *this, event );
}

void JackTripToolBar::OnAudioSetup(wxCommandEvent& WXUNUSED(evt))
{
   wxMenu menu;

   if (mUserID.empty() || mAccessToken.empty()) {
      menu.Append(kAudioSettings, _("&Login"));

      menu.Bind(wxEVT_MENU_CLOSE, [this](auto&) { mJackTrip->PopUp(); });
      menu.Bind(wxEVT_MENU, &JackTripToolBar::OnAuth, this, kAudioSettings);
   } else {
      for (auto it = mServerIdToRecordings.begin(); it != mServerIdToRecordings.end(); it++) {
         auto serverID = it->first;
         if (serverID != "") {
            auto name = mServerIdToName[serverID];
            JackTripChoices c = it->second;
            c.AppendSubMenu(*this, menu, serverID,
               &JackTripToolBar::OnRecording, _("&" + name));
            menu.AppendSeparator();
         }
      }
   }

   wxWindow* btn = FindWindow(ID_JACKTRIP_BUTTON);
   wxRect r = btn->GetRect();
   BasicMenu::Handle{ &menu }.Popup(
      wxWidgetsWindowPlacement{ btn },
      { r.GetLeft(), r.GetBottom() }
   );
}

void JackTripToolBar::UpdatePrefs()
{
   RegenerateTooltips();

   // Set label to pull in language change
   SetLabel(XO("JackTrip"));

   // Give base class a chance
   ToolBar::UpdatePrefs();

   Layout();
   Refresh();
}

void JackTripToolBar::UpdateSelectedPrefs( int id )
{
   if (id == DeviceToolbarPrefsID())
      UpdatePrefs();
   ToolBar::UpdateSelectedPrefs( id );
}

void JackTripToolBar::EnableDisableButtons()
{
   auto gAudioIO = AudioIOBase::Get();
   if (gAudioIO) {
      // we allow changes when monitoring, but not when recording
      bool audioStreamActive = gAudioIO->IsStreamActive() && !gAudioIO->IsMonitoring();

      if (audioStreamActive) {
         mJackTrip->Disable();
      }
      else {
         mJackTrip->Enable();
      }
   }
}

void JackTripToolBar::RegenerateTooltips()
{
#if wxUSE_TOOLTIPS
   for (long iWinID = ID_JACKTRIP_BUTTON; iWinID < BUTTON_COUNT; iWinID++)
   {
      auto pCtrl = static_cast<AButton*>(this->FindWindow(iWinID));
      CommandID name;
      switch (iWinID)
      {
      case ID_JACKTRIP_BUTTON:
         name = wxT("Open JackTrip");
         break;
      }
      std::vector<ComponentInterfaceSymbol> commands(
         1u, { name, Verbatim(pCtrl->GetLabel()) });

      // Some have a second
      switch (iWinID)
      {
      case ID_JACKTRIP_BUTTON:
         break;
      }
      ToolBar::SetButtonToolTip(
         mProject, *pCtrl, commands.data(), commands.size());
   }
#endif
}

void JackTripToolBar::RepopulateMenus()
{
   FillRecordings();
   // make the device display selection reflect the prefs if they exist
   UpdatePrefs();
}

void JackTripToolBar::GetUserInfo()
{
   if (mAccessToken.empty()) {
      return;
   }

   mUserID.clear();

   audacity::network_manager::Request request("https://auth.jacktrip.org/userinfo");
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         std::cout << "HTTP code: " << httpCode << std::endl;

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;
         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            std::cout << "Error parsing JSON: " << document.GetParseError() << std::endl;
            wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
            return;
         }

         auto sub = document["sub"].GetString();
         std::cout << "sub is: " << sub << std::endl;
         mUserID = sub;
         RepopulateMenus();
      }
   );
}

void JackTripToolBar::FillRecordings()
{
   if (mUserID.empty() || mAccessToken.empty()) {
      return;
   }
   mServerIdToName.clear();
   mRecordingIdToName.clear();
   mServerIdToRecordings.clear();

   audacity::network_manager::Request request("https://app.jacktrip.org/api/users/" + mUserID + "/recordings");
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         std::cout << "HTTP code: " << httpCode << std::endl;

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;

         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            std::cout << "Error parsing JSON: " << document.GetParseError() << std::endl;
            return;
         }

         // sort with newest on top
         struct CreatedAtComparer {
            bool operator()(const Value& left, const Value& right) const {
               auto leftStr = left.GetObject()["createdAt"].GetString();
               auto rightStr = right.GetObject()["createdAt"].GetString();
               return strcmp(leftStr, rightStr) > 0;
            }
         };
         std::sort(document.Begin(), document.End(), CreatedAtComparer());

         // Iterate over the array of objects
         Value::ConstValueIterator itr;
         JackTripChoices recordings;
         std::map<std::string, wxArrayStringEx> recordingNamesByServerID;
         std::map<std::string, wxArrayStringEx> recordingIdsByServerID;
         for (itr = document.Begin(); itr != document.End(); ++itr) {
            auto serverID = itr->GetObject()["serverId"].GetString();
            auto serverName = itr->GetObject()["serverName"].GetString();
            mServerIdToName[serverID] = serverName;

            auto recordingName = itr->GetObject()["name"].GetString();
            auto recordingID = itr->GetObject()["id"].GetString();
            recordingNamesByServerID[serverID].push_back(recordingName);
            recordingIdsByServerID[serverID].push_back(recordingID);
         }

         for (auto it = mServerIdToName.begin(); it != mServerIdToName.end(); it++) {
            auto serverID = it->first;
            JackTripChoices recordings;

            auto names = recordingNamesByServerID[serverID];
            auto ids = recordingIdsByServerID[serverID];

            recordings.Set(std::move(names), std::move(ids));
            mServerIdToRecordings[serverID] = std::move(recordings);
         }
      }
   );
}

void JackTripToolBar::AppendSubMenu(JackTripToolBar &toolbar,
   wxMenu& menu, const wxArrayString &labels, int checkedItem,
   std::string serverID, JackTripCallback callback, const wxString& title)
{
   auto subMenu = std::make_unique<wxMenu>();
   int ii = 0;
   for (const auto &label : labels) {
      // Assign fresh ID with wxID_ANY
      auto subMenuItem = subMenu->AppendRadioItem(wxID_ANY, label);
      if (ii == checkedItem)
         subMenuItem->Check();
      subMenu->Bind(wxEVT_MENU,
         [&toolbar, serverID, callback, ii](wxCommandEvent &){ (toolbar.*callback)(serverID, ii); },
         subMenuItem->GetId());
      ++ii;
   }
   auto menuItem = menu.AppendSubMenu(subMenu.release(), title);
   if (checkedItem < 0)
      menuItem->Enable(false);
}

void JackTripToolBar::JackTripChoices::AppendSubMenu(JackTripToolBar &toolBar,
   wxMenu &menu, std::string serverID, JackTripCallback callback, const wxString &title)
{
   JackTripToolBar::AppendSubMenu(toolBar, menu, mStrings, mIndex, serverID, callback, title);
}

void JackTripToolBar::OnRescannedDevices(DeviceChangeMessage m)
{
   // Hosts may have disappeared or appeared so a complete repopulate is needed.
   if (m == DeviceChangeMessage::Rescan)
      RepopulateMenus();
}

void JackTripToolBar::OnRecording(std::string serverID, int id)
{
   std::cout << "Clicked on recording: " << id << " for serverID: " << serverID << std::endl;
   if (mServerIdToRecordings.find(serverID) == mServerIdToRecordings.end()) {
      std::cout << "Key not found" << std::endl;
      return;
   }

   JackTripChoices c = mServerIdToRecordings[serverID];
   auto recordingID = std::string(c.GetID(id).mb_str());

   // do nothing if the directory exists - this should mean we already have the files locally
   auto outputDir = GetDownloadLocalDir(recordingID);
   if (wxDirExists(outputDir)) {
      std::cout << "Directory exists " << outputDir << std::endl;
      ImportRecordingFiles(recordingID);
      return;
   }

   GetRecordingDownloadURL(serverID, recordingID);
}

void JackTripToolBar::OnAuth(wxCommandEvent& event)
{
   std::cout << "Clicked on web menu item" << std::endl;

   VirtualStudioAuthDialog dlg(this, &mAccessToken);
   dlg.SetSize(800, 600);
   int retCode = dlg.ShowModal();

   dlg.Center();
   std::cout << "Return code: " << retCode << std::endl;

   std::cout << "Parent access token: " << mAccessToken << std::endl;
   if (!mAccessToken.empty()) {
      GetUserInfo();
   }
   /*
   wxTheApp->CallAfter([this]{
      mBrowser = wxWebView::New(this, wxID_ANY);
      mBrowser->LoadURL("https://www.audacityteam.org");
      wxDialogWrapper webDialog(this, wxID_ANY, XO("Virtual Studio"));
      webDialog.SetTitle("Audacity Website");
      webDialog.SetChild(mBrowser);
      webDialog.SetSize(600, 400);
      webDialog.ShowModal();
   });
   */
}

std::string JackTripToolBar::ExecCommand(const char* cmd)
{
   //std::array<char, 128> buffer;
   std::string result;
   /*
   std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
   if (!pipe) {
      throw std::runtime_error("popen() failed!");
   }
   while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
   }
   */
   return result;
}

bool JackTripToolBar::JackTripExists()
{
   auto output = ExecCommand("jacktrip --version");
   std::cout << output << std::endl;
   auto const regex = std::regex(R"(JackTrip VERSION: (\d+\.\d+\.\d+))");
   return std::regex_search(output, regex);
}

void JackTripToolBar::RunJackTrip()
{
   /*
   if (mExec) {
      throw std::runtime_error("JackTrip instance is already running");
   }
   const char* cmd = "jacktrip -R -C 54.219.88.160 -z -t --bufstrategy 3 -q auto --srate 48000 --bufsize 128 --audiodevice \"Generic: Yeti Stereo Microphone\"";
   mExec = popen(cmd, "r");
   */
}

void JackTripToolBar::StopJackTrip()
{
   /*
   if (mExec) {
      pclose(mExec);
   }
   */
}

void JackTripToolBar::GetRecordingDownloadURL(std::string serverID, std::string recordingID)
{
   if (mAccessToken.empty()) {
      return;
   }
   std::string url = "https://app.jacktrip.org/api/servers/" + serverID + "/recordings/" + recordingID + "/download";
   audacity::network_manager::Request request(url);
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   mProgressDialog = BasicUI::MakeProgress(XO("Virtual Studio"), XO("Downloading recording"));

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         std::cout << "HTTP code: " << httpCode << std::endl;

         if (httpCode != 200) {
            std::cout << "bad http code" << std::endl;
            wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
            wxTheApp->CallAfter([] {BasicUI::ShowErrorDialog( {},
                                  XC("Error downloading recording", "Virtual Studio"),
                                  XC("Can't access the download link.", "Virtual Studio"),
                                  wxString(),
                                  BasicUI::ErrorDialogOptions{ BasicUI::ErrorDialogType::ModalErrorReport });
                               });
            return;
         }

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;
         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            std::cout << "Error parsing JSON: " << document.GetParseError() << std::endl;
            wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
            return;
         }

         auto downloadLink = document["url"].GetString();
         if (downloadLink) {
            std::cout << downloadLink << std::endl;
            DownloadRecording(downloadLink);
         }
      }
   );
}

std::string JackTripToolBar::GetDownloadLocalDir(std::string recordingID)
{
   auto tempDefaultLoc = TempDirectory::DefaultTempDir();
   return wxFileName(tempDefaultLoc, recordingID).GetFullPath().ToStdString();
}

std::string JackTripToolBar::GetRecordingIDFromUrl(std::string url)
{
   std::string recordingID = url.substr(0, url.rfind("?"));
   recordingID = recordingID.substr(recordingID.rfind("/")+1);
   return recordingID.substr(0, recordingID.rfind("-"));
}

std::string JackTripToolBar::GetDownloadFilenameFromUrl(std::string url)
{
   std::string downloadFilename = url.substr(0, url.rfind("?"));
   return downloadFilename.substr(downloadFilename.rfind("/")+1);
}

void JackTripToolBar::DownloadRecording(std::string url)
{
   // parse url for the expected filename
   std::string downloadFilename = GetDownloadFilenameFromUrl(url);

   // determine where to save the file
   auto recordingID = GetRecordingIDFromUrl(url);
   auto outputDir = GetDownloadLocalDir(recordingID);

   // do nothing if the directory exists - this should mean we already have the files locally
   if (wxDirExists(outputDir)) {
      std::cout << "Directory exists " << outputDir << std::endl;
      wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
      ImportRecordingFiles(recordingID);
      return;
   }
   wxMkdir(outputDir);

   mDownloadFile = wxFileName(outputDir, downloadFilename).GetFullPath().ToStdString();
   mDownloadOutput.open(mDownloadFile, std::ios::binary);
   std::cout << "Downloading to " << mDownloadFile << std::endl;

   // issue request
   audacity::network_manager::Request request(url);
   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);

   // Called once, when downloading is real will finish.
   response->setRequestFinishedCallback([response, recordingID, this](audacity::network_manager::IResponse*) {
      wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });

      if (mDownloadOutput.is_open()) {
         mDownloadOutput.close();
      }
      std::cout << "done" << std::endl;

      if (response->getError() != audacity::network_manager::NetworkError::NoError) {
         AudacityMessageBox( XO("Error downloading file") );
         return;
      } else {
         ExtractRecording(recordingID);
      }
   });

   // Called each time, since downloading for update progress status.
   response->setDownloadProgressCallback([this](int64_t current, int64_t expected) {
      wxTheApp->CallAfter([this, current, expected]{
            if(mProgressDialog != nullptr)
               mProgressDialog->Poll(current, expected);
      });
   });

   // Called each time, since downloading for get data.
   response->setOnDataReceivedCallback([response, this](audacity::network_manager::IResponse*) {
      if (response->getError() == audacity::network_manager::NetworkError::NoError) {
         std::vector<char> buffer(response->getBytesAvailable());
         size_t bytes = response->readData(buffer.data(), buffer.size());

         if (mDownloadOutput.is_open()) {
            mDownloadOutput.write(buffer.data(), buffer.size());
         }
      }
   });
}

void JackTripToolBar::ExtractRecording(std::string recordingID)
{
   if (mDownloadFile.find(".zip") == std::string::npos) {
      return;
   }

   // determine where to save the extracted files
   auto outputDir = GetDownloadLocalDir(recordingID);
   std::cout << "Extracting to " << outputDir << std::endl;

   wxFileInputStream in(mDownloadFile);
   wxZipInputStream zis(in);
   std::unique_ptr<wxZipEntry> upZe;

   // https://wiki.wxwidgets.org/WxZipInputStream
   while (upZe.reset(zis.GetNextEntry()), upZe) // != nullptr
   {
      // Access meta-data.
      wxString strFileName = outputDir + wxFileName::GetPathSeparator() + upZe->GetName();
      int nPermBits = upZe->GetMode();
      wxFileName fn;

      if (upZe->IsDir()) // This entry is a directory.
         fn.AssignDir(strFileName);
      else // This entry is a regular file.
         fn.Assign(strFileName);

      // Check if the directory exists, and if not, create it recursively.
      if (!wxDirExists(fn.GetPath()))
         wxFileName::Mkdir(fn.GetPath(), nPermBits, wxPATH_MKDIR_FULL);

      if (upZe->IsDir()) // This entry is a directory.
         continue; // Skip the file creation, because this entry is not a regular file, but a directory.

      // Read 'zis' to access the 'upZe's' data.
      if (!zis.CanRead()) {
         wxLogError(wxS("Couldn't read the zip entry '%s'."), upZe->GetName());
         return;
      }

      wxFileOutputStream fos(strFileName);

      if (!fos.IsOk()) {
         wxLogError(wxS("Couldn't create the file '%s'."), strFileName);
         return;
      }

      zis.Read(fos);
      //fos.Close();
      //zis.CloseEntry();
   }

   wxRemoveFile(mDownloadFile);
   std::cout << "Done extracting: " << outputDir << std::endl;
   ImportRecordingFiles(recordingID);
}

void JackTripToolBar::ImportRecordingFiles(std::string recordingID)
{
   auto outputDir = GetDownloadLocalDir(recordingID);

   wxDir dir(outputDir);
   if (dir.IsOpened()) {
      wxString filename;
      bool cont = dir.GetFirst(&filename);
      while (cont) {
         wxFileName file(filename);
         if (file.GetExt() == "flac") {
            std::cout << "Found file: " << filename << std::endl;
            // TODO: This doesn't work yet, need to figure out how to actually import the file
            wxTheApp->CallAfter([outputDir, filename, this]{
               bool success = ProjectFileManager::Get( mProject ).Import(outputDir + wxFileName::GetPathSeparator() + filename, false);
               std::cout << "Success: " << success << std::endl;
            });
         }
         cont = dir.GetNext(&filename);
      }
   }
}

void JackTripToolBar::JackTripListDevices(std::regex rex, std::vector<std::string>& devices)
{
   auto output = ExecCommand("jacktrip --listdevices");
   // parse for devices
   for (std::sregex_iterator it = std::sregex_iterator(output.begin(), output.end(), rex); it != std::sregex_iterator(); it++) {
      std::smatch match;
      match = *it;
      devices.push_back(match.str(1));
   }
}

void JackTripToolBar::JackTripListInputDevices(std::vector<std::string>& devices)
{
   auto const inputRex = std::regex(R"(.*"(.*)\"\s\([123456789]+\sins)");
   JackTripListDevices(inputRex, devices);
}

void JackTripToolBar::JackTripListOutputDevices(std::vector<std::string>& devices)
{
   auto const outputRex = std::regex(R"(.*"(.*)\"\s\(.*\s[123456789]+\souts)");
   JackTripListDevices(outputRex, devices);
}

static RegisteredToolbarFactory factory{
   []( AudacityProject &project ){
      return ToolBar::Holder{ safenew JackTripToolBar{ project } };
   }
};

namespace {
AttachedToolBarMenuItem sAttachment{
   /* i18n-hint: Clicking this menu item shows the toolbar
      that manages the audio devices */
   JackTripToolBar::ID(), wxT("ShowJackTripTB"), XXO("&JackTrip Toolbar")
};
}

enum {
   CancelButtonID = 10000,
   SignInButtonID,
   CloseButtonID,
};

BEGIN_EVENT_TABLE(VirtualStudioAuthDialog, wxDialogWrapper)
   EVT_BUTTON(CancelButtonID, VirtualStudioAuthDialog::OnClose)
   EVT_BUTTON(SignInButtonID, VirtualStudioAuthDialog::OnSignIn)
   EVT_BUTTON(CloseButtonID, VirtualStudioAuthDialog::OnClose)
END_EVENT_TABLE();

VirtualStudioAuthDialog::VirtualStudioAuthDialog(wxWindow* parent, std::string* parentAccessTokenPtr):
   wxDialogWrapper(parent, wxID_ANY, XO("Login to Virtual Studio"), wxDefaultPosition, { 480, -1 }, wxDEFAULT_DIALOG_STYLE)
{
   std::cout << "Yoooooo" << std::endl;

   if (mDeviceCode.empty()) {
      InitDeviceAuthorizationCodeFlow(parentAccessTokenPtr);
   }
}

VirtualStudioAuthDialog::~VirtualStudioAuthDialog()
{
   std::cout << "VirtualStudioAuthDialog destrutctor called" << std::endl;

   mIDToken = "";
   mAccessToken = "";
   mRefreshToken = "";

   mDeviceCode = "";
   mUserCode = "";
   mVerificationUri = "";
   mVerificationUriComplete = "";
   mPollingInterval = 0;
   mExpiresIn = 0;

   mError = false;
   mSuccess = false;
}

void VirtualStudioAuthDialog::DoLayout()
{
   ShuttleGui s(this, eIsCreating);

   s.StartVerticalLay();
   {
      s.StartInvisiblePanel(16);
      {
         s.SetBorder(0);
         s.AddSpace(0, 16, 0);
         s.AddTitle(XO("JackTrip"));

         s.AddSpace(0, 4, 0);
         s.AddTitle(XO("Virtual Studio"));

         s.AddSpace(0, 16, 0);
         s.AddTitle(XO("Please sign in and confirm the following code using your web browser. Return here when you are done."));

         if (!mUserCode.empty()) {
            s.AddSpace(0, 16, 0);
            s.AddTextBox(TranslatableString {}, mUserCode, 60);
         }

         s.AddSpace(0, 16, 0);

         s.AddWindow(safenew wxStaticLine { s.GetParent() }, wxEXPAND);

         s.AddSpace(0, 10, 0);

         s.StartHorizontalLay(wxEXPAND, 0);
         {
            s.AddSpace(0, 0, 1);

            mCancel = s.Id(CancelButtonID).AddButton(XXO("&Cancel"));
            mSignIn = s.Id(SignInButtonID).AddButton(XXO("&Sign In"));

            s.AddSpace(0, 0, 1);
         }
         s.EndHorizontalLay();
      }
      s.EndInvisiblePanel();
   }
   s.EndVerticalLay();

   Layout();
   Fit();
   Centre();
}

void VirtualStudioAuthDialog::UpdateLayout()
{
   ShuttleGui s(this, eIsCreating);

   wxWindowList children = s.GetParent()->GetChildren();
   for (auto child : children) {
      child->Show(false);
   }

   s.StartVerticalLay();
   {
      s.StartInvisiblePanel(16);
      {
         s.SetBorder(0);
         s.AddSpace(0, 16, 0);
         s.AddTitle(XO("Success"));
         s.AddSpace(0, 8, 0);
         s.AddTitle(XO("Close this window and re-open the JackTrip menu button to import Virtual Studio recordings"));
         s.AddSpace(0, 16, 0);

         s.StartHorizontalLay(wxEXPAND, 0);
         {
            s.AddSpace(0, 0, 1);

            mClose = s.Id(CloseButtonID).AddButton(XXO("&Close"));

            s.AddSpace(0, 0, 1);
         }
         s.EndHorizontalLay();

         s.AddSpace(0, 16, 0);
      }
      s.EndInvisiblePanel();
   }
   s.EndVerticalLay();

   Layout();
   Fit();
   Centre();
}

void VirtualStudioAuthDialog::InitDeviceAuthorizationCodeFlow(std::string* parentAccessTokenPtr)
{
   // prepare request for device authorization code
   audacity::network_manager::Request request("https://" + kAuthHost + "/oauth/device/code");
   request.setHeader("Content-Type", "application/x-www-form-urlencoded");
   request.setHeader("Accept", "application/json");

   // create payload
   std::string data = "client_id=" + kAuthClientId + "&scope=openid profile email offline_access read:servers&audience=" + kAuthAudience;

   auto response = audacity::network_manager::NetworkManager::GetInstance().doPost(request, data.data(), data.size());
   response->setRequestFinishedCallback(
      [response, parentAccessTokenPtr, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         std::cout << "InitDeviceAuthorizationCodeFlow HTTP code: " << httpCode << std::endl;

         if (httpCode != 200) {
            mDeviceCode = "";
            mError = true;
            return;
         }

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;

         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            mDeviceCode = "";
            mError = true;
            std::cout << "Error parsing JSON: " << document.GetParseError() << std::endl;
            return;
         }

         mDeviceCode = document["device_code"].GetString();
         mUserCode = document["user_code"].GetString();
         mVerificationUri = document["verification_uri"].GetString();
         mVerificationUriComplete = document["verification_uri_complete"].GetString();
         mPollingInterval = document["interval"].GetInt();
         mExpiresIn = document["expires_in"].GetInt();

         std::cout << "Device code: " << mDeviceCode << std::endl;
         std::cout << "User code: " << mUserCode << std::endl;
         std::cout << "Verification URI: " << mVerificationUri << std::endl;
         std::cout << "Verification URI complete: " << mVerificationUriComplete << std::endl;
         std::cout << "Polling interval: " << mPollingInterval << std::endl;
         std::cout << "Expires in: " << mExpiresIn << std::endl;

         wxTheApp->CallAfter([this]{ DoLayout(); });
         StartPolling(parentAccessTokenPtr);
      }
   );
}

void VirtualStudioAuthDialog::StartPolling(std::string* parentAccessTokenPtr)
{
   mExpiresIn -= mPollingInterval;
   if (mPollingInterval <= 0 || mExpiresIn <= 0) {
      std::cout << "Stopped polling" << std::endl;
      return;
   }

   std::this_thread::sleep_for(std::chrono::seconds(mPollingInterval));
   if (mIDToken.empty() || mAccessToken.empty() || mRefreshToken.empty()) {
      CheckForToken(parentAccessTokenPtr);
      StartPolling(parentAccessTokenPtr);
   }
}

void VirtualStudioAuthDialog::CheckForToken(std::string* parentAccessTokenPtr)
{
   if (mDeviceCode.empty()) return;

   // prepare request for device authorization code
   audacity::network_manager::Request request("https://" + kAuthHost + "/oauth/token");
   request.setHeader("Content-Type", "application/x-www-form-urlencoded");
   request.setHeader("Accept", "application/json");

   // create payload
   std::string data = "client_id=" + kAuthClientId + "&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=" + mDeviceCode;

   auto response = audacity::network_manager::NetworkManager::GetInstance().doPost(request, data.data(), data.size());
   response->setRequestFinishedCallback(
      [response, parentAccessTokenPtr, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         std::cout << "CheckForToken HTTP code: " << httpCode << std::endl;

         if (httpCode != 200) {
            return;
         }

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;

         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            std::cout << "Error parsing JSON: " << document.GetParseError() << std::endl;
            return;
         }

         mIDToken = document["id_token"].GetString();
         mAccessToken = document["access_token"].GetString();
         mRefreshToken = document["refresh_token"].GetString();
         *parentAccessTokenPtr = document["access_token"].GetString();

         if (mIDToken.empty() || mAccessToken.empty() || mRefreshToken.empty()) {
            return;
         }

         std::cout << "ID token: " << mIDToken << std::endl;
         std::cout << "Access token: " << mAccessToken << std::endl;
         std::cout << "Refresh token: " << mRefreshToken << std::endl;
         mError = false;
         mSuccess = true;
         mPollingInterval = 0;
         mExpiresIn = 0;
         wxTheApp->CallAfter([this]{ UpdateLayout(); });
      }
   );
}

void VirtualStudioAuthDialog::OnSignIn(wxCommandEvent &event)
{
   if (mVerificationUriComplete.empty()) {
      return;
   }
   BasicUI::OpenInDefaultBrowser(mVerificationUriComplete);
}

void VirtualStudioAuthDialog::OnClose(wxCommandEvent &event)
{
   Close();
}
