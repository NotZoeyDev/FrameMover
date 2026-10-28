#pragma once
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// UI language. Every language is one entry of the table in translations.cpp; English is the fallback, both for the whole UI and
// for any string a translation does not have yet.
// Detection order (the first source that is set decides; a value naming no known language means English):
//   1. $FRAME_MOVER_LANG
//   2. ~/.config/frame-mover/lang (e.g. "ja" or "en"; written by the panel's language menu)
//   3. the Steam client's language (registry.vdf)
//   4. $LC_ALL / $LC_MESSAGES / $LANG
namespace i18n
{
struct Language
{
	const char *code;      // locale prefix, lower case: "ja" matches "ja", "ja_JP.UTF-8", "JA"
	const char *steamName; // the Steam client's "language" value: "japanese"
	const char *name;      // shown in the panel's language menu, in the language itself: "日本語"
	const char *font;      // cairo font family covering the script (cairo's toy API does not fall back per glyph)
	// Exact English UI string -> translation. "{0}", "{1}"... are arguments (see Tr below) and may be reordered.
	std::unordered_map<std::string, std::string> strings;
	// Replaced in this order inside SteamVR input names ("Left Hand D-Pad Up").
	std::vector<std::pair<std::string, std::string>> bindingWords;
};

// All languages; the first one is English, the fallback.
const std::vector<Language> &Languages();
// The language a value (an env var, the lang file, a Steam "language" value) names, or nullptr.
const Language *Find( std::string_view value );
// The detected language, or the one set with Choose / SetLanguage.
const Language &Current();
// The language picked in the panel (the lang file), or nullptr for automatic detection.
const Language *Chosen();
// Saves `l` to the lang file (nullptr removes it and detects again) and switches to it at once.
void Choose( const Language *l );
// Overrides the language for this run only (tests); a value naming no known language selects English.
void SetLanguage( std::string_view value );

// English UI string -> current language (unchanged when there is no translation).
std::string Tr( const std::string &en );
// Same, then "{0}", "{1}"... are replaced with `args`: Tr( "Turn left {0}", { "45°" } ).
std::string Tr( const std::string &en, std::initializer_list<std::string> args );
// SteamVR input names such as "Left Hand D-Pad Up" -> "左手 十字キー 上".
std::string TrBinding( std::string en );
} // namespace i18n
