#pragma once
#include "config.h"
#include "playspace.h"
#include "ui.h"

#include <string>
#include <utility>
#include <vector>

constexpr int kPanelW = 1600; // 16:9
constexpr int kPanelH = 900;

enum class MoverState
{
	WaitingForChaperone, // SteamVR has not given us a playspace yet
	Ready,
	PausedBySetup, // another program (room setup) is editing the chaperone; we stay out of the way
};

struct PanelModel
{
	Settings settings;
	MoverState state = MoverState::WaitingForChaperone;
	bool moved = false;
	Vec3 offset;
	double yawDegrees = 0;
	std::vector<std::pair<std::string, std::string>> bindings; // action, what it is bound to
	std::string inputError;                                    // non-empty when SteamVR Input could not be set up
	bool controllersConnected = true; // false: SteamVR has no controller yet, so it cannot resolve any binding
	bool languageMenuOpen = false;    // the language drop-down is shown (and takes every click)
};

// Draws the whole panel and fills `buttons` (cleared first) with the clickable areas; while the language menu is open those are
// only its entries.
void DrawPanel( Canvas &c, ButtonPanel &buttons, const PanelModel &m, int hovered = -1 );

// App glyph (four arrows) centred on (cx, cy), `size` pixels wide; used for the dashboard tab icon and artwork.
void DrawGlyph( Canvas &c, double cx, double cy, double size );
