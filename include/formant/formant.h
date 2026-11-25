#pragma once

// Umbrella header for embedders:
//   #include <formant/formant.h>
//
// The engine has no third-party dependencies and does not open audio devices;
// it renders floating-point PCM that callers can feed to any audio stack.

#include "formant/phoneme.h"
#include "formant/hash.h"
#include "formant/gibberish.h"
#include "formant/language.h"
#include "formant/lexicon.h"
#include "formant/ipa.h"
#include "formant/metrics.h"
#include "formant/synthesizer.h"
#include "formant/ssml.h"
#include "formant/prosody_file.h"
#include "formant/intonation.h"
#include "formant/song.h"
#include "formant/affect.h"
#include "formant/breath.h"
#include "formant/neural.h"
#include "formant/mrt.h"
#include "formant/timing.h"
#include "formant/transpose.h"
#include "formant/voice.h"
#include "formant/voice_file.h"
#include "formant/register.h"
#include "formant/rhythm.h"
#include "formant/wav.h"
