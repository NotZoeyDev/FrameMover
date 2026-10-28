#include "panel_action.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <tuple>

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

auto Values( const Settings &s )
{
	return std::tie( s.dragMultiplier, s.snapDegrees, s.heightSpeed, s.lockHeight, s.leftHand, s.rightHand,
		s.haptics, s.doublePress, s.largeChaperone, s.disableChaperone, s.overrideGameInput );
}
}

int main()
{
	using A = PanelAction;
	auto setting = []( A action, auto field, auto value ) {
		Settings actual, expected;
		expected.*field = value;
		Check( ApplyPanelSetting( action, value, actual ), "setting action was rejected" );
		Check( Values( actual ) == Values( expected ), "action changed the wrong setting or another field" );
	};
	setting( A::DragMultiplier, &Settings::dragMultiplier, 2.25f );
	setting( A::SnapDegrees, &Settings::snapDegrees, 90.0f );
	setting( A::HeightSpeed, &Settings::heightSpeed, 1.7f );
	for ( bool value : { false, true } )
	{
		setting( A::LockHeight, &Settings::lockHeight, value );
		setting( A::LeftHand, &Settings::leftHand, value );
		setting( A::RightHand, &Settings::rightHand, value );
		setting( A::Haptics, &Settings::haptics, value );
		setting( A::DoublePress, &Settings::doublePress, value );
		setting( A::LargeChaperone, &Settings::largeChaperone, value );
		setting( A::DisableChaperone, &Settings::disableChaperone, value );
	}
	Settings original;
	original.overrideGameInput = true;
	for ( A action : { A::Reset, A::EditBindings, A::Raise, A::Turn, A::LanguageMenu, A::Language,
		(A)-1, (A)999, (A)std::numeric_limits<int>::min(), (A)std::numeric_limits<int>::max() } )
	{
		Settings actual = original;
		Check( !ApplyPanelSetting( action, 123, actual ), "non-setting action was accepted" );
		Check( Values( actual ) == Values( original ), "non-setting action changed settings" );
	}
	puts( "panel action tests passed" );
}
