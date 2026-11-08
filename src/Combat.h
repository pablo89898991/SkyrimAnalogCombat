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
// Co hook ma zrobić z przyciskiem ataku.
enum class ButtonAction : std::uint8_t
{
	Pass,           // przekaż do gry bez zmian
	Swallow,        // połknij
	PassAsPress,    // przekaż jako świeże wciśnięcie (start naciągania / strzał / czar)
	PressThenPass   // najpierw sztuczne wciśnięcie, potem to zdarzenie (szybkie kliknięcie)
};

enum class RangedKind : std::uint8_t
{
	None,
	Bow,
	Crossbow,
	Staff
};

// Jedna instancja na każdą rękę: Get(false) = prawa (LPM), Get(true) = lewa (PPM).
// Mysz/drążek trafia do ręki, która ostatnio zaczęła gest (ActiveHand).
class Combat
{
public:
	static Combat& Get(bool a_left = false)
	{
		static Combat right{ false };
		static Combat left{ true };
		return a_left ? left : right;
	}

	// Ręka, która aktualnie "trzyma" mysz (gest w toku), albo nullptr.
	static Combat* ActiveHand() noexcept { return s_active; }

	// Tarcza: szybkie pchnięcie myszy podczas bloku = uderzenie tarczą.
	// Zwraca true, jeśli ruch ma zostać połknięty.
	static bool OnShieldMotion(std::int32_t a_dx, std::int32_t a_dy);
	static bool OnShieldStick(float a_x, float a_y);

	// Po każdym geście kamera przez chwilę ignoruje ruch (końcówka szarpnięcia/pchnięcia),
	// a przy padzie – dopóki drążek nie wróci do środka.
	static void StartSettle(bool a_stick);
	static bool SwallowMouse();
	static bool SwallowStick(float a_x, float a_y);

	[[nodiscard]] bool IsLeft() const noexcept { return _left; }

	// Każda funkcja zwraca true, jeśli zdarzenie ma zostać "połknięte" (nie trafi do gry).
	ButtonAction OnAttackButton(RE::ButtonEvent* a_event);
	bool OnMouseMove(std::int32_t a_dx, std::int32_t a_dy);
	bool OnRightStick(float a_x, float a_y);

	// Wywoływane co klatkę: ponawia zbuforowany cios, aż gra go przyjmie.
	void Update(float a_delta);

	[[nodiscard]] bool Active() const noexcept { return _state != State::Idle; }

	// Aktualne "wychylenie" broni do efektu wizualnego: wektor o długości 0..1
	// (x = prawo, y = góra). Podczas szarpnięcia w drugą stronę wraca do zera.
	struct Aim
	{
		float x = 0.0f;
		float y = 0.0f;
	};
	[[nodiscard]] Aim GetAim() const;

	// Ostatni wyprowadzony cios – do animacji proceduralnej.
	[[nodiscard]] std::uint32_t StrikeId() const noexcept { return _strikeId; }
	[[nodiscard]] float         StrikeAngle() const noexcept { return _strikeAngle; }
	[[nodiscard]] bool          StrikePower() const noexcept { return _strikePower; }

	// Zamach przytrzymany wystarczająco długo – następny cios będzie ciosem mocy.
	[[nodiscard]] bool Charged() const;

	// Czy gracz ma wyjętą broń białą w danej ręce (i mod jest włączony).
	[[nodiscard]] static bool MeleeReady(bool a_left) { return CanUseDirectionalCombat(a_left); }

	// Jaka broń dystansowa jest w danej ręce (lewa: tylko kostur).
	[[nodiscard]] static RangedKind CurrentRanged(bool a_left);

	// Czy w lewej ręce jest tarcza (i obsługa tarczy włączona).
	[[nodiscard]] static bool ShieldReady();

private:
	using Clock = std::chrono::steady_clock;

	explicit Combat(bool a_left) :
		_left(a_left)
	{}

	const bool             _left;
	static inline Combat*  s_active = nullptr;
	static inline std::chrono::steady_clock::time_point s_settleUntil{};
	static inline bool                                  s_stickLatch = false;

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
	static bool  CanUseDirectionalCombat(bool a_left);
	[[nodiscard]] bool MeleeHere() const { return CanUseDirectionalCombat(_left); }
	[[nodiscard]] const std::string& HandFallback() const;
	static float Seconds(Clock::time_point a_from) { return std::chrono::duration<float>(Clock::now() - a_from).count(); }

	void Begin(bool a_gamepad);
	void Reset();
	void Arm(float a_deg);
	void Strike();
	void AfterStrike();
	void NormalAttack();

	// Wysyła atak do gry. Gdy gra go odrzuci, bo poprzedni jeszcze trwa, cios trafia do bufora.
	bool Dispatch(std::string a_event, float a_angle, std::string a_label);
	bool TrySend(const std::string& a_event);
	void Commit(float a_angle, bool a_power);
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
	Clock::time_point _lastMouse{};     // do liczenia prędkości ruchu
	Clock::time_point _lastReverse{};   // ostatni szybki ruch wstecz (ciągłość szarpnięcia)
	Vec               _dz{};            // ruch zebrany w martwej strefie
	Clock::time_point _dzStart{};       // kiedy zaczął się zbierać

	static Vec ApplyStickDeadZone(Vec a_v);
	Clock::time_point _lastStrike{};
	bool              _struckOnce = false;
	Clock::time_point _armStart{};      // kiedy zamach został uzbrojony (do ciosu mocy)
	std::uint32_t     _strikeId = 0;
	bool              _strikePower = false;

	// --- broń dystansowa ---
	enum class RangedPhase : std::uint8_t
	{
		Idle,
		Waiting,      // trzymasz LPM, czekamy na gest
		Cocked,       // kusza napięta, czekamy na pchnięcie
		PassThrough   // gest wykonany – gra dostała wciśnięcie, reszta idzie normalnie
	};
	ButtonAction OnRangedButton(RE::ButtonEvent* a_event, RangedKind a_kind);
	bool         OnRangedMotion(float a_up, bool a_stick);

	RangedKind        _rangedKind = RangedKind::None;
	RangedPhase       _rangedPhase = RangedPhase::Idle;
	float             _rangedAccum = 0.0f;
	bool              _rangedFire = false;  // gest wykonany – przy następnym zdarzeniu przycisku podaj wciśnięcie
	bool              _rangedMoved = false;
	Clock::time_point _rangedPress{};

	struct Pending
	{
		bool        active = false;
		std::string event;
		std::string label;
		float       angle = 0.0f;
		float       age = 0.0f;
	} _pending;
	float             _strikeAngle = 0.0f;
};
