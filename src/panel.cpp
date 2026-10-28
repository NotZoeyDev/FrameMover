#include "panel.h"

#include "i18n.h"

#include <cmath>
#include <cstdio>

namespace
{
using i18n::Tr;
constexpr double kLeftX = 40, kLeftW = 740;
constexpr double kRightX = 840, kRightW = 720;

std::string Fmt( const char *f, double a )
{
	char b[64];
	snprintf( b, sizeof b, f, a );
	return b;
}

// Shortens `s` with an ellipsis until it fits `maxWidth` at the given size.
std::string Fit( Canvas &c, std::string s, double size, double maxWidth )
{
	if ( c.TextWidth( s, size ) <= maxWidth )
		return s;
	while ( !s.empty() && c.TextWidth( s + "...", size ) > maxWidth )
	{
		size_t end = s.size() - 1;
		while ( end && ( (unsigned char)s[end] & 0xc0 ) == 0x80 )
			--end;
		s.resize( end ); // Keep UTF-8 binding names valid when shortening them.
	}
	return s + "...";
}

// "label     [-]  value  [+]"
void Stepper( Canvas &c, ButtonPanel &bp, double y, const std::string &label, const std::string &value, PanelAction action,
	double dec, double inc )
{
	c.Text( Tr( label ), kLeftX, y + 34, 25, 0.85, 0.88, 0.92 );
	bp.Add( { 470, y, 70, 48, "-", false, action, dec } );
	c.Text( value, 550 + ( 160 - c.TextWidth( value, 25, true ) ) / 2, y + 34, 25, 1, 1, 1, true );
	bp.Add( { 710, y, 70, 48, "+", false, action, inc } );
}

void Toggle( ButtonPanel &bp, double x, double y, const std::string &label, bool on, PanelAction action )
{
	bp.Add( { x, y, 362, 50, Tr( label ) + ( on ? ":  ON" : ":  OFF" ), on, action, on ? 0.0 : 1.0 } );
}
} // namespace

void DrawPanel( Canvas &c, ButtonPanel &bp, const PanelModel &m, int hovered )
{
	const bool ja = i18n::Japanese();
	bp.Clear();
	c.Clear( 0.07, 0.08, 0.11 );

	// ---- left column: state, reset, quick moves, settings -------------------------------------------------------------
	c.Text( "FrameMover", kLeftX, 64, 42, 1, 1, 1, true );

	std::string status;
	double sr = 0.55, sg = 0.85, sb = 0.6;
	switch ( m.state )
	{
	case MoverState::Ready:
		status = m.settings.doublePress ? "Ready. Double-press and hold the grab button, then move your hand."
										: "Ready. Hold the grab button and move your hand.";
		break;
	case MoverState::PausedBySetup:
		status = "Paused: SteamVR is editing the chaperone (room setup).";
		sr = 0.95, sg = 0.75, sb = 0.3;
		break;
	default:
		status = "Waiting for SteamVR to report your playspace.";
		sr = 0.95, sg = 0.75, sb = 0.3;
		break;
	}
	c.Text( Fit( c, Tr( status ), 22, kLeftW ), kLeftX, 102, 22, sr, sg, sb );

	bp.Add( { kLeftX, 124, kLeftW, 84, Tr( "Reset to original playspace" ), false, PanelAction::Reset } );

	if ( m.moved )
	{
		const double dist = std::sqrt( m.offset.x * m.offset.x + m.offset.y * m.offset.y + m.offset.z * m.offset.z );
		const std::string pos = "(x " + Fmt( "%+.2f", m.offset.x ) + ", y " + Fmt( "%+.2f", m.offset.y ) + ", z " + Fmt( "%+.2f", m.offset.z ) + ")";
		const std::string yaw = Fmt( "%.1f", m.yawDegrees ) + "\xC2\xB0";
		c.Text( Fit( c,
					ja ? "移動 " + Fmt( "%.2f m", dist ) + " " + pos + "、回転 " + yaw
					   : "Moved " + Fmt( "%.2f m", dist ) + " " + pos + ", turned " + yaw,
					23, kLeftW ),
			kLeftX, 246, 23, 1, 1, 1 );
	}
	else
		c.Text( Tr( "Your playspace is exactly as you saved it." ), kLeftX, 246, 23, 0.7, 0.75, 0.8 );

	const Settings &s = m.settings;
	const std::string snap = Fmt( "%.0f", s.snapDegrees ) + "\xC2\xB0";
	bp.Add( { kLeftX, 266, 362, 58, Tr( "Raise me 5 cm" ), false, PanelAction::Raise, 0.05 } );
	bp.Add( { 418, 266, 362, 58, Tr( "Lower me 5 cm" ), false, PanelAction::Raise, -0.05 } );
	bp.Add( { kLeftX, 334, 362, 58, ( ja ? "左に回転 " : "Turn left " ) + snap, false, PanelAction::Turn, -s.snapDegrees } );
	bp.Add( { 418, 334, 362, 58, ( ja ? "右に回転 " : "Turn right " ) + snap, false, PanelAction::Turn, s.snapDegrees } );

	Stepper( c, bp, 424, "Grab speed", Fmt( "%.2f", s.dragMultiplier ) + "x",
		PanelAction::DragMultiplier, std::round( ( s.dragMultiplier - 0.25f ) * 4 ) / 4, std::round( ( s.dragMultiplier + 0.25f ) * 4 ) / 4 );
	Stepper( c, bp, 482, "Snap turn angle", snap, PanelAction::SnapDegrees, s.snapDegrees - 15, s.snapDegrees + 15 );
	Stepper( c, bp, 540, "Raise / lower speed", Fmt( "%.1f", s.heightSpeed ) + " m/s", PanelAction::HeightSpeed,
		s.heightSpeed - 0.1f, s.heightSpeed + 0.1f );

	Toggle( bp, kLeftX, 612, "Lock height", s.lockHeight, PanelAction::LockHeight );
	Toggle( bp, 418, 612, "Haptics", s.haptics, PanelAction::Haptics );
	Toggle( bp, kLeftX, 674, "Left hand grabs", s.leftHand, PanelAction::LeftHand );
	Toggle( bp, 418, 674, "Right hand grabs", s.rightHand, PanelAction::RightHand );

	bp.Add( { kLeftX, 736, kLeftW, 50, Tr( "Double-press + hold to grab" ) + ":  " + ( s.doublePress ? "ON" : "OFF" ), s.doublePress,
		PanelAction::DoublePress, s.doublePress ? 0.0 : 1.0 } );

	Toggle( bp, kLeftX, 794, "Huge chaperone", s.largeChaperone, PanelAction::LargeChaperone );
	Toggle( bp, 418, 794, "No chaperone", s.disableChaperone, PanelAction::DisableChaperone );

	c.Text( Tr( "Nothing is saved to SteamVR: Reset restores exactly the playspace" ), kLeftX, 868, 20, 0.55, 0.6, 0.66 );
	c.Text( Tr( "and chaperone you had set." ), kLeftX, 892, 20, 0.55, 0.6, 0.66 );

	// ---- right column: key binds ---------------------------------------------------------------------------------------
	c.Text( Tr( "Button bindings" ), kRightX, 64, 34, 1, 1, 1, true );
	if ( m.controllersConnected )
		c.Text( Tr( "SteamVR Input: changes you make in the editor show up here at once" ), kRightX, 102, 20, 0.55, 0.6, 0.66 );
	else
		c.Text( Tr( "No controller connected yet: bindings appear once SteamVR sees one." ), kRightX, 102, 20, 0.95, 0.75, 0.3 );
	double y = 150;
	if ( !m.inputError.empty() )
	{
		c.Text( Fit( c, Tr( "SteamVR Input unavailable: " ) + m.inputError, 20, kRightW ), kRightX, y, 20, 0.95, 0.5, 0.5 );
		y += 36;
	}
	for ( const auto &b : m.bindings )
	{
		const bool bound = b.second != "not bound" && b.second.rfind( "unavailable", 0 ) != 0;
		c.Text( Tr( b.first ), kRightX, y, 24, 0.95, 0.96, 0.98, true );
		c.Text( Fit( c, i18n::TrBinding( b.second ), 21, kRightW ), kRightX, y + 28, 21, bound ? 0.6 : 0.95, bound ? 0.8 : 0.6, bound ? 1.0 : 0.5 );
		y += 72;
	}
	bp.Add( { kRightX, 782, kRightW, 70, Tr( "Edit bindings in SteamVR" ), false, PanelAction::EditBindings } );

	bp.Draw( c, hovered );
}

void DrawGlyph( Canvas &c, double cx, double cy, double size )
{
	const double k = size / 112.0;
	cairo_t *cr = c.cr();
	cairo_save( cr );
	cairo_translate( cr, cx, cy );
	cairo_scale( cr, k, k );
	c.RoundRect( -56, -56, 112, 112, 24, 0.15, 0.42, 0.72 );
	cairo_set_source_rgb( cr, 1, 1, 1 );
	cairo_set_line_width( cr, 9 );
	cairo_set_line_cap( cr, CAIRO_LINE_CAP_ROUND );
	cairo_set_line_join( cr, CAIRO_LINE_JOIN_ROUND );
	for ( int i = 0; i < 4; i++ )
	{
		cairo_save( cr );
		cairo_rotate( cr, i * M_PI / 2 );
		cairo_move_to( cr, 0, -8 );
		cairo_line_to( cr, 0, -36 );
		cairo_stroke( cr );
		cairo_move_to( cr, -11, -27 );
		cairo_line_to( cr, 0, -38 );
		cairo_line_to( cr, 11, -27 );
		cairo_stroke( cr );
		cairo_restore( cr );
	}
	cairo_restore( cr );
}
