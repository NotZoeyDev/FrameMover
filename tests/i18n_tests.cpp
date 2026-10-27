#include "i18n.h"

#include <cstdio>
#include <cstdlib>

namespace
{
void Check( bool condition, const char *message )
{
	if ( !condition )
	{
		fprintf( stderr, "%s\n", message );
		exit( 1 );
	}
}
}

int main()
{
	Check( i18n::NamesJapanese( "ja" ) && i18n::NamesJapanese( "ja_JP.UTF-8" ) && i18n::NamesJapanese( "japanese" ) &&
			i18n::NamesJapanese( "  Japanese\n" ),
		"Japanese values were not recognised" );
	Check( !i18n::NamesJapanese( "" ) && !i18n::NamesJapanese( "j" ) && !i18n::NamesJapanese( "en" ) && !i18n::NamesJapanese( "en_US.UTF-8" ) &&
			!i18n::NamesJapanese( "english" ) && !i18n::NamesJapanese( "C" ),
		"non-Japanese values were taken for Japanese" );

	i18n::SetJapanese( false );
	Check( i18n::Tr( "Reset to original playspace" ) == "Reset to original playspace", "English text was changed" );
	Check( i18n::TrBinding( "Left Hand D-Pad Up" ) == "Left Hand D-Pad Up", "English binding name was changed" );

	i18n::SetJapanese( true );
	Check( i18n::Japanese(), "SetJapanese( true ) was ignored" );
	Check( i18n::Tr( "Reset to original playspace" ) != "Reset to original playspace", "known string was not translated" );
	Check( i18n::Tr( "text that is not in the table" ) == "text that is not in the table", "unknown text must pass through" );
	Check( i18n::TrBinding( "Left Hand D-Pad Up" ) == "左手 十字キー 上", "binding name was translated wrongly" );
	Check( i18n::TrBinding( "Left Hand Bumper, Right Hand Bumper" ) == "左手 バンパー, 右手 バンパー", "every word of a binding list must be translated" );
	Check( i18n::TrBinding( "not bound" ) == "未割り当て", "'not bound' was not translated" );
	puts( "i18n tests passed" );
}
