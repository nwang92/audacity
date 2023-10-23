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
#include "ListNavigationEnabled.h"
#include "ListNavigationPanel.h"
#include "MovableControl.h"
#include "menus/MenuHelper.h"
#include "Menus.h"
#include "prefs/EffectsPrefs.h"

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

   //UI control that represents individual effect from the effect list
   class ParticipantControl : public ListNavigationEnabled<MovableControl>
   {
      wxWeakRef<AudacityProject> mProject;
      std::shared_ptr<StudioParticipant> mParticipant;
      std::shared_ptr<EffectSettingsAccess> mSettingsAccess;

      RealtimeEffectPicker* mEffectPicker { nullptr };

      ThemedAButtonWrapper<AButton>* mChangeButton{nullptr};
      AButton* mEnableButton{nullptr};
      ThemedAButtonWrapper<AButton>* mOptionsButton{};
      wxStaticText* mParticipantName {nullptr};
      MeterPanel *mMeter{nullptr};

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
         mEnableButton = enableButton;

         enableButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            std::cout << "enableButton clicked" << std::endl;
         });

         auto topSizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
         auto participantName = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
         participantName->SetForegroundColorIndex(clrTrackPanelText);
         mParticipantName = participantName;
         topSizer->Add(mParticipantName, 1, wxLEFT | wxTOP | wxCENTER, 4);

         auto bottomSizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
         auto bottomText = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
         bottomText->SetForegroundColorIndex(clrTrackPanelText);
         bottomText->SetLabel("Testing testing testing");
         bottomSizer->Add(bottomText, 1, wxLEFT | wxBOTTOM, 4);

         auto controlsSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
         controlsSizer->Add(topSizer.release(), 1, wxLEFT | wxTOP, 4);
         controlsSizer->Add(bottomSizer.release(), 1, wxLEFT | wxBOTTOM, 4);
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

         auto meter = safenew MeterPanel( mProject, this, wxID_ANY, false, wxDefaultPosition, wxSize( 20, 56 ), MeterPanel::Style::MixerTrackCluster, 0.1 );
         meter->Reset(48000, true);
         mMeter = meter;
         /*
         auto dragArea = safenew wxStaticBitmap(this, wxID_ANY, theTheme.Bitmap(bmpDragArea));
         dragArea->Disable();
         sizer->Add(dragArea, 0, wxLEFT | wxCENTER, 5);
         */
         sizer->Add(enableButton, 0, wxLEFT | wxCENTER, 5);
         sizer->Add(controlsSizer.release(), 1, wxLEFT | wxCENTER, 5);
         //sizer->Add(optionsButton, 1, wxLEFT | wxCENTER, 5);
         //sizer->Add(changeButton, 0, wxLEFT | wxRIGHT | wxCENTER, 5);
         sizer->Add(meter, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxCENTER, 0);
         //mChangeButton = changeButton;
         //mOptionsButton = optionsButton;
         mMeter = meter;

         auto vSizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
         vSizer->Add(sizer.release(), 0, wxUP | wxDOWN | wxEXPAND, 4);

         SetSizer(vSizer.release());

#if wxUSE_ACCESSIBILITY
         SetAccessible(safenew ParticipantControlAx(this));
#endif
      }

      static const PluginDescriptor *GetPlugin(const PluginID &ID) {
         auto desc = PluginManager::Get().GetPlugin(ID);
         return desc;
      }

      std::string GetEffectName() const
      {
         return mParticipant->GetName();
         /*
         const auto &ID = mEffectState->GetID();
         const auto desc = GetPlugin(ID);
         return desc
            ? desc->GetSymbol().Msgid()
            : XO("%s (missing)")
               .Format(PluginManager::GetEffectNameFromID(ID).GET());
         */
      }

      void SetParticipant(AudacityProject& project,
         const std::shared_ptr<StudioParticipant>& participant)
      {
         mProject = &project;
         mParticipant = participant;

         mParticipantSubscription = mParticipant->Subscribe([this](ParticipantEvent evt) {
            if (evt.mType == ParticipantEvent::VOLUME_CHANGE) {
               if (mMeter) {
                  auto val = mParticipant->GetVolume();
                  std::cout << "helloooooo " << val << std::endl;
                  mMeter->SetDB(2, 2, val);
               }
            }
         });

         std::string label;
         if (participant != nullptr) {
            label = GetEffectName();
            if (mParticipantName) {
               mParticipantName->SetLabel(label);
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
   std::shared_ptr<SampleTrack> mTrack;
   AButton* mAddEffect{nullptr};
   wxStaticText* mParticipantsHint{nullptr};
   wxWindow* mStudioLink{nullptr};
   wxWindow* mParticipantListContainer{nullptr};
   StudioParticipantMap* mSubscriptionsMap{nullptr};

   std::unique_ptr<MenuTable::MenuItem> mEffectMenuRoot;

   Observer::Subscription mEffectListItemMovedSubscription;
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
      Bind(wxEVT_SIZE, &VirtualStudioParticipantListWindow::OnSizeChanged, this);
#ifdef __WXMSW__
      //Fixes flickering on redraw
      wxScrolledWindow::SetDoubleBuffered(true);
#endif
      auto vsp = dynamic_cast<VirtualStudioPanel*>(parent);
      mSubscriptionsMap = vsp->GetSubscriptionsMap();
      mParticipantChangeSubscription = mSubscriptionsMap->Subscribe(
         [this](ParticipantEvent)
         {
            std::cout << "Participant list changed" << std::endl;
            ReloadEffectsList();
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

      auto participantsHint = safenew ThemedWindowWrapper<wxStaticText>(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
      //Workaround: text is set in the OnSizeChange
      participantsHint->SetForegroundColorIndex(clrTrackPanelText);
      mParticipantsHint = participantsHint;

      //std::string url = kApiBaseUrl + "/studios";
      //wxString wxUrl(url);
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
      rootSizer->Add(addEffect, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 20);
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
         std::cout << "From: " << from << std::endl;
         std::cout << "To: " << to << std::endl;

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
         mParticipantsHint->SetLabel(_("Join this studio to manage participants."));
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
      mParticipantChangeSubscription.Reset();
      mProject = nullptr;
      ReloadEffectsList();
      /*
      mEffectListItemMovedSubscription.Reset();

      mTrack.reset();
      mProject = nullptr;
      ReloadEffectsList();
      */
   }

   void SetProject(AudacityProject& project)
   {
      mEffectListItemMovedSubscription.Reset();
      mProject = &project;
      ReloadEffectsList();
      /*
      if (track)
      {
         auto& effects = RealtimeEffectList::Get(*mTrack);
         mEffectListItemMovedSubscription = effects.Subscribe(
            *this, &VirtualStudioParticipantListWindow::OnEffectListItemChange);

         UpdateRealtimeEffectUIData(*track);
      }
      */
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

      auto isEmpty = mSubscriptionsMap->GetParticipantsCount() == 0;
      if (isEmpty) {
         mParticipantListContainer->Hide();
         return;
      }

      wxArrayString userIDs;
      for (auto& participant : mSubscriptionsMap->GetMap()) {
         userIDs.push_back(participant.first);
      }

      wxSortedArrayString sortedUserIDs(userIDs);
      for(size_t i = 0, count = sortedUserIDs.GetCount(); i < count; ++i) {
         auto uid = std::string(sortedUserIDs[i].mb_str());
         std::cout << "User ID: " << uid << std::endl;
         auto participant = mSubscriptionsMap->GetParticipantByID(std::string(sortedUserIDs[i].mb_str()));
         participant->SetIndex(i);
         InsertParticipantRow(i, participant);
      }

      /*
      auto isEmpty{true};
      if(mTrack)
      {
         auto& effects = RealtimeEffectList::Get(*mTrack);
         isEmpty = effects.GetStatesCount() == 0;
         for(size_t i = 0, count = effects.GetStatesCount(); i < count; ++i)
            InsertEffectRow(i, effects.GetStateAt(i));
      }
      */
      mAddEffect->SetEnabled(true);
      //Workaround for GTK: Underlying GTK widget does not update
      //its size when wxWindow size is set to zero
      mParticipantListContainer->Show(true);
      mParticipantsHint->Show(false);
      mStudioLink->Show(false);

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
      if (mProject == nullptr) {
         return;
      }

      // See comment in ReloadEffectsList
      if(!mParticipantListContainer->IsShown())
         mParticipantListContainer->Show();

      auto row = safenew ThemedWindowWrapper<ParticipantControl>(mParticipantListContainer, this, wxID_ANY);
      row->SetBackgroundColorIndex(clrEffectListItemBackground);
      row->SetParticipant(*mProject, p);
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

StudioParticipant::StudioParticipant(wxWindow* parent, std::string id, std::string name, std::string picture, float volume)
{
   mParent = parent;
   mID = id;
   mName = name;
   mPicture = picture;
   mVolume = volume;
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

float StudioParticipant::GetVolume()
{
   return mVolume;
}

void StudioParticipant::UpdateVolume(float volume)
{
   if (mVolume == volume) {
      return;
   }
   mVolume = volume;
   QueueEvent({ ParticipantEvent::VOLUME_CHANGE, 0 });
}

void StudioParticipant::SetIndex(int idx)
{
   if (mIndex == idx) {
      return;
   }
   mIndex = idx;
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

void StudioParticipantMap::AddParticipant(std::string id, std::string name, std::string picture, float volume)
{
   auto p = std::make_shared<StudioParticipant>(mParent, id, name, picture, volume);
   mMap[id] = p;
   QueueEvent({ ParticipantEvent::ADDITION, 0 });
}

void StudioParticipantMap::UpdateParticipantVolume(std::string id, float volume)
{
   if (auto participant = mMap[id]) {
      participant->UpdateVolume(volume);
      QueueEvent({ ParticipantEvent::SELECTION_CHANGE, 0 });
   }
}

unsigned long StudioParticipantMap::GetParticipantsCount()
{
   return mMap.size();
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
      std::cout << participant.first << " " << participant.second->GetName() << " " << participant.second->GetVolume() << std::endl;
   };
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
   mOwnerToDeviceMap.clear();
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
   mTrackListChanged = TrackList::Get(mProject).Subscribe([this](const TrackListEvent& evt) {
         auto track = evt.mpTrack.lock();
         auto waveTrack = std::dynamic_pointer_cast<WaveTrack>(track);

         if (waveTrack == nullptr)
            return;

         switch (evt.mType)
         {
         case TrackListEvent::TRACK_DATA_CHANGE:
            if (mCurrentTrack.lock() == waveTrack)
               mStudioTitle->SetLabel(track->GetName());
            UpdateRealtimeEffectUIData(*waveTrack);
            break;
         case TrackListEvent::DELETION:
            if (evt.mExtra == 0)
               mPotentiallyRemovedTracks.push_back(waveTrack);
            break;
         case TrackListEvent::ADDITION:
            // Addition can be fired as a part of "replace" event.
            // Calling UpdateRealtimeEffectUIData is mostly no-op,
            // it will just create a new State and Access for it.
            UpdateRealtimeEffectUIData(*waveTrack);
            break;
         default:
            break;
         }
   });

   mUndoSubscription = UndoManager::Get(mProject).Subscribe(
      [this](UndoRedoMessage message)
      {
         if (
            message.type == UndoRedoMessage::Type::Purge ||
            message.type == UndoRedoMessage::Type::BeginPurge ||
            message.type == UndoRedoMessage::Type::EndPurge)
            return;

         auto& trackList = TrackList::Get(mProject);

         // Realtime effect UI is only updated on Undo or Redo
         auto waveTracks = trackList.Leaders<WaveTrack>();

         if (
            message.type == UndoRedoMessage::Type::UndoOrRedo ||
            message.type == UndoRedoMessage::Type::Reset)
         {
            for (auto waveTrack : waveTracks)
               UpdateRealtimeEffectUIData(*waveTrack);
         }

         // But mPotentiallyRemovedTracks processing happens as fast as possible.
         // This event is fired right after the track is deleted, so we do not
         // hold the strong reference to the track much longer than need.
         if (mPotentiallyRemovedTracks.empty())
            return;

         // Collect RealtimeEffectUIs that are currently shown
         // for the potentially removed tracks
         std::vector<RealtimeEffectStateUI*> shownUIs;

         for (auto track : mPotentiallyRemovedTracks)
         {
            // By construction, track cannot be null
            assert(track != nullptr);

            VisitRealtimeEffectStateUIs(
               *track,
               [&shownUIs](auto& ui)
               {
                  if (ui.IsShown())
                     shownUIs.push_back(&ui);
               });
         }

         // For every UI shown - check if the corresponding state
         // is reachable from the current track list.
         for (auto effectUI : shownUIs)
         {
            bool reachable = false;

            for (auto track : waveTracks)
            {
               VisitRealtimeEffectStateUIs(
                  *track,
                  [effectUI, &reachable](auto& ui)
                  {
                     if (effectUI == &ui)
                        reachable = true;
                  });

               if (reachable)
                  break;
            }

            if (!reachable)
               // Don't need to autosave for an unreachable state
               effectUI->Hide();
         }

         mPotentiallyRemovedTracks.clear();
      });

   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
      HidePanel(); });
}

VirtualStudioPanel::~VirtualStudioPanel()
{
}

void VirtualStudioPanel::UpdateServerName(std::string name)
{
   if (mServerName == name) {
      return;
   }
   mServerName = name;
   mStudioTitle->SetLabel(name);
   mHeader->SetName(wxString::Format(_("Realtime effects for %s"), name));
   std::cout << mServerName << std::endl;
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
   } else {
      StopMetersWebsocket();
   }
   std::cout << mServerStatus << std::endl;
}

void VirtualStudioPanel::UpdateServerBanner(std::string banner)
{
   if (mServerBanner == banner) {
      return;
   }
   mServerBanner = banner;
   std::cout << mServerBanner << std::endl;
}

void VirtualStudioPanel::UpdateServerSessionID(std::string sessionID)
{
   if (mServerSessionID == sessionID) {
      return;
   }
   mServerSessionID = sessionID;
   if (!sessionID.empty()) {
      InitMetersWebsocket();
   } else {
      StopMetersWebsocket();
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
      mStudioOnline->PushDown();
   } else {
      mStudioOnline->PopUp();
   }
   std::cout << mServerEnabled << std::endl;
}

void VirtualStudioPanel::ShowPanel(std::string serverID, std::string accessToken, bool focus)
{
   if(serverID.empty() || accessToken.empty())
   {
      ResetStudio();
      return;
   }

   wxWindowUpdateLocker freeze(this);
   SetStudio(serverID, accessToken);

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

void VirtualStudioPanel::OnServerWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg)
{
   using namespace rapidjson;

   auto payload = msg->get_payload();

   Document document;
   document.Parse(payload.c_str());
   // Check for parse errors
   if (document.HasParseError()) {
      wxLogInfo("Error parsing JSON: %s", document.GetParseError());
      return;
   }

   UpdateServerName(document["name"].GetString());
   UpdateServerStatus(document["status"].GetString());
   UpdateServerBanner(document["bannerURL"].GetString());
   UpdateServerSessionID(document["sessionId"].GetString());
   UpdateServerOwnerID(document["ownerId"].GetString());
   UpdateServerEnabled(document["enabled"].GetBool());
}

void VirtualStudioPanel::OnSubscriptionWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg)
{
   using namespace rapidjson;

   auto payload = msg->get_payload();

   Document document;
   document.Parse(payload.c_str());
   // Check for parse errors
   if (document.HasParseError()) {
      wxLogInfo("Error parsing JSON: %s", document.GetParseError());
      return;
   }

   auto userID = std::string(document["user_id"].GetString());
   auto name = std::string(document["nickname"].GetString());
   auto picture = std::string(document["picture"].GetString());
   mSubscriptionsMap->AddParticipant(userID, name, picture, 0.0);
}

void VirtualStudioPanel::OnDeviceWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg)
{
   using namespace rapidjson;

   auto payload = msg->get_payload();

   Document document;
   document.Parse(payload.c_str());
   // Check for parse errors
   if (document.HasParseError()) {
      wxLogInfo("Error parsing JSON: %s", document.GetParseError());
      return;
   }

   auto deviceID = std::string(document["id"].GetString());
   auto ownerID = std::string(document["ownerId"].GetString());
   mDeviceToOwnerMap[deviceID] = ownerID;
   mOwnerToDeviceMap[ownerID] = deviceID;
}

void VirtualStudioPanel::OnMeterWssMessage(ConnectionHdl hdl, websocketpp::config::asio_client::message_type::ptr msg)
{
   using namespace rapidjson;

   auto payload = msg->get_payload();

   Document document;
   document.Parse(payload.c_str());
   // Check for parse errors
   if (document.HasParseError()) {
      wxLogInfo("Error parsing JSON: %s", document.GetParseError());
      return;
   }

   if (document["clients"].IsArray() && document["musicians"].IsArray()) {
      auto musicians = document["musicians"].GetArray();
      int idx = 0;
      for (auto& v : document["clients"].GetArray()) {
         auto device = std::string(v.GetString());
         std::string ownerID = "";
         if (device != "Jamulus" && device != "supernova") {
            if (device.rfind("rtc-") == 0) {
               // webrtc client
               ownerID = device.substr(4);
            } else {
               auto it = mDeviceToOwnerMap.find(device);
               if (it!= mDeviceToOwnerMap.end()) {
                  ownerID = it->second;
               }
            }
         }

         if (!ownerID.empty()) {
            auto dbVals = musicians[idx].GetArray();
            auto leftDbVal = dbVals[0].GetDouble();
            auto rightDbVal = dbVals[1].GetDouble();
            //std::cout << "Device " << device << " owned by " << ownerID << " with left val " << leftDbVal << " and right val " << rightDbVal << std::endl;

            auto participant = mSubscriptionsMap->GetParticipantByID(ownerID);
            if (participant != nullptr) {
               auto dB = std::max(leftDbVal, rightDbVal);
               float linear = powf(10.0, dB/20.0);
               participant->UpdateVolume(linear);
            }

         }
         idx++;
      }
   }
   /*
      Value::ConstValueIterator itr;
      for (itr = document["clients"].Begin(); itr != document.End(); ++itr) {
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
   }
   std::cout << "Meter: " << payload << std::endl;
   */
}

void VirtualStudioPanel::OnWssOpen(ConnectionHdl hdl)
{
   std::cout << "yaaaayyy" << std::endl;
   //std::string msg{"hello"};
   //std::cout << "on_open: send " << msg << std::endl;
   //client->send(hdl, msg, websocketpp::frame::opcode::text);
}

websocketpp::lib::shared_ptr<SslContext> VirtualStudioPanel::OnTlsInit()
{
   auto ctx = websocketpp::lib::make_shared<SslContext>(boost::asio::ssl::context::sslv23);
   return ctx;
};

void VirtualStudioPanel::DisableLogging(WSSClient& client)
{
   client.clear_access_channels(websocketpp::log::alevel::all);
   client.clear_error_channels(websocketpp::log::elevel::all);
}

void VirtualStudioPanel::SetUrl(WSSClient& client, std::string url)
{
   websocketpp::lib::error_code ec;
   auto connection = client.get_connection(url, ec);
   if (ec) {
      std::cout << "error " << ec << std::endl;
      return;
   }
   connection->append_header("Origin", "https://app.jacktrip.org");
   client.connect(connection);
}

void VirtualStudioPanel::InitializeWebsockets()
{
   InitServerWebsocket();
   InitSubscriptionsWebsocket();
   InitDevicesWebsocket();
}

void VirtualStudioPanel::StopWebsockets()
{
   StopMetersWebsocket();
   mDevicesThread.interrupt();
   mDevicesThread.join();
   mSubscriptionsThread.interrupt();
   mSubscriptionsThread.join();
   mServerThread.interrupt();
   mServerThread.join();
}

void VirtualStudioPanel::InitServerWebsocket()
{
   mServerThread = boost::thread([&]
   {
      WSSClient client;
      DisableLogging(client);
      client.init_asio();
      client.set_tls_init_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnTlsInit));
      client.set_open_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnWssOpen, this, ::_1));
      client.set_message_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnServerWssMessage, this, ::_1, ::_2));
      std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "?auth_code=" + mAccessToken;
      //std::cout << "URL is: " << url << std::endl;
      SetUrl(client, url);

      try {
         client.run();
      } catch (websocketpp::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (std::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (...) {
         std::cout << "other exception" << std::endl;
      }
   });

   mServerThread.detach();
   /* This is the way the docs suggest doing this but it didn't work for me...
   WSSClient client;
   turn_off_logging(client);
   client.init_asio();
   client.start_perpetual();

   set_tls_init_handler(client);
   set_open_handler(client);
   set_message_handler(client);
   std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "?auth_code=" + mAccessToken;
   std::cout << "URL is: " << url << std::endl;
   set_url(client, url);
   mThread.reset(new websocketpp::lib::thread(&WSSClient::run, &client));
   mThread->detach();
   */
}

void VirtualStudioPanel::InitSubscriptionsWebsocket()
{
   mSubscriptionsThread = boost::thread([&]
   {
      WSSClient client;
      DisableLogging(client);
      client.init_asio();
      client.set_tls_init_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnTlsInit));
      client.set_open_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnWssOpen, this, ::_1));
      client.set_message_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnSubscriptionWssMessage, this, ::_1, ::_2));
      std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "/subscriptions?auth_code=" + mAccessToken;
      //std::cout << "URL is: " << url << std::endl;
      SetUrl(client, url);

      try {
         client.run();
      } catch (websocketpp::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (std::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (...) {
         std::cout << "other exception" << std::endl;
      }
   });

   mSubscriptionsThread.detach();
}

void VirtualStudioPanel::InitDevicesWebsocket()
{
   mDevicesThread = boost::thread([&]
   {
      WSSClient client;
      DisableLogging(client);
      client.init_asio();
      client.set_tls_init_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnTlsInit));
      client.set_open_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnWssOpen, this, ::_1));
      client.set_message_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnDeviceWssMessage, this, ::_1, ::_2));
      std::string url = "wss://" + kApiHost + "/api/servers/" + mServerID + "/devices?auth_code=" + mAccessToken;
      //std::cout << "URL is: " << url << std::endl;
      SetUrl(client, url);

      try {
         client.run();
      } catch (websocketpp::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (std::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (...) {
         std::cout << "other exception" << std::endl;
      }
   });

   mDevicesThread.detach();
}

void VirtualStudioPanel::InitMetersWebsocket()
{
   std::cout << "InitMetersWebsocket" << std::endl;
   if (mServerStatus != "Ready" || mServerSessionID.empty()) {
      return;
   }
   std::cout << "InitMetersWebsocket done" << std::endl;

   mMetersThread = boost::thread([&]
   {
      WSSClient client;
      DisableLogging(client);
      client.init_asio();
      client.set_tls_init_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnTlsInit));
      client.set_open_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnWssOpen, this, ::_1));
      client.set_message_handler(websocketpp::lib::bind(&VirtualStudioPanel::OnMeterWssMessage, this, ::_1, ::_2));

      std::string secret = sha256("jktp-" + mServerID + "-" + mServerSessionID);
      std::cout << "SHA: " << secret << std::endl;
      std::string url = "wss://" + mServerSessionID + ".jacktrip.cloud/meters?auth_code=" + secret;
      std::cout << "URL is: " << url << std::endl;
      SetUrl(client, url);

      try {
         client.run();
      } catch (websocketpp::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (std::exception const & e) {
         std::cout << e.what() << std::endl;
      } catch (...) {
         std::cout << "other exception" << std::endl;
      }
   });

   mMetersThread.detach();
}

void VirtualStudioPanel::StopMetersWebsocket()
{
   mMetersThread.interrupt();
   mMetersThread.join();
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
            auto displayName = std::string(document["user_metadata"]["display_name"].GetString());
            if (!displayName.empty()) {
               name = displayName;
            }
         }
         auto picture = std::string(document["picture"].GetString());
         mSubscriptionsMap->AddParticipant(userID, name, picture, 0.0);
      }
   );
}

void VirtualStudioPanel::DoClose()
{
   mDeviceToOwnerMap.clear();
   mOwnerToDeviceMap.clear();
   std::cout << "Stopping websocket in DoClose" << std::endl;
   StopWebsockets();
   std::cout << "Stopped websocket in DoClose" << std::endl;
   Close();
}

StudioParticipantMap* VirtualStudioPanel::GetSubscriptionsMap()
{
   return mSubscriptionsMap;
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
      mServerID = serverID;
      mAccessToken = accessToken;
      mParticipantsList->SetProject(mProject);
      InitializeWebsockets();
   }
}

void VirtualStudioPanel::ResetStudio()
{
   mServerThread.interrupt();
   mServerThread.join();
   UpdateServerName("");
   UpdateServerStatus("Disabled");
   UpdateServerEnabled(false);
   UpdateServerOwnerID("");

   mParticipantsList->Reset();
   mCurrentTrack.reset();
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
