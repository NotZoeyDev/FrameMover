#pragma once
// Pure playspace model: no OpenVR types, so it can be unit tested without SteamVR.
//
// Coordinate frame: OpenVR "raw tracking space" (right handed, +Y up, gravity aligned). The playspace is the
// standing/seated zero pose expressed in that space (what IVRChaperoneSetup::GetWorking*ZeroPoseToRawTrackingPose
// returns). Moving the playspace means replacing a zero pose M with D(M), where D is a yaw rotation plus a
// translation applied in raw space. The chaperone bounds stay anchored to the real room (verified on the Frame), so
// only the virtual world moves.

#include <cmath>

struct Vec3
{
	double x = 0, y = 0, z = 0;
};

inline Vec3 operator+( Vec3 a, Vec3 b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-( Vec3 a, Vec3 b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*( Vec3 a, double s ) { return { a.x * s, a.y * s, a.z * s }; }

// Row-major 3x4 rigid transform, same layout as vr::HmdMatrix34_t.
struct Mat34
{
	double m[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	Vec3 translation() const { return { m[0][3], m[1][3], m[2][3] }; }
};

// The zero pose SteamVR is *really* using, recovered from one device's pose in raw space and in the universe:
// raw = Z * universe  =>  Z = raw * universe^-1. Both poses must be the same device at the same moment.
Mat34 ZeroPoseFromPoses( const Mat34 &deviceInRaw, const Mat34 &deviceInUniverse );
// True when two poses differ by less than `posTol` meters and `rotTolRad` radians.
bool NearPose( const Mat34 &a, const Mat34 &b, double posTol, double rotTolRad );

// True when `a` and `b` agree within the tolerances in x, z and rotation but not in height: what SteamVR does when it
// pins the floor height of a relocalized playspace onto our zero pose.
bool OnlyHeightDiffers( const Mat34 &a, const Mat34 &b, double posTol, double rotTolRad );
// Rotation about +Y by `a` radians. Increases atan2(x, z) of a point by `a`.
Vec3 RotateY( Vec3 v, double a );

// Largest translation component OpenVR accepts in a working-copy zero pose (as used by OVR-AS).
constexpr double kMaxOffset = 39900.0;

// D(p) = RotateY(yaw, p) + t.
class Playspace
{
public:
	// Captures the untouched zero poses (what the user had set) and clears all movement.
	void Rebase( const Mat34 &standing, const Mat34 &seated );
	// Swaps in new untouched zero poses but keeps the movement: the same D now applies to the new poses.
	void Rebaseline( const Mat34 &standing, const Mat34 &seated );
	// Back to the captured poses.
	void Reset();

	void Translate( Vec3 d );
	// Rotates the world about the vertical axis through `pivot` (raw space) by `angle` radians.
	void RotateAbout( Vec3 pivot, double angle );

	Mat34 Standing() const { return Apply( m_standing ); }
	Mat34 Seated() const { return Apply( m_seated ); }
	const Mat34 &BaseStanding() const { return m_standing; }
	const Mat34 &BaseSeated() const { return m_seated; }

	bool Moved() const { return m_yaw != 0 || m_t.x != 0 || m_t.y != 0 || m_t.z != 0; }
	Vec3 Offset() const { return m_t; }
	double YawDegrees() const { return m_yaw * 180.0 / M_PI; }
	// False once a result would leave the range OpenVR accepts.
	bool InRange() const;

private:
	Mat34 Apply( const Mat34 &base ) const;
	Mat34 m_standing, m_seated;
	double m_yaw = 0; // radians, unbounded
	Vec3 m_t;
};

// "Grab the world": while held, the world follows the hand's translation. Hand positions are raw-space, so the
// result does not feed back into the measurement.
class DragGesture
{
public:
	// `valid` false (pose lost) keeps the gesture paused and re-anchors on the next valid sample.
	void Update( bool held, bool valid, Vec3 handRaw, double multiplier, bool lockHeight, Playspace &ps );
	bool Active() const { return m_active; }

private:
	bool m_active = false;
	Vec3 m_last;
};

// "Grab and turn": the world rotates about the head by the hand's change of heading around the head.
class TurnGesture
{
public:
	void Update( bool held, bool valid, Vec3 handRaw, Vec3 hmdRaw, Playspace &ps );
	bool Active() const { return m_active; }

private:
	bool m_active = false;
	double m_lastAngle = 0;
};

// OVR Advanced Settings style activation: tap, release, then press again and keep holding. The gated output is true only
// for that second, held press, so an ordinary single press (a game's own use of the button) never activates it.
class DoublePressGate
{
public:
	static constexpr double kWindow = 0.4; // seconds: longest tap, and longest gap between the tap and the second press
	// `held` is the raw button state; returns whether the gated action is active. `now` in seconds, monotonic.
	bool Update( bool held, double now );

private:
	bool m_prevHeld = false;
	bool m_active = false;
	bool m_lastWasTap = false;
	double m_pressedAt = -1e9;
	double m_releasedAt = -1e9;
};
