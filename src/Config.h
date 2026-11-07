#pragma once

#include "PCH.h"

// Kierunki liczone jako kąt zamachu, przeciwnie do ruchu wskazówek zegara:
//   0° = w prawo, 90° = w górę, 180° = w lewo, 270° = w dół.
// Cios leci w przeciwną stronę: zamach w prawo-górę -> cięcie ukośne w lewo-dół.
//
// Wewnętrznie zawsze 16 sektorów po 22.5° (indeks 0..15, 0 = prawo, 4 = góra, 8 = lewo, 12 = dół).
// Ustawienie Directions = 4 / 8 / 16 decyduje, z jaką rozdzielczością gest jest "zatrzaskiwany".
inline constexpr int kMaxSectors = 16;

struct Config
{
	// [General]
	bool enabled = true;
	bool freezeCamera = true;
	bool chainStrikes = true;
	bool strikeOnRelease = true;
	bool tapForNormalAttack = true;
	bool debugLog = false;
	int  directions = 16;  // 4, 8 albo 16

	// [Mouse]
	float mouseWindupThreshold = 80.0f;
	float mouseStrikeThreshold = 50.0f;
	float mouseSensitivity = 1.0f;
	bool  invertY = false;

	// [Gamepad]
	bool  gamepadEnabled = true;
	float stickWindup = 0.70f;
	float stickStrike = 0.50f;

	// [Timing]
	float strikeCooldown = 0.30f;
	float tapMaxTime = 0.25f;

	// [Animations]
	// Animacje "bazowe" dla 4 głównych kierunków – używane, gdy dany kąt nie ma własnej animacji.
	std::string windupRight = "attackPowerStartRight";
	std::string windupUp = "attackPowerStartForward";
	std::string windupLeft = "attackPowerStartLeft";
	std::string windupDown = "attackPowerStartInPlace";
	// Opcjonalne animacje dla każdego z 16 kątów (puste = najbliższa bazowa).
	std::array<std::string, kMaxSectors> sectorEvents{};
	std::string normalAttackEvent = "attackStart";
	std::string fallbackEvent = "attackStart";
	bool        swapLeftRight = false;

	// [Integration]
	std::string    directionActorValue = "";  // 1..16 = sektor zamachu (1 = prawo, 5 = góra, 9 = lewo, 13 = dół)
	RE::ActorValue directionAV = RE::ActorValue::kNone;
	std::string    angleActorValue = "";  // kąt zamachu w stopniach 0..360 (pełny analog)
	RE::ActorValue angleAV = RE::ActorValue::kNone;

	// [FloatingWeapon] – tylko 1. osoba
	bool  hideArms = true;              // ukryj ręce/rękawice, widać samą broń
	bool  hideArmsOnlyWhenDrawn = true; // ukrywaj tylko z wyjętą bronią białą
	bool  followAim = true;             // broń podąża za myszą podczas zamachu
	float maxOffset = 12.0f;            // maks. przesunięcie broni (jednostki gry)
	float maxTilt = 25.0f;              // maks. przechył broni (stopnie); ujemne = odwrotnie
	float smoothing = 12.0f;            // szybkość wygładzania ruchu (1/s)

	static Config& Get()
	{
		static Config c;
		return c;
	}

	void Load();

	// Nazwa klucza INI dla sektora 0..15, np. "Windup_022" dla 22.5°.
	static std::string SectorKey(int a_sector16);
};
