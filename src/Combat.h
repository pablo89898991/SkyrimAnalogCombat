#pragma once

#include "Config.h"

// Maszyna stanów gestu "analog combat".
//
//  Idle --(LPM w dół, broń biała wyjęta)--> Charging
//  Charging --(ruch >= WindupThreshold)--> Armed(kierunek zamachu)
//  Armed --(ruch w przeciwną stronę >= StrikeThreshold)--> CIOS
//  Armed --(puszczenie LPM, StrikeOnRelease)--> CIOS
//  Charging --(szybkie puszczenie bez ruchu)--> zwykły atak
//  po ciosie: ChainStrikes ? Charging (z cooldownem) : Spent (czeka na puszczenie)
class Combat
{
public:
	static Combat& Get()
	{
		static Combat c;
		return c;
	}

	// Każda funkcja zwraca true, jeśli zdarzenie ma zostać "połknięte" (nie trafi do gry).
	bool OnAttackButton(RE::ButtonEvent* a_event);
	bool OnMouseMove(std::int32_t a_dx, std::int32_t a_dy);
	bool OnRightStick(float a_x, float a_y);

	[[nodiscard]] bool Active() const noexcept { return _state != State::Idle; }

private:
	using Clock = std::chrono::steady_clock;

	enum class State : std::uint8_t
	{
		Idle,
		Charging,
		Armed,
		Spent
	};

	struct Vec
	{
		float x = 0.0f;
		float y = 0.0f;  // + = w górę

		[[nodiscard]] float Len() const { return std::sqrt(x * x + y * y); }
		[[nodiscard]] float Dot(const Vec& o) const { return x * o.x + y * o.y; }
	};

	static Dir   Snap(const Vec& a_v);
	static Vec   Unit(Dir a_d);
	static bool  CanUseDirectionalCombat();
	static float Seconds(Clock::time_point a_from) { return std::chrono::duration<float>(Clock::now() - a_from).count(); }

	void Begin(bool a_gamepad);
	void Reset();
	void Arm(Dir a_dir);
	void Strike();
	void AfterStrike();
	void NormalAttack();
	[[nodiscard]] bool InCooldown() const;

	State             _state = State::Idle;
	bool              _gamepad = false;
	bool              _movedAtAll = false;
	Vec               _windup{};        // nagromadzony ruch zamachu
	float             _reverse = 0.0f;  // nagromadzony ruch w przeciwną stronę
	Dir               _dir = Dir::Right;
	Clock::time_point _pressTime{};
	Clock::time_point _lastStrike{};
	bool              _struckOnce = false;
};
