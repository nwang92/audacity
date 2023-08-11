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

#include <wx/app.h>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/tooltip.h>
#include <wx/filename.h>
#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/dir.h>
#include <wx/filename.h>

#include "../ActiveProject.h"

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

   /*
   menu.Append(kAudioSettings, _("&Audio Settings..."));

   menu.Bind(wxEVT_MENU_CLOSE, [this](auto&) { mJackTrip->PopUp(); });
   menu.Bind(wxEVT_MENU, &JackTripToolBar::OnSettings, this, kAudioSettings);
   */

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
   FillServers();
   // make the device display selection reflect the prefs if they exist
   UpdatePrefs();
}

void JackTripToolBar::FillServers()
{
   mServerIdToName.clear();
   mRecordingIdToName.clear();
   mServerIdToRecordings.clear();

   audacity::network_manager::Request request("https://app.jacktrip.org/api/servers");
   request.setHeader("Authorization", kAuthToken);
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

         // Iterate over the array of objects
         Value::ConstValueIterator itr;
         for (itr = document.Begin(); itr != document.End(); ++itr) {
            auto serverID = itr->GetObject()["id"].GetString();
            auto serverName = itr->GetObject()["name"].GetString();
            FetchRecordings(serverID);
            mServerIdToName[serverID] = serverName;
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
      std::cout << "Key found" << std::endl;
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

std::string JackTripToolBar::ExecCommand(const char* cmd)
{
   std::array<char, 128> buffer;
   std::string result;
   std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
   if (!pipe) {
      throw std::runtime_error("popen() failed!");
   }
   while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
   }
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
   if (mExec) {
      throw std::runtime_error("JackTrip instance is already running");
   }
   const char* cmd = "jacktrip -R -C 54.219.88.160 -z -t --bufstrategy 3 -q auto --srate 48000 --bufsize 128 --audiodevice \"Generic: Yeti Stereo Microphone\"";
   mExec = popen(cmd, "r");
}

void JackTripToolBar::StopJackTrip()
{
   if (mExec) {
      pclose(mExec);
   }
}

void JackTripToolBar::FetchRecordings(std::string serverID)
{
   if (serverID.empty()) {
      return;
   }
   std::string url = "https://app.jacktrip.org/api/servers/" + serverID + "/recordings";
   audacity::network_manager::Request request(url);
   request.setHeader("Authorization", kAuthToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, serverID, this](auto)
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

         // Iterate over the array of objects
         JackTripChoices recordings;
         wxArrayStringEx names;
         wxArrayStringEx ids;
         Value::ConstValueIterator itr;
         for (itr = document.Begin(); itr != document.End(); ++itr) {
            auto recordingName = itr->GetObject()["name"].GetString();
            auto recordingID = itr->GetObject()["id"].GetString();
            names.push_back(recordingName);
            ids.push_back(recordingID);
         }

         if (names.size() > 0) {
            recordings.Set(std::move(names), std::move(ids));
            mServerIdToRecordings[serverID] = std::move(recordings);
         }
      }
   );
}

void JackTripToolBar::GetRecordingDownloadURL(std::string serverID, std::string recordingID)
{
   std::string url = "https://app.jacktrip.org/api/servers/" + serverID + "/recordings/" + recordingID + "/download";
   audacity::network_manager::Request request(url);
   request.setHeader("Authorization", kAuthToken);
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

