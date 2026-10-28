#include "i18n.h"
#include "files.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
void SetEnv( const char *key, const char *value )
{
#ifdef _WIN32
	_putenv_s( key, value ? value : "" );
#else
	if ( value )
		setenv( key, value, 1 );
	else
		unsetenv( key );
#endif
}

void Check( bool condition, const char *message )
{
	if ( !condition )
	{
		fprintf( stderr, "%s\n", message );
		exit( 1 );
	}
}

const char *CodeOf( const char *value )
{
	const i18n::Language *l = i18n::Find( value );
	return l ? l->code : "";
}

// The "{N}" markers of `s`, in order of N.
std::string Markers( const std::string &s )
{
	std::string out;
	for ( int i = 0; i < 10; ++i )
		if ( s.find( "{" + std::to_string( i ) + "}" ) != std::string::npos )
			out += std::to_string( i );
	return out;
}
} // namespace

int main()
{
	Check( !strcmp( i18n::Languages().front().code, "en" ), "English must be the first language (the fallback)" );

	for ( const char *ja : { "ja", "ja_JP.UTF-8", "japanese", "  Japanese\n", "JA" } )
		Check( !strcmp( CodeOf( ja ), "ja" ), "Japanese value was not recognised" );
	for ( const char *en : { "en", "en_US.UTF-8", "english" } )
		Check( !strcmp( CodeOf( en ), "en" ), "English value was not recognised" );
	for ( const char *none : { "", "j", "jap", "javanese", "C", "german" } )
		Check( i18n::Find( none ) == nullptr, "value naming no known language was matched" );

	for ( const i18n::Language &l : i18n::Languages() )
	{
		Check( l.font && *l.font, "every language needs a font" );
		for ( const auto &kv : l.strings )
			Check( Markers( kv.first ) == Markers( kv.second ), "a translation must keep the {N} markers of its English key" );
	}

	i18n::SetLanguage( "en" );
	Check( i18n::Tr( "Reset to original playspace" ) == "Reset to original playspace", "English text was changed" );
	Check( i18n::Tr( "Turn left {0}", { "45" } ) == "Turn left 45", "English arguments were not filled in" );
	Check( i18n::TrBinding( "Left Hand D-Pad Up" ) == "Left Hand D-Pad Up", "English binding name was changed" );

	i18n::SetLanguage( "german" );
	Check( !strcmp( i18n::Current().code, "en" ), "an unknown language must fall back to English" );

	i18n::SetLanguage( "ja" );
	Check( !strcmp( i18n::Current().code, "ja" ), "SetLanguage( \"ja\" ) was ignored" );
	Check( i18n::Tr( "Reset to original playspace" ) != "Reset to original playspace", "known string was not translated" );
	Check( i18n::Tr( "text that is not in the table" ) == "text that is not in the table", "unknown text must fall back to English" );
	Check( i18n::Tr( "Moved {0} {1}, turned {2}", { "1 m", "(x)", "9°" } ) == "移動 1 m (x)、回転 9°", "arguments were not filled in" );
	Check( i18n::Tr( "not in the table {0}", { "1" } ) == "not in the table 1", "arguments must be filled into the English fallback" );
	Check( i18n::TrBinding( "Left Hand D-Pad Up" ) == "左手 十字キー 上", "binding name was translated wrongly" );
	Check( i18n::TrBinding( "Left Hand Bumper, Right Hand Bumper" ) == "左手 バンパー, 右手 バンパー", "every word of a binding list must be translated" );
	Check( i18n::TrBinding( "not bound" ) == "未割り当て", "'not bound' was not translated" );

	// The panel's choice is saved to the lang file and beats the locale; "Automatic" removes it and detects again.
	namespace fs = std::filesystem;
	const fs::path root =
		fs::temp_directory_path() / ( "frame-mover-i18n-test-" + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
	Check( fs::create_directory( root ), "could not create a temporary directory" );
	const std::string base = root.generic_string(), langFile = base + "/frame-mover/lang";
	SetEnv( "HOME", base.c_str() ); // no Steam registry.vdf
	SetEnv( "XDG_CONFIG_HOME", base.c_str() );
	for ( const char *key : { "FRAME_MOVER_LANG", "LC_ALL", "LC_MESSAGES" } )
		SetEnv( key, nullptr );
	SetEnv( "LANG", "ja_JP.UTF-8" );
	const i18n::Language &english = i18n::Languages().front();
	i18n::Choose( &english );
	std::string text;
	Check( files::ReadFile( langFile, text ) && text == "en\n", "the chosen language was not saved" );
	Check( i18n::Chosen() == &english && &i18n::Current() == &english, "the chosen language was not applied" );
	i18n::Choose( nullptr );
	Check( !fs::exists( langFile ), "automatic detection must remove the lang file" );
	Check( !i18n::Chosen() && !strcmp( i18n::Current().code, "ja" ), "automatic detection must use the locale again" );
	fs::remove_all( root );
	puts( "i18n tests passed" );
}
