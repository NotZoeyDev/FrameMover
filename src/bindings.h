#pragma once

#include <string>
#include <string_view>

namespace bindings
{
enum { Drag, Turn, SnapLeft, SnapRight, Up, Down, Reset, Haptic };
inline constexpr struct
{
	const char *path;
	const char *label;
} kActions[] = {
	{ "/actions/playspace/in/drag", "Grab and move" },
	{ "/actions/playspace/in/turn", "Grab and turn" },
	{ "/actions/playspace/in/snapturnleft", "Snap turn left" },
	{ "/actions/playspace/in/snapturnright", "Snap turn right" },
	{ "/actions/playspace/in/heightup", "Raise me" },
	{ "/actions/playspace/in/heightdown", "Lower me" },
	{ "/actions/playspace/in/resetoriginal", "Reset to original" },
	{ "/actions/playspace/out/haptic", nullptr },
};

// SteamVR remembers the bindings a user saved from its editor as a Steam Workshop item, "vr-input-workshop://<item id>".
// Returns the item id, or "" when `url` is anything else (empty, a file:// url, a malformed id).
inline std::string WorkshopItemId( std::string_view url )
{
	constexpr std::string_view prefix = "vr-input-workshop://";
	if ( url.substr( 0, prefix.size() ) != prefix )
		return "";
	url.remove_prefix( prefix.size() );
	if ( url.empty() || url.find_first_not_of( "0123456789" ) != std::string_view::npos )
		return "";
	return std::string( url );
}

inline void AppendName( std::string &list, std::string_view name )
{
	if ( !list.empty() )
		list += ", ";
	list += name;
}

// "/user/hand/left" -> "Left Hand", "/input/dpad_left" -> "Dpad Left".
inline std::string Pretty( std::string s )
{
	for ( std::string_view prefix : { "/user/hand/", "/user/", "/input/" } )
		if ( s.rfind( prefix, 0 ) == 0 )
		{
			s.erase( 0, prefix.size() );
			bool start = true;
			for ( char &c : s )
			{
				if ( c == '_' || c == '/' )
					c = ' ';
				if ( start && c >= 'a' && c <= 'z' )
					c = (char)( c - 'a' + 'A' );
				start = c == ' ';
			}
			return prefix == "/user/hand/" ? s + " Hand" : s;
		}
	return s;
}

// Consume a JSON string, including escaped quotes and backslashes in unrelated fields. The binding
// paths themselves are ASCII and need only the JSON escapes for slash, backslash and quote.
inline bool StringToken( std::string_view text, size_t &pos, std::string &value )
{
	value.clear();
	for ( ++pos; pos < text.size(); )
	{
		char c = text[pos++];
		if ( c == '"' )
			return true;
		if ( c == '\\' )
		{
			if ( pos == text.size() )
				return false;
			c = text[pos++];
		}
		value += c;
	}
	return false;
}

// The shipped sources list their action ("output") before "path". We only need those two fields;
// a small string scanner avoids linking a regular-expression engine for this one fixed data file.
inline void LoadDefaults( std::string_view text, std::string ( &out )[Haptic] )
{
	std::string pending, key, value;
	for ( size_t pos = 0; ( pos = text.find( '"', pos ) ) != std::string_view::npos; )
	{
		if ( !StringToken( text, pos, key ) )
			break;
		const size_t colon = text.find_first_not_of( " \t\r\n", pos );
		if ( colon == std::string_view::npos || text[colon] != ':' )
			continue;
		pos = text.find_first_not_of( " \t\r\n", colon + 1 );
		if ( pos == std::string_view::npos )
			break;
		if ( text[pos] != '"' )
			continue;
		if ( !StringToken( text, pos, value ) )
			break;
		if ( key == "output" )
			pending = value;
		else if ( key == "path" )
		{
			const size_t split = value.find( "/input/" );
			for ( int i = 0; i < Haptic && split != std::string::npos; i++ )
				if ( pending == kActions[i].path )
					AppendName( out[i], Pretty( value.substr( 0, split ) ) + " " + Pretty( value.substr( split ) ) );
			pending.clear();
		}
	}
}
} // namespace bindings
