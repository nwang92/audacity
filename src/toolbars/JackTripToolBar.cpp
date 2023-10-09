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
#include <wx/bitmap.h>
// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
// Include your minimal set of headers here, or wx.h
#include <wx/window.h>
#endif
#include <wx/defs.h>
#include <wx/file.h>
#include <wx/ffile.h>

#include "wxPanelWrapper.h"

#include "../ActiveProject.h"

#include "ShuttleGui.h"

#include "AColor.h"
#include "AllThemeResources.h"
#include "AudioIOBase.h"
#include "DeviceToolBar.h"
#include "../KeyboardCapture.h"
#include "../ProjectAudioManager.h"
#include "Project.h"
#include "../ProjectWindows.h"
#include "DeviceManager.h"
#include "../prefs/PrefsDialog.h"
#include "../widgets/AButton.h"
#include "../widgets/BasicMenu.h"
#include "WaveTrack.h"
#include "SampleFormat.h"
#include "wxWidgetsWindowPlacement.h"
#include "Prefs.h"
#include "../import/Import.h"
#include "../import/ImportPlugin.h"

#include "SelectFile.h"
#include "Tags.h"
#include "ProgressDialog.h"

#define FLAC_HEADER "fLaC"

#define DESC XO("FLAC files")

static const auto exts = {
   wxT("flac"),
   wxT("flc")
};

#ifndef USE_LIBFLAC
#else /* USE_LIBFLAC */

#include "FLAC++/decoder.h"

#ifdef USE_LIBID3TAG
extern "C" {
#include <id3tag.h>
}
#endif

/* FLACPP_API_VERSION_CURRENT is 6 for libFLAC++ from flac-1.1.3 (see <FLAC++/export.h>) */
#if !defined FLACPP_API_VERSION_CURRENT || FLACPP_API_VERSION_CURRENT < 6
#define LEGACY_FLAC
#else
#undef LEGACY_FLAC
#endif


class VSFLACImportFileHandle;

class MyVSFLACFile final : public FLAC::Decoder::File
{
 public:
   MyVSFLACFile(VSFLACImportFileHandle *handle) : mFile(handle)
   {
      mWasError = false;
      set_metadata_ignore_all();
      set_metadata_respond(FLAC__METADATA_TYPE_VORBIS_COMMENT);
      set_metadata_respond(FLAC__METADATA_TYPE_STREAMINFO);
   }

   bool get_was_error() const
   {
      return mWasError;
   }
 private:
   friend class VSFLACImportFileHandle;
   VSFLACImportFileHandle *mFile;
   bool                  mWasError;
   wxArrayString         mComments;
 protected:
   FLAC__StreamDecoderWriteStatus write_callback(const FLAC__Frame *frame,
                                                         const FLAC__int32 * const buffer[]) override;
   void metadata_callback(const FLAC__StreamMetadata *metadata) override;
   void error_callback(FLAC__StreamDecoderErrorStatus status) override;
};


class VSFLACImportPlugin final : public ImportPlugin
{
 public:
   VSFLACImportPlugin():
   ImportPlugin( FileExtensions( exts.begin(), exts.end() ) )
   {
   }

   ~VSFLACImportPlugin() { }

   wxString GetPluginStringID() override { return wxT("libflac"); }
   TranslatableString GetPluginFormatDescription() override;
   std::unique_ptr<ImportFileHandle> Open(
      const FilePath &Filename, AudacityProject*)  override;
};


class VSFLACImportFileHandle final : public ImportFileHandle
{
   friend class MyVSFLACFile;
public:
   VSFLACImportFileHandle(const FilePath & name);
   ~VSFLACImportFileHandle();

   bool Init(WaveTrackFactory *trackFactory);

   TranslatableString GetFileDescription() override;
   ByteCount GetFileUncompressedBytes() override;
   ProgressResult Import(WaveTrackFactory *trackFactory, TrackHolders &outTracks,
              Tags *tags) override;

   ProgressResult ImportForVS(TrackHolders &outTracks);

   wxInt32 GetStreamCount() override { return 1; }

   const TranslatableStrings &GetStreamInfo() override
   {
      static TranslatableStrings empty;
      return empty;
   }

   void SetStreamUsage(wxInt32 WXUNUSED(StreamID), bool WXUNUSED(Use)) override
   {}

private:
   sampleFormat          mFormat;
   std::unique_ptr<MyVSFLACFile> mFile;
   wxFFile               mHandle;
   unsigned long         mSampleRate;
   unsigned long         mNumChannels;
   unsigned long         mBitsPerSample;
   FLAC__uint64          mNumSamples;
   FLAC__uint64          mSamplesDone;
   bool                  mStreamInfoDone;
   ProgressResult        mUpdateResult;
   NewChannelGroup       mChannels;
};


void MyVSFLACFile::metadata_callback(const FLAC__StreamMetadata *metadata)
{
   switch (metadata->type)
   {
      case FLAC__METADATA_TYPE_VORBIS_COMMENT:
         for (FLAC__uint32 i = 0; i < metadata->data.vorbis_comment.num_comments; i++) {
            mComments.push_back(UTF8CTOWX((char *)metadata->data.vorbis_comment.comments[i].entry));
         }
      break;

      case FLAC__METADATA_TYPE_STREAMINFO:
         mFile->mSampleRate=metadata->data.stream_info.sample_rate;
         mFile->mNumChannels=metadata->data.stream_info.channels;
         mFile->mBitsPerSample=metadata->data.stream_info.bits_per_sample;
         mFile->mNumSamples=metadata->data.stream_info.total_samples;

         // Widen mFormat after examining the file header
         if (mFile->mBitsPerSample<=16) {
            mFile->mFormat=int16Sample;
         } else if (mFile->mBitsPerSample<=24) {
            mFile->mFormat=int24Sample;
         } else {
            mFile->mFormat=floatSample;
         }
         mFile->mStreamInfoDone=true;
      break;
      // handle the other types we do nothing with to avoid a warning
      case FLAC__METADATA_TYPE_PADDING:	// do nothing with padding
      case FLAC__METADATA_TYPE_APPLICATION:	// no idea what to do with this
      case FLAC__METADATA_TYPE_SEEKTABLE:	// don't need a seektable here
      case FLAC__METADATA_TYPE_CUESHEET:	// convert this to labels?
      case FLAC__METADATA_TYPE_PICTURE:		// ignore pictures
      case FLAC__METADATA_TYPE_UNDEFINED:	// do nothing with this either

      // FIXME: not declared when compiling on Ubuntu.
      //case FLAC__MAX_METADATA_TYPE: // quiet compiler warning with this line
      default:
      break;
   }
}

void MyVSFLACFile::error_callback(FLAC__StreamDecoderErrorStatus WXUNUSED(status))
{
   mWasError = true;

   /*
   switch (status)
   {
   case FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC:
      wxPrintf(wxT("Flac Error: Lost sync\n"));
      break;
   case FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH:
      wxPrintf(wxT("Flac Error: Crc mismatch\n"));
      break;
   case FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER:
      wxPrintf(wxT("Flac Error: Bad Header\n"));
      break;
   default:
      wxPrintf(wxT("Flac Error: Unknown error code\n"));
      break;
   }*/
}

FLAC__StreamDecoderWriteStatus MyVSFLACFile::write_callback(const FLAC__Frame *frame,
                                                          const FLAC__int32 * const buffer[])
{
   // Don't let C++ exceptions propagate through libflac
   return GuardedCall< FLAC__StreamDecoderWriteStatus > ( [&] {
      auto tmp = ArrayOf< short >{ frame->header.blocksize };

      auto iter = mFile->mChannels.begin();
      for (unsigned int chn=0; chn<mFile->mNumChannels; ++iter, ++chn) {
         if (frame->header.bits_per_sample <= 16) {
            if (frame->header.bits_per_sample == 8) {
               for (unsigned int s = 0; s < frame->header.blocksize; s++) {
                  tmp[s] = buffer[chn][s] << 8;
               }
            } else /* if (frame->header.bits_per_sample == 16) */ {
               for (unsigned int s = 0; s < frame->header.blocksize; s++) {
                  tmp[s] = buffer[chn][s];
               }
            }

            iter->get()->Append((samplePtr)tmp.get(),
                     int16Sample,
                     frame->header.blocksize, 1,
                     int16Sample);
         }
         else {
            iter->get()->Append((samplePtr)buffer[chn],
                     int24Sample,
                     frame->header.blocksize, 1,
                     int24Sample);
         }
      }

      mFile->mSamplesDone += frame->header.blocksize;

      mFile->mUpdateResult = mFile->mProgress->Update((wxULongLong_t) mFile->mSamplesDone, mFile->mNumSamples != 0 ? (wxULongLong_t)mFile->mNumSamples : 1);
      if (mFile->mUpdateResult != ProgressResult::Success)
      {
         return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
      }

      return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
   }, MakeSimpleGuard(FLAC__STREAM_DECODER_WRITE_STATUS_ABORT) );
}

TranslatableString VSFLACImportPlugin::GetPluginFormatDescription()
{
    return DESC;
}


std::unique_ptr<ImportFileHandle> VSFLACImportPlugin::Open(
   const FilePath &filename, AudacityProject* project)
{
   // First check if it really is a FLAC file

   int cnt;
   wxFile binaryFile;
   if (!binaryFile.Open(filename)) {
      return nullptr; // File not found
   }

   // FIXME: TRAP_ERR wxFILE ops in FLAC Import could fail.
   // Seek() return value is not examined, for example.
#ifdef USE_LIBID3TAG
   // Skip any ID3 tags that might be present
   id3_byte_t query[ID3_TAG_QUERYSIZE];
   cnt = binaryFile.Read(query, sizeof(query));
   cnt = id3_tag_query(query, cnt);
   binaryFile.Seek(cnt);
#endif

   char buf[5];
   cnt = binaryFile.Read(buf, 4);
   binaryFile.Close();

   if (cnt == wxInvalidOffset || strncmp(buf, FLAC_HEADER, 4) != 0) {
      // File is not a FLAC file
      return nullptr;
   }

   // Open the file for import
   auto handle = std::make_unique<VSFLACImportFileHandle>(filename);

   auto &trackFactory = WaveTrackFactory::Get( *project );
   bool success = handle->Init(&trackFactory);
   if (!success) {
      return nullptr;
   }

   // This std::move is needed to "upcast" the pointer type
   return std::move(handle);
}

VSFLACImportFileHandle::VSFLACImportFileHandle(const FilePath & name)
:  ImportFileHandle(name),
   mSamplesDone(0),
   mStreamInfoDone(false),
   mUpdateResult(ProgressResult::Success)
{
   // Initialize mFormat as narrowest
   mFormat = narrowestSampleFormat;
   mFile = std::make_unique<MyVSFLACFile>(this);
}

bool VSFLACImportFileHandle::Init(WaveTrackFactory *trackFactory)
{
#ifdef LEGACY_FLAC
   bool success = mFile->set_filename(OSINPUT(mFilename));
   if (!success) {
      return false;
   }
   mFile->set_metadata_respond(FLAC__METADATA_TYPE_STREAMINFO);
   mFile->set_metadata_respond(FLAC__METADATA_TYPE_VORBIS_COMMENT);
   FLAC::Decoder::File::State state = mFile->init();
   if (state != FLAC__FILE_DECODER_OK) {
      return false;
   }
#else
   if (!mHandle.Open(mFilename, wxT("rb"))) {
      return false;
   }

   // Even though there is an init() method that takes a filename, use the one that
   // takes a file handle because wxWidgets can open a file with a Unicode name and
   // libflac can't (under Windows).
   //
   // Responsibility for closing the file is passed to libflac.
   // (it happens when mFile->finish() is called)
   bool result = mFile->init(mHandle.fp())?true:false;
   mHandle.Detach();

   if (result != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
      return false;
   }
#endif
   mFile->process_until_end_of_metadata();

#ifdef LEGACY_FLAC
   state = mFile->get_state();
   if (state != FLAC__FILE_DECODER_OK) {
      return false;
   }
#else
   // not necessary to check state, error callback will catch errors, but here's how:
   if (mFile->get_state() > FLAC__STREAM_DECODER_READ_FRAME) {
      return false;
   }
#endif

   if (!mFile->is_valid() || mFile->get_was_error()) {
      // This probably is not a FLAC file at all
      return false;
   }


   std::cout << "init " << mNumChannels << " samplerate " << mSampleRate << std::endl;
   mChannels.resize(mNumChannels);
   {
      auto iter = mChannels.begin();
      for (size_t c = 0; c < mNumChannels; ++iter, ++c)
         *iter = NewWaveTrack(*trackFactory, mFormat, mSampleRate);
   }
   return true;
}

TranslatableString VSFLACImportFileHandle::GetFileDescription()
{
   return DESC;
}


auto VSFLACImportFileHandle::GetFileUncompressedBytes() -> ByteCount
{
   // TODO: Get Uncompressed byte count.
   return 0;
}


ProgressResult VSFLACImportFileHandle::Import(WaveTrackFactory *trackFactory,
                                 TrackHolders &outTracks,
                                 Tags *tags)
{
   outTracks.clear();

   wxASSERT(mStreamInfoDone);

   CreateProgress();

   mChannels.resize(mNumChannels);

   {
      auto iter = mChannels.begin();
      for (size_t c = 0; c < mNumChannels; ++iter, ++c)
         *iter = NewWaveTrack(*trackFactory, mFormat, mSampleRate);
   }

   // TODO: Vigilant Sentry: Variable res unused after assignment (error code DA1)
   //    Should check the result.
   #ifdef LEGACY_FLAC
      bool res = (mFile->process_until_end_of_file() != 0);
   #else
      bool res = (mFile->process_until_end_of_stream() != 0);
   #endif
      wxUnusedVar(res);

   if (mUpdateResult == ProgressResult::Failed || mUpdateResult == ProgressResult::Cancelled) {
      return mUpdateResult;
   }

   for (const auto &channel : mChannels)
      channel->Flush();

   if (!mChannels.empty())
      outTracks.push_back(std::move(mChannels));

   wxString comment;
   wxString description;

   size_t cnt = mFile->mComments.size();
   if (cnt > 0) {
      tags->Clear();
      for (size_t c = 0; c < cnt; c++) {
         wxString name = mFile->mComments[c].BeforeFirst(wxT('='));
         wxString value = mFile->mComments[c].AfterFirst(wxT('='));
         wxString upper = name.Upper();
         if (upper == wxT("DATE") && !tags->HasTag(TAG_YEAR)) {
            long val;
            if (value.length() == 4 && value.ToLong(&val)) {
               name = TAG_YEAR;
            }
         }
         else if (upper == wxT("COMMENT") || upper == wxT("COMMENTS")) {
            comment = value;
            continue;
         }
         else if (upper == wxT("DESCRIPTION")) {
            description = value;
            continue;
         }
         tags->SetTag(name, value);
      }

      if (comment.empty()) {
         comment = description;
      }
      if (!comment.empty()) {
         tags->SetTag(TAG_COMMENTS, comment);
      }
   }

   return mUpdateResult;
}

ProgressResult VSFLACImportFileHandle::ImportForVS(TrackHolders &outTracks)
{
   outTracks.clear();

   CreateProgress();

   wxASSERT(mStreamInfoDone);

   // TODO: Vigilant Sentry: Variable res unused after assignment (error code DA1)
   //    Should check the result.
   #ifdef LEGACY_FLAC
      bool res = (mFile->process_until_end_of_file() != 0);
   #else
      bool res = (mFile->process_until_end_of_stream() != 0);
   #endif
      wxUnusedVar(res);

   if (mUpdateResult == ProgressResult::Failed || mUpdateResult == ProgressResult::Cancelled) {
      return mUpdateResult;
   }

   for (const auto &channel : mChannels)
      channel->Flush();

   if (!mChannels.empty())
      outTracks.push_back(std::move(mChannels));

   return mUpdateResult;
}


VSFLACImportFileHandle::~VSFLACImportFileHandle()
{
   mFile->finish();
}

#endif /* USE_LIBFLAC */


















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
   mServers.Clear();
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
   wxMenu* recordingsSubMenu = new wxMenu();
   wxMenu* serversSubMenu = new wxMenu();

   if (mUserID.empty() || mAccessToken.empty()) {
      menu.Append(kAudioSettings, _("&Login"));

      menu.Bind(wxEVT_MENU_CLOSE, [this](auto&) { mJackTrip->PopUp(); });
      menu.Bind(wxEVT_MENU, &JackTripToolBar::OnAuth, this, kAudioSettings);

      menu.Append(kAudioSettings, _("&Test Record"));
      menu.Bind(wxEVT_MENU, &JackTripToolBar::OnRecord, this, kAudioSettings);
   } else {
      for (auto it = mServerIdToRecordings.begin(); it != mServerIdToRecordings.end(); it++) {
         auto serverID = it->first;
         if (serverID != "") {
            auto name = mServerIdToName[serverID];
            JackTripRecordingChoices c = it->second;
            c.AppendSubMenu(*this, *recordingsSubMenu, serverID,
               &JackTripToolBar::OnRecording, _("&" + name));
            recordingsSubMenu->AppendSeparator();
         }
      }
      menu.AppendSubMenu(recordingsSubMenu, _("Import Recordings..."));
      mServers.AppendSubMenu(*this, menu, &JackTripToolBar::OnStudio, _("&Join Studio..."));
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
   FillVirtualStudio();
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
         wxLogInfo("GetUserInfo HTTP code: %d", httpCode);

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;
         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
            wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
            return;
         }

         auto sub = document["sub"].GetString();
         wxLogInfo("Sub: %s", sub);
         mUserID = sub;
         RepopulateMenus();
      }
   );
}

void JackTripToolBar::FillVirtualStudio()
{
   if (mUserID.empty() || mAccessToken.empty()) {
      return;
   }

   FillServers();
   FillRecordings();
   std::this_thread::sleep_for(std::chrono::seconds(30));
   RepopulateMenus();
}

void JackTripToolBar::FillServers()
{
   if (mUserID.empty() || mAccessToken.empty()) {
      return;
   }
   mServers.Clear();

   audacity::network_manager::Request request(kApiBaseUrl + "/api/servers");
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("FillServers HTTP code: %d", httpCode);

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;

         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
            return;
         }

         wxArrayString serverIds;
         wxArrayString serverNames;

         // Iterate over the array of objects
         Value::ConstValueIterator itr;
         for (itr = document.Begin(); itr != document.End(); ++itr) {
            auto serverID = itr->GetObject()["id"].GetString();
            auto serverName = itr->GetObject()["name"].GetString();
            auto sessionID = itr->GetObject()["sessionId"].GetString();
            auto enabled = itr->GetObject()["enabled"].GetBool();
            auto managed = itr->GetObject()["managed"].GetBool();
            if (!managed) {
               continue;
            }

            serverIds.push_back(serverID);
            serverNames.push_back(serverName);
         }

         mServers.Set(std::move(serverNames), std::move(serverIds));
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

   audacity::network_manager::Request request(kApiBaseUrl +  "/api/users/" + mUserID + "/recordings");
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("FillRecordings HTTP code: %d", httpCode);

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;

         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
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
            JackTripRecordingChoices recordings;

            auto names = recordingNamesByServerID[serverID];
            auto ids = recordingIdsByServerID[serverID];

            recordings.Set(std::move(names), std::move(ids));
            mServerIdToRecordings[serverID] = std::move(recordings);
         }
      }
   );
}

void JackTripToolBar::AppendRecordingsSubMenu(JackTripToolBar &toolbar,
   wxMenu& menu, const wxArrayString &labels, int checkedItem,
   std::string serverID, JackTripCallback callback, const wxString& title)
{
   auto subMenu = std::make_unique<wxMenu>();
   int ii = 0;
   for (const auto &label : labels) {
      // Assign fresh ID with wxID_ANY
      auto subMenuItem = subMenu->AppendCheckItem(wxID_ANY, label);
      //if (ii == checkedItem)
      subMenuItem->Check(false);
      subMenuItem->Enable(true);
      subMenu->Bind(wxEVT_MENU,
         [&toolbar, serverID, callback, ii](wxCommandEvent &){ (toolbar.*callback)(serverID, ii); },
         subMenuItem->GetId());
      ++ii;
   }
   auto menuItem = menu.AppendSubMenu(subMenu.release(), title);
   //if (checkedItem < 0)
   //   menuItem->Enable(false);
}

void JackTripToolBar::AppendStudiosSubMenu(JackTripToolBar &toolbar,
   wxMenu& menu, const wxArrayString &names, const wxArrayString &ids, int checkedItem,
   JackTripCallback callback, const wxString& title)
{
   auto subMenu = std::make_unique<wxMenu>();
   int ii = 0;
   for(size_t i = 0; i < names.GetCount(); i++) {
      std::string serverID = std::string(ids[i].mb_str());
      wxString name = names[i];

      // Assign fresh ID with wxID_ANY
      auto subMenuItem = subMenu->AppendCheckItem(wxID_ANY, name);
      //if (ii == checkedItem)
      subMenuItem->Check(false);
      subMenuItem->Enable(true);
      subMenu->Bind(wxEVT_MENU,
         [&toolbar, serverID, callback, ii](wxCommandEvent &){ (toolbar.*callback)(serverID, ii); },
         subMenuItem->GetId());
      ++ii;
   }
   auto menuItem = menu.AppendSubMenu(subMenu.release(), title);
   //if (checkedItem < 0)
   //   menuItem->Enable(false);
}


void JackTripToolBar::JackTripRecordingChoices::AppendSubMenu(JackTripToolBar &toolBar,
   wxMenu &menu, std::string serverID, JackTripCallback callback, const wxString &title)
{
   JackTripToolBar::AppendRecordingsSubMenu(toolBar, menu, mStrings, mIndex, serverID, callback, title);
}

void JackTripToolBar::JackTripStudioChoices::AppendSubMenu(JackTripToolBar &toolBar,
   wxMenu &menu, JackTripCallback callback, const wxString &title)
{
   JackTripToolBar::AppendStudiosSubMenu(toolBar, menu, mStrings, mIds, mIndex, callback, title);
}

void JackTripToolBar::OnRescannedDevices(DeviceChangeMessage m)
{
   // Hosts may have disappeared or appeared so a complete repopulate is needed.
   if (m == DeviceChangeMessage::Rescan)
      RepopulateMenus();
}

void JackTripToolBar::OnStudio(std::string serverID, int id)
{
   wxLogInfo("Clicked on %d for serverID %s", id, serverID);
   std::cout << "Clicked on " << serverID << std::endl;
   VirtualStudioServerDialog dlg(this, &mProject, serverID, mAccessToken);
   int retCode = dlg.ShowModal();
   dlg.Center();
}

void JackTripToolBar::OnRecording(std::string serverID, int id)
{
   wxLogInfo("Clicked on recording %d for serverID %s", id, serverID);

   if (mServerIdToRecordings.find(serverID) == mServerIdToRecordings.end()) {
      wxLogInfo("Key %s not found", serverID);
      return;
   }

   JackTripRecordingChoices c = mServerIdToRecordings[serverID];
   auto recordingID = std::string(c.GetID(id).mb_str());

   // do nothing if the directory exists - this should mean we already have the files locally
   auto outputDir = GetDownloadLocalDir(recordingID);
   if (wxDirExists(outputDir)) {
      wxLogInfo("Directory exists: %s", outputDir);
      ImportRecordingFiles(recordingID);
      return;
   }

   GetRecordingDownloadURL(serverID, recordingID);
}

void JackTripToolBar::OnRecord(wxCommandEvent& event)
{
   std::cout << "OnRecord called" << std::endl;

   std::string flacFile = "/Users/nwang/work/basic-hls-server/good4u.flac";
   VSFLACImportFileHandle flacHandle(flacFile);
   auto &trackFactory = WaveTrackFactory::Get( mProject );
   flacHandle.Init(&trackFactory);

   TrackHolders newTracks;
   flacHandle.ImportForVS(newTracks);

   /*
   double sampleRate = 48000;
   auto &tracks = TrackList::Get( mProject );
   auto track = WaveTrackFactory::Get( mProject ).Create();

   //auto track = WaveTrack::New( mProject );
   track->SetRate(sampleRate);
   track->SetName("New Wave Track");

   tracks.Add(track);
   */


   /*
   auto numSamples = 2048;
   std::ifstream infile(flacFile, std::ios::binary);
   char buffer[numSamples];
   infile.read(buffer, numSamples);
   track->Append((samplePtr)buffer, floatSample, numSamples);
   */

   /*
   auto track = WaveTrack::New( *mCurrProject );
   std::string flacFile = "/Users/nwang/work/basic-hls-server/good4u.flac";

   auto importer = FileImporter::Create(FileImporter::FLAC, flacFile);
   importer->Import(track, false); // set append to false

   bool s1 = ProjectFileManager::Get( *mCurrProject ).Import(flacFile, true);
   std::cout << "s1: " << s1 << std::endl;

   std::this_thread::sleep_for(std::chrono::seconds(20));

   bool s2 = ProjectFileManager::Get( *mCurrProject ).Import(flacFile, true);
   std::cout << "s2: " << s2 << std::endl;
   */

   /*
   ProjectAudioManager::Get( *mCurrProject ).OnVirtualStudioRecord( false );
   mIsRecording = true;
   */
}
void JackTripToolBar::OnAuth(wxCommandEvent& event)
{
   VirtualStudioAuthDialog dlg(this, &mAccessToken);
   dlg.SetSize(800, 600);
   int retCode = dlg.ShowModal();

   dlg.Center();

   wxLogInfo("Parent access token: %s", mAccessToken);
   if (!mAccessToken.empty()) {
      GetUserInfo();
   }
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
   std::string url = kApiBaseUrl +  "/api/servers/" + serverID + "/recordings/" + recordingID + "/download";
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
         wxLogInfo("GetRecordingDownloadURL HTTP code: %d", httpCode);

         if (httpCode != 200) {
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
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
            wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
            return;
         }

         auto downloadLink = document["url"].GetString();
         if (downloadLink) {
            wxLogInfo("Got download link: %s", downloadLink);
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
      wxLogInfo("Directory exists: %s", outputDir);
      wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });
      ImportRecordingFiles(recordingID);
      return;
   }
   wxMkdir(outputDir);

   mDownloadFile = wxFileName(outputDir, downloadFilename).GetFullPath().ToStdString();
   mDownloadOutput.open(mDownloadFile, std::ios::binary);
   wxLogInfo("Downloading to: %s", mDownloadFile);

   // issue request
   audacity::network_manager::Request request(url);
   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);

   // Called once, when downloading is real will finish.
   response->setRequestFinishedCallback([response, recordingID, this](audacity::network_manager::IResponse*) {
      wxTheApp->CallAfter([this]{ mProgressDialog.reset(); });

      if (mDownloadOutput.is_open()) {
         mDownloadOutput.close();
      }
      wxLogInfo("Download complete");

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
   wxLogInfo("Extracting to: %s", outputDir);

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
   wxLogInfo("Done extracting: %s", outputDir);
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
            wxLogInfo("Found file: %s", filename);
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
   CancelAuthButtonID = 10000,
   SignInButtonID,
   CloseAuthButtonID,
   JoinButtonID,
   RecordingButtonID,
   StopButtonID,
   CloseStudioButtonID,
};

BEGIN_EVENT_TABLE(VirtualStudioAuthDialog, wxDialogWrapper)
   EVT_BUTTON(CancelAuthButtonID, VirtualStudioAuthDialog::OnClose)
   EVT_BUTTON(SignInButtonID, VirtualStudioAuthDialog::OnSignIn)
   EVT_BUTTON(CloseAuthButtonID, VirtualStudioAuthDialog::OnClose)
END_EVENT_TABLE();

VirtualStudioAuthDialog::VirtualStudioAuthDialog(wxWindow* parent, std::string* parentAccessTokenPtr):
   wxDialogWrapper(parent, wxID_ANY, XO("Login to Virtual Studio"), wxDefaultPosition, { 480, -1 }, wxDEFAULT_DIALOG_STYLE)
{
   if (mDeviceCode.empty()) {
      InitDeviceAuthorizationCodeFlow(parentAccessTokenPtr);
   }
}

VirtualStudioAuthDialog::~VirtualStudioAuthDialog()
{
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

            mCancel = s.Id(CancelAuthButtonID).AddButton(XXO("&Cancel"));
            mSignIn = s.Id(SignInButtonID).AddButton(XXO("&Sign In"));

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

            mClose = s.Id(CloseAuthButtonID).AddButton(XXO("&Close"));

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
         wxLogInfo("InitDeviceAuthorizationCodeFlow HTTP code: %d", httpCode);

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
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
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
         wxLogInfo("CheckForToken HTTP code: %d", httpCode);

         if (httpCode != 200) {
            return;
         }

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;

         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
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

BEGIN_EVENT_TABLE(VirtualStudioServerDialog, wxDialogWrapper)
   EVT_BUTTON(JoinButtonID, VirtualStudioServerDialog::OnJoin)
   EVT_BUTTON(RecordingButtonID, VirtualStudioServerDialog::OnRecord)
   EVT_BUTTON(StopButtonID, VirtualStudioServerDialog::OnStop)
   EVT_BUTTON(CloseStudioButtonID, VirtualStudioServerDialog::OnClose)
END_EVENT_TABLE();

VirtualStudioServerDialog::VirtualStudioServerDialog(wxWindow* parent, AudacityProject* projectPtr, std::string serverID, std::string accessToken):
   wxDialogWrapper(parent, wxID_ANY, XO("Join Virtual Studio"), wxDefaultPosition, { 480, -1 }, wxDEFAULT_DIALOG_STYLE)
{
   mServerID = serverID;
   mAccessToken = accessToken;
   mCurrProject = projectPtr;
   if (!mServerID.empty() && !mAccessToken.empty()) {
      FetchServer();
   }
}

VirtualStudioServerDialog::~VirtualStudioServerDialog()
{
   mServerID = "";
   mAccessToken = "";
}

void VirtualStudioServerDialog::DoLayout()
{
   ShuttleGui s(this, eIsCreating);

   s.StartVerticalLay();
   {
      s.StartInvisiblePanel(16);
      {
         s.SetBorder(0);
         s.AddSpace(0, 16, 0);
         s.AddTitle(XO("JackTrip Virtual Studio"));

         if (!mServerBannerUrl.empty()) {
            s.AddSpace(0, 16, 0);
            // TODO: This doesn't work
            // wxImage img;
            // img.LoadFile(mServerBannerUrl);
            // img.Rescale((int)(LOGOWITHNAME_WIDTH), (int)(LOGOWITHNAME_HEIGHT));
            // wxBitmap bitmap(img);
            // s.AddIcon(&bitmap);
         }

         s.AddSpace(0, 16, 0);
         s.AddTitle(XO("Name: " + mServerName));
         s.AddTitle(XO("Status: " + mServerStatus));

         s.AddSpace(0, 16, 0);

         s.AddWindow(safenew wxStaticLine { s.GetParent() }, wxEXPAND);

         s.StartHorizontalLay(wxEXPAND, 0);
         {
            s.AddSpace(0, 0, 1);

            mClose = s.Id(CloseStudioButtonID).AddButton(XXO("&Close"));
            s.AddSpace(4, 0, 0);
            mJoin = s.Id(JoinButtonID).AddButton(XXO("&Launch JackTrip App"));

            s.AddSpace(0, 0, 1);
         }
         s.EndHorizontalLay();

         if (!mServerSessionID.empty() || mServerSessionID.empty()) {
            s.AddSpace(0, 24, 0);
            s.AddTitle(XO("Record Live Session"));
            s.AddSpace(0, 8, 0);
            s.StartHorizontalLay(wxEXPAND, 0);
            {
               s.AddSpace(0, 0, 1);

               mRecord = s.Id(RecordingButtonID).AddButton(XXO("&Record"));
               s.AddSpace(4, 0, 0);
               mStop = s.Id(StopButtonID).AddButton(XXO("&Stop"));

               s.AddSpace(0, 0, 1);
            }
            s.EndHorizontalLay();
         }

         s.AddSpace(0, 16, 0);
      }
      s.EndInvisiblePanel();
   }
   s.EndVerticalLay();

   Layout();
   Fit();
   Centre();
}

void VirtualStudioServerDialog::UpdateLayout()
{
   std::cout << "UpdateLayout called" << std::endl;
}

void VirtualStudioServerDialog::FetchServer()
{
   audacity::network_manager::Request request(kApiBaseUrl + "/api/servers/" + mServerID);
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("FetchServer HTTP code: %d", httpCode);

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();

         using namespace rapidjson;
         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
            return;
         }

         mServerName = document["name"].GetString();
         mServerBannerUrl = document["bannerURL"].GetString();
         mServerSessionID = document["sessionId"].GetString();
         mServerStatus = document["status"].GetString();
         mServerSampleRate = document["sampleRate"].GetDouble();
         mServerEnabled = document["enabled"].GetBool();

         std::cout << mServerName << std::endl;
         std::cout << mServerBannerUrl << std::endl;
         std::cout << mServerSessionID << std::endl;
         std::cout << mServerStatus << std::endl;
         std::cout << mServerSampleRate << std::endl;
         std::cout << mServerEnabled << std::endl;

         wxTheApp->CallAfter([this]{ DoLayout(); });
      }
   );
}

void VirtualStudioServerDialog::OnJoin(wxCommandEvent &event)
{
   if (mServerID.empty()) {
      return;
   }
   auto url = "jacktrip://join/" + mServerID;
   BasicUI::OpenInDefaultBrowser(url);
}

void VirtualStudioServerDialog::OnRecord(wxCommandEvent &event)
{
   if (mServerID.empty()) {
      return;
   }
   std::cout << "OnRecord called" << std::endl;

   NewChannelGroup       mChannels;
   std::string flacFile = "/Users/nwang/work/basic-hls-server/good4u.flac";

   //auto track = WaveTrackFactory::Get(*mCurrProject).Create();
   auto track = WaveTrack::New( *mCurrProject );
   track->SetRate(mServerSampleRate);

   auto numSamples = 2048;
   std::ifstream infile(flacFile, std::ios::binary);
   char buffer[numSamples];
   infile.read(buffer, numSamples);
   track->Append((samplePtr)buffer, floatSample, numSamples);



   /*
   auto track = WaveTrack::New( *mCurrProject );
   std::string flacFile = "/Users/nwang/work/basic-hls-server/good4u.flac";

   auto importer = FileImporter::Create(FileImporter::FLAC, flacFile);
   importer->Import(track, false); // set append to false

   bool s1 = ProjectFileManager::Get( *mCurrProject ).Import(flacFile, true);
   std::cout << "s1: " << s1 << std::endl;

   std::this_thread::sleep_for(std::chrono::seconds(20));

   bool s2 = ProjectFileManager::Get( *mCurrProject ).Import(flacFile, true);
   std::cout << "s2: " << s2 << std::endl;
   */

   /*
   ProjectAudioManager::Get( *mCurrProject ).OnVirtualStudioRecord( false );
   mIsRecording = true;
   */
}

void VirtualStudioServerDialog::OnStop(wxCommandEvent &event)
{
   auto &projectAudioManager = ProjectAudioManager::Get( *mCurrProject );
   bool canStop = projectAudioManager.CanStopAudioStream();
   if ( canStop ) {
      projectAudioManager.Stop();
   }
   mIsRecording = false;
}

void VirtualStudioServerDialog::OnClose(wxCommandEvent &event)
{
   Close();
}
