#include "i18n.h"

#include "config.h"
#include "files.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace i18n
{
namespace
{
const Language *g_current = nullptr; // detected on first use
const Language *g_chosen = nullptr;  // the lang file's language, nullptr when there is none

std::string LangFile()
{
	return ConfigDir() + "/lang";
}

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

const Language &Named( std::string_view value )
{
	const Language *l = Find( value );
	return l ? *l : Languages().front();
}

// Also reads the lang file into g_chosen.
const Language &Detect()
{
	std::string text;
	const bool chosen = files::ReadFile( LangFile(), text ) && text.find_first_not_of( " \t\r\n" ) != std::string::npos;
	g_chosen = chosen ? &Named( text ) : nullptr;
	if ( const char *e = getenv( "FRAME_MOVER_LANG" ); e && *e )
		return Named( e );
	if ( g_chosen )
		return *g_chosen;
	const std::string steam = SteamLanguage();
	if ( !steam.empty() )
		return Named( steam );
	for ( const char *var : { "LC_ALL", "LC_MESSAGES", "LANG" } )
		if ( const char *e = getenv( var ); e && *e )
			return Named( e );
	return Languages().front();
}
} // namespace

const Language *Find( std::string_view value )
{
	while ( !value.empty() && isspace( (unsigned char)value.front() ) )
		value.remove_prefix( 1 );
	while ( !value.empty() && isspace( (unsigned char)value.back() ) )
		value.remove_suffix( 1 );
	const std::string v = Lower( std::string( value ) );
	// The longest matching code wins, so a regional entry ("pt_br") beats its base language ("pt").
	const Language *best = nullptr;
	size_t bestLength = 0;
	for ( const Language &l : Languages() )
	{
		if ( v == l.steamName )
			return &l;
		const size_t n = strlen( l.code );
		const bool codeMatches = v.compare( 0, n, l.code ) == 0 && ( v.size() == n || strchr( "_-.@", v[n] ) );
		if ( codeMatches && n > bestLength )
			best = &l, bestLength = n;
	}
	return best;
}

const Language &Current()
{
	if ( !g_current )
		g_current = &Detect();
	return *g_current;
}

const Language *Chosen()
{
	Current();
	return g_chosen;
}

void Choose( const Language *l )
{
	if ( l )
	{
		files::MakeDirs( ConfigDir() );
		files::WriteFile( LangFile(), std::string( l->code ) + "\n" );
		g_current = g_chosen = l;
	}
	else
	{
		std::remove( LangFile().c_str() );
		g_current = &Detect();
	}
}

void SetLanguage( std::string_view value )
{
	g_current = &Named( value );
}

std::string Tr( const std::string &en )
{
	const auto &strings = Current().strings;
	const auto it = strings.find( en );
	return it == strings.end() ? en : it->second;
}

std::string Tr( const std::string &en, std::initializer_list<std::string> args )
{
	std::string s = Tr( en );
	size_t i = 0;
	for ( const std::string &arg : args )
	{
		const std::string mark = "{" + std::to_string( i++ ) + "}";
		for ( size_t p = 0; ( p = s.find( mark, p ) ) != std::string::npos; p += arg.size() )
			s.replace( p, mark.size(), arg );
	}
	return s;
}

std::string TrBinding( std::string s )
{
	for ( const auto &w : Current().bindingWords )
		for ( size_t p = 0; ( p = s.find( w.first, p ) ) != std::string::npos; p += w.second.size() )
			s.replace( p, w.first.size(), w.second );
	return s;
}
} // namespace i18n
