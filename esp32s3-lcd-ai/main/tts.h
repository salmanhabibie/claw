#pragma once

#include <stdbool.h>

/* Speak `text` out loud via ElevenLabs text-to-speech, streaming the audio to
 * the speaker as it arrives. Blocks until playback finishes. Needs WiFi and a
 * configured ElevenLabs API key. Returns false when nothing was spoken. */
bool tts_say(const char *text);

/* Short human-readable reason the last tts_say() failed (Indonesian, safe to
 * show on screen). Empty string when the last call succeeded. */
const char *tts_last_error(void);
