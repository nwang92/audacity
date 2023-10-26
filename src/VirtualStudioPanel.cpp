/*!********************************************************************

   Audacity: A Digital Audio Editor

   @file VirtualStudioPanel.cpp

   @author Vitaly Sverchinsky

**********************************************************************/

#include "VirtualStudioPanel.h"

#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/menu.h>
#include <wx/wupdlock.h>
#include <wx/hyperlink.h>

#include <wx/dcbuffer.h>

#include "HelpSystem.h"
#include "Theme.h"
#include "AllThemeResources.h"
#include "AudioIO.h"
#include "BasicUI.h"
#include "Observer.h"
#include "PluginManager.h"
#include "Project.h"
#include "ProjectHistory.h"
#include "ProjectWindow.h"
#include "ProjectWindows.h"
#include "TrackPanelAx.h"
#include "AColor.h"
#include "WaveTrack.h"
#include "effects/EffectUI.h"
#include "effects/EffectManager.h"
#include "RealtimeEffectList.h"
#include "RealtimeEffectState.h"
#include "effects/RealtimeEffectStateUI.h"
#include "UndoManager.h"
#include "Prefs.h"
#include "BasicUI.h"
#include "wxPanelWrapper.h"
#include "ListNavigationEnabled.h"
#include "ListNavigationPanel.h"
#include "MovableControl.h"
#include "menus/MenuHelper.h"
#include "Menus.h"
#include "prefs/EffectsPrefs.h"
#include "widgets/ASlider.h"

#if wxUSE_ACCESSIBILITY
#include "WindowAccessible.h"
#endif

namespace
{

   class RealtimeEffectsMenuVisitor final : public MenuVisitor
   {
      wxMenu& mMenu;
      wxMenu* mMenuPtr { nullptr };
      int mMenuItemIdCounter { wxID_HIGHEST };
      std::vector<Identifier> mIndexedPluginList;
      int mMenuLevelCounter { 0 };
   public:

      RealtimeEffectsMenuVisitor(wxMenu& menu)
         : mMenu(menu), mMenuPtr(&mMenu) { }

      void DoBeginGroup( MenuTable::GroupItemBase &item, const Path& ) override
      {
         if(auto menuItem = dynamic_cast<MenuTable::MenuItem*>(&item))
         {
            //Don't create a group item for root
            if (mMenuLevelCounter != 0)
            {
               auto submenu = std::make_unique<wxMenu>();
               mMenuPtr->AppendSubMenu(submenu.get(), menuItem->GetTitle().Translation());
               mMenuPtr = submenu.release();
            }
            ++mMenuLevelCounter;
         }
      }

      void DoEndGroup( MenuTable::GroupItemBase &item, const Path& ) override
      {
         if(auto menuItem = dynamic_cast<MenuTable::MenuItem*>(&item))
         {
            --mMenuLevelCounter;
            if (mMenuLevelCounter != 0)
            {
               assert(mMenuPtr->GetParent() != nullptr);
               mMenuPtr = mMenuPtr->GetParent();
            }
         }
      }

      void DoVisit( MenuTable::SingleItem &item, const Path& ) override
      {
         if(auto commandItem = dynamic_cast<MenuTable::CommandItem*>(&item))
         {
            mMenuPtr->Append(mMenuItemIdCounter, commandItem->label_in.Translation());
            mIndexedPluginList.push_back(commandItem->name);
            ++mMenuItemIdCounter;
         }
      }

      void DoSeparator() override
      {
         mMenuPtr->AppendSeparator();
      }

      Identifier GetPluginID(int menuIndex) const
      {
         assert(menuIndex >= wxID_HIGHEST && menuIndex < (wxID_HIGHEST + mIndexedPluginList.size()));
         return mIndexedPluginList[menuIndex - wxID_HIGHEST];
      }

   };

   template <typename Visitor>
   void VisitRealtimeEffectStateUIs(SampleTrack& track, Visitor&& visitor)
   {
      if (!track.IsLeader())
         return;
      auto& effects = RealtimeEffectList::Get(track);
      effects.Visit(
         [visitor](auto& effectState, bool)
         {
            auto& ui = RealtimeEffectStateUI::Get(effectState);
            visitor(ui);
         });
   }

   void UpdateRealtimeEffectUIData(SampleTrack& track)
   {
      VisitRealtimeEffectStateUIs(
         track, [&](auto& ui) { ui.UpdateTrackData(track); });
   }

   void ReopenRealtimeEffectUIData(AudacityProject& project, SampleTrack& track)
   {
      VisitRealtimeEffectStateUIs(
         track,
         [&](auto& ui)
         {
            if (ui.IsShown())
            {
               ui.Hide(&project);
               ui.Show(project);
            }
         });
   }
   //fwd
   class ParticipantControl;

   class DropHintLine : public wxWindow
   {
   public:
      DropHintLine(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize)
                   : wxWindow(parent, id, pos, size, wxNO_BORDER, wxEmptyString)
      {
         wxWindow::SetBackgroundStyle(wxBG_STYLE_PAINT);
         Bind(wxEVT_PAINT, &DropHintLine::OnPaint, this);
      }

      bool AcceptsFocus() const override { return false; }

   private:
      void OnPaint(wxPaintEvent&)
      {
         wxBufferedPaintDC dc(this);
         const auto rect = wxRect(GetSize());

         dc.SetPen(*wxTRANSPARENT_PEN);
         dc.SetBrush(GetBackgroundColour());
         dc.DrawRectangle(rect);
      }
   };

   class HyperLinkCtrlWrapper : public ListNavigationEnabled<wxHyperlinkCtrl>
   {
   public:
      HyperLinkCtrlWrapper(wxWindow *parent,
                           wxWindowID id,
                           const wxString& label,
                           const wxString& url,
                           const wxPoint& pos = wxDefaultPosition,
                           const wxSize& size = wxDefaultSize,
                           long style = wxHL_DEFAULT_STYLE,
                           const wxString& name = wxHyperlinkCtrlNameStr)
      {
         Create(parent, id, label, url, pos, size, style, name);
      }

      void Create(wxWindow *parent,
                  wxWindowID id,
                  const wxString& label,
                  const wxString& url,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = wxHL_DEFAULT_STYLE,
                  const wxString& name = wxHyperlinkCtrlNameStr)
      {
         ListNavigationEnabled<wxHyperlinkCtrl>::Create(parent, id, label, url, pos, size, style, name);
         Bind(wxEVT_PAINT, &HyperLinkCtrlWrapper::OnPaint, this);
      }

      void OnPaint(wxPaintEvent& evt)
      {
         wxPaintDC dc(this);
         dc.SetFont(GetFont());
         dc.SetTextForeground(GetForegroundColour());
         dc.SetTextBackground(GetBackgroundColour());

         auto labelRect = GetLabelRect();

         dc.DrawText(GetLabel(), labelRect.GetTopLeft());
         if (HasFocus())
            AColor::DrawFocus(dc, labelRect);
      }
   };

#if wxUSE_ACCESSIBILITY
   class ParticipantControlAx : public wxAccessible
   {
   public:
      ParticipantControlAx(wxWindow* win = nullptr) : wxAccessible(win) { }

      wxAccStatus GetName(int childId, wxString* name) override
      {
         if(childId != wxACC_SELF)
            return wxACC_NOT_IMPLEMENTED;

         if(auto movable = wxDynamicCast(GetWindow(), MovableControl))
            //i18n-hint: argument - position of the effect in the effect stack
            *name = wxString::Format(_("Effect %d"), movable->FindIndexInParent() + 1);
         return wxACC_OK;
      }

      wxAccStatus GetChildCount(int* childCount) override
      {
         const auto window = GetWindow();
         *childCount = window->GetChildren().size();
         return wxACC_OK;
      }

      wxAccStatus GetChild(int childId, wxAccessible** child) override
      {
         if(childId == wxACC_SELF)
            *child = this;
         else
         {
            const auto window = GetWindow();
            const auto& children = window->GetChildren();
            const auto childIndex = childId - 1;
            if(childIndex < children.size())
               *child = children[childIndex]->GetAccessible();
            else
               *child = nullptr;
         }
         return wxACC_OK;
      }

      wxAccStatus GetRole(int childId, wxAccRole* role) override
      {
         if(childId != wxACC_SELF)
            return wxACC_NOT_IMPLEMENTED;

         *role = wxROLE_SYSTEM_PANE;
         return wxACC_OK;
      }

      wxAccStatus GetState(int childId, long* state) override
      {
         if(childId != wxACC_SELF)
            return wxACC_NOT_IMPLEMENTED;

         const auto window = GetWindow();
         if(!window->IsEnabled())
            *state = wxACC_STATE_SYSTEM_UNAVAILABLE;
         else
         {
            *state = wxACC_STATE_SYSTEM_FOCUSABLE;
            if(window->HasFocus())
               *state |= wxACC_STATE_SYSTEM_FOCUSED;
         }
         return wxACC_OK;
      }
   };
#endif

   class RealtimeEffectPicker
   {
   public:
      virtual std::optional<wxString> PickEffect(wxWindow* parent, const wxString& selectedEffectID) = 0;
   };

   class ParticipantBars
      : public wxPanelWrapper
      , public NonInterferingBase
   {
      AudacityProject *mProject;
      int mWidth;
      int mHeight;
      bool mClip;
      bool mGradient;
      bool mDB;
      int mDBRange;
      int mNumPeakSamplesToClip;
      double mPeakHoldDuration;

      unsigned mNumBars;
      VSMeterBar mBar[kMaxVSMeterBars]{};
      std::unique_ptr<wxBitmap> mBitmap;
      wxBrush mBkgndBrush;
      wxPen     mPeakPeakPen;
      wxBrush   mClipBrush;

   public:
      ParticipantBars(AudacityProject *project,
            wxWindow* parent,
            wxWindowID winid,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            float fDecayRate = 60.0f):
         wxPanelWrapper(parent, winid, pos, size),
         mProject(project),
         mWidth(size.x),
         mHeight(size.y),
         mGradient(true),
         mDB(true),
         mNumBars(2),
         mNumPeakSamplesToClip(3),
         mPeakHoldDuration(3),
         mDBRange(DecibelScaleCutoff.Read()),
         mClip(true)
      {
         Bind(wxEVT_PAINT, &ParticipantBars::OnPaint, this);
         Bind(wxEVT_SIZE, &ParticipantBars::OnSize, this);

         for (unsigned int j=0; j<mNumBars; j++) {
            mBar[j] = VSMeterBar{};
         }
      }

      ~ParticipantBars() {}

      static float ClipZeroToOne(float z) {
         if (z > 1.0)
            return 1.0;
         else if (z < 0.0)
            return 0.0;
         else
            return z;
      }

      static float ToDB(float v, float range) {
         double db;
         if (v > 0)
            db = LINEAR_TO_DB(fabs(v));
         else
            db = -999;
         return ClipZeroToOne((db + range) / range);
      }

      void UpdateDisplay(int numFrames, float left, float right) {
         for(unsigned int j=0; j<mNumBars; j++) {
            mBar[j].isclipping = false;

            auto val = right;
            if (j == 0) {
               val = left;
            }

            //
            if (mDB) {
               val = ToDB(val, mDBRange);
            }

            mBar[j].peak = val;

            // This smooths out the RMS signal
            float smooth = pow(0.9, (double)numFrames/1024.0);
            mBar[j].rms = mBar[j].rms * smooth + val * (1.0 - smooth);

            /*
            if (mT - mBar[j].peakHoldTime > mPeakHoldDuration || mBar[j].peak > mBar[j].peakHold) {
               mBar[j].peakHold = mBar[j].peak;
               mBar[j].peakHoldTime = mT;
            }
            */

            if (mBar[j].peak > mBar[j].peakPeakHold )
               mBar[j].peakPeakHold = mBar[j].peak;

            /*
            if (msg.clipping[j] || mBar[j].tailPeakCount+msg.headPeakCount[j] >= mNumPeakSamplesToClip) {
               mBar[j].clipping = true;
               mBar[j].isclipping = true;
            }
            mBar[j].tailPeakCount = msg.tailPeakCount[j];
            */
         }

         RepaintBarsNow();
      }

   private:

      void DrawMeterBar(wxDC &dc, VSMeterBar *bar)
      {
         // Cache some metrics
         wxCoord x = bar->r.GetLeft();
         wxCoord y = bar->r.GetTop();
         wxCoord w = bar->r.GetWidth();
         wxCoord h = bar->r.GetHeight();
         wxCoord ht;
         wxCoord wd;

         // Setup for erasing the background
         dc.SetPen(*wxTRANSPARENT_PEN);
         dc.SetBrush(mBkgndBrush);

         // Map the predrawn bitmap into the source DC
         wxMemoryDC srcDC;
         srcDC.SelectObject(*mBitmap);

         if (bar->vert)
         {
            // Copy as much of the predrawn meter bar as is required for the
            // current peak.
            // (h - 1) corresponds to the mRuler.SetBounds() in HandleLayout()
            ht = (int)(bar->peak * (h - 1) + 0.5);

            // Blank out the rest
            if (h - ht)
            {
               // ht includes peak value...not really needed but doesn't hurt
               dc.DrawRectangle(x, y, w, h - ht);
            }

            // Copy as much of the predrawn meter bar as is required for the
            // current peak.
            // +/-1 to include the peak position
            if (ht)
            {
               dc.Blit(x, y + h - ht - 1, w, ht + 1, &srcDC, x, y + h - ht - 1);
            }

            // Draw the "recent" peak hold line using the predrawn meter bar so that
            // it will be the same color as the original level.
            // (h - 1) corresponds to the mRuler.SetBounds() in HandleLayout()
            ht = (int)(bar->peakHold * (h - 1) + 0.5);
            if (ht > 1)
            {
               dc.Blit(x, y + h - ht - 1, w, 2, &srcDC, x, y + h - ht - 1);
            }

            // Draw the "maximum" peak hold line
            // (h - 1) corresponds to the mRuler.SetBounds() in HandleLayout()
            dc.SetPen(mPeakPeakPen);
            ht = (int)(bar->peakPeakHold * (h - 1) + 0.5);
            if (ht > 0)
            {
               AColor::Line(dc, x, y + h - ht - 1, x + w - 1, y + h - ht - 1);
               if (ht > 1)
               {
                  AColor::Line(dc, x, y + h - ht, x + w - 1, y + h - ht);
               }
            }
         }

         // No longer need the source DC, so unselect the predrawn bitmap
         srcDC.SelectObject(wxNullBitmap);

         // If meter had a clipping indicator, draw or erase it
         // LLL:  At least I assume that's what "mClip" is supposed to be for as
         //       it is always "true".
         if (mClip)
         {
            if (bar->clipping)
            {
               dc.SetBrush(mClipBrush);
            }
            else
            {
               dc.SetBrush(mBkgndBrush);
            }
            dc.SetPen(*wxTRANSPARENT_PEN);
            wxRect r(bar->rClip.GetX() + 1,
                     bar->rClip.GetY() + 1,
                     bar->rClip.GetWidth() - 1,
                     bar->rClip.GetHeight() - 1);
            dc.DrawRectangle(r);
         }
      }

      void ResetBar(VSMeterBar *b, bool resetClipping) {
         b->peak = 0.0;
         b->rms = 0.0;
         b->peakHold = 0.0;
         b->peakHoldTime = 0.0;
         if (resetClipping)
         {
            b->clipping = false;
            b->peakPeakHold = 0.0;
         }
         b->isclipping = false;
         b->tailPeakCount = 0;
      }

      void RepaintBarsNow() {
         // Invalidate the bars so they get redrawn
         for (unsigned int i = 0; i < mNumBars; i++)
         {
            Refresh(false);
         }
         // Immediate redraw (using wxPaintDC)
         Update();
         return;
      }

      void SetBarAndClip(int iBar, bool vert) {
         // Save the orientation
         mBar[iBar].vert = vert;

         // Create the bar rectangle and educe to fit inside the bevel
         mBar[iBar].r = mBar[iBar].b;
         mBar[iBar].r.x += 1;
         mBar[iBar].r.width -= 1;
         mBar[iBar].r.y += 1;
         mBar[iBar].r.height -= 1;

         if (vert)
         {
            if (mClip)
            {
               // Create the clip rectangle
               mBar[iBar].rClip = mBar[iBar].b;
               mBar[iBar].rClip.height = 3;

               // Make room for the clipping indicator
               mBar[iBar].b.y += 3 + kVSMeterBarsGap;
               mBar[iBar].b.height -= 3 + kVSMeterBarsGap;
               mBar[iBar].r.y += 3 + kVSMeterBarsGap;
               mBar[iBar].r.height -= 3 + kVSMeterBarsGap;
            }
         }
         else
         {
            if (mClip)
            {
               // Make room for the clipping indicator
               mBar[iBar].b.width -= 4;
               mBar[iBar].r.width -= 4;

               // Create the indicator rectangle
               mBar[iBar].rClip = mBar[iBar].b;
               mBar[iBar].rClip.x = mBar[iBar].b.GetRight() + 1 + kVSMeterBarsGap; // +1 for bevel
               mBar[iBar].rClip.width = 3;
            }
         }
      }

      void HandleLayout(wxDC &dc) {
         dc.SetFont(GetFont());
         int width = mWidth;
         int height = mHeight;
         int left = 0;
         int top = 0;
         int barw;
         int barh;
         int lside;
         int rside;

         // height is now the entire height of the meter canvas
         height -= top + kVSMeterBarsGap;

         // barw is half of the canvas while allowing for a gap between meters
         barw = (width - kVSMeterBarsGap) / 2;

         // barh is now the height of the canvas
         barh = height;

         // Save dimensions of the left bevel
         mBar[0].b = wxRect(left, top, barw, barh);

         // Save dimensions of the right bevel
         mBar[1].b = mBar[0].b;
         mBar[1].b.SetLeft(mBar[0].b.GetRight() + 1 + kVSMeterBarsGap); // +1 for right edge

         // Set bar and clipping indicator dimensions
         SetBarAndClip(0, true);
         SetBarAndClip(1, true);
      }

      //
      // Event handlers
      //
      void OnPaint(wxPaintEvent &evt) {
#if defined(__WXMAC__)
         auto paintDC = std::make_unique<wxPaintDC>(this);
#else
         std::unique_ptr<wxDC> paintDC{ wxAutoBufferedPaintDCFactory(this) };
#endif
         wxDC & destDC = *paintDC;
         wxColour clrText = theTheme.Colour( clrTrackPanelText );
         wxColour clrBoxFill = theTheme.Colour( clrMedium );

         if (true)
         {
            // Create a NEW one using current size and select into the DC
            mBitmap = std::make_unique<wxBitmap>();
            mBitmap->Create(mWidth, mHeight, destDC);
            wxMemoryDC dc;
            dc.SelectObject(*mBitmap);

            // Go calculate all of the layout metrics
            HandleLayout(dc);

            // Start with a clean background
            // LLL:  Should research USE_AQUA_THEME usefulness...
      //#ifndef USE_AQUA_THEME
#ifdef EXPERIMENTAL_THEMING
            //if( !mMeterDisabled )
            //{
            //   mBkgndBrush.SetColour( GetParent()->GetBackgroundColour() );
            //}
#endif
            mBkgndBrush.SetColour( GetBackgroundColour() );
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(mBkgndBrush);
            dc.DrawRectangle(0, 0, mWidth, mHeight);
      //#endif

            // Setup the colors for the 3 sections of the meter bars
            wxColor green(117, 215, 112);
            wxColor yellow(255, 255, 0);
            wxColor red(255, 0, 0);

            // Bug #2473 - (Sort of) Hack to make text on meters more
            // visible with darker backgrounds. It would be better to have
            // different colors entirely and as part of the theme.
            if (GetBackgroundColour().GetLuminance() < 0.25)
            {
               green = wxColor(117-100, 215-100, 112-100);
               yellow = wxColor(255-100, 255-100, 0);
               red = wxColor(255-100, 0, 0);
            }
            else if (GetBackgroundColour().GetLuminance() < 0.50)
            {
               green = wxColor(117-50, 215-50, 112-50);
               yellow = wxColor(255-50, 255-50, 0);
               red = wxColor(255-50, 0, 0);
            }

            // Draw the meter bars at maximum levels
            for (unsigned int i = 0; i < mNumBars; i++)
            {
               // Give it a recessed look
               AColor::Bevel(dc, false, mBar[i].b);

               // Draw the clip indicator bevel
               if (mClip)
               {
                  AColor::Bevel(dc, false, mBar[i].rClip);
               }

               // Cache bar rect
               wxRect r = mBar[i].r;

               if (mGradient)
               {
                  // Calculate the size of the two gradiant segments of the meter
                  double gradw;
                  double gradh;
                  if (mDB)
                  {
                     gradw = (double) r.GetWidth() / mDBRange * 6.0;
                     gradh = (double) r.GetHeight() / mDBRange * 6.0;
                  }
                  else
                  {
                     gradw = (double) r.GetWidth() / 100 * 25;
                     gradh = (double) r.GetHeight() / 100 * 25;
                  }

                  if (mBar[i].vert)
                  {
                     // Draw the "critical" segment (starts at top of meter and works down)
                     r.SetHeight(gradh);
                     dc.GradientFillLinear(r, red, yellow, wxSOUTH);

                     // Draw the "warning" segment
                     r.SetTop(r.GetBottom());
                     dc.GradientFillLinear(r, yellow, green, wxSOUTH);

                     // Draw the "safe" segment
                     r.SetTop(r.GetBottom());
                     r.SetBottom(mBar[i].r.GetBottom());
                     dc.SetPen(*wxTRANSPARENT_PEN);
                     dc.SetBrush(green);
                     dc.DrawRectangle(r);
                  }
                  else
                  {
                     // Draw the "safe" segment
                     r.SetWidth(r.GetWidth() - (int) (gradw + gradw + 0.5));
                     dc.SetPen(*wxTRANSPARENT_PEN);
                     dc.SetBrush(green);
                     dc.DrawRectangle(r);

                     // Draw the "warning"  segment
                     r.SetLeft(r.GetRight() + 1);
                     r.SetWidth(floor(gradw));
                     dc.GradientFillLinear(r, green, yellow);

                     // Draw the "critical" segment
                     r.SetLeft(r.GetRight() + 1);
                     r.SetRight(mBar[i].r.GetRight());
                     dc.GradientFillLinear(r, yellow, red);
                  }
#ifdef EXPERIMENTAL_METER_LED_STYLE
                  if (!mBar[i].vert)
                  {
                     wxRect r = mBar[i].r;
                     wxPen BackgroundPen;
                     BackgroundPen.SetColour( wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE) );
                     dc.SetPen( BackgroundPen );
                     int i;
                     for(i=0;i<r.width;i++)
                     {
                        // 2 pixel spacing between the LEDs
                        if( (i%7)<2 ){
                           AColor::Line( dc, i+r.x, r.y, i+r.x, r.y+r.height );
                        } else {
                           // The LEDs have triangular ends.
                           // This code shapes the ends.
                           int j = abs( (i%7)-4);
                           AColor::Line( dc, i+r.x, r.y, i+r.x, r.y+j +1);
                           AColor::Line( dc, i+r.x, r.y+r.height-j, i+r.x, r.y+r.height );
                        }
                     }
                  }
#endif
               }
            }
            dc.SetTextForeground( clrText );

            // Bitmap created...unselect
            dc.SelectObject(wxNullBitmap);
         }

         // Copy predrawn bitmap to the dest DC
         destDC.DrawBitmap(*mBitmap, 0, 0);

         // Go draw the meter bars, Left & Right channels using current levels
         for (unsigned int i = 0; i < mNumBars; i++)
         {
            DrawMeterBar(destDC, &mBar[i]);
         }

         destDC.SetTextForeground( clrText );
      }

      void OnSize(wxSizeEvent &evt) {
         GetClientSize(&mWidth, &mHeight);
         //Refresh();
      }

   };

   //UI control that represents individual effect from the effect list
   class ParticipantControl : public ListNavigationEnabled<MovableControl>
   {
      wxWeakRef<AudacityProject> mProject;
      std::shared_ptr<StudioParticipant> mParticipant;

      std::shared_ptr<EffectSettingsAccess> mSettingsAccess;

      RealtimeEffectPicker* mEffectPicker { nullptr };

      ThemedAButtonWrapper<AButton>* mChangeButton{nullptr};
      ThemedAButtonWrapper<AButton>* mEnableButton{nullptr};
      ThemedAButtonWrapper<AButton>* mMuteButton{nullptr};
      ASlider* mVolumeSlider{nullptr};
      ThemedAButtonWrapper<AButton>* mOptionsButton{};
      wxStaticText* mParticipantName{nullptr};
      ParticipantBars *mMeter{nullptr};

      Observer::Subscription mParticipantSubscription;

   public:
      ParticipantControl() = default;

      ParticipantControl(wxWindow* parent,
                   RealtimeEffectPicker* effectPicker,
                   wxWindowID winid,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize)
      {
         Create(parent, effectPicker, winid, pos, size);
      }

      void Create(wxWindow* parent,
                   RealtimeEffectPicker* effectPicker,
                   wxWindowID winid,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize)
      {
         mEffectPicker = effectPicker;

         //Prevents flickering and paint order issues
         MovableControl::SetBackgroundStyle(wxBG_STYLE_PAINT);
         MovableControl::Create(parent, winid, pos, size, wxNO_BORDER | wxWANTS_CHARS);

         Bind(wxEVT_PAINT, &ParticipantControl::OnPaint, this);
         Bind(wxEVT_SET_FOCUS, &ParticipantControl::OnFocusChange, this);
         Bind(wxEVT_KILL_FOCUS, &ParticipantControl::OnFocusChange, this);

         auto sizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);

         //On/off button
         auto enableButton = safenew ThemedAButtonWrapper<AButton>(this);
         enableButton->SetTranslatableLabel(XO("Power"));
         enableButton->SetImageIndices(0, bmpEffectOff, bmpEffectOff, bmpEffectOn, bmpEffectOn, bmpEffectOff);
         enableButton->SetButtonToggles(true);
         enableButton->SetBackgroundColorIndex(clrEffectListItemBackground);
         enableButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            std::cout << "enableButton clicked" << std::endl;
         });
         mEnableButton = enableButton;

         auto topSizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
         auto participantName = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
         participantName->SetForegroundColorIndex(clrTrackPanelText);
         mParticipantName = participantName;
         topSizer->Add(mParticipantName, 1, wxLEFT | wxTOP | wxCENTER, 4);

         auto bottomSizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);

         //Mute button
         auto muteButton = safenew ThemedAButtonWrapper<AButton>(this);
         muteButton->SetMinSize(wxSize(24, 24));
         muteButton->SetTranslatableLabel(XO("Mute"));
         muteButton->SetToolTip(XO("Mute"));
         muteButton->SetImageIndices(0, bmpMic, bmpMic, bmpMic, bmpMic, bmpMic);
         muteButton->SetButtonToggles(true);
         muteButton->SetBackgroundColorIndex(clrEffectListItemBackground);
         muteButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (mParticipant) {
               auto device = mParticipant->GetDeviceID();
               if (device.empty() || VirtualStudioPanel::IsWebrtcDevice(device)) {
                  wxTheApp->CallAfter([] {BasicUI::ShowErrorDialog( {},
                                  XC("Unsupported Action", "Virtual Studio"),
                                  XC("Cannot mute WebRTC participants.", "Virtual Studio"),
                                  wxString(),
                                  BasicUI::ErrorDialogOptions{ BasicUI::ErrorDialogType::ModalErrorReport });
                               });
                  return;
               }
               mParticipant->SetMute(!mParticipant->GetMute());
               mParticipant->SyncDeviceAPI();
            }
         });
         mMuteButton = muteButton;

         //Add a slider that controls the speed of playback.
         auto volumeSlider = safenew ThemedWindowWrapper<ASlider>(this,
            wxID_ANY,
            XO("Input Volume"),
            wxDefaultPosition,
            wxDefaultSize,
            ASlider::Options{}
               .Style( PERCENT_SLIDER )
               // 20 steps using page up/down, and 60 using arrow keys
               .Line( 0.05f )
               .Page( 0.05f )
               .DrawTicks(true)
               .StepValue(0.05f)
               //.ShowLabels(false)
         );
         //auto sz = bottomSizer->GetSize();
         //wxSize sz = GetSize();
         //SetSizeHints(sz.x, std::min(sz.y, 600));
         //volumeSlider->SetSizeHints(wxSize(150, 25), wxSize(sz.x, 25));
         volumeSlider->SetMinSize(wxSize(120, 20));
         volumeSlider->Set(1);
         volumeSlider->SetLabel(_("Input Volume"));
         volumeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
            if (mParticipant && mVolumeSlider) {
               auto device = mParticipant->GetDeviceID();
               if (device.empty() || VirtualStudioPanel::IsWebrtcDevice(device)) {
                  wxTheApp->CallAfter([] {BasicUI::ShowErrorDialog( {},
                                  XC("Unsupported Action", "Virtual Studio"),
                                  XC("Cannot change WebRTC participants volume.", "Virtual Studio"),
                                  wxString(),
                                  BasicUI::ErrorDialogOptions{ BasicUI::ErrorDialogType::ModalErrorReport });
                               });
                  return;
               }
               float val = mVolumeSlider->Get()*100;
               int valInt = (int)val;
               mParticipant->SetCaptureVolume(valInt);
               mParticipant->SyncDeviceAPI();
            }
         });
         mVolumeSlider = volumeSlider;

         //auto bottomText = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
         //bottomText->SetForegroundColorIndex(clrTrackPanelText);
         //bottomText->SetLabel("Testing testing testing");

         bottomSizer->Add(muteButton, 0, wxALL | wxEXPAND, 2);
         bottomSizer->Add(volumeSlider, 1, wxALL | wxEXPAND, 2);

         auto controlsSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
         controlsSizer->Add(topSizer.release(), 1, wxLEFT | wxEXPAND, 2);
         controlsSizer->Add(bottomSizer.release(), 1, wxEXPAND, 2);
         //Central button with effect name, show settings
         // const auto optionsButton = safenew ThemedAButtonWrapper<AButton>(this, wxID_ANY);
         // optionsButton->SetImageIndices(0,
         //    bmpHButtonNormal,
         //    bmpHButtonHover,
         //    bmpHButtonDown,
         //    bmpHButtonHover,
         //    bmpHButtonDisabled);
         // optionsButton->SetBackgroundColorIndex(clrEffectListItemBackground);
         // optionsButton->SetForegroundColorIndex(clrTrackPanelText);
         // optionsButton->SetButtonType(AButton::TextButton);
         // optionsButton->Bind(wxEVT_BUTTON, &ParticipantControl::OnOptionsClicked, this);

         //Remove/replace effect
         // auto changeButton = safenew ThemedAButtonWrapper<AButton>(this);
         // changeButton->SetImageIndices(0, bmpMoreNormal, bmpMoreHover, bmpMoreDown, bmpMoreHover, bmpMoreDisabled);
         // changeButton->SetBackgroundColorIndex(clrEffectListItemBackground);
         // changeButton->SetTranslatableLabel(XO("Replace effect"));
         // changeButton->Bind(wxEVT_BUTTON, &ParticipantControl::OnChangeButtonClicked, this);

         auto meter = safenew ParticipantBars( mProject, this, wxID_ANY, wxDefaultPosition, wxSize( 20, 56 ) );
         mMeter = meter;
         /*
         auto dragArea = safenew wxStaticBitmap(this, wxID_ANY, theTheme.Bitmap(bmpDragArea));
         dragArea->Disable();
         sizer->Add(dragArea, 0, wxLEFT | wxCENTER, 5);
         */
         sizer->Add(enableButton, 0, wxLEFT | wxCENTER, 4);
         sizer->Add(controlsSizer.release(), 1, wxLEFT | wxCENTER, 4);
         //sizer->Add(optionsButton, 1, wxLEFT | wxCENTER, 5);
         //sizer->Add(changeButton, 0, wxLEFT | wxRIGHT | wxCENTER, 5);
         sizer->Add(meter, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxCENTER, 4);
         //mChangeButton = changeButton;
         //mOptionsButton = optionsButton;

         auto vSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
         vSizer->Add(sizer.release(), 0, wxUP | wxDOWN | wxEXPAND, 2);

         SetSizer(vSizer.release());

#if wxUSE_ACCESSIBILITY
         SetAccessible(safenew ParticipantControlAx(this));
#endif
      }

      static const PluginDescriptor *GetPlugin(const PluginID &ID) {
         auto desc = PluginManager::Get().GetPlugin(ID);
         return desc;
      }

      void SetParticipant(AudacityProject& project,
         const std::shared_ptr<StudioParticipant>& participant, bool showHidden = false)
      {
         mProject = &project;
         mParticipant = participant;

         mParticipantSubscription.Reset();
         mParticipantSubscription = mParticipant->Subscribe([this](const ParticipantEvent& evt) {
            switch (evt.mType)
            {
            case ParticipantEvent::METER_CHANGE:
               if (this->IsShown() && mParticipant->GetID() == evt.mUid && mMeter) {
                  mMeter->UpdateDisplay(1, mParticipant->GetLeftVolume(), mParticipant->GetRightVolume());
               }
               break;
            case ParticipantEvent::VOLUME_CHANGE:
               if (this->IsShown() && mParticipant->GetID() == evt.mUid && mVolumeSlider) {
                  mVolumeSlider->Set(mParticipant->GetCaptureVolume());
               }
               break;
            case ParticipantEvent::MUTE_CHANGE:
               if (this->IsShown() && mParticipant->GetID() == evt.mUid && mMuteButton) {
                  if (mParticipant->GetMute()) {
                     mMuteButton->PushDown();
                     mMuteButton->SetBackgroundColour(theTheme.Colour(clrMeterInputLightPen));
                  } else {
                     mMuteButton->PopUp();
                     mMuteButton->SetBackgroundColour(theTheme.Colour(clrEffectListItemBackground));
                  }
               }
               break;
            /* TODO: Pretty sure this doesn't actually work
            case ParticipantEvent::SHOW:
               if (mParticipant->GetID() == evt.mUid) {
                  std::cout << "Show " << mParticipant->GetName() << std::endl;
                  {
                     this->Show(true);
                     Update();
                     GetSizer()->Layout();
                  }
               }
               break;
            case ParticipantEvent::HIDE:
               if (mParticipant->GetID() == evt.mUid) {
                  std::cout << "Hide " << mParticipant->GetName() << std::endl;
                  {
                     this->Show(false);
                     Update();
                     GetSizer()->Layout();
                  }
               }
               break;
            */
            default:
               break;
            }
         });

         if (mParticipant != nullptr) {
            auto device = mParticipant->GetDeviceID();
            auto name = mParticipant->GetName();
            if (mParticipantName) {
               mParticipantName->SetLabel(name);
            }
            auto img = mParticipant->GetImage();
            if (mEnableButton) {
               mEnableButton->SetImages(img, img, img, img, img);
            }
            if (mMuteButton) {
               if (VirtualStudioPanel::IsWebrtcDevice(device)) {
                  mMuteButton->Disable();
               } else {
                  mMuteButton->Enable();
                  if (mParticipant->GetMute()) {
                     mMuteButton->PushDown();
                     mMuteButton->SetBackgroundColour(theTheme.Colour(clrMeterInputLightPen));
                  } else {
                     mMuteButton->PopUp();
                     mMuteButton->SetBackgroundColour(theTheme.Colour(clrEffectListItemBackground));
                  }
               }
            }
            if (mVolumeSlider) {
               if (VirtualStudioPanel::IsWebrtcDevice(device)) {
                  mVolumeSlider->Enable(false);
               } else {
                  mVolumeSlider->Enable(true);
                  mVolumeSlider->Set(mParticipant->GetCaptureVolume());
               }
            }
            if (!showHidden) {
               {
                  this->Show(!device.empty());
                  Update();
                  GetSizer()->Layout();
               }
            }
         }
      }

      void RemoveFromList()
      {
         std::cout << "RemoveFromList" << std::endl;
         if (mProject == nullptr || mParticipant == nullptr) {
            return;
         }

         // if(mProject == nullptr || mEffectState == nullptr)
         //    return;

         // auto& ui = RealtimeEffectStateUI::Get(*mEffectState);
         // // Don't need autosave for the effect that is being removed
         // ui.Hide();

         // auto effectName = GetEffectName();
         // //After AudioIO::RemoveState call this will be destroyed
         // auto project = mProject.get();
         // auto trackName = mTrack->GetName();

         // AudioIO::Get()->RemoveState(*project, &*mTrack, mEffectState);
         // ProjectHistory::Get(*project).PushState(
         //    /*! i18n-hint: undo history record
         //     first parameter - realtime effect name
         //     second parameter - track name
         //     */
         //    XO("Removed %s from %s").Format(effectName, trackName),
         //    /*! i18n-hint: undo history record
         //     first parameter - realtime effect name */
         //    XO("Remove %s").Format(effectName)
         // );
      }

      void OnOptionsClicked(wxCommandEvent& event)
      {
         std::cout << "OnOptionsClicked" << std::endl;
         if (mProject == nullptr || mParticipant == nullptr) {
            return;
         }

         /*
         const auto ID = mEffectState->GetID();
         const auto effectPlugin = EffectManager::Get().GetEffect(ID);

         if(effectPlugin == nullptr)
         {
            ///TODO: effect is not available
            return;
         }

         auto& effectStateUI = RealtimeEffectStateUI::Get(*mEffectState);

         effectStateUI.UpdateTrackData(*mTrack);
         effectStateUI.Toggle( *mProject );
         */
      }

      void OnChangeButtonClicked(wxCommandEvent& event)
      {
         std::cout << "OnChangeButtonClicked" << std::endl;
         if (mProject == nullptr || mParticipant == nullptr) {
            return;
         }

         // if(!mTrack || mProject == nullptr)
         //    return;
         // if(mEffectState == nullptr)
         //    return;//not initialized

         // const auto effectID = mEffectPicker->PickEffect(mChangeButton, mEffectState->GetID());
         // if(!effectID)
         //    return;//nothing

         // if(effectID->empty())
         // {
         //    RemoveFromList();
         //    return;
         // }

         // auto &em = RealtimeEffectManager::Get(*mProject);
         // auto oIndex = em.FindState(&*mTrack, mEffectState);
         // if (!oIndex)
         //    return;

         // auto oldName = GetEffectName();
         // auto &project = *mProject;
         // auto trackName = mTrack->GetName();
         // if (auto state = AudioIO::Get()
         //    ->ReplaceState(project, &*mTrack, *oIndex, *effectID)
         // ){
         //    // Message subscription took care of updating the button text
         //    // and destroyed `this`!
         //    auto effect = state->GetEffect();
         //    assert(effect); // postcondition of ReplaceState
         //    ProjectHistory::Get(project).PushState(
         //       /*i18n-hint: undo history,
         //        first and second parameters - realtime effect names
         //        */
         //       XO("Replaced %s with %s")
         //          .Format(oldName, effect->GetName()),
         //       /*! i18n-hint: undo history record
         //        first parameter - realtime effect name */
         //       XO("Replace %s").Format(oldName));
         // }
      }

      void OnPaint(wxPaintEvent&)
      {
         wxBufferedPaintDC dc(this);
         const auto rect = wxRect(GetSize());

         dc.SetPen(*wxTRANSPARENT_PEN);
         dc.SetBrush(GetBackgroundColour());
         dc.DrawRectangle(rect);

         dc.SetPen(theTheme.Colour(clrEffectListItemBorder));
         dc.SetBrush(theTheme.Colour(clrEffectListItemBorder));
         dc.DrawLine(rect.GetBottomLeft(), rect.GetBottomRight());

         if(HasFocus())
            AColor::DrawFocus(dc, GetClientRect().Deflate(3, 3));
      }

      void OnFocusChange(wxFocusEvent& evt)
      {
         Refresh(false);
         evt.Skip();
      }
   };

   static wxString GetSafeVendor(const PluginDescriptor& descriptor)
   {
      if (descriptor.GetVendor().empty())
         return XO("Unknown").Translation();

      return descriptor.GetVendor();
   }
}

class VirtualStudioParticipantListWindow
   : public wxScrolledWindow
   , public RealtimeEffectPicker
   , public PrefsListener
{
   wxWeakRef<AudacityProject> mProject;
   std::string mServerID;
   std::shared_ptr<SampleTrack> mTrack;
   AButton* mAddEffect{nullptr};
   wxStaticText* mParticipantsHint{nullptr};
   wxWindow* mStudioLink{nullptr};
   wxWindow* mParticipantListContainer{nullptr};
   StudioParticipantMap* mSubscriptionsMap{nullptr};

   bool mShowHiddenParticipants;

   std::unique_ptr<MenuTable::MenuItem> mEffectMenuRoot;

   Observer::Subscription mParticipantChangeSubscription;

public:
   VirtualStudioParticipantListWindow(wxWindow *parent,
                     wxWindowID winid = wxID_ANY,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxScrolledWindowStyle,
                     const wxString& name = wxPanelNameStr)
      : wxScrolledWindow(parent, winid, pos, size, style, name)
   {
      // this is a convenience flag to debug participants by showing all subscribers of this studio
      mShowHiddenParticipants = false;

      Bind(wxEVT_SIZE, &VirtualStudioParticipantListWindow::OnSizeChanged, this);
#ifdef __WXMSW__
      //Fixes flickering on redraw
      wxScrolledWindow::SetDoubleBuffered(true);
#endif
      auto vsp = dynamic_cast<VirtualStudioPanel*>(parent);
      mSubscriptionsMap = vsp->GetSubscriptionsMap();

      mParticipantChangeSubscription.Reset();
      mParticipantChangeSubscription = mSubscriptionsMap->Subscribe([this](const ParticipantEvent& evt) {
         switch (evt.mType)
         {
         case ParticipantEvent::REFRESH:
            std::cout << "Participant list changed" << std::endl;
            ReloadEffectsList();
            break;
         default:
            break;
         }
      });

      auto rootSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);

      auto participantsTitle = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, "Participants:", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
      participantsTitle->SetForegroundColorIndex(clrTrackPanelText);

      auto participantListContainer = safenew ThemedWindowWrapper<wxPanel>(this, wxID_ANY);
      participantListContainer->SetBackgroundColorIndex(clrMedium);
      participantListContainer->SetSizer(safenew wxBoxSizer(wxVERTICAL));
      participantListContainer->SetDoubleBuffered(true);
      participantListContainer->Hide();
      mParticipantListContainer = participantListContainer;

      /*
      auto addEffect = safenew ThemedAButtonWrapper<AButton>(this, wxID_ANY);
      addEffect->SetImageIndices(0,
            bmpHButtonNormal,
            bmpHButtonHover,
            bmpHButtonDown,
            bmpHButtonHover,
            bmpHButtonDisabled);
      addEffect->SetTranslatableLabel(XO("Add effect"));
      addEffect->SetButtonType(AButton::TextButton);
      addEffect->SetBackgroundColorIndex(clrMedium);
      addEffect->SetForegroundColorIndex(clrTrackPanelText);
      addEffect->Bind(wxEVT_BUTTON, &VirtualStudioParticipantListWindow::OnAddEffectClicked, this);
      mAddEffect = addEffect;
      */

      auto participantsHint = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
      //Workaround: text is set in the OnSizeChange
      participantsHint->SetForegroundColorIndex(clrTrackPanelText);
      mParticipantsHint = participantsHint;

      auto studioLink = safenew ThemedWindowWrapper<wxHyperlinkCtrl>(
         this, wxID_ANY, _("Manage studio"),
         kApiBaseUrl + "/studios", wxDefaultPosition,
         wxDefaultSize, wxHL_ALIGN_LEFT | wxHL_CONTEXTMENU);

      //i18n-hint: Hyperlink to the effects stack panel tutorial video
      studioLink->SetTranslatableLabel(XO("Manage studio"));
#if wxUSE_ACCESSIBILITY
      safenew WindowAccessible(studioLink);
#endif

      studioLink->Bind(
         wxEVT_HYPERLINK, [](wxHyperlinkEvent& event)
         { BasicUI::OpenInDefaultBrowser(event.GetURL()); });

      mStudioLink = studioLink;

      //indicates the insertion position of the item
      auto dropHintLine = safenew ThemedWindowWrapper<DropHintLine>(participantListContainer, wxID_ANY);
      dropHintLine->SetBackgroundColorIndex(clrDropHintHighlight);
      dropHintLine->Hide();

      rootSizer->Add(participantsTitle, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 4);
      rootSizer->Add(participantListContainer, 0, wxEXPAND | wxBOTTOM, 0);
      //rootSizer->Add(addEffect, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 20);
      rootSizer->Add(participantsHint, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 20);
      rootSizer->Add(studioLink, 0, wxLEFT | wxRIGHT | wxEXPAND, 20);

      SetSizer(rootSizer.release());
      SetMinSize({});

      Bind(EVT_MOVABLE_CONTROL_DRAG_STARTED, [dropHintLine](const MovableControlEvent& event)
      {
         if(auto window = dynamic_cast<wxWindow*>(event.GetEventObject()))
            window->Raise();
      });
      Bind(EVT_MOVABLE_CONTROL_DRAG_POSITION, [this, dropHintLine](const MovableControlEvent& event)
      {
         constexpr auto DropHintLineHeight { 3 };//px

         auto sizer = mParticipantListContainer->GetSizer();
         assert(sizer != nullptr);

         if(event.GetSourceIndex() == event.GetTargetIndex())
         {
            //do not display hint line if position didn't change
            dropHintLine->Hide();
            return;
         }

         if(!dropHintLine->IsShown())
         {
            dropHintLine->Show();
            dropHintLine->Raise();
            if(auto window = dynamic_cast<wxWindow*>(event.GetEventObject()))
               window->Raise();
         }

         auto item = sizer->GetItem(event.GetTargetIndex());
         dropHintLine->SetSize(item->GetSize().x, DropHintLineHeight);

         if(event.GetTargetIndex() > event.GetSourceIndex())
            dropHintLine->SetPosition(item->GetRect().GetBottomLeft() - wxPoint(0, DropHintLineHeight));
         else
            dropHintLine->SetPosition(item->GetRect().GetTopLeft());
      });
      Bind(EVT_MOVABLE_CONTROL_DRAG_FINISHED, [this, dropHintLine](const MovableControlEvent& event)
      {
         dropHintLine->Hide();

         if(mProject == nullptr)
            return;

         // auto& effectList = RealtimeEffectList::Get(*mTrack);
         const auto from = event.GetSourceIndex();
         const auto to = event.GetTargetIndex();
         std::cout << "Attempting move from " << from << " to " << to << std::endl;

         // if(from != to)
         // {
         //    auto effectName =
         //       effectList.GetStateAt(from)->GetEffect()->GetName();
         //    bool up = (to < from);
         //    effectList.MoveEffect(from, to);
         //    ProjectHistory::Get(*mProject).PushState(
         //       (up
         //          /*! i18n-hint: undo history record
         //           first parameter - realtime effect name
         //           second parameter - track name
         //           */
         //          ? XO("Moved %s up in %s")
         //          /*! i18n-hint: undo history record
         //           first parameter - realtime effect name
         //           second parameter - track name
         //           */
         //          : XO("Moved %s down in %s"))
         //          .Format(effectName, mTrack->GetName()),
         //       XO("Change effect order"), UndoPush::CONSOLIDATE);
         // }
         // else
         // {
         //    wxWindowUpdateLocker freeze(this);
         //    Layout();
         // }
         Layout();
      });
      SetScrollRate(0, 20);

      UpdateEffectMenuItems();
   }

   void UpdatePrefs() override
   {
      UpdateEffectMenuItems();
   }

   std::optional<wxString> PickEffect(wxWindow* parent, const wxString& selectedEffectID) override
   {
      wxMenu menu;
      if(!selectedEffectID.empty())
      {
         //no need to handle language change since menu creates its own event loop
         menu.Append(wxID_REMOVE, _("No Effect"));
         menu.AppendSeparator();
      }

      RealtimeEffectsMenuVisitor visitor { menu };

      Registry::Visit(visitor, mEffectMenuRoot.get());

      int commandId = wxID_NONE;

      menu.AppendSeparator();
      menu.Append(wxID_MORE, _("Get more effects..."));

      menu.Bind(wxEVT_MENU, [&](wxCommandEvent evt) { commandId = evt.GetId(); });

      if(parent->PopupMenu(&menu, parent->GetClientRect().GetLeftBottom()) && commandId != wxID_NONE)
      {
         if(commandId == wxID_REMOVE)
            return wxString {};
         else if(commandId == wxID_MORE)
            OpenInDefaultBrowser("https://plugins.audacityteam.org/");
         else
            return visitor.GetPluginID(commandId).GET();
      }

      return {};
   }

   void UpdateEffectMenuItems()
   {
      using namespace MenuTable;
      auto root = Menu("", TranslatableString{});

      static auto realtimeEffectPredicate = [](const PluginDescriptor& desc)
      {
         return desc.IsEffectRealtime();
      };

      const auto groupby = RealtimeEffectsGroupBy.Read();

      auto analyzeSection = Section("", Menu("", XO("Analyze")));
      auto submenu =
         static_cast<MenuItem*>(analyzeSection->begin()->get());
      MenuHelper::PopulateEffectsMenu(
         *submenu,
         EffectTypeAnalyze,
         {}, groupby, nullptr,
         realtimeEffectPredicate
      );

      if(!submenu->empty())
      {
         root->push_back(move(analyzeSection));
      }

      MenuHelper::PopulateEffectsMenu(
         *root,
         EffectTypeProcess,
         {}, groupby, nullptr,
         realtimeEffectPredicate
      );

      mEffectMenuRoot.swap(root);
   }

   void OnSizeChanged(wxSizeEvent& event)
   {
      if(auto sizerItem = GetSizer()->GetItem(mParticipantsHint))
      {
         //We need to wrap the text whenever panel width changes and adjust widget height
         //so that text is fully visible, but there is no height-for-width layout algorithm
         //in wxWidgets yet, so for now we just do it manually

         //Restore original text, because 'Wrap' will replace it with wrapped one
         mParticipantsHint->SetLabel(_("View participants in this studio once they join."));
         mParticipantsHint->Wrap(GetClientSize().x - sizerItem->GetBorder() * 2);
         mParticipantsHint->InvalidateBestSize();
      }
      event.Skip();
   }

   void OnEffectListItemChange(const RealtimeEffectListMessage& msg)
   {
      auto sizer = mParticipantListContainer->GetSizer();
      /*
      const auto insertItem = [this, &msg](){
         auto& effects = RealtimeEffectList::Get(*mTrack);
         InsertParticipantRow(msg.srcIndex, effects.GetStateAt(msg.srcIndex));
         mParticipantsHint->Hide();
         mStudioLink->Hide();
      };
      const auto removeItem = [&](){
         auto& ui = RealtimeEffectStateUI::Get(*msg.affectedState);
         // Don't need to auto-save changed settings of effect that is deleted
         // Undo history push will do it anyway
         ui.Hide();

         auto window = sizer->GetItem(msg.srcIndex)->GetWindow();
         sizer->Remove(msg.srcIndex);
         wxTheApp->CallAfter([ref = wxWeakRef { window }] {
            if(ref) ref->Destroy();
         });

         if(sizer->IsEmpty())
         {
            if(mParticipantListContainer->IsDescendant(FindFocus()))
               mAddEffect->SetFocus();

            mParticipantListContainer->Hide();
            mParticipantsHint->Show();
            mStudioLink->Show();
         }
      };

      wxWindowUpdateLocker freeze(this);
      if(msg.type == RealtimeEffectListMessage::Type::Move)
      {
         const auto sizer = mParticipantListContainer->GetSizer();

         const auto movedItem = sizer->GetItem(msg.srcIndex);

         const auto proportion = movedItem->GetProportion();
         const auto flag = movedItem->GetFlag();
         const auto border = movedItem->GetBorder();
         const auto window = movedItem->GetWindow();

         if(msg.srcIndex < msg.dstIndex)
            window->MoveAfterInTabOrder(sizer->GetItem(msg.dstIndex)->GetWindow());
         else
            window->MoveBeforeInTabOrder(sizer->GetItem(msg.dstIndex)->GetWindow());

         sizer->Remove(msg.srcIndex);
         sizer->Insert(msg.dstIndex, window, proportion, flag, border);
      }
      else if(msg.type == RealtimeEffectListMessage::Type::Insert)
      {
         insertItem();
      }
      else if(msg.type == RealtimeEffectListMessage::Type::WillReplace)
      {
         removeItem();
      }
      else if(msg.type == RealtimeEffectListMessage::Type::DidReplace)
      {
         insertItem();
      }
      else if(msg.type == RealtimeEffectListMessage::Type::Remove)
      {
         removeItem();
      }
      */
      SendSizeEventToParent();
   }

   void Reset()
   {
      mProject = nullptr;
      mServerID = "";
      ReloadEffectsList();
      /*
      mTrack.reset();
      mProject = nullptr;
      ReloadEffectsList();
      */
   }

   void SetProject(AudacityProject& project, std::string serverID)
   {
      mProject = &project;
      mServerID = serverID;
      ReloadEffectsList();

      if (mStudioLink) {
         auto link = dynamic_cast<wxHyperlinkCtrl*>(mStudioLink);
         link->SetURL(kApiBaseUrl + "/studios/" + serverID);
      }
   }

   void EnableEffects(bool enable)
   {
      if (mTrack)
         RealtimeEffectList::Get(*mTrack).SetActive(enable);
   }

   void ReloadEffectsList()
   {
      wxWindowUpdateLocker freeze(this);

      const auto hadFocus = mParticipantListContainer->IsDescendant(FindFocus());
      //delete items that were added to the sizer
      mParticipantListContainer->Hide();
      mParticipantListContainer->GetSizer()->Clear(true);

      bool isEmpty = true;

      // TODO: This area could use a lock around mSubscriptionsMap access
      wxArrayString userIDs;
      for (auto& participant : mSubscriptionsMap->GetMap()) {
         userIDs.push_back(participant.first);
      }

      wxSortedArrayString sortedUserIDs(userIDs);
      for(size_t i = 0, count = sortedUserIDs.GetCount(); i < count; ++i) {
         auto uid = std::string(sortedUserIDs[i].mb_str());
         auto participant = mSubscriptionsMap->GetParticipantByID(std::string(sortedUserIDs[i].mb_str()));
         if (!participant) {
            break;
         }
         InsertParticipantRow(i, participant);
         if (mShowHiddenParticipants) {
            isEmpty = false;
         } else {
            auto device = participant->GetDeviceID();
            if (!device.empty()) {
               isEmpty = false;
            }
         }
      }

      mParticipantListContainer->Show(!isEmpty);
      mParticipantsHint->Show(isEmpty);
      mStudioLink->Show(isEmpty);

      SendSizeEventToParent();
   }

   void OnAddEffectClicked(const wxCommandEvent& event)
   {
      mSubscriptionsMap->Print();
      // if(!mTrack || mProject == nullptr)
      //    return;

      // const auto effectID = PickEffect(dynamic_cast<wxWindow*>(event.GetEventObject()), {});

      // if(!effectID || effectID->empty())
      //    return;

      // auto plug = PluginManager::Get().GetPlugin(*effectID);
      // if(!plug)
      //    return;

      // if(!PluginManager::IsPluginAvailable(*plug)) {
      //    BasicUI::ShowMessageBox(
      //       XO("This plugin could not be loaded.\nIt may have been deleted."),
      //       BasicUI::MessageBoxOptions()
      //          .Caption(XO("Plugin Error")));

      //    return;
      // }

      // if(auto state = AudioIO::Get()->AddState(*mProject, &*mTrack, *effectID))
      // {
      //    auto effect = state->GetEffect();
      //    assert(effect); // postcondition of AddState
      //    const auto effectName = effect->GetName();
      //    ProjectHistory::Get(*mProject).PushState(
      //       /*! i18n-hint: undo history record
      //        first parameter - realtime effect name
      //        second parameter - track name
      //        */
      //       XO("Added %s to %s").Format(effectName, mTrack->GetName()),
      //       //i18n-hint: undo history record
      //       XO("Add %s").Format(effectName));
      // }
   }

   void InsertParticipantRow(size_t index, const std::shared_ptr<StudioParticipant> p)
   {
      if (mProject == nullptr || mServerID.empty()) {
         std::cout << "shit" << std::endl;
         return;
      }

      // See comment in ReloadEffectsList
      if(!mParticipantListContainer->IsShown())
         mParticipantListContainer->Show();

      auto row = safenew ThemedWindowWrapper<ParticipantControl>(mParticipantListContainer, this, wxID_ANY);
      row->SetBackgroundColorIndex(clrEffectListItemBackground);
      row->SetParticipant(*mProject, p, mShowHiddenParticipants);
      mParticipantListContainer->GetSizer()->Insert(index, row, 0, wxEXPAND);
   }
};


struct VirtualStudioPanel::PrefsListenerHelper : PrefsListener
{
   AudacityProject& mProject;

   explicit PrefsListenerHelper(AudacityProject& project)
       : mProject { project }
   {}

   void UpdatePrefs() override
   {
      auto& trackList = TrackList::Get(mProject);
      for (auto waveTrack : trackList.Leaders<WaveTrack>())
         ReopenRealtimeEffectUIData(mProject, *waveTrack);
   }
};

namespace {
AttachedWindows::RegisteredFactory sKey{
[](AudacityProject &project) -> wxWeakRef<wxWindow> {
   constexpr auto PanelMinWidth { 255 };

   const auto pProjectWindow = &ProjectWindow::Get(project);
   auto vsPanel = safenew ThemedWindowWrapper<VirtualStudioPanel>(
      project, pProjectWindow->GetContainerWindow(), wxID_ANY);
   vsPanel->SetMinSize({PanelMinWidth, -1});
   vsPanel->SetName(_("Virtual Studio"));
   vsPanel->SetBackgroundColorIndex(clrMedium);
   vsPanel->Hide();//initially hidden
   return vsPanel;
}
};
}

StudioParticipant::StudioParticipant(wxWindow* parent, std::string id, std::string name, std::string picture)
{
   mParent = parent;
   mID = id;
   mName = name;
   mPicture = picture;
   mLeftVolume = 0;
   mRightVolume = 0;
   mDeviceID = "";
   mImage = theTheme.Image(bmpAnonymousUser);

   if (!picture.empty()) {
      FetchImage();
      //if (!mImage.LoadFile(picture, wxBITMAP_TYPE_ANY)) {
      //   std::cout << "failed to load file: " << picture << std::endl;
      //}
   }
}

StudioParticipant::~StudioParticipant()
{
}

std::string StudioParticipant::GetID()
{
   return mID;
}

std::string StudioParticipant::GetName()
{
   return mName;
}

std::string StudioParticipant::GetPicture()
{
   return mPicture;
}

wxImage StudioParticipant::GetImage()
{
   return mImage;
}

std::string StudioParticipant::GetDeviceID()
{
   return mDeviceID;
}

bool StudioParticipant::GetMute()
{
   return mMute;
}

float StudioParticipant::GetLeftVolume()
{
   return mLeftVolume;
}

float StudioParticipant::GetRightVolume()
{
   return mRightVolume;
}

float StudioParticipant::GetCaptureVolume()
{
   return mCaptureVolume/100.0f;
}

void StudioParticipant::UpdateMeter(float left, float right)
{
   if (mLeftVolume == left && mRightVolume == right) {
      return;
   }
   mLeftVolume = left;
   mRightVolume = right;
   QueueEvent({ ParticipantEvent::METER_CHANGE, mID });
}

void StudioParticipant::SetCaptureVolume(int volume)
{
   if (mCaptureVolume == volume) {
      return;
   }
   mCaptureVolume = volume;
   QueueEvent({ ParticipantEvent::VOLUME_CHANGE, mID });
}

bool StudioParticipant::SetDeviceID(std::string deviceID)
{
   if (mDeviceID == deviceID) {
      return false;
   }
   mDeviceID = deviceID;
   if (deviceID.empty()) {
      QueueEvent({ ParticipantEvent::HIDE, mID });
   } else {
      QueueEvent({ ParticipantEvent::SHOW, mID });
   }
   return true;
}

bool StudioParticipant::SetMute(bool mute)
{

   if (mMute == mute) {
      return false;
   }
   mMute = mute;
   QueueEvent({ ParticipantEvent::MUTE_CHANGE, mID });
   return true;
}

void StudioParticipant::SyncDeviceAPI()
{
   if (mDeviceID.empty()) {
      return;
   }
   auto vsp = dynamic_cast<VirtualStudioPanel*>(mParent);
   vsp->SyncDeviceAPI(mDeviceID, mMute, mCaptureVolume);
}

std::string StudioParticipant::GetDownloadLocalDir()
{
   auto tempDefaultLoc = TempDirectory::DefaultTempDir();
   return wxFileName(tempDefaultLoc, mID).GetFullPath().ToStdString();
}

void StudioParticipant::FetchImage()
{
   if (mPicture.empty() || mPicture.rfind("http") != 0) {
      return;
   }

   auto outputDir = GetDownloadLocalDir();
   if (!wxDirExists(outputDir)) {
      wxMkdir(outputDir);
   }

   // get last segment from URL and use this as the filename
   std::string imgName = mPicture.substr(0, mPicture.rfind("?"));
   imgName = imgName.substr(imgName.rfind("/")+1);

   mImageFile = wxFileName(outputDir, imgName).GetFullPath().ToStdString();

   // check if this image file exists
   std::ifstream f(mImageFile.c_str());
   if (f.good()) {
      LoadImage();
      return;
   }

   mDownloadOutput.open(mImageFile, std::ios::binary);
   wxLogInfo("Downloading to: %s", mImageFile);

   audacity::network_manager::Request request(mPicture);
   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);

   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("FetchImage HTTP code: %d", httpCode);

         if (mDownloadOutput.is_open()) {
            mDownloadOutput.close();
         }
         wxLogInfo("Download complete");
         LoadImage();
      }
   );

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

void StudioParticipant::LoadImage()
{
   if (mImageFile.empty()) {
      return;
   }

   if (!mImage.LoadFile(mImageFile, wxBITMAP_TYPE_ANY)) {
      std::cout << "Failed to load mImage from " << mImageFile << std::endl;
   }
   mImage = mImage.Rescale(24, 24);

   /* Supposedly we could do this but I can't get it to work: https://forums.wxwidgets.org/viewtopic.php?t=25202
   wxMemoryInputStream *memin = new wxMemoryInputStream(mMemBuf.GetData(), mMemBuf.GetDataLen());
   std::cout << "URL: " << mPicture << " is ok " << memin->IsOk() << " size " << memin->GetSize() << std::endl;

   wxImage *newImage = new wxImage();
   if (!newImage->LoadFile(*memin, wxBITMAP_TYPE_ANY)) {
      std::cout << "Failed to load newImage" << std::endl;
   }
   */
}

void StudioParticipant::QueueEvent(ParticipantEvent event)
{
   BasicUI::CallAfter([this, event = std::move(event)]{ this->Publish(event); });
}

StudioParticipantMap::StudioParticipantMap(wxWindow* parent)
{
   mParent = parent;
}

StudioParticipantMap::~StudioParticipantMap()
{
}

std::map<std::string, std::shared_ptr<StudioParticipant>> StudioParticipantMap::GetMap()
{
   return mMap;
}

std::shared_ptr<StudioParticipant> StudioParticipantMap::GetParticipantByID(std::string id)
{
   return mMap[id];
}

void StudioParticipantMap::AddParticipant(std::string id, std::string name, std::string picture)
{
   if (auto participant = mMap[id]) {
      return;
   }
   auto p = std::make_shared<StudioParticipant>(mParent, id, name, picture);
   mMap[id] = p;
   QueueEvent({ ParticipantEvent::REFRESH, id });
}

void StudioParticipantMap::UpdateParticipantMeter(std::string id, float left, float right)
{
   if (auto participant = mMap[id]) {
      participant->UpdateMeter(left, right);
   }
}

void StudioParticipantMap::UpdateParticipantDevice(std::string id, std::string device)
{
   if (auto participant = mMap[id]) {
      if (participant->SetDeviceID(device)) {
         QueueEvent({ ParticipantEvent::REFRESH, id });
      }
   }
}

void StudioParticipantMap::UpdateParticipantCaptureVolume(std::string id, int volume)
{
   if (auto participant = mMap[id]) {
      participant->SetCaptureVolume(volume);
   }
}

void StudioParticipantMap::UpdateParticipantMute(std::string id, bool mute)
{
   if (auto participant = mMap[id]) {
      participant->SetMute(mute);
   }
}


unsigned long StudioParticipantMap::GetParticipantsCount()
{
   return mMap.size();
}

void StudioParticipantMap::Clear()
{
   mMap.clear();
}

void StudioParticipantMap::QueueEvent(ParticipantEvent event)
{
   BasicUI::CallAfter([this, event = std::move(event)]{ this->Publish(event); });
   /*
   BasicUI::CallAfter( [wThis = weak_from_this(), event = std::move(event)]{
      if (auto pThis = wThis.lock())
         pThis->Publish(event);
   } );
   */
}

void StudioParticipantMap::Print()
{
   for (auto participant : mMap) {
      std::cout << participant.first << " " << participant.second->GetName() << std::endl;
   };
}

ConnectionMetadata::ConnectionMetadata(VirtualStudioPanel* panel, int id, ConnectionHdl hdl, const std::string &uri, WSSType type)
{
   mPanel = panel;
   mID = id;
   mHdl = hdl;
   mUri = uri;
   mType = type;
   mStatus = "Connecting";
}

void ConnectionMetadata::OnOpen(WSSClient* client, ConnectionHdl hdl)
{
   mStatus = "Open";
   WSSClient::connection_ptr con = client->get_con_from_hdl(hdl);

   std::cout << "Connection #" << mID << " opened to " << mUri << std::endl;
}

void ConnectionMetadata::OnFail(WSSClient* client, ConnectionHdl hdl)
{
   mStatus = "Failed";
   WSSClient::connection_ptr con = client->get_con_from_hdl(hdl);
   mErrorReason = con->get_ec().message();

   std::cout << "Connection #" << mID << " to " << mUri << " failed with: " << mErrorReason << std::endl;
}

void ConnectionMetadata::OnClose(WSSClient* client, ConnectionHdl hdl)
{
   mStatus = "Closed";
   WSSClient::connection_ptr con = client->get_con_from_hdl(hdl);
   auto code = con->get_remote_close_code();
   if (code != websocketpp::close::status::normal) {
      QueueEvent({ WebsocketEvent::RECONNECT });
   }

   std::stringstream s;
   s << "close code: " << con->get_remote_close_code() << " ("
      << websocketpp::close::status::get_string(con->get_remote_close_code())
      << "), close reason: " << con->get_remote_close_reason();
   mErrorReason = s.str();
   std::cout << "Connection #" << mID << " to " << mUri << " closed with: " << mErrorReason << std::endl;
}

void ConnectionMetadata::OnMessage(ConnectionHdl hdl, WSSClient::message_ptr msg)
{
   using namespace rapidjson;
   auto payload = msg->get_payload();

   rapidjson::Document document;
   document.Parse(payload.c_str());
   // Check for parse errors
   if (document.HasParseError()) {
      wxLogInfo("Error parsing JSON: %s", document.GetParseError());
      return;
   }

   if (mType == WSSType::SERVER) {
      HandleServerMessage(document);
   } else if (mType == WSSType::SUBSCRIPTIONS) {
      HandleSubscriptionsMessage(document);
   } else if (mType == WSSType::DEVICES) {
      HandleDevicesMessage(document);
   } else if (mType == WSSType::METERS) {
      HandleMetersMessage(document);
   } else {
      std::cout << "Unhandled message is " << payload << std::endl;
   }
}

void ConnectionMetadata::HandleServerMessage(const rapidjson::Document &document)
{
   // skip warning/alert messages passed on this socket
   if (document.HasMember("message")) {
      return;
   }
   mPanel->UpdateServerName(document["name"].GetString());
   mPanel->UpdateServerStatus(document["status"].GetString());
   std::string banner;
   if (document.HasMember("bannerURL")) {
      banner = document["bannerURL"].GetString();
   }
   mPanel->UpdateServerBanner(banner);
   mPanel->UpdateServerSessionID(document["sessionId"].GetString());
   mPanel->UpdateServerOwnerID(document["ownerId"].GetString());
   mPanel->UpdateServerSampleRate(document["sampleRate"].GetDouble());
   mPanel->UpdateServerBroadcast(document["broadcast"].GetInt());
   mPanel->UpdateServerEnabled(document["enabled"].GetBool());
}

void ConnectionMetadata::HandleSubscriptionsMessage(const rapidjson::Document &document)
{
   auto subMap = mPanel->GetSubscriptionsMap();
   auto userID = std::string(document["user_id"].GetString());
   auto name = std::string(document["nickname"].GetString());
   auto picture = std::string(document["picture"].GetString());
   subMap->AddParticipant(userID, name, picture);
}

void ConnectionMetadata::HandleDevicesMessage(const rapidjson::Document &document)
{
   auto subMap = mPanel->GetSubscriptionsMap();
   auto deviceToOwnerMap = mPanel->GetDeviceToOwnerMap();

   auto deviceID = std::string(document["id"].GetString());
   auto ownerID = std::string(document["ownerId"].GetString());
   auto serverID = std::string(document["serverId"].GetString());
   auto captureMute = document["captureMute"].GetBool();
   auto captureVolume = document["captureVolume"].GetInt();
   if (serverID.empty()) {
      deviceToOwnerMap->erase(deviceID);
   } else {
      deviceToOwnerMap->insert({deviceID, ownerID});
      subMap->UpdateParticipantMute(ownerID, captureMute);
      subMap->UpdateParticipantCaptureVolume(ownerID, captureVolume);
   }
}

void ConnectionMetadata::HandleMetersMessage(const rapidjson::Document &document)
{
   if (!document.HasMember("clients") || !document.HasMember("musicians")) {
      return;
   }
   if (!document["clients"].IsArray() || !document["musicians"].IsArray()) {
      return;
   }

   auto subMap = mPanel->GetSubscriptionsMap();
   auto deviceToOwnerMap = mPanel->GetDeviceToOwnerMap();

   std::map<std::string, std::string> usersToDevice;
   std::map<std::string, int> deviceToIndex;

   //std::cout << "Meters: " << payload << std::endl;
   auto musicians = document["musicians"].GetArray();
   int idx = 0;
   for (auto& v : document["clients"].GetArray()) {
      auto device = std::string(v.GetString());
      std::string ownerID = "";
      if (device != "Jamulus" && device != "supernova") {
         auto it = deviceToOwnerMap->find(device);
         if (it != deviceToOwnerMap->end()) {
            ownerID = it->second;
         }
      }

      // there could be both a webrtc connection and a device belonging to the same user; prioritize the device
      if (!ownerID.empty()) {
         auto val = usersToDevice[ownerID];
         if (val.empty() || VirtualStudioPanel::IsWebrtcDevice(val)) {
            usersToDevice[ownerID] = device;
            deviceToIndex[device] = idx;
         }
      }

      idx++;
   }

   for (auto& item : usersToDevice) {
      auto ownerID = item.first;
      auto device = item.second;
      auto musicianIdx = deviceToIndex[device];
      auto dbVals = musicians[musicianIdx].GetArray();
      auto leftDbVal = dbVals[0].GetDouble();
      auto rightDbVal = dbVals[1].GetDouble();
      float left = powf(10.0, leftDbVal/20.0);
      float right = powf(10.0, rightDbVal/20.0);
      subMap->UpdateParticipantMeter(ownerID, left, right);
      subMap->UpdateParticipantDevice(ownerID, device);
   }

   for (auto& participant : subMap->GetMap()) {
      auto device = usersToDevice[participant.first];
      if (device.empty()) {
         subMap->UpdateParticipantDevice(participant.first, "");
      }
   }
}

void ConnectionMetadata::QueueEvent(WebsocketEvent event)
{
   BasicUI::CallAfter([this, event = std::move(event)]{ this->Publish(event); });
}

WebsocketEndpoint::WebsocketEndpoint(VirtualStudioPanel* panel, const std::string &uri, WSSType type)
{
   mPanel = panel;
   mNextID = 0;
   mUri = uri;
   mType = type;
   DisableLogging();
   mClient.init_asio();
   mClient.start_perpetual();
   mClient.set_tls_init_handler(websocketpp::lib::bind(&WebsocketEndpoint::OnTlsInit));

   mThread = websocketpp::lib::make_shared<boost::thread>(&WSSClient::run, &mClient);
}

WebsocketEndpoint::~WebsocketEndpoint()
{
   mConnectionSubscription.Reset();
   mClient.stop_perpetual();

   for (con_list::const_iterator it = mConnectionList.begin(); it != mConnectionList.end(); ++it) {
      if (it->second->GetStatus() != "Open") {
         // Only close open connections
         continue;
      }

      auto connID = it->second->GetID();
      auto handle = it->second->GetHdl();
      std::cout << "> Closing connection " << connID << " from url " << mUri << std::endl;
      websocketpp::lib::error_code ec;
      mClient.close(handle, websocketpp::close::status::normal, "", ec);
      if (ec) {
         std::cout << "> Error closing connection " << connID << ": " << ec.message() << std::endl;
      }
   }

   mClient.stop();
   mThread->interrupt();
   mThread->join();
}

websocketpp::lib::shared_ptr<SslContext> WebsocketEndpoint::OnTlsInit()
{
   auto ctx = websocketpp::lib::make_shared<SslContext>(boost::asio::ssl::context::sslv23);
   return ctx;
};

void WebsocketEndpoint::DisableLogging()
{
   mClient.clear_access_channels(websocketpp::log::alevel::all);
   mClient.clear_error_channels(websocketpp::log::elevel::all);
}

void WebsocketEndpoint::SetReconnect(bool reconnect)
{
   if (mReconnect == reconnect) {
      return;
   }
   mReconnect = reconnect;
}

int WebsocketEndpoint::Connect()
{
   websocketpp::lib::error_code ec;
   WSSClient::connection_ptr con = mClient.get_connection(mUri, ec);
   if (ec) {
      std::cout << "> Connect initialization error: " << ec.message() << std::endl;
      return -1;
   }

   int new_id = mNextID++;
   ConnectionMetadata::ptr metadata_ptr = websocketpp::lib::make_shared<ConnectionMetadata>(mPanel, new_id, con->get_handle(), mUri, mType);
   mConnectionList[new_id] = metadata_ptr;

   mConnectionSubscription.Reset();
   mConnectionSubscription = metadata_ptr->Subscribe([this](const WebsocketEvent& evt) {
      switch (evt.mType)
      {
      case WebsocketEvent::RECONNECT:
         if (mReconnect) {
            Connect();
         }
         break;
      default:
         break;
      }
   });

   con->set_open_handler(websocketpp::lib::bind(
      &ConnectionMetadata::OnOpen,
      metadata_ptr,
      &mClient,
      websocketpp::lib::placeholders::_1
   ));
   con->set_fail_handler(websocketpp::lib::bind(
      &ConnectionMetadata::OnFail,
      metadata_ptr,
      &mClient,
      websocketpp::lib::placeholders::_1
   ));
   con->set_close_handler(websocketpp::lib::bind(
      &ConnectionMetadata::OnClose,
      metadata_ptr,
      &mClient,
      websocketpp::lib::placeholders::_1
   ));
   con->set_message_handler(websocketpp::lib::bind(
      &ConnectionMetadata::OnMessage,
      metadata_ptr,
      websocketpp::lib::placeholders::_1,
      websocketpp::lib::placeholders::_2
   ));

   con->append_header("Origin", "https://app.jacktrip.org");
   mClient.connect(con);
   return new_id;
}

void WebsocketEndpoint::Close(int id, websocketpp::close::status::value code, std::string reason)
{
   websocketpp::lib::error_code ec;

   con_list::iterator metadata_it = mConnectionList.find(id);
   if (metadata_it == mConnectionList.end()) {
      std::cout << "> No connection found with id " << id << std::endl;
      return;
   }

   mClient.close(metadata_it->second->GetHdl(), code, reason, ec);
   if (ec) {
      std::cout << "> Error initiating close: " << ec.message() << std::endl;
   }
}

const ConnectionMetadata::ptr WebsocketEndpoint::GetMetadata(int id)
{
   con_list::const_iterator metadata_it = mConnectionList.find(id);
   if (metadata_it == mConnectionList.end()) {
      return ConnectionMetadata::ptr();
   } else {
      return metadata_it->second;
   }
}

bool VirtualStudioPanel::IsWebrtcDevice(const std::string &device) {
   return device.rfind("rtc-") == 0;
}

VirtualStudioPanel &VirtualStudioPanel::Get(AudacityProject &project)
{
   return GetAttachedWindows(project).Get<VirtualStudioPanel>(sKey);
}

const VirtualStudioPanel &
VirtualStudioPanel::Get(const AudacityProject &project)
{
   return Get(const_cast<AudacityProject &>(project));
}

VirtualStudioPanel::VirtualStudioPanel(
   AudacityProject& project, wxWindow* parent, wxWindowID id, const wxPoint& pos,
   const wxSize& size,
   long style, const wxString& name)
      : wxPanel(parent, id, pos, size, style, name)
      , mProject(project)
      , mPrefsListenerHelper(std::make_unique<PrefsListenerHelper>(project))
{
   mDeviceToOwnerMap.clear();
   mWebrtcUsers.clear();
   mSubscriptionsMap = safenew StudioParticipantMap(this);

   auto vSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);

   auto header = safenew ThemedWindowWrapper<ListNavigationPanel>(this, wxID_ANY);
#if wxUSE_ACCESSIBILITY
   safenew WindowAccessible(header);
#endif
   header->SetBackgroundColorIndex(clrMedium);
   {
      auto hSizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
      auto enabled = safenew ThemedAButtonWrapper<AButton>(header);
      enabled->SetImageIndices(0, bmpEffectOff, bmpEffectOff, bmpEffectOn, bmpEffectOn, bmpEffectOff);
      enabled->SetButtonToggles(true);
      enabled->Disable();
      enabled->SetTranslatableLabel(XO("Online"));
      enabled->SetBackgroundColorIndex(clrMedium);
      mStudioOnline = enabled;

      hSizer->Add(enabled, 0, wxSTRETCH_NOT | wxALIGN_CENTER | wxLEFT, 5);
      {
         auto vSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);

         auto headerText = safenew ThemedWindowWrapper<wxStaticText>(header, wxID_ANY, wxEmptyString);
         headerText->SetFont(wxFont(wxFontInfo().Bold()));
         headerText->SetTranslatableLabel(XO("Virtual Studio"));
         headerText->SetForegroundColorIndex(clrTrackPanelText);

         auto serverName = safenew ThemedWindowWrapper<wxStaticText>(header, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
         serverName->SetForegroundColorIndex(clrTrackPanelText);
         mStudioTitle = serverName;

         auto serverStatus = safenew ThemedWindowWrapper<wxStaticText>(header, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
         serverStatus->SetForegroundColorIndex(clrTrackPanelText);
         mStudioStatus = serverStatus;

         vSizer->Add(headerText);
         vSizer->Add(serverName);
         vSizer->Add(mStudioStatus);

         hSizer->Add(vSizer.release(), 1, wxEXPAND | wxALL, 10);
      }
      auto close = safenew ThemedAButtonWrapper<AButton>(header);
      close->SetTranslatableLabel(XO("Close"));
      close->SetImageIndices(0, bmpCloseNormal, bmpCloseHover, bmpCloseDown, bmpCloseHover, bmpCloseDisabled);
      close->SetBackgroundColorIndex(clrMedium);

      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DoClose(); });

      hSizer->Add(close, 0, wxSTRETCH_NOT | wxALIGN_CENTER | wxRIGHT, 5);

      header->SetSizer(hSizer.release());
   }
   vSizer->Add(header, 0, wxEXPAND);

   auto actions = safenew ThemedWindowWrapper<ListNavigationPanel>(this, wxID_ANY);
#if wxUSE_ACCESSIBILITY
   safenew WindowAccessible(header);
#endif
   actions->SetBackgroundColorIndex(clrMedium);
   {
      auto shSizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
      auto joinButton = safenew ThemedAButtonWrapper<AButton>(actions, wxID_ANY);
      joinButton->SetImageIndices(0,
            bmpHButtonNormal,
            bmpHButtonHover,
            bmpHButtonDown,
            bmpHButtonHover,
            bmpHButtonDisabled);
      joinButton->SetTranslatableLabel(XO("Join Studio"));
      joinButton->SetButtonType(AButton::TextButton);
      joinButton->SetBackgroundColorIndex(clrMedium);
      joinButton->SetForegroundColorIndex(clrTrackPanelText);
      joinButton->Bind(wxEVT_BUTTON, &VirtualStudioPanel::OnJoin, this);
      mJoinStudio = joinButton;

      shSizer->Add(joinButton, 1, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 4);
      actions->SetSizer(shSizer.release());
   }
   vSizer->Add(actions, 0, wxEXPAND);
   vSizer->Add(2, 4);

   auto participantList = safenew ThemedWindowWrapper<VirtualStudioParticipantListWindow>(this, wxID_ANY);
   participantList->SetBackgroundColorIndex(clrMedium);
   vSizer->Add(participantList, 1, wxEXPAND);

   mHeader = header;
   mActions = actions;
   mParticipantsList = participantList;

   SetSizerAndFit(vSizer.release());

   Bind(wxEVT_CHAR_HOOK, &VirtualStudioPanel::OnCharHook, this);

   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
      HidePanel(); });
}

VirtualStudioPanel::~VirtualStudioPanel()
{
   ResetStudio();
}

void VirtualStudioPanel::UpdateServerName(std::string name)
{
   if (mServerName == name) {
      return;
   }
   mServerName = name;
   mStudioTitle->SetLabel(name);
   mHeader->SetName(wxString::Format(_("JackTrip Virtual Studio: %s"), name));
}

void VirtualStudioPanel::UpdateServerStatus(std::string status)
{
   if (mServerStatus == status) {
      return;
   }
   mServerStatus = status;
   mStudioStatus->SetLabel(status);
   if (status == "Ready") {
      InitMetersWebsocket();
      InitActiveParticipants();
   } else {
      StopMetersWebsocket();
      StopActiveParticipants();
   }
}

void VirtualStudioPanel::UpdateServerBanner(std::string banner)
{
   if (mServerBanner == banner) {
      return;
   }
   mServerBanner = banner;
}

void VirtualStudioPanel::UpdateServerAdmin(bool admin)
{
   if (mServerAdmin == admin) {
      return;
   }
   mServerAdmin = admin;
}

void VirtualStudioPanel::UpdateServerSessionID(std::string sessionID)
{
   if (mServerSessionID == sessionID) {
      return;
   }
   mServerSessionID = sessionID;
   if (!sessionID.empty()) {
      InitMetersWebsocket();
      InitActiveParticipants();
   } else {
      StopMetersWebsocket();
      StopActiveParticipants();
   }
}

void VirtualStudioPanel::UpdateServerOwnerID(std::string ownerID)
{
   if (mServerOwnerID == ownerID) {
      return;
   }
   mServerOwnerID = ownerID;
   if (!ownerID.empty()) {
      FetchOwner(ownerID);
   }
}

void VirtualStudioPanel::UpdateServerEnabled(bool enabled) {
   if (mServerEnabled == enabled) {
      return;
   }
   mServerEnabled = enabled;
   if (enabled) {
      InitMetersWebsocket();
      InitActiveParticipants();
      mStudioOnline->PushDown();
   } else {
      StopMetersWebsocket();
      StopActiveParticipants();
      mStudioOnline->PopUp();

   }
}

void VirtualStudioPanel::UpdateServerSampleRate(double sampleRate) {
   if (mServerSampleRate == sampleRate) {
      return;
   }
   mServerSampleRate = sampleRate;
}

void VirtualStudioPanel::UpdateServerBroadcast(int broadcast) {
   if (mServerBroadcast == broadcast) {
      return;
   }
   mServerBroadcast = broadcast;
}

void VirtualStudioPanel::SyncDeviceAPI(std::string deviceID, bool mute, int captureVolume) {
   if (mServerID.empty() || mAccessToken.empty() || deviceID.empty()) {
      return;
   }
   std::cout << "Device: " << deviceID << std::endl;
   if (VirtualStudioPanel::IsWebrtcDevice(deviceID)) {
      return;
   }

   // generate payload
   rapidjson::Document document;
   document.SetObject();

   wxString wxServerID(mServerID);
   document.AddMember(
      "serverId",
      rapidjson::Value(wxServerID.data(), wxServerID.length(), document.GetAllocator()),
      document.GetAllocator());
   document.AddMember("captureMute", rapidjson::Value(mute), document.GetAllocator());
   document.AddMember("captureVolume", rapidjson::Value(captureVolume), document.GetAllocator());

   rapidjson::StringBuffer buffer;
   rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
   document.Accept(writer);
   auto payload = std::string(buffer.GetString());

   const auto url = kApiBaseUrl + "/api/devices/" + deviceID;
   audacity::network_manager::Request request(url);
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doPut(request, payload.data(), payload.size());
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("SyncDeviceAPI HTTP code: %d", httpCode);

         if (httpCode != 200)
            return;
      }
   );
}

void VirtualStudioPanel::ShowPanel(std::string serverID, std::string accessToken, bool focus)
{
   if (serverID.empty() || accessToken.empty()) {
      ResetStudio();
      return;
   }

   wxWindowUpdateLocker freeze(this);

   mServerID = serverID;
   mAccessToken = accessToken;
   SetStudio(mServerID, mAccessToken);

   auto &projectWindow = ProjectWindow::Get(mProject);
   const auto pContainerWindow = projectWindow.GetContainerWindow();
   if (pContainerWindow->GetWindow1() != this)
   {
      //Restore previous effects window size
      pContainerWindow->SplitVertically(
         this,
         projectWindow.GetTrackListWindow(),
         this->GetSize().GetWidth());
   }
   if(focus)
      SetFocus();
   projectWindow.Layout();
}

void VirtualStudioPanel::HidePanel()
{
   wxWindowUpdateLocker freeze(this);

   auto &projectWindow = ProjectWindow::Get(mProject);
   const auto pContainerWindow = projectWindow.GetContainerWindow();
   const auto pTrackListWindow = projectWindow.GetTrackListWindow();
   if (pContainerWindow->GetWindow2() == nullptr)
      //only effects panel is present, restore split positions before removing effects panel
      //Workaround: ::Replace and ::Initialize do not work here...
      pContainerWindow->SplitVertically(this, pTrackListWindow);

   pContainerWindow->Unsplit(this);
   pTrackListWindow->SetFocus();
   projectWindow.Layout();
}

void VirtualStudioPanel::Disconnect(std::shared_ptr<WebsocketEndpoint>& endpoint)
{
   if (endpoint) {
      endpoint->SetReconnect(false);
   }
   endpoint.reset();
}

void VirtualStudioPanel::InitializeWebsockets()
{
   InitServerWebsocket();
   InitSubscriptionsWebsocket();
   InitDevicesWebsocket();
   InitMetersWebsocket();
   InitActiveParticipants();
}

void VirtualStudioPanel::StopWebsockets()
{
   StopMetersWebsocket();
   StopActiveParticipants();
   Disconnect(mServerClient);
   Disconnect(mDevicesClient);
   Disconnect(mSubscriptionsClient);
}

void VirtualStudioPanel::InitServerWebsocket()
{
   if (mServerClient || mServerID.empty() || mAccessToken.empty()) {
      return;
   }
   std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "?auth_code=" + mAccessToken;
   mServerClient.reset(new WebsocketEndpoint(this, url, WSSType::SERVER));
   mServerClient->SetReconnect(true);
   auto id = mServerClient->Connect();
   if (id == -1) {
      std::cout << "Failed starting mServerClient" << std::endl;
   }
}

void VirtualStudioPanel::InitSubscriptionsWebsocket()
{
   if (mSubscriptionsClient || mServerID.empty() || mAccessToken.empty()) {
      return;
   }
   std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "/subscriptions?auth_code=" + mAccessToken;
   mSubscriptionsClient.reset(new WebsocketEndpoint(this, url, WSSType::SUBSCRIPTIONS));
   mSubscriptionsClient->SetReconnect(true);
   auto id = mSubscriptionsClient->Connect();
   if (id == -1) {
      std::cout << "Failed starting mSubscriptionsClient" << std::endl;
   }
}

void VirtualStudioPanel::InitDevicesWebsocket()
{
   if (mDevicesClient || mServerID.empty() || mAccessToken.empty()) {
      return;
   }
   std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "/devices?auth_code=" + mAccessToken;
   mDevicesClient.reset(new WebsocketEndpoint(this, url, WSSType::DEVICES));
   mDevicesClient->SetReconnect(true);
   auto id = mDevicesClient->Connect();
   if (id == -1) {
      std::cout << "Failed starting mDevicesClient" << std::endl;
   }
}

void VirtualStudioPanel::InitActiveParticipants()
{
   if (!mServerEnabled || mServerStatus != "Ready" || mServerID.empty() || mServerSessionID.empty()) {
      return;
   }
   if (mActiveParticipantsThread) {
      return;
   }
   mActiveParticipantsThread.reset(new boost::thread([&]
      {
         while (mServerEnabled && mServerStatus == "Ready" && !mServerID.empty() && !mAccessToken.empty()) {
            FetchActiveServerParticipants();
            std::this_thread::sleep_for(std::chrono::seconds(3));
         }
      }
   ));
   mActiveParticipantsThread->detach();
}

void VirtualStudioPanel::StopActiveParticipants()
{
   if (mActiveParticipantsThread) {
      mActiveParticipantsThread->interrupt();
      mActiveParticipantsThread->join();
   }
   mActiveParticipantsThread.reset();
}

void VirtualStudioPanel::InitMetersWebsocket()
{
   if (!mServerEnabled || mServerStatus != "Ready" || mServerID.empty() || mServerSessionID.empty()) {
      return;
   }
   if (mMetersClient) {
      return;
   }
   std::string secret = sha256("jktp-" + mServerID + "-" + mServerSessionID);
   std::string url = "wss://" + mServerSessionID + ".jacktrip.cloud/meters?auth_code=" + secret;
   mMetersClient.reset(new WebsocketEndpoint(this, url, WSSType::METERS));
   mMetersClient->SetReconnect(true);
   auto id = mMetersClient->Connect();
   if (id == -1) {
      std::cout << "Failed starting mMetersClient" << std::endl;
   }
}

void VirtualStudioPanel::StopMetersWebsocket()
{
   Disconnect(mMetersClient);
}

void VirtualStudioPanel::FetchOwner(std::string ownerID)
{
   audacity::network_manager::Request request(kApiBaseUrl + "/api/users/" + ownerID);
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("FetchOwner HTTP code: %d", httpCode);

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

         auto userID = std::string(document["user_id"].GetString());
         auto name = std::string(document["nickname"].GetString());
         if (document["user_metadata"].IsObject()) {
            if (document["user_metadata"].HasMember("display_name")) {
               auto displayName = std::string(document["user_metadata"]["display_name"].GetString());
               if (!displayName.empty()) {
                  name = displayName;
               }
            }
         }
         auto picture = std::string(document["picture"].GetString());
         mSubscriptionsMap->AddParticipant(userID, name, picture);
      }
   );
}

void VirtualStudioPanel::FetchServer()
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

         UpdateServerAdmin(document["admin"].GetBool());
      }
   );
}

void VirtualStudioPanel::FetchActiveServerParticipants()
{
   if (!mServerEnabled || mServerStatus != "Ready" || mServerID.empty() || mAccessToken.empty()) {
      return;
   }

   audacity::network_manager::Request request(kApiBaseUrl + "/api/servers/" + mServerID + "/participants");
   request.setHeader("Authorization", "Bearer " + mAccessToken);
   request.setHeader("Content-Type", "application/json");
   request.setHeader("Accept", "application/json");

   auto response = audacity::network_manager::NetworkManager::GetInstance().doGet(request);
   response->setRequestFinishedCallback(
      [response, this](auto)
      {
         const auto httpCode = response->getHTTPCode();
         wxLogInfo("FetchActiveServerParticipants HTTP code: %d", httpCode);

         if (httpCode != 200)
            return;

         const auto body = response->readAll<std::string>();
         //std::cout << "Body: " << body << std::endl;

         using namespace rapidjson;
         Document document;
         document.Parse(body.data(), body.size());

         // Check for parse errors
         if (document.HasParseError()) {
            wxLogInfo("Error parsing JSON: %s", document.GetParseError());
            return;
         }

         std::map<std::string, bool> activeParticipants;

         // Iterate over the array of objects
         Value::ConstValueIterator itr;
         for (itr = document.Begin(); itr != document.End(); ++itr) {
            auto uid = std::string(itr->GetObject()["user_id"].GetString());
            //std::cout << "Found " << uid << std::endl;
            if (!uid.empty()) {
               activeParticipants[uid] = true;
            }
         }

         // TODO: Would be good to use locks around mDeviceToOwnerMap here too...
         std::map<std::string, bool> toRemove;
         for (auto& currItem : mDeviceToOwnerMap) {
            auto device = currItem.first;
            if (VirtualStudioPanel::IsWebrtcDevice(device)) {
               auto userID = device.substr(4);
               if (!activeParticipants[userID]) {
                  //std::cout << "Adding " << device << " to remove list" << std::endl;
                  toRemove[device] = true;
               }
            }
         }

         // do removal
         //std::cout << "toRemove size " << toRemove.size() << std::endl;
         for (auto& removeItem : toRemove) {
            if (removeItem.second) {
               auto device = removeItem.first;
               auto userID = device.substr(4);
               //std::cout << "Removing " << device << std::endl;
               mDeviceToOwnerMap.erase(device);
               mSubscriptionsMap->UpdateParticipantDevice(userID, "");
            }
         }

         // do addition
         //std::cout << "activeParticipants size " << activeParticipants.size() << std::endl;
         for (auto& newItem : activeParticipants) {
            if (newItem.second) {
               //std::cout << "Adding " << newItem.first << std::endl;
               mDeviceToOwnerMap["rtc-" + newItem.first] = newItem.first;
            }
         }
      }
   );
}

void VirtualStudioPanel::DoClose()
{
   ResetStudio();
   mDeviceToOwnerMap.clear();
   mWebrtcUsers.clear();
   Close();
}

StudioParticipantMap* VirtualStudioPanel::GetSubscriptionsMap()
{
   return mSubscriptionsMap;
}

std::map<std::string, std::string>* VirtualStudioPanel::GetDeviceToOwnerMap()
{
   return &mDeviceToOwnerMap;
}

void VirtualStudioPanel::OnJoin(const wxCommandEvent& event)
{
   if (mServerID.empty()) {
      return;
   }
   auto url = "jacktrip://join/" + mServerID;
   BasicUI::OpenInDefaultBrowser(url);
}

void VirtualStudioPanel::SetStudio(std::string serverID, std::string accessToken)
{
   if (serverID.empty() || accessToken.empty()) {
      ResetStudio();
   } else {
      FetchServer();
      InitializeWebsockets();
      mParticipantsList->SetProject(mProject, mServerID);
   }
}

void VirtualStudioPanel::ResetStudio()
{
   std::cout << "Stopping websocket in ResetStudio" << std::endl;
   StopWebsockets();
   std::cout << "Stopped websocket in ResetStudio" << std::endl;
   UpdateServerName("");
   UpdateServerStatus("Disabled");
   UpdateServerEnabled(false);
   UpdateServerOwnerID("");
   UpdateServerSampleRate(0);
   UpdateServerBroadcast(0);
   UpdateServerAdmin(false);
   mSubscriptionsMap->Clear();
   mParticipantsList->Reset();
}

void VirtualStudioPanel::SetFocus()
{
   mHeader->SetFocus();
}

void VirtualStudioPanel::OnCharHook(wxKeyEvent& evt)
{
   if(evt.GetKeyCode() == WXK_ESCAPE && IsShown() && IsDescendant(FindFocus()))
      DoClose();
   else
      evt.Skip();
}
