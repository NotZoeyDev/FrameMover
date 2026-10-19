#pragma once
// Diagnostics for the running mover. SteamVR starts the mover with stderr connected to nothing useful, so everything it
// wants to say goes to a size-capped log file instead. Without a log file (--selftest) or when stderr is a terminal it
// also goes to stderr.

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <sys/time.h>
#include <unistd.h>

namespace logging
{
inline FILE *&Sink()
{
	static FILE *file = nullptr;
	return file;
}

// Appends to `path`. A log above `maxBytes` is first moved to `path`.1 (one generation is kept).
inline void Open( const std::string &path, long maxBytes = 512 * 1024 )
{
	if ( FILE *old = std::fopen( path.c_str(), "rb" ) )
	{
		std::fseek( old, 0, SEEK_END );
		const long size = std::ftell( old );
		std::fclose( old );
		if ( size > maxBytes )
			std::rename( path.c_str(), ( path + ".1" ).c_str() );
	}
	Sink() = std::fopen( path.c_str(), "ab" );
}

#if defined( __GNUC__ )
__attribute__( ( format( printf, 1, 2 ) ) )
#endif
inline void Printf( const char *fmt, ... )
{
	timeval tv;
	gettimeofday( &tv, nullptr );
	tm local;
	localtime_r( &tv.tv_sec, &local );
	char stamp[32];
	std::strftime( stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local );
	char text[2048];
	va_list args;
	va_start( args, fmt );
	std::vsnprintf( text, sizeof text, fmt, args );
	va_end( args );
	if ( FILE *file = Sink() )
	{
		std::fprintf( file, "%s.%03d %s", stamp, (int)( tv.tv_usec / 1000 ), text );
		std::fflush( file );
	}
	if ( !Sink() || isatty( fileno( stderr ) ) )
		std::fputs( text, stderr );
}
} // namespace logging
