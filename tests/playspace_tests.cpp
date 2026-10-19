// Behavioural tests for the playspace model. No SteamVR needed: ctest --test-dir build
#include "playspace.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

static int g_failures = 0;

#define CHECK( cond )                                                        \
	do                                                                       \
	{                                                                        \
		if ( !( cond ) )                                                     \
		{                                                                    \
			printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond );         \
			g_failures++;                                                    \
		}                                                                    \
	} while ( 0 )

static bool Near( Vec3 a, Vec3 b, double eps = 1e-9 )
{
	return std::fabs( a.x - b.x ) < eps && std::fabs( a.y - b.y ) < eps && std::fabs( a.z - b.z ) < eps;
}

// Zero pose M maps universe coordinates to raw coordinates: raw = R * u + t.
static Vec3 ToRaw( const Mat34 &m, Vec3 u )
{
	return { m.m[0][0] * u.x + m.m[0][1] * u.y + m.m[0][2] * u.z + m.m[0][3],
		m.m[1][0] * u.x + m.m[1][1] * u.y + m.m[1][2] * u.z + m.m[1][3],
		m.m[2][0] * u.x + m.m[2][1] * u.y + m.m[2][2] * u.z + m.m[2][3] };
}

static Vec3 ToUniverse( const Mat34 &m, Vec3 raw )
{
	const Vec3 d = raw - m.translation();
	return { m.m[0][0] * d.x + m.m[1][0] * d.y + m.m[2][0] * d.z, m.m[0][1] * d.x + m.m[1][1] * d.y + m.m[2][1] * d.z,
		m.m[0][2] * d.x + m.m[1][2] * d.y + m.m[2][2] * d.z };
}

// A non-trivial original playspace: yawed 2.04 rad (like the real chaperone_info.vrchap) and offset.
static Mat34 YawedPose( double yaw, Vec3 t )
{
	Mat34 m;
	for ( int col = 0; col < 3; col++ )
	{
		Vec3 unit;
		( col == 0 ? unit.x : col == 1 ? unit.y : unit.z ) = 1;
		const Vec3 r = RotateY( unit, yaw );
		m.m[0][col] = r.x;
		m.m[1][col] = r.y;
		m.m[2][col] = r.z;
	}
	m.m[0][3] = t.x;
	m.m[1][3] = t.y;
	m.m[2][3] = t.z;
	return m;
}

static Playspace MakePlayspace()
{
	Playspace ps;
	ps.Rebase( YawedPose( 2.04, { -0.14, -1.16, -1.15 } ), YawedPose( -0.16, { -0.51, -0.39, -0.99 } ) );
	return ps;
}

// The point of the virtual world that is under the hand when the grab starts must still be under the hand afterwards.
static void TestDragKeepsWorldUnderHand()
{
	Playspace ps = MakePlayspace();
	DragGesture g;
	const Vec3 hand0 = { 0.3, 1.1, -0.4 };
	const Vec3 world = ToUniverse( ps.Standing(), hand0 );
	const Vec3 seatedWorld = ToUniverse( ps.Seated(), hand0 );

	g.Update( true, true, hand0, 1.0, false, ps );
	CHECK( Near( ps.Offset(), {} ) ); // the first sample only anchors
	Vec3 hand = hand0;
	for ( int i = 1; i <= 50; i++ ) // wiggly path
	{
		hand = { hand0.x + 0.02 * i, hand0.y + 0.01 * ( i % 7 ), hand0.z - 0.015 * i };
		g.Update( true, true, hand, 1.0, false, ps );
	}
	CHECK( Near( ToRaw( ps.Standing(), world ), hand ) );
	CHECK( Near( ToRaw( ps.Seated(), seatedWorld ), hand ) ); // seated universe moves with it
	CHECK( ps.Moved() );
}

static void TestDragMultiplierAndHeightLock()
{
	Playspace ps = MakePlayspace();
	DragGesture g;
	g.Update( true, true, { 0, 1, 0 }, 2.0, true, ps );
	g.Update( true, true, { 0.1, 1.5, -0.2 }, 2.0, true, ps );
	CHECK( Near( ps.Offset(), { 0.2, 0.0, -0.4 } ) ); // doubled, height untouched
}

static void TestDragPausesOnTrackingLossWithoutJump()
{
	Playspace ps = MakePlayspace();
	DragGesture g;
	g.Update( true, true, { 0, 1, 0 }, 1.0, false, ps );
	g.Update( true, true, { 0.1, 1, 0 }, 1.0, false, ps );
	g.Update( true, false, { 9, 9, 9 }, 1.0, false, ps ); // pose lost, garbage position
	CHECK( !g.Active() );
	g.Update( true, true, { 5, 1, 5 }, 1.0, false, ps ); // far away on return: re-anchor, no move
	CHECK( Near( ps.Offset(), { 0.1, 0, 0 } ) );
	g.Update( true, true, { 5.2, 1, 5 }, 1.0, false, ps );
	CHECK( Near( ps.Offset(), { 0.3, 0, 0 } ) );
}

static void TestReleaseStopsDrag()
{
	Playspace ps = MakePlayspace();
	DragGesture g;
	g.Update( true, true, { 0, 1, 0 }, 1.0, false, ps );
	g.Update( false, true, { 1, 1, 0 }, 1.0, false, ps );
	g.Update( true, true, { 2, 1, 0 }, 1.0, false, ps ); // new grab: anchors again
	CHECK( Near( ps.Offset(), {} ) );
}

// Hand orbits the head: the world point under the hand follows it, and the point under the head does not move.
static void TestTurnOrbitsHeadAndKeepsHeadFixed()
{
	Playspace ps = MakePlayspace();
	TurnGesture g;
	const Vec3 head = { 0.5, 1.6, -0.3 };
	const Vec3 hand0 = head + Vec3{ 0.0, -0.4, 0.5 };
	const Vec3 worldUnderHand = ToUniverse( ps.Standing(), hand0 );
	const Vec3 worldUnderHead = ToUniverse( ps.Standing(), head );

	g.Update( true, true, hand0, head, ps );
	const double total = 1.3; // radians, counter-clockwise seen as atan2(x, z) increasing
	Vec3 hand = hand0;
	for ( int i = 1; i <= 26; i++ )
		hand = head + RotateY( hand0 - head, total * i / 26.0 ), g.Update( true, true, hand, head, ps );

	CHECK( Near( ToRaw( ps.Standing(), worldUnderHand ), hand, 1e-9 ) );
	CHECK( Near( ToRaw( ps.Standing(), worldUnderHead ), head, 1e-9 ) );
	CHECK( std::fabs( ps.YawDegrees() - total * 180.0 / M_PI ) < 1e-9 );
}

static void TestTurnCrossesPiBoundary()
{
	Playspace ps = MakePlayspace();
	TurnGesture g;
	const Vec3 head = {};
	// hand behind the user, sweeping through atan2's +-pi seam
	g.Update( true, true, RotateY( { 0, 0, -0.5 }, -0.1 ), head, ps );
	g.Update( true, true, RotateY( { 0, 0, -0.5 }, 0.1 ), head, ps );
	CHECK( std::fabs( ps.YawDegrees() - 0.2 * 180.0 / M_PI ) < 1e-9 );
}

static void TestTurnIgnoresHandOnTheHeadAndGlitches()
{
	Playspace ps = MakePlayspace();
	TurnGesture g;
	const Vec3 head = {};
	g.Update( true, true, { 0.01, 0, 0.01 }, head, ps ); // too close to the head
	CHECK( !g.Active() );
	g.Update( true, true, { 0, 0, 0.5 }, head, ps );
	g.Update( true, true, { 0, 0, -0.5 }, head, ps ); // 180 degrees in one sample: tracking glitch
	CHECK( !ps.Moved() );
}

static void TestRotateAboutPivotComposesWithDrag()
{
	Playspace ps = MakePlayspace();
	const Vec3 pivot = { 2, 0, 3 };
	const Vec3 worldAtPivot = ToUniverse( ps.Standing(), pivot );
	ps.Translate( { 1, 0, 0 } );
	const Vec3 pivotAfterTranslate = ToRaw( ps.Standing(), worldAtPivot );
	ps.RotateAbout( pivotAfterTranslate, M_PI / 2 );
	CHECK( Near( ToRaw( ps.Standing(), worldAtPivot ), pivotAfterTranslate ) ); // pivot is a fixed point
	// a quarter turn about the vertical axis maps raw +Z direction to +X
	CHECK( Near( RotateY( { 0, 0, 1 }, M_PI / 2 ), { 1, 0, 0 } ) );
}

static void TestResetRestoresOriginalExactly()
{
	Playspace ps = MakePlayspace();
	const Mat34 s0 = ps.Standing(), q0 = ps.Seated();
	DragGesture d;
	TurnGesture t;
	d.Update( true, true, { 0, 1, 0 }, 1.0, false, ps );
	d.Update( true, true, { 3, 2, -1 }, 1.0, false, ps );
	t.Update( true, true, { 0, 1, 0.5 }, {}, ps );
	t.Update( true, true, { 0.3, 1, 0.4 }, {}, ps );
	CHECK( ps.Moved() );
	ps.Reset();
	CHECK( !ps.Moved() );
	for ( int r = 0; r < 3; r++ )
		for ( int c = 0; c < 4; c++ )
		{
			CHECK( ps.Standing().m[r][c] == s0.m[r][c] );
			CHECK( ps.Seated().m[r][c] == q0.m[r][c] );
		}
}

static void TestRebaseDiscardsMovement()
{
	Playspace ps = MakePlayspace();
	ps.Translate( { 1, 2, 3 } );
	ps.Rebase( YawedPose( 0.5, { 7, 8, 9 } ), YawedPose( 0, {} ) );
	CHECK( !ps.Moved() );
	CHECK( Near( ps.Standing().translation(), { 7, 8, 9 } ) );
}

// SteamVR relocalizes and hands us a new saved playspace: the user's move must still apply, to the new one.
static void TestRebaselineKeepsMovement()
{
	Playspace ps = MakePlayspace();
	ps.Translate( { 0.4, -0.7, 1.3 } );
	ps.RotateAbout( { 0.2, 1.6, -0.5 }, 0.8 );
	const Vec3 offset = ps.Offset();
	const double yaw = ps.YawDegrees();
	const Mat34 newStanding = YawedPose( -0.9, { 3, -1.4, 2 } ), newSeated = YawedPose( 0.3, { 2.5, -0.2, 2.2 } );
	ps.Rebaseline( newStanding, newSeated );
	CHECK( ps.Moved() );
	CHECK( Near( ps.Offset(), offset ) && std::fabs( ps.YawDegrees() - yaw ) < 1e-9 );
	CHECK( Near( ps.BaseStanding().translation(), newStanding.translation() ) );
	const double a = yaw * M_PI / 180.0;
	for ( Vec3 u : { Vec3{ 0, 0, 0 }, Vec3{ 1, 0.5, -2 }, Vec3{ -3, 1.8, 0.4 } } )
	{
		CHECK( Near( ToRaw( ps.Standing(), u ), RotateY( ToRaw( newStanding, u ), a ) + offset ) );
		CHECK( Near( ToRaw( ps.Seated(), u ), RotateY( ToRaw( newSeated, u ), a ) + offset ) );
	}
	ps.Reset();
	CHECK( Near( ps.Standing().translation(), newStanding.translation() ) ); // Reset goes back to the new baseline
}

static void TestRangeLimit()
{
	Playspace ps = MakePlayspace();
	CHECK( ps.InRange() );
	ps.Translate( { 39000, 0, 0 } );
	CHECK( ps.InRange() );
	ps.Translate( { 2000, 0, 0 } );
	CHECK( !ps.InRange() );
	ps.Reset();
	ps.Translate( { std::nan( "" ), 0, 0 } );
	CHECK( !ps.InRange() );
}


// Feeds a press/release script to a gate: each entry is {time, held}; returns the gated output at every entry.
static std::vector<bool> RunGate( const std::vector<std::pair<double, bool>> &script )
{
	DoublePressGate g;
	std::vector<bool> out;
	for ( const auto &s : script )
		out.push_back( g.Update( s.second, s.first ) );
	return out;
}

static void TestDoublePressActivatesOnlyOnSecondHeldPress()
{
	// tap (0.00-0.10), second press at 0.25 held until 1.5
	auto o = RunGate( { { 0.00, true }, { 0.10, false }, { 0.25, true }, { 0.60, true }, { 1.50, true }, { 1.60, false } } );
	CHECK( !o[0] && !o[1] );              // the first tap never activates
	CHECK( o[2] && o[3] && o[4] );        // second press: active for as long as it is held
	CHECK( !o[5] );                       // released
}

static void TestSinglePressAndLongFirstPressDoNotActivate()
{
	auto single = RunGate( { { 0.0, true }, { 0.5, true }, { 2.0, true }, { 2.1, false } } );
	for ( bool b : single )
		CHECK( !b );
	// first press held longer than the window is not a tap, so the next press is just a first press again
	auto slow = RunGate( { { 0.0, true }, { 1.0, false }, { 1.1, true }, { 1.5, true } } );
	for ( bool b : slow )
		CHECK( !b );
}

static void TestDoublePressNeedsSecondPressWithinWindow()
{
	auto late = RunGate( { { 0.0, true }, { 0.1, false }, { 0.8, true }, { 1.2, true } } ); // gap 0.7 s
	for ( bool b : late )
		CHECK( !b );
}

static void TestAfterDoublePressThirdPressIsAFirstPressAgain()
{
	auto o = RunGate( { { 0.0, true }, { 0.1, false }, { 0.2, true }, { 0.3, true }, { 0.4, false },  // double, active at 0.2/0.3
		{ 0.5, true }, { 0.9, true }, { 1.0, false } } );                                         // right after: a plain press
	CHECK( o[2] && o[3] );
	CHECK( !o[5] && !o[6] );
	// but a tap after that, then a second press, works again
	auto again = RunGate( { { 0.0, true }, { 0.1, false }, { 0.2, true }, { 0.3, false }, { 2.0, true }, { 2.1, false }, { 2.2, true }, { 2.6, true } } );
	CHECK( again[6] && again[7] );
}

// SteamVR pinning the floor height (x, z, yaw ours, height replaced) must be told apart from any other replacement, because
// only that one makes the mover re-anchor (commit) the saved playspace.
static void TestOnlyHeightDiffersSeparatesPinnedFloorFromOtherReplacements()
{
	const Mat34 ours = YawedPose( 0.9, { 0.22, -1.26, 0.06 } );
	Mat34 pinned = ours;
	pinned.m[1][3] = -1.726; // the saved floor SteamVR forced on the device on 2026-10-19
	CHECK( OnlyHeightDiffers( pinned, ours, 0.01, 0.01 ) );
	CHECK( !OnlyHeightDiffers( ours, ours, 0.01, 0.01 ) ); // nothing differs: nothing to fix
	Mat34 shifted = pinned;
	shifted.m[2][3] += 0.05; // the whole saved pose came back, not only the floor
	CHECK( !OnlyHeightDiffers( shifted, ours, 0.01, 0.01 ) );
	Mat34 turned = YawedPose( 0.9 + 0.05, { 0.22, -1.726, 0.06 } );
	CHECK( !OnlyHeightDiffers( turned, ours, 0.01, 0.01 ) );
	Mat34 tiny = ours;
	tiny.m[1][3] += 0.005; // below the tolerance: float noise, not pinning
	CHECK( !OnlyHeightDiffers( tiny, ours, 0.01, 0.01 ) );
}

// The live zero pose recovered from a device's raw and universe poses equals the zero pose that produced them, which is how
// the app notices SteamVR silently dropped the preview.
static void TestZeroPoseRecoveredFromDevicePoses()
{
	const Mat34 z = YawedPose( 2.04, { -0.14, -1.16, -1.15 } );
	const Mat34 raw = YawedPose( 0.7, { 0.3, 1.5, -0.4 } ); // the headset in raw space
	const Mat34 uni = YawedPose( 0.7 - 2.04, ToUniverse( z, raw.translation() ) ); // the same headset in the universe
	CHECK( NearPose( ZeroPoseFromPoses( raw, uni ), z, 1e-9, 1e-9 ) );
	Playspace ps = MakePlayspace();
	ps.Translate( { 0, 0.3, 0 } ); // a floor-height move is different from the original and is detected as such
	CHECK( !NearPose( ps.Standing(), MakePlayspace().Standing(), 0.01, 0.01 ) );
	CHECK( NearPose( ps.Standing(), ps.Standing(), 0.01, 0.01 ) );
	ps.Reset();
	CHECK( NearPose( ps.Standing(), MakePlayspace().Standing(), 1e-12, 1e-12 ) );
	Playspace turned = MakePlayspace();
	turned.RotateAbout( {}, 0.05 ); // 2.9 degrees, no translation of the origin's height
	CHECK( !NearPose( turned.Standing(), MakePlayspace().Standing(), 0.01, 0.01 ) );
}

// Compare the reduced transform with the original column-by-column algorithm, including tilted bases.
static void TestTransformEquivalence()
{
	for ( int sample = 0; sample < 200; ++sample )
	{
		Mat34 base = YawedPose( sample * 0.17, { sample * 0.3, -1.2, -sample * 0.13 } );
		const double pitch = sample * 0.11, c = std::cos( pitch ), s = std::sin( pitch );
		for ( int col = 0; col < 3; ++col )
		{
			const double y = base.m[1][col], z = base.m[2][col];
			base.m[1][col] = c * y - s * z;
			base.m[2][col] = s * y + c * z;
		}
		Playspace ps;
		ps.Rebase( base, base );
		ps.Translate( { 0.2, -0.4, 1.7 } );
		const double yaw = ( sample - 100 ) * 0.61;
		ps.RotateAbout( { -1.5, 1.6, 0.7 }, yaw );
		Mat34 expected;
		for ( int col = 0; col < 4; ++col )
		{
			Vec3 v = RotateY( { base.m[0][col], base.m[1][col], base.m[2][col] }, yaw );
			if ( col == 3 )
				v = v + ps.Offset();
			expected.m[0][col] = v.x;
			expected.m[1][col] = v.y;
			expected.m[2][col] = v.z;
		}
		CHECK( NearPose( ps.Standing(), expected, 1e-10, 1e-10 ) );
		CHECK( NearPose( ps.Seated(), expected, 1e-10, 1e-10 ) );
		CHECK( ps.InRange() );
		CHECK( NearPose( base, base, 0, 0 ) );
	}
}

static void TestPoseComparisonBoundaries()
{
	const Mat34 identity;
	CHECK( NearPose( identity, YawedPose( 0.009, {} ), 0, 0.01 ) );
	CHECK( !NearPose( identity, YawedPose( 0.011, {} ), 0, 0.01 ) );
	CHECK( NearPose( YawedPose( M_PI - 0.001, {} ), YawedPose( -M_PI + 0.001, {} ), 0, 0.003 ) );
	CHECK( NearPose( identity, YawedPose( M_PI, {} ), 0, M_PI ) );
	CHECK( !NearPose( identity, identity, -1, 0 ) );
	CHECK( !NearPose( identity, identity, 0, -1 ) );
	CHECK( !NearPose( identity, identity, std::nan( "" ), 0 ) );
	CHECK( !NearPose( identity, identity, 0, INFINITY ) );
	Mat34 invalid;
	invalid.m[0][0] = std::nan( "" );
	CHECK( !NearPose( invalid, identity, 0.01, 0.01 ) );
	invalid = identity;
	invalid.m[0][3] = INFINITY;
	CHECK( !NearPose( invalid, identity, 0.01, 0.01 ) );
}

int main()
{
	TestTransformEquivalence();
	TestPoseComparisonBoundaries();
	TestZeroPoseRecoveredFromDevicePoses();
	TestDoublePressActivatesOnlyOnSecondHeldPress();
	TestSinglePressAndLongFirstPressDoNotActivate();
	TestDoublePressNeedsSecondPressWithinWindow();
	TestAfterDoublePressThirdPressIsAFirstPressAgain();
	TestDragKeepsWorldUnderHand();
	TestDragMultiplierAndHeightLock();
	TestDragPausesOnTrackingLossWithoutJump();
	TestReleaseStopsDrag();
	TestTurnOrbitsHeadAndKeepsHeadFixed();
	TestTurnCrossesPiBoundary();
	TestTurnIgnoresHandOnTheHeadAndGlitches();
	TestRotateAboutPivotComposesWithDrag();
	TestResetRestoresOriginalExactly();
	TestRebaseDiscardsMovement();
	TestRebaselineKeepsMovement();
	TestOnlyHeightDiffersSeparatesPinnedFloorFromOtherReplacements();
	TestRangeLimit();
	if ( g_failures )
	{
		printf( "%d check(s) failed\n", g_failures );
		return 1;
	}
	printf( "all playspace tests passed\n" );
	return 0;
}
