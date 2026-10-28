#pragma once
#include "config.h"

enum class PanelAction
{
	Reset, EditBindings, Raise, Turn,
	LanguageMenu, // opens / closes the language menu
	Language,     // value: index into i18n::Languages(), -1 for automatic detection
	DragMultiplier, SnapDegrees, HeightSpeed,
	LockHeight, LeftHand, RightHand, Haptics, DoublePress, LargeChaperone, DisableChaperone,
};

// Payloads are absolute values from the displayed controls. The caller clamps and saves settings.
inline bool ApplyPanelSetting( PanelAction action, double value, Settings &s )
{
	constexpr float Settings::*numbers[] = { &Settings::dragMultiplier, &Settings::snapDegrees, &Settings::heightSpeed };
	constexpr bool Settings::*toggles[] = { &Settings::lockHeight, &Settings::leftHand, &Settings::rightHand,
		&Settings::haptics, &Settings::doublePress, &Settings::largeChaperone, &Settings::disableChaperone };
	const unsigned number = (unsigned)action - (unsigned)PanelAction::DragMultiplier;
	const unsigned toggle = (unsigned)action - (unsigned)PanelAction::LockHeight;
	if ( number < 3 )
		s.*numbers[number] = (float)value;
	else if ( toggle < 7 )
		s.*toggles[toggle] = value != 0;
	else
		return false;
	return true;
}
