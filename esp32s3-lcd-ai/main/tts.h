#pragma once

/* Speak `text` out loud via ElevenLabs text-to-speech, streaming the audio to
 * the speaker as it arrives. Blocks until playback finishes. Needs WiFi and a
 * configured ElevenLabs API key. */
void tts_say(const char *text);
