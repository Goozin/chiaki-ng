// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <mouselook.h>
#include <settings.h>
#include <streamsession.h>

#include <QCoreApplication>
#include <QCursor>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <iterator>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(chiakiMouseLook, "chiaki.mouselook", QtInfoMsg);

#define MOUSELOOK_TICK_MS 4
// mouse speed (counts/s * sensitivity) that maps to full stick deflection
#define MOUSELOOK_FULL_SCALE 2500.0
#define MOUSELOOK_WHEEL_PRESS_MS 60
#define MOUSELOOK_CLIP_REFRESH_MS 500
// roughly one game frame at 60 fps
#define MOUSELOOK_HOLD_S 0.016

MouseLook::MouseLook(Settings *settings, QWindow *window, QObject *parent)
	: QObject(parent),
	settings(settings),
	window(window)
{
	sensitivity = settings->GetMouseLookSensitivity();
	curve = settings->GetMouseLookCurve();
	deadzone = settings->GetMouseLookDeadzone();
	smoothing = settings->GetMouseLookSmoothing();
	chiaki_controller_state_set_idle(&state);

	tick_timer = new QTimer(this);
	tick_timer->setTimerType(Qt::PreciseTimer);
	tick_timer->setInterval(MOUSELOOK_TICK_MS);
	connect(tick_timer, &QTimer::timeout, this, &MouseLook::Tick);

	wheel_release_timer = new QTimer(this);
	wheel_release_timer->setSingleShot(true);
	wheel_release_timer->setInterval(MOUSELOOK_WHEEL_PRESS_MS);
	connect(wheel_release_timer, &QTimer::timeout, this, [this]() {
		state.buttons &= ~wheel_buttons;
		wheel_buttons = 0;
		SendState();
	});

	QCoreApplication::instance()->installNativeEventFilter(this);
}

MouseLook::~MouseLook()
{
	QCoreApplication::instance()->removeNativeEventFilter(this);
#ifdef Q_OS_WIN
	ClipCursor(nullptr);
	if(raw_input_registered)
	{
		RAWINPUTDEVICE rid = {};
		rid.usUsagePage = 0x01; // generic desktop
		rid.usUsage = 0x02; // mouse
		rid.dwFlags = RIDEV_REMOVE;
		rid.hwndTarget = nullptr;
		RegisterRawInputDevices(&rid, 1, sizeof(rid));
	}
#endif
}

void MouseLook::SetSession(StreamSession *session)
{
	this->session = session;
	chiaki_controller_state_set_idle(&state);
	if(session && settings->GetMouseLookEnabled())
		enabled = true;
	else if(!session)
		enabled = false;
	UpdateCapture();
}

void MouseLook::Toggle()
{
	SetEnabled(!enabled);
}

void MouseLook::SetEnabled(bool enabled)
{
	this->enabled = enabled;
	qCInfo(chiakiMouseLook) << "Mouse look" << (enabled ? "enabled" : "disabled");
	UpdateCapture();
}

void MouseLook::SetSuspended(bool suspended)
{
	this->suspended = suspended;
	UpdateCapture();
}

void MouseLook::SetFocused(bool focused)
{
	this->focused = focused;
	UpdateCapture();
}

void MouseLook::UpdateCapture()
{
	bool capture = enabled && !suspended && focused && session;
	if(capture == capturing)
		return;
	capturing = capture;

	accum_x = accum_y = 0.0;
	std::fill(std::begin(window_dx), std::end(window_dx), 0.0);
	std::fill(std::begin(window_dy), std::end(window_dy), 0.0);
	std::fill(std::begin(window_dt), std::end(window_dt), 0.0);
	smooth_vx = smooth_vy = 0.0;
	last_out_x = last_out_y = 0.0;
	hold_remaining = 0.0;
	chiaki_controller_state_set_idle(&state);
	wheel_buttons = 0;
	wheel_release_timer->stop();

	if(capturing)
	{
#ifdef Q_OS_WIN
		if(!raw_input_registered)
		{
			RAWINPUTDEVICE rid = {};
			rid.usUsagePage = 0x01; // generic desktop
			rid.usUsage = 0x02; // mouse
			rid.dwFlags = 0;
			rid.hwndTarget = reinterpret_cast<HWND>(window->winId());
			raw_input_registered = RegisterRawInputDevices(&rid, 1, sizeof(rid));
			if(!raw_input_registered)
				qCWarning(chiakiMouseLook) << "Failed to register raw mouse input, error" << GetLastError();
		}
#endif
		window->setCursor(Qt::BlankCursor);
		ApplyCursorClip();
		tick_clock.start();
		tick_timer->start();
	}
	else
	{
		tick_timer->stop();
#ifdef Q_OS_WIN
		ClipCursor(nullptr);
#endif
		window->setCursor(Qt::ArrowCursor);
	}
	SendState();
}

void MouseLook::ApplyCursorClip()
{
#ifdef Q_OS_WIN
	// work in physical pixels from the native window so DPI scaling doesn't matter
	HWND hwnd = reinterpret_cast<HWND>(window->winId());
	RECT client;
	if(GetClientRect(hwnd, &client))
	{
		POINT center = { (client.right - client.left) / 2, (client.bottom - client.top) / 2 };
		ClientToScreen(hwnd, &center);
		RECT clip = { center.x, center.y, center.x + 1, center.y + 1 };
		ClipCursor(&clip);
	}
#else
	QPoint center_local(window->width() / 2, window->height() / 2);
	warping = true;
	QCursor::setPos(window->mapToGlobal(center_local));
	last_pos = center_local;
#endif
	clip_clock.start();
}

bool MouseLook::nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result)
{
	Q_UNUSED(result);
#ifdef Q_OS_WIN
	if(!capturing || event_type != "windows_generic_MSG")
		return false;
	MSG *msg = static_cast<MSG *>(message);
	if(msg->message != WM_INPUT)
		return false;

	RAWINPUT raw;
	UINT size = sizeof(raw);
	if(GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
		return false;
	if(raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
	{
		accum_x += raw.data.mouse.lLastX;
		accum_y += raw.data.mouse.lLastY;
	}
#else
	Q_UNUSED(event_type);
	Q_UNUSED(message);
#endif
	// let default processing continue so the raw input buffer is cleaned up
	return false;
}

bool MouseLook::HandleMouseEvent(QMouseEvent *event)
{
	if(!capturing)
		return false;

	switch(event->type())
	{
		case QEvent::MouseMove:
		{
#ifndef Q_OS_WIN
			QPointF pos = event->position();
			if(warping)
			{
				warping = false;
				last_pos = pos;
				break;
			}
			accum_x += pos.x() - last_pos.x();
			accum_y += pos.y() - last_pos.y();
			ApplyCursorClip();
#endif
			break;
		}
		case QEvent::MouseButtonPress:
		// Qt delivers the second press of a fast double click as DblClick only
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseButtonRelease:
		{
			bool pressed = event->type() != QEvent::MouseButtonRelease;
			switch(event->button())
			{
				case Qt::LeftButton:
					state.r2_state = pressed ? 0xff : 0;
					break;
				case Qt::RightButton:
					state.l2_state = pressed ? 0xff : 0;
					break;
				case Qt::MiddleButton:
					SetButton(CHIAKI_CONTROLLER_BUTTON_R3, pressed);
					break;
				case Qt::BackButton:
					SetButton(CHIAKI_CONTROLLER_BUTTON_L1, pressed);
					break;
				case Qt::ForwardButton:
					SetButton(CHIAKI_CONTROLLER_BUTTON_R1, pressed);
					break;
				default:
					break;
			}
			SendState();
			break;
		}
		default:
			break;
	}
	return true;
}

bool MouseLook::HandleWheelEvent(QWheelEvent *event)
{
	if(!capturing)
		return false;
	int delta = event->angleDelta().y();
	if(delta == 0)
		return true;
	uint32_t button = delta > 0 ? CHIAKI_CONTROLLER_BUTTON_DPAD_UP : CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
	state.buttons &= ~wheel_buttons;
	wheel_buttons = button;
	state.buttons |= button;
	wheel_release_timer->start();
	SendState();
	return true;
}

void MouseLook::SetButton(uint32_t button, bool pressed)
{
	if(pressed)
		state.buttons |= button;
	else
		state.buttons &= ~button;
}

void MouseLook::Tick()
{
	double dt = tick_clock.nsecsElapsed() / 1e9;
	tick_clock.restart();
	if(dt <= 0.0)
		return;

	// Windows may drop the clip (alt-tab, UAC, window move), so keep reapplying
	if(clip_clock.elapsed() > MOUSELOOK_CLIP_REFRESH_MS)
		ApplyCursorClip();

	window_dx[window_pos] = accum_x;
	window_dy[window_pos] = accum_y;
	window_dt[window_pos] = dt;
	window_pos = (window_pos + 1) % VELOCITY_WINDOW;
	accum_x = accum_y = 0.0;

	// averaging a few ticks evens out mice that report slower than the tick rate
	double sum_dx = 0.0, sum_dy = 0.0, sum_dt = 0.0;
	for(int i = 0; i < VELOCITY_WINDOW; i++)
	{
		sum_dx += window_dx[i];
		sum_dy += window_dy[i];
		sum_dt += window_dt[i];
	}
	double vx = sum_dx / sum_dt;
	double vy = sum_dy / sum_dt;
	smooth_vx = smooth_vx * smoothing + vx * (1.0 - smoothing);
	smooth_vy = smooth_vy * smoothing + vy * (1.0 - smoothing);

	double speed = std::hypot(smooth_vx, smooth_vy);
	double out_x = 0.0, out_y = 0.0;
	if(speed > 1.0)
	{
		double mag = speed * sensitivity / MOUSELOOK_FULL_SCALE;
		double shaped = std::min(1.0, std::pow(mag, (double)curve));
		// jump past the game's own stick deadzone so small movements register
		double out = deadzone + (1.0 - deadzone) * shaped;
		out_x = smooth_vx / speed * out;
		out_y = smooth_vy / speed * out;
		last_out_x = out_x;
		last_out_y = out_y;
		hold_remaining = MOUSELOOK_HOLD_S;
	}
	else
	{
		smooth_vx = smooth_vy = 0.0;
		if(hold_remaining > 0.0)
		{
			hold_remaining -= dt;
			out_x = last_out_x;
			out_y = last_out_y;
		}
	}

	int16_t right_x = (int16_t)std::lround(std::clamp(out_x, -1.0, 1.0) * 0x7fff);
	int16_t right_y = (int16_t)std::lround(std::clamp(out_y, -1.0, 1.0) * 0x7fff);
	if(right_x != state.right_x || right_y != state.right_y)
	{
		state.right_x = right_x;
		state.right_y = right_y;
		SendState();
	}
}

void MouseLook::SendState()
{
	if(session)
		session->SetMouseLookState(state);
}

void MouseLook::AdjustSensitivity(float factor)
{
	sensitivity = std::clamp(sensitivity * factor, 0.05f, 20.0f);
	settings->SetMouseLookSensitivity(sensitivity);
	ReportTuning();
}

void MouseLook::AdjustDeadzone(float delta)
{
	deadzone = std::clamp(deadzone + delta, 0.0f, 0.6f);
	settings->SetMouseLookDeadzone(deadzone);
	ReportTuning();
}

void MouseLook::AdjustCurve(float delta)
{
	curve = std::clamp(curve + delta, 0.3f, 3.0f);
	settings->SetMouseLookCurve(curve);
	ReportTuning();
}

void MouseLook::ReportTuning()
{
	QString text = QStringLiteral("Mouse look: sensitivity %1, deadzone %2, curve %3")
		.arg(sensitivity, 0, 'f', 2)
		.arg(deadzone, 0, 'f', 2)
		.arg(curve, 0, 'f', 2);
	qCInfo(chiakiMouseLook).noquote() << text;
	window->setTitle(text);
}
