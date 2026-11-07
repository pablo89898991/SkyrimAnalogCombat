#pragma once

#include "Config.h"

// Maszyna stanów gestu "analog combat" – kierunek na całym okręgu.
//
//  Idle --(LPM w dół, broń biała wyjęta)--> Charging
//  Charging --(ruch >= WindupThreshold)--> Armed(kąt zamachu)
//  Armed --(dalszy ruch w stronę zamachu / w bok)--> korekta kąta (płynnie, po okręgu)
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

	// Aktualne "wychylenie" broni do efektu wizualnego: wektor o długości 0..1
	// (x = prawo, y = góra). Podczas szarpnięcia w drugą stronę wraca do zera.
	struct Aim
	{
		float x = 0.0f;
		float y = 0.0f;
	};
	[[nodiscard]] Aim GetAim() const;

	// Czy gracz ma wyjętą broń białą w prawej ręce (i mod jest włączony).
	[[nodiscard]] static bool MeleeReady() { return CanUseDirectionalCombat(); }

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

	static float AngleDeg(const Vec& a_v);  // 0..360, 0 = prawo, 90 = góra
	static Vec   FromAngle(float a_deg);
	static int   Sector16(float a_deg);     // kąt -> sektor 0..15 z rozdzielczością Directions
	static const std::string& EventFor(int a_sector16);
	static bool  CanUseDirectionalCombat();
	static float Seconds(Clock::time_point a_from) { return std::chrono::duration<float>(Clock::now() - a_from).count(); }

	void Begin(bool a_gamepad);
	void Reset();
	void Arm(float a_deg);
	void Strike();
	void AfterStrike();
	void NormalAttack();
	[[nodiscard]] bool InCooldown() const;

	State             _state = State::Idle;
	bool              _gamepad = false;
	bool              _movedAtAll = false;
	Vec               _windup{};        // nagromadzony ruch zamachu
	Vec               _stick{};         // ostatnia pozycja prawego drążka
	float             _reverse = 0.0f;  // nagromadzony ruch w przeciwną stronę
	float             _angle = 0.0f;    // aktualny kąt zamachu (stopnie)
	int               _sector = 0;      // aktualny sektor 0..15
	Clock::time_point _pressTime{};
	Clock::time_point _lastStrike{};
	bool              _struckOnce = false;
};
