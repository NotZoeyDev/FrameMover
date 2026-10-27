#include "i18n.h"

#include "config.h"
#include "files.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace i18n
{
namespace
{
int g_forced = -1; // -1: detect, 0: English, 1: Japanese

std::string Lower( std::string s )
{
	for ( char &c : s )
		c = (char)tolower( (unsigned char)c );
	return s;
}

// The Steam client keeps its UI language in registry.vdf as  "language"  "japanese".
std::string SteamLanguage()
{
	const std::string home = files::Home();
	for ( const char *rel : { "/.steam/registry.vdf", "/.local/share/Steam/registry.vdf" } )
	{
		std::string text;
		if ( !files::ReadFile( home + rel, text ) )
			continue;
		text = Lower( text );
		const size_t key = text.find( "\"language\"" );
		const size_t open = key == std::string::npos ? key : text.find( '"', key + 10 );
		const size_t close = open == std::string::npos ? open : text.find( '"', open + 1 );
		if ( close != std::string::npos )
			return text.substr( open + 1, close - open - 1 );
	}
	return "";
}

bool Detect()
{
	if ( const char *e = getenv( "FRAME_MOVER_LANG" ); e && *e )
		return NamesJapanese( e );
	std::string text;
	if ( files::ReadFile( ConfigDir() + "/lang", text ) && !Lower( text ).empty() )
		return NamesJapanese( text );
	const std::string steam = SteamLanguage();
	if ( !steam.empty() )
		return NamesJapanese( steam );
	for ( const char *var : { "LC_ALL", "LC_MESSAGES", "LANG" } )
		if ( const char *e = getenv( var ); e && *e )
			return NamesJapanese( e );
	return false;
}
} // namespace

bool NamesJapanese( std::string_view value )
{
	while ( !value.empty() && isspace( (unsigned char)value.front() ) )
		value.remove_prefix( 1 );
	return value.size() >= 2 && tolower( (unsigned char)value[0] ) == 'j' && tolower( (unsigned char)value[1] ) == 'a';
}

bool Japanese()
{
	static const bool detected = Detect();
	return g_forced < 0 ? detected : g_forced == 1;
}

void SetJapanese( bool on )
{
	g_forced = on ? 1 : 0;
}

std::string Tr( const std::string &en )
{
	static const std::unordered_map<std::string, std::string> table = {
		{ "Reset to original playspace", "元のプレイスペースに戻す" },
		{ "Your playspace is exactly as you saved it.", "プレイスペースは保存した状態のままです。" },
		{ "Raise me 5 cm", "5 cm 上げる" },
		{ "Lower me 5 cm", "5 cm 下げる" },
		{ "Grab speed", "つかみ速度" },
		{ "Snap turn angle", "スナップターン角度" },
		{ "Raise / lower speed", "上げ下げの速度" },
		{ "Lock height", "高さを固定" },
		{ "Haptics", "触覚フィードバック" },
		{ "Left hand grabs", "左手でつかむ" },
		{ "Right hand grabs", "右手でつかむ" },
		{ "Double-press + hold to grab", "2度押し＋長押しでつかむ" },
		{ "Huge chaperone", "巨大シャペロン" },
		{ "No chaperone", "シャペロンなし" },
		{ "Nothing is saved to SteamVR: Reset restores exactly the playspace", "SteamVR には何も保存されません。リセットすると、" },
		{ "and chaperone you had set.", "設定済みのプレイスペースとシャペロンに完全に戻ります。" },
		{ "Button bindings", "ボタン割り当て" },
		{ "SteamVR Input: changes you make in the editor show up here at once", "SteamVR 入力：エディタでの変更はすぐここに反映されます" },
		{ "No controller connected yet: bindings appear once SteamVR sees one.", "コントローラー未接続：SteamVR が認識すると表示されます。" },
		{ "SteamVR Input unavailable: ", "SteamVR 入力を利用できません：" },
		{ "Edit bindings in SteamVR", "SteamVR で割り当てを編集" },
		{ "Ready. Double-press and hold the grab button, then move your hand.", "準備完了：「つかんで移動」を2度押しし、押したまま手を動かす" },
		{ "Ready. Hold the grab button and move your hand.", "準備完了：「つかんで移動」を押したまま、手を動かす" },
		{ "Paused: SteamVR is editing the chaperone (room setup).", "一時停止：SteamVR がシャペロン（部屋設定）を編集中です" },
		{ "Waiting for SteamVR to report your playspace.", "SteamVR からプレイスペース情報を待っています" },
		// action labels (actions.cpp)
		{ "Grab and move", "つかんで移動" },
		{ "Grab and turn", "つかんで回転" },
		{ "Snap turn left", "左にスナップターン" },
		{ "Snap turn right", "右にスナップターン" },
		{ "Raise me", "上げる" },
		{ "Lower me", "下げる" },
		{ "Reset to original", "元に戻す" },
	};
	if ( !Japanese() )
		return en;
	const auto it = table.find( en );
	return it == table.end() ? en : it->second;
}

std::string TrBinding( std::string s )
{
	if ( !Japanese() )
		return s;
	static const std::vector<std::pair<const char *, const char *>> words = {
		{ "not bound", "未割り当て" },
		{ "Left Hand", "左手" },
		{ "Right Hand", "右手" },
		{ "Thumbstick", "サムスティック" },
		{ "Bumper", "バンパー" },
		{ "Trigger", "トリガー" },
		{ "Grip", "グリップ" },
		{ "D-Pad", "十字キー" },
		{ "Dpad", "十字キー" },
		{ "View", "ビュー" },
		{ "Menu", "メニュー" },
		{ "Click", "クリック" },
		{ " Up", " 上" },
		{ " Down", " 下" },
		{ " Left", " 左" },
		{ " Right", " 右" },
	};
	for ( const auto &w : words )
		for ( size_t p = 0; ( p = s.find( w.first, p ) ) != std::string::npos; p += strlen( w.second ) )
			s.replace( p, strlen( w.first ), w.second );
	return s;
}
} // namespace i18n
