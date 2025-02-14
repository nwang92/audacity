/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportUtils.h

  Dominic Mazzoni
 
  Vitaly Sverchinsky split from ImportPlugin.h

**********************************************************************/

#pragma once

#include <memory>
#include "SampleFormat.h"
#include "Internat.h"

class wxString;

class WaveTrackFactory;
class WaveTrack;

class IMPORT_EXPORT_API ImportUtils final
{
public:
   
   //! Choose appropriate format, which will not be narrower than the specified one
   static sampleFormat ChooseFormat(sampleFormat effectiveFormat);
   
   //! Build a wave track with appropriate format, which will not be narrower than the specified one
   static std::shared_ptr<WaveTrack> NewWaveTrack( WaveTrackFactory &trackFactory,
      sampleFormat effectiveFormat, double rate);
   
   static void ShowMessageBox(const TranslatableString& message, const TranslatableString& caption = XO("Import Project"));
   
};
