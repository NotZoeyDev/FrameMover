#include "bindings.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int failures = 0;
#define CHECK(condition) do { if ( !(condition) ) { \
	std::printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition ); ++failures; } } while ( false )

int main( int argc, char **argv )
{
	using namespace bindings;
	std::ifstream input( argc > 1 ? argv[1] : "data/bindings/frame_controller.json" );
	CHECK( input.good() );
	const std::string shipped( ( std::istreambuf_iterator<char>( input ) ), std::istreambuf_iterator<char>() );
	std::string actual[Haptic];
	LoadDefaults( shipped, actual );
	const char *expected[] = {
		"Left Hand Bumper, Right Hand Bumper",
		"Left Hand Thumbstick, Right Hand Thumbstick",
		"Left Hand Dpad Left", "Left Hand Dpad Right",
		"Left Hand Dpad Up", "Left Hand Dpad Down", "Left Hand View",
	};
	for ( int i = 0; i < Haptic; i++ )
		CHECK( actual[i] == expected[i] );

	CHECK( Pretty( "/user/hand/right" ) == "Right Hand" );
	CHECK( Pretty( "/input/dpad_left" ) == "Dpad Left" );
	CHECK( Pretty( "/user/head" ) == "Head" );
	CHECK( Pretty( "already localized" ) == "already localized" );

	// Only a well-formed workshop url counts as "saved in SteamVR's editor"; anything else must be left alone.
	CHECK( WorkshopItemId( "vr-input-workshop://3822741892" ) == "3822741892" );
	for ( const char *url : { "", "file:///home/x/bindings.json", "vr-input-workshop://", "vr-input-workshop://12ab", "vr-input-workshop://12/../3",
			  "vr-input-workshop:/1", "xvr-input-workshop://1" } )
		CHECK( WorkshopItemId( url ).empty() );
	// A localized description may contain escaped JSON-looking text, backslashes, or UTF-8.
	// None may be mistaken for a source. Empty/unrelated fields and haptic paths are ignored.
	const char *escaped = R"JSON({
		"description": "Café: \"output\":\"/actions/playspace/in/resetoriginal\", \"path\":\"/user/hand/left/input/view\"; C:\\test\\",
		"output" : "\/actions\/playspace\/in\/drag",
		"mode": "button", "parameters": {}, "path" : "\/user\/hand\/left\/input\/bumper",
		"output":"/actions/playspace/in/drag", "path":"/user/hand/right/input/bumper",
		"output":"/actions/playspace/out/haptic", "path":"/user/hand/left/output/haptic",
		"empty": "", "unrelated": "path", "other": ["output", "path"]
	})JSON";
	std::string parsed[Haptic];
	LoadDefaults( escaped, parsed );
	CHECK( parsed[Drag] == expected[Drag] );
	for ( int i = 1; i < Haptic; i++ )
		CHECK( parsed[i].empty() );

	// Truncated or absent data must terminate and leave missing bindings empty.
	for ( const char *text : { "", "\"", "\"output\"", "\"output\":", "\"output\":\"unterminated\\" } )
	{
		std::string empty[Haptic];
		LoadDefaults( text, empty );
		for ( const auto &name : empty )
			CHECK( name.empty() );
	}

	std::printf( "%s: default bindings, escaped fields, formatting and truncated data\n", failures ? "FAIL" : "PASS" );
	return failures ? 1 : 0;
}
