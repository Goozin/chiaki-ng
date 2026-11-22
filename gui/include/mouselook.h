// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MOUSELOOK_H
#define CHIAKI_MOUSELOOK_H

#include <chiaki/controller.h>

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>

class QWindow;
class QMouseEvent;
class QWheelEvent;
class Settings;
class StreamSession;

// Turns relative mouse movement into right analog stick deflection (and mouse
// buttons into controller buttons), so a mouse can be used to aim/look.
// On Windows raw input is used so OS pointer acceleration doesn't apply.
class MouseLook : public QObject, public QAbstractNativeEventFilter
{
	Q_OBJECT

	public:
		MouseLook(Settings *settings, QWindow *window, QObject *parent = nullptr);
		~MouseLook() override;

		void SetSession(StreamSession *session);

		// user toggle (hotkey); actual capture also requires !suspended and focus
		void Toggle();
		void SetEnabled(bool enabled);
		bool GetEnabled() const { return enabled; }

		// suspended while a chiaki menu grabs input
		void SetSuspended(bool suspended);
		void SetFocused(bool focused);
		bool IsCapturing() const { return capturing; }

		// returns true if the event was consumed
		bool HandleMouseEvent(QMouseEvent *event);
		bool HandleWheelEvent(QWheelEvent *event);

		void AdjustSensitivity(float factor);
		void AdjustDeadzone(float delta);
		void AdjustCurve(float delta);

		bool nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result) override;

	private:
		void UpdateCapture();
		void ApplyCursorClip();
		void Tick();
		void SendState();
		void SetButton(uint32_t button, bool pressed);
		void ReportTuning();

		Settings *settings;
		QWindow *window;
		StreamSession *session = nullptr;

		bool enabled = false;
		bool suspended = false;
		bool focused = true;
		bool capturing = false;
		bool raw_input_registered = false;

		float sensitivity;
		float curve;
		float deadzone;
		float smoothing;

		// accumulated raw counts since last tick
		double accum_x = 0.0;
		double accum_y = 0.0;
		// sliding window of recent ticks for velocity estimation
		static constexpr int VELOCITY_WINDOW = 2;
		double window_dx[VELOCITY_WINDOW] = {};
		double window_dy[VELOCITY_WINDOW] = {};
		double window_dt[VELOCITY_WINDOW] = {};
		int window_pos = 0;
		double smooth_vx = 0.0;
		double smooth_vy = 0.0;
		// last deflection, held briefly after movement stops so the game samples short flicks
		double last_out_x = 0.0;
		double last_out_y = 0.0;
		double hold_remaining = 0.0;

		ChiakiControllerState state;
		QTimer *tick_timer;
		QTimer *wheel_release_timer;
		uint32_t wheel_buttons = 0;
		QElapsedTimer tick_clock;
		QElapsedTimer clip_clock;
		QPointF last_pos;
		bool warping = false;
};

#endif // CHIAKI_MOUSELOOK_H
