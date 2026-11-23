#pragma once

// Umbrella header for embedders:
//   #include <formant/formant.h>
//
// The engine has no third-party dependencies and does not open audio devices;
// it renders floating-point PCM that callers can feed to any audio stack.

#include "formant/phoneme.h"
#include "formant/language.h"
#include "formant/lexicon.h"
#include "formant/ipa.h"
#include "formant/metrics.h"
#include "formant/synthesizer.h"
#include "formant/ssml.h"
#include "formant/timing.h"
#include "formant/transpose.h"
#include "formant/voice.h"
#include "formant/wav.h"
