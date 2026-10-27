#pragma once
#include <string>
#include <string_view>

// UI language. English is the default; Japanese is used when asked for, or when the Steam client / system language is Japanese.
// Detection order (first match wins):
//   1. $FRAME_MOVER_LANG
//   2. ~/.config/frame-mover/lang ("ja" or "en")
//   3. the Steam client's language (registry.vdf)
//   4. $LC_ALL / $LC_MESSAGES / $LANG
// A value starting with "ja" selects Japanese, anything else English.
namespace i18n
{
bool Japanese();
// Overrides the detected language (used by tests).
void SetJapanese( bool on );
// true when `value` (an env var, a lang file, a Steam "language" value) names Japanese.
bool NamesJapanese( std::string_view value );
// Exact English UI string -> Japanese (unchanged when Japanese is off or the string is unknown).
std::string Tr( const std::string &en );
// SteamVR input names such as "Left Hand D-Pad Up" -> "左手 十字キー 上".
std::string TrBinding( std::string en );
} // namespace i18n
