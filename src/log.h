#pragma once
// A small record of what the mover did, for when something goes wrong: SteamVR starts it with stderr connected to nothing.
// Only one-line events are written (no per-frame data), and the file is capped: above kMaxBytes it is moved to `.1`,
// replacing the older one, so the two together never exceed twice that. Without a log file (--selftest) lines go to stderr.

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

namespace logging
{
constexpr long kMaxBytes = 64 * 1024;

inline FILE *&Sink()
{
	static FILE *file = nullptr;
	return file;
}

inline std::string &Path()
{
	static std::string path;
	return path;
}

inline void Open( const std::string &path )
{
	Path() = path;
	Sink() = std::fopen( path.c_str(), "ab" );
}

#if defined( __GNUC__ )
__attribute__( ( format( printf, 1, 2 ) ) )
#endif
inline void Printf( const char *fmt, ... )
{
	FILE *out = Sink() ? Sink() : stderr;
	if ( Sink() && std::ftell( Sink() ) > kMaxBytes )
	{
		std::fclose( Sink() );
		std::rename( Path().c_str(), ( Path() + ".1" ).c_str() );
		out = Sink() = std::fopen( Path().c_str(), "ab" );
		if ( !out )
			out = stderr;
	}
	const std::time_t now = std::time( nullptr );
	std::tm local;
	localtime_r( &now, &local );
	char stamp[24];
	std::strftime( stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S ", &local );
	std::fputs( stamp, out );
	va_list args;
	va_start( args, fmt );
	std::vfprintf( out, fmt, args );
	va_end( args );
	std::fflush( out );
}
} // namespace logging
