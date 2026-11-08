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
	bool tapForNormalAttack = false;
	bool debugLog = false;
	int  directions = 16;  // 4, 8 albo 16

	// [Mouse]
	float mouseWindupThreshold = 80.0f;
	float mouseStrikeThreshold = 70.0f;
	float strikeMinSpeed = 900.0f;   // min. prędkość szarpnięcia (jednostki/s) – wolny ruch wstecz nie tnie
	float chainWindupMult = 1.6f;    // kolejny zamach w serii wymaga tyle razy większego ruchu
	float mouseSensitivity = 1.0f;
	bool  invertY = false;

	// [Gamepad]
	bool  gamepadEnabled = true;
	float stickWindup = 0.70f;
	float stickStrike = 0.50f;

	// [Timing]
	float strikeCooldown = 0.30f;
	float tapMaxTime = 0.25f;
	float strikeBufferTime = 0.8f;
	float gestureSettle = 0.25f;    // s po geście, przez które kamera ignoruje "końcówkę" ruchu  // s, jak długo czekać, aż gra przyjmie zbuforowany cios

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
	bool hideArms = true;              // ukryj ręce/rękawice, widać samą broń
	bool hideArmsOnlyWhenDrawn = true; // ukrywaj tylko z wyjętą bronią białą

	// [LeftHand] – broń w lewej ręce (PPM): broń biała (dual wield) i kostur
	bool        leftEnabled = true;
	std::string leftAttackEvent = "attackStartLeftHand";
	std::string leftPowerEvent = "attackStartLeftHand";

	// [Shield] – szybkie pchnięcie myszy w górę podczas bloku = uderzenie tarczą
	bool        shieldEnabled = true;
	std::string bashEvent = "bashStart";
	float       bashFlick = 120.0f;    // ile ruchu w górę w krótkim czasie
	float       bashWindow = 0.12f;    // s – okno, w którym liczy się ruch
	float       bashCooldown = 0.8f;   // s między uderzeniami
	float       bashStick = 0.85f;     // pad: wychylenie drążka w górę
	float       bashSwallowSpeed = 1500.0f;  // szybszy ruch w górę podczas bloku nie rusza kamerą
	bool        freezeCameraWhileBlocking = false;

	// [Ranged] – łuk, kusza, kostur
	bool  rangedEnabled = true;
	bool  rangedBow = true;           // pociągnij mysz do siebie = naciągnij cięciwę
	bool  rangedCrossbow = true;      // w dół = napnij, w górę = strzał
	bool  rangedStaff = true;         // pchnij mysz od siebie = rzuć czar
	float rangedPull = 70.0f;         // ruch myszy do naciągnięcia / napięcia
	float rangedPush = 60.0f;         // ruch myszy do strzału / rzucenia
	bool  rangedFreezeCamera = true;  // kamera stoi do momentu naciągnięcia, potem celujesz
	float rangedStickPull = 0.70f;    // to samo dla drążka pada
	bool  hideArmsRanged = true;      // ukryj ręce także z łukiem/kuszą/kosturem

	// [Procedural] – w 1. osobie mod sam animuje broń po dowolnym kącie
	bool        procEnabled = true;
	std::string procAttackEvent = "attackStart";             // zdarzenie od obrażeń (zwykły atak)
	std::string procPowerEvent = "attackPowerStartInPlace";  // po naładowaniu
	float       powerChargeTime = 0.6f;   // s trzymania zamachu do ciosu mocy; 0 = nigdy
	float       procDistance = 45.0f;     // jak daleko przed kamerą (jednostki gry)
	float       procCenterX = 4.0f;
	float       procCenterZ = -6.0f;
	float       procRadius = 22.0f;       // promień okręgu zamachu
	float       procPullBack = 10.0f;     // zamach przyciąga broń do kamery
	float       procReach = 14.0f;        // wysunięcie do przodu w połowie cięcia
	float       procRestX = 16.0f;        // pozycja spoczynkowa
	float       procRestZ = -16.0f;
	float       procRestTilt = 25.0f;     // pochylenie ostrza do przodu w spoczynku (stopnie)
	float       swingTimeScale = 1.0f;    // mnożnik czasu cięcia (czas bazowy zależy od typu broni)
	float       procSmoothing = 18.0f;
	float       procBladeRoll = 0.0f;     // obrót broni wokół ostrza (stopnie)
	bool        procFlipBlade = false;    // gdy broń jest odwrócona (rękojeść na zewnątrz)
	bool        procPivotAtCenter = true; // obracaj wokół środka broni (efekt "latania")
	bool        liveReload = true;

	// [Activate]
	bool        disableHoldToGrab = true;  // przytrzymanie E nie podnosi przedmiotów – służy do tego klawisz "Grab"

	// [Feel] – "naturalność" ruchu
	bool  weaponWeight = true;
	bool  syncToHits = true;     // dopasuj tempo cięcia do momentu trafienia w grze   // tempo i zasięg zależne od typu i wagi broni
	float overshoot = 0.18f;     // ile broń "przelatuje" za drugą stronę (ułamek łuku)
	float walkBob = 1.0f;        // kołysanie przy chodzeniu/bieganiu
	float cameraSway = 1.0f;     // broń spóźnia się przy obrocie kamery
	float breathing = 1.0f;      // delikatny ruch w spoczynku
	float hitStop = 0.06f;       // s zatrzymania cięcia przy trafieniu
	float hitRecoil = 1.0f;      // odbicie broni przy trafieniu
	float hitShake = 0.15f;      // drżenie kamery przy trafieniu (0 = wył.)
	bool  blockPose = true;      // broń w poziomie podczas bloku
	bool  drawAnimation = true;  // broń "wjeżdża" od dołu po wyjęciu        // przeładuj .ini po zapisaniu, bez restartu gry

	static Config& Get()
	{
		static Config c;
		return c;
	}

	void Load();

	// Nazwa klucza INI dla sektora 0..15, np. "Windup_022" dla 22.5°.
	static std::string SectorKey(int a_sector16);
};
