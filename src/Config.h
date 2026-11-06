#pragma once

#include "PCH.h"

// Kierunek ZAMACHU (strona, na którą odciągnąłeś broń).
// Cios leci w przeciwną stronę: zamach w prawo -> cięcie z prawej do lewej.
enum class Dir : std::uint8_t
{
	Left = 0,
	Right = 1,
	Up = 2,
	Down = 3,
	Count
};

constexpr std::string_view DirName(Dir d)
{
	switch (d) {
	case Dir::Left:
		return "Left";
	case Dir::Right:
		return "Right";
	case Dir::Up:
		return "Up";
	case Dir::Down:
		return "Down";
	default:
		return "?";
	}
}

struct Config
{
	// [General]
	bool enabled = true;
	bool freezeCamera = true;        // kamera stoi w miejscu, gdy trzymasz LPM
	bool chainStrikes = true;        // po ciosie można od razu wziąć kolejny zamach (bez puszczania LPM)
	bool strikeOnRelease = true;     // puszczenie LPM po zamachu też wyprowadza cios
	bool tapForNormalAttack = true;  // krótkie kliknięcie bez ruchu = zwykły atak
	bool debugLog = false;

	// [Mouse]
	float mouseWindupThreshold = 80.0f;   // ile "pikseli" ruchu myszy, żeby uzbroić zamach
	float mouseStrikeThreshold = 50.0f;   // ile ruchu w przeciwną stronę, żeby wyprowadzić cios
	float mouseSensitivity = 1.0f;
	bool invertY = false;

	// [Gamepad] (prawy drążek, trzymając prawy spust)
	bool gamepadEnabled = true;
	float stickWindup = 0.70f;
	float stickStrike = 0.50f;

	// [Timing]
	float strikeCooldown = 0.30f;  // s, blokada po ciosie zanim można uzbroić kolejny
	float tapMaxTime = 0.25f;      // s, maksymalny czas "kliknięcia"

	// [Animations] – zdarzenia grafu animacji wysyłane dla każdego kierunku zamachu
	std::array<std::string, static_cast<std::size_t>(Dir::Count)> events{
		"attackPowerStartLeft",     // zamach w lewo  -> cięcie w prawo
		"attackPowerStartRight",    // zamach w prawo -> cięcie w lewo
		"attackPowerStartForward",  // zamach w górę  -> cios z góry
		"attackPowerStartInPlace"   // zamach w dół   -> cios od dołu / pchnięcie
	};
	std::string normalAttackEvent = "attackStart";
	std::string fallbackEvent = "attackStart";  // gdy kierunkowe się nie uda (np. brak staminy)
	bool swapLeftRight = false;

	// [Integration] – zapis kierunku do wartości aktora (np. dla warunków OAR)
	std::string directionActorValue = "";  // np. "Variable09"; puste = wyłączone
	RE::ActorValue directionAV = RE::ActorValue::kNone;

	static Config& Get()
	{
		static Config c;
		return c;
	}

	void Load();
};
