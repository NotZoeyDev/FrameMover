#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include "app.h"
#include "backup.h"
#include "files.h"
#include "install.h"
#include "log.h"
#include "panel.h"

static void OnSignal( int )
{
	App::s_quit = true;
}

static void OnShowDashboard( int )
{
	App::s_showDashboard = true;
}

static void Usage()
{
	printf( "frame-mover - move, turn and reset the playspace from inside the Steam Frame\n\n"
			"  (no arguments)      run the mover (dashboard tab + SteamVR Input bindings)\n"
			"  --show-dashboard    open the dashboard on the mover's tab at startup\n"
			"  --selftest          move the live playspace through SteamVR, verify, restore, verify; exit 0 on success\n"
			"  --screenshot FILE   render the dashboard panel to a PNG (no SteamVR needed)\n"
			"  --install           install to ~/.local and register with SteamVR (starts with SteamVR)\n"
			"  --uninstall         undo --install\n" );
}

// Renders the panel with sample data, so layout can be checked without a headset.
static int Screenshot( const char *path )
{
	PanelModel m;
	m.state = MoverState::Ready;
	m.moved = true;
	m.offset = { 0.42, -0.10, 1.37 };
	m.yawDegrees = 45;
	m.bindings = { { "Grab and move", "Left Hand Bumper, Right Hand Bumper" },
		{ "Grab and turn", "Left Hand Thumbstick, Right Hand Thumbstick" },
		{ "Snap turn left", "Left Hand D-Pad Left" },
		{ "Snap turn right", "Left Hand D-Pad Right" },
		{ "Raise me", "Left Hand D-Pad Up" },
		{ "Lower me", "not bound" },
		{ "Reset to original", "Left Hand View" } };
	Canvas c( kPanelW, kPanelH );
	ButtonPanel bp;
	DrawPanel( c, bp, m, 1 );
	return c.WritePng( path ) ? 0 : 1;
}

// How the Frame launches things varies: Desktop Mode is a Plasma session nested in Game Mode (XDG_RUNTIME_DIR
// is /run/user/<uid>/nested_plasma) and Flatpak launchers redirect XDG_CONFIG_HOME, while SteamVR's own processes see
// the plain environment. OpenVR finds SteamVR through $XDG_CONFIG_HOME/openvr/openvrpaths.vrpath, so a redirected
// XDG_CONFIG_HOME makes VR_Init fail even though SteamVR is running. Put the variables back to what SteamVR sees.
static void NormalizeEnvironment()
{
	const char *home = getenv( "HOME" );
	if ( !home || home[0] != '/' )
	{
		const passwd *pw = getpwuid( getuid() );
		if ( pw && pw->pw_dir )
			setenv( "HOME", pw->pw_dir, 1 );
	}
	const char *xdg = getenv( "XDG_CONFIG_HOME" );
	home = getenv( "HOME" );
	if ( xdg && *xdg && home )
	{
		const std::string registry = "/openvr/openvrpaths.vrpath";
		if ( access( ( std::string( xdg ) + registry ).c_str(), R_OK ) != 0 && access( ( std::string( home ) + "/.config" + registry ).c_str(), R_OK ) == 0 )
			unsetenv( "XDG_CONFIG_HOME" );
	}
	const std::string runtime = "/run/user/" + std::to_string( getuid() );
	if ( access( runtime.c_str(), X_OK ) == 0 )
		setenv( "XDG_RUNTIME_DIR", runtime.c_str(), 1 );
}

int main( int argc, char **argv )
{
	NormalizeEnvironment();
	bool selftest = false, showDashboard = false;
	for ( int i = 1; i < argc; i++ )
	{
		if ( !strcmp( argv[i], "--install" ) )
			return InstallApp();
		if ( !strcmp( argv[i], "--uninstall" ) )
			return UninstallApp();
		if ( !strcmp( argv[i], "--screenshot" ) && i + 1 < argc )
			return Screenshot( argv[i + 1] );
		if ( !strcmp( argv[i], "--selftest" ) )
			selftest = true;
		else if ( !strcmp( argv[i], "--show-dashboard" ) )
			showDashboard = true;
		else
		{
			Usage();
			return strcmp( argv[i], "--help" ) == 0 ? 0 : 2;
		}
	}

	signal( SIGINT, OnSignal );
	signal( SIGTERM, OnSignal );
	signal( SIGUSR1, OnShowDashboard );

	if ( !selftest && !BecomeOnlyInstance() )
		return 0;
	const std::string backups = BackupChaperoneFile(); // before anything can touch the working copy
	if ( !selftest )
	{
		files::MakeDirs( files::Home() + "/.local/share/frame-mover" );
		logging::Open( files::Home() + "/.local/share/frame-mover/frame-mover.log" );
		logging::Printf( "frame-mover starting\n" );
	}
	if ( !backups.empty() )
		logging::Printf( "chaperone backups: %s\n", backups.c_str() );

	App app;
	std::string error;
	if ( !app.Init( error ) )
	{
		logging::Printf( "%s\n", error.c_str() );
		return 1;
	}
	if ( !selftest )
		Chaperone::RecoverSetting(); // we are the only running copy: nothing else can be holding the override
	if ( showDashboard )
		app.ShowDashboardPanel();
	return selftest ? app.SelfTest() : app.Run();
}
