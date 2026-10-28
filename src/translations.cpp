// UI translations. To add a language, append an entry below (and a "localization" block to data/action_manifest.json for the
// SteamVR binding editor). Keys are the exact English strings used in the code; a missing key falls back to English.
#include "i18n.h"

namespace i18n
{
const std::vector<Language> &Languages()
{
	static const std::vector<Language> languages = {
		// English is the fallback and must stay first: the code's strings are already English.
		{ "en", "english", "English", "Noto Sans", {}, {} },

		{ "ja", "japanese", "日本語", "Noto Sans CJK JP", // also carries the Latin glyphs
			{
				{ "Automatic", "自動" },
				{ "Reset to original playspace", "元のプレイスペースに戻す" },
				{ "Moved {0} {1}, turned {2}", "移動 {0} {1}、回転 {2}" },
				{ "Your playspace is exactly as you saved it.", "プレイスペースは保存した状態のままです。" },
				{ "Raise me 5 cm", "5 cm 上げる" },
				{ "Lower me 5 cm", "5 cm 下げる" },
				{ "Turn left {0}", "左に回転 {0}" },
				{ "Turn right {0}", "右に回転 {0}" },
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
			},
			{
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
			} },
	};
	return languages;
}
} // namespace i18n
