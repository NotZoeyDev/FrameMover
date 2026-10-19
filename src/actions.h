#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum class Hand
{
	Left,
	Right
};

// One frame of mover input, read through SteamVR Input so that every button is a rebindable action.
struct InputFrame
{
	bool drag[2] = {};      // indexed by Hand: grab-and-move held with that hand
	bool turn[2] = {};      // grab-and-turn held with that hand
	bool snapLeft = false;  // rising edge this frame
	bool snapRight = false; // rising edge this frame
	bool heightUp = false;  // held
	bool heightDown = false;
	bool reset = false;     // held; the app decides how long it must be held
};

class Actions
{
public:
	// `overrideGameInput` asks SteamVR to withhold inputs bound to our actions from the running game
	// (needs the "Experimental overlay input overrides" developer setting).
	bool Init( const std::string &manifestPath, bool overrideGameInput, std::string &error );
	bool Update( InputFrame &out );
	// Short buzz on one controller as feedback.
	void Pulse( Hand hand, float seconds = 0.06f, float amplitude = 0.6f );
	// (action label, comma separated list of what it is bound to), e.g. ("Grab and move", "Left Hand Bumper, ...").
	std::vector<std::pair<std::string, std::string>> Bindings();
	// Opens SteamVR's own binding editor for this app.
	bool OpenBindingEditor( const std::string &appKey );
	// SteamVR loads the bindings a user saved in its editor from the Steam Workshop. When the controllers connect before
	// Steam has logged in, that load fails and SteamVR silently falls back to the shipped default bindings for the rest
	// of the session. The saved item is already on disk, so point SteamVR's "current bindings" setting at a local copy of
	// it instead: loading it then never needs Steam. Safe to call again; does nothing once the setting is a local file.
	void PinUserBindings();

private:
	uint64_t m_set = 0;
	uint64_t m_action[8] = {}; // input actions in Bindings() order, followed by haptics
	uint64_t m_hand[2] = {};
	int32_t m_priority = 0;
	std::string m_lastGood[7]; // last non-empty binding text per action, see Bindings()
	std::string m_defaults[7]; // what the shipped default binding file maps each action to, in Bindings()'s text format
};
