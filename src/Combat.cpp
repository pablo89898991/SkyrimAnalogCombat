#include "Combat.h"

#include "Visual.h"

namespace
{
	constexpr float kPi = 3.14159265358979f;
	constexpr float kSectorDeg = 360.0f / kMaxSectors;  // 22.5°

	// Opis sektora do logu, np. "67.5° (prawo-góra)"
	std::string Describe(int a_sector16)
	{
		static constexpr std::array<const char*, kMaxSectors> names{
			"prawo", "prawo-prawo-góra", "prawo-góra", "góra-góra-prawo",
			"góra", "góra-góra-lewo", "lewo-góra", "lewo-lewo-góra",
			"lewo", "lewo-lewo-dół", "lewo-dół", "dół-dół-lewo",
			"dół", "dół-dół-prawo", "prawo-dół", "prawo-prawo-dół"
		};
		return std::format("{:.1f}° ({})", a_sector16 * kSectorDeg, names[static_cast<std::size_t>(a_sector16)]);
	}
}

float Combat::AngleDeg(const Vec& a_v)
{
	float deg = std::atan2(a_v.y, a_v.x) * 180.0f / kPi;
	if (deg < 0.0f) {
		deg += 360.0f;
	}
	return deg;
}

Combat::Vec Combat::FromAngle(float a_deg)
{
	const float r = a_deg * kPi / 180.0f;
	return { std::cos(r), std::sin(r) };
}

int Combat::Sector16(float a_deg)
{
	const int   n = Config::Get().directions;  // 4 / 8 / 16
	const float step = 360.0f / static_cast<float>(n);
	const int   s = static_cast<int>(std::lround(a_deg / step)) % n;
	return s * (kMaxSectors / n);
}

const std::string& Combat::EventFor(int a_sector16)
{
	const auto& cfg = Config::Get();

	int s = a_sector16;
	if (cfg.swapLeftRight) {
		s = (kMaxSectors / 2 - s + kMaxSectors) % kMaxSectors;  // lustro poziome: kąt -> 180° - kąt
	}

	// 1) własna animacja dla tego kąta
	if (const auto& ev = cfg.sectorEvents[static_cast<std::size_t>(s)]; !ev.empty()) {
		return ev;
	}

	// 2) najbliższa z 4 bazowych; skosy (dokładnie 45°) idą do cięć poziomych,
	//    bo wyglądają bardziej jak ukośne cięcie niż cios z góry.
	const Vec v = FromAngle(s * kSectorDeg);
	if (std::abs(v.x) + 0.001f >= std::abs(v.y)) {
		return v.x >= 0.0f ? cfg.windupRight : cfg.windupLeft;
	}
	return v.y >= 0.0f ? cfg.windupUp : cfg.windupDown;
}

Combat::Aim Combat::GetAim() const
{
	if (_state != State::Charging && _state != State::Armed) {
		return {};
	}

	const auto& cfg = Config::Get();
	Vec v{};
	if (_gamepad) {
		v = _stick;
	} else {
		v = { _windup.x / cfg.mouseWindupThreshold, _windup.y / cfg.mouseWindupThreshold };
		if (_state == State::Armed) {
			// szarpnięcie w drugą stronę "ciągnie" broń z powrotem do środka
			const float back = std::clamp(_reverse / cfg.mouseStrikeThreshold, 0.0f, 1.0f);
			v.x *= 1.0f - back;
			v.y *= 1.0f - back;
		}
	}

	if (const float len = v.Len(); len > 1.0f) {
		v.x /= len;
		v.y /= len;
	}
	return { v.x, v.y };
}

bool Combat::CanUseDirectionalCombat(bool a_left)
{
	const auto& cfg = Config::Get();
	if (!cfg.enabled || (a_left && !cfg.leftEnabled)) {
		return false;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}

	if (!player->AsActorState()->IsWeaponDrawn() || player->IsOnMount()) {
		return false;
	}

	// Broń biała w tej ręce (miecze, topory, buławy, sztylety; w prawej też dwuręczne).
	auto* obj = player->GetEquippedObject(a_left);
	auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
	if (!weap || !weap->IsMelee() || weap->IsHandToHandMelee()) {
		return false;
	}
	if (a_left) {
		// broń dwuręczna "siedzi" w obu rękach – lewa ręka wtedy blokuje, nie atakuje
		const auto type = weap->GetWeaponType();
		if (type == RE::WeaponTypes::kTwoHandSword || type == RE::WeaponTypes::kTwoHandAxe) {
			return false;
		}
	}

	return true;
}

const std::string& Combat::HandFallback() const
{
	const auto& cfg = Config::Get();
	return _left ? cfg.leftAttackEvent : cfg.fallbackEvent;
}

bool Combat::ShieldReady()
{
	const auto& cfg = Config::Get();
	if (!cfg.enabled || !cfg.shieldEnabled) {
		return false;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}
	auto* obj = player->GetEquippedObject(true);
	auto* armo = obj ? obj->As<RE::TESObjectARMO>() : nullptr;
	return armo && armo->IsShield();
}

namespace
{
	struct ShieldState
	{
		float                                 accum = 0.0f;
		std::chrono::steady_clock::time_point last{};
		std::chrono::steady_clock::time_point lastBash{};
		bool                                  stickLatched = false;
	} g_shield;

	void Bash(const char* a_src)
	{
		const auto& cfg = Config::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const auto  now = std::chrono::steady_clock::now();
		if (!player || std::chrono::duration<float>(now - g_shield.lastBash).count() < cfg.bashCooldown) {
			return;
		}
		g_shield.lastBash = now;
		g_shield.accum = 0.0f;
		Combat::StartSettle(std::string_view(a_src) == "pad");
		const bool ok = player->NotifyAnimationGraph(cfg.bashEvent.c_str());
		logger::debug("Tarcza: uderzenie ({}) -> '{}' ({})", a_src, cfg.bashEvent, ok ? "ok" : "odrzucony");
	}
}

bool Combat::OnShieldMotion(std::int32_t, std::int32_t a_dy)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !ShieldReady() || !player->IsBlocking()) {
		g_shield.accum = 0.0f;
		return false;
	}
	const auto& cfg = Config::Get();
	const auto  now = std::chrono::steady_clock::now();
	const float dt = std::chrono::duration<float>(now - g_shield.last).count();
	g_shield.last = now;

	g_shield.accum *= std::exp(-dt / cfg.bashWindow);  // stary ruch "wygasa"
	const float up = (cfg.invertY ? a_dy : -a_dy) * cfg.mouseSensitivity;
	bool        swallow = cfg.freezeCameraWhileBlocking;
	if (up > 0.0f) {
		g_shield.accum += up;
		// szybkie pchnięcie w górę to gest, a nie rozglądanie się – nie ruszaj nim kamery
		if (dt > 1e-4f && up / dt >= cfg.bashSwallowSpeed) {
			swallow = true;
		}
	}
	if (g_shield.accum >= cfg.bashFlick) {
		Bash("mysz");
		swallow = true;
	}
	return swallow;
}

bool Combat::OnShieldStick(float, float a_y)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !ShieldReady() || !player->IsBlocking()) {
		g_shield.stickLatched = false;
		return false;
	}
	const auto& cfg = Config::Get();
	if (a_y >= cfg.bashStick && !g_shield.stickLatched) {
		g_shield.stickLatched = true;
		Bash("pad");
		return true;
	}
	if (a_y < 0.3f) {
		g_shield.stickLatched = false;
	}
	return cfg.freezeCameraWhileBlocking;
}

void Combat::StartSettle(bool a_stick)
{
	s_settleUntil = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(Config::Get().gestureSettle));
	if (a_stick) {
		s_stickLatch = true;
	}
}

bool Combat::SwallowMouse()
{
	return Clock::now() < s_settleUntil;
}

bool Combat::SwallowStick(float a_x, float a_y)
{
	if (s_stickLatch) {
		if (std::abs(a_x) < 0.3f && std::abs(a_y) < 0.3f) {
			s_stickLatch = false;  // drążek wrócił do środka
		} else {
			return true;
		}
	}
	return Clock::now() < s_settleUntil;
}

Combat::Vec Combat::ApplyStickDeadZone(Vec a_v)
{
	// radialna martwa strefa z przeskalowaniem, żeby poza nią ruch był płynny od zera
	const float dz = Config::Get().stickDeadZone;
	const float len = a_v.Len();
	if (len <= dz || len < 1e-5f) {
		return {};
	}
	const float scaled = (std::min)((len - dz) / (1.0f - dz), 1.0f);
	return { a_v.x / len * scaled, a_v.y / len * scaled };
}

bool Combat::Charged() const
{
	const auto& cfg = Config::Get();
	return _state == State::Armed && cfg.powerChargeTime > 0.0f && Seconds(_armStart) >= cfg.powerChargeTime;
}

bool Combat::InCooldown() const
{
	return _struckOnce && Seconds(_lastStrike) < Config::Get().strikeCooldown;
}

void Combat::Begin(bool a_gamepad)
{
	s_active = this;
	_state = State::Charging;
	_gamepad = a_gamepad;
	_movedAtAll = false;
	_windup = {};
	_stick = {};
	_dz = {};
	_reverse = 0.0f;
	_pressTime = Clock::now();
	_struckOnce = false;
	logger::debug("Gest: start ({})", a_gamepad ? "pad" : "mysz");
}

void Combat::Reset()
{
	if (s_active == this && _rangedPhase == RangedPhase::Idle) {
		s_active = nullptr;
	}
	_state = State::Idle;
	_windup = {};
	_reverse = 0.0f;
}

void Combat::Arm(float a_deg)
{
	const int sector = Sector16(a_deg);
	if (_state != State::Armed || sector != _sector) {
		logger::debug("Gest: zamach {} (kąt surowy {:.1f}°)", Describe(sector), a_deg);
	}
	if (_state != State::Armed) {
		_armStart = Clock::now();
	}
	_state = State::Armed;
	_angle = a_deg;
	_sector = sector;
}

void Combat::Strike()
{
	const auto& cfg = Config::Get();
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		Reset();
		return;
	}

	auto* av = player->AsActorValueOwner();
	if (cfg.directionAV != RE::ActorValue::kNone) {
		// 1..16: 1 = prawo, 5 = góra, 9 = lewo, 13 = dół (przeciwnie do wskazówek zegara)
		av->SetActorValue(cfg.directionAV, static_cast<float>(_sector + 1));
	}
	if (cfg.angleAV != RE::ActorValue::kNone) {
		av->SetActorValue(cfg.angleAV, _angle);
	}

	const bool stamina = av->GetActorValue(RE::ActorValue::kStamina) > 0.0f;
	const bool procedural = Visual::ProceduralActive(_left);

	// 1. osoba + tryb proceduralny: wygląd rysuje mod, gra dostaje tylko atak od obrażeń.
	// Inaczej: kierunkowe animacje z [Animations].
	// Cios mocy = zamach przytrzymany co najmniej PowerChargeTime (i jest stamina).
	std::string ev;
	const bool  power = Charged() && stamina;
	if (_left) {
		// lewa ręka: vanilla ma tylko zwykły / mocny atak lewą ręką
		ev = power ? cfg.leftPowerEvent : cfg.leftAttackEvent;
	} else if (procedural) {
		ev = power ? cfg.procPowerEvent : cfg.procAttackEvent;
	} else {
		// 3. osoba: zwykły cios = zwykły atak; cios mocy = kierunkowy cios mocy z [Animations]
		ev = power ? EventFor(_sector) : cfg.procAttackEvent;
	}
	if (ev.empty()) {
		ev = HandFallback();
	}

	StartSettle(_gamepad);
	Dispatch(ev, _angle, std::string(_left ? "[L] " : "[P] ") + "cios z " + Describe(_sector) + (procedural ? " [proceduralny]" : ""));

	AfterStrike();
}

void Combat::AfterStrike()
{
	_lastStrike = Clock::now();
	_struckOnce = true;
	_windup = {};
	_reverse = 0.0f;
	_state = Config::Get().chainStrikes ? State::Charging : State::Spent;
}

void Combat::NormalAttack()
{
	const auto& cfg = Config::Get();
	// zwykłe kliknięcie: prawa ręka tnie z prawej w lewo, lewa z lewej w prawo
	const std::string& ev = _left ? cfg.leftAttackEvent : cfg.normalAttackEvent;
	if (!ev.empty()) {
		Dispatch(ev, _left ? 160.0f : 20.0f, _left ? "[L] kliknięcie" : "[P] kliknięcie");
	}
}

bool Combat::TrySend(const std::string& a_event)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}
	const auto& cfg = Config::Get();
	bool        ok = !a_event.empty() && player->NotifyAnimationGraph(a_event.c_str());
	if (const auto& fb = HandFallback(); !ok && !fb.empty() && fb != a_event) {
		ok = player->NotifyAnimationGraph(fb.c_str());
	}
	return ok;
}

void Combat::Commit(float a_angle, bool a_power)
{
	_strikeAngle = a_angle;
	_strikePower = a_power;
	++_strikeId;  // animacja proceduralna startuje dopiero teraz -> widać tylko ciosy, które zadają obrażenia
}

bool Combat::Dispatch(std::string a_event, float a_angle, std::string a_label)
{
	const auto& cfg = Config::Get();
	auto*       player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}

	// Nowy cios zastępuje stary z bufora (liczy się ostatni kierunek).
	_pending.active = false;

	if (TrySend(a_event)) {
		Commit(a_angle, a_event.find("Power") != std::string::npos);
		logger::debug("Gest: {} -> '{}' (ok)", a_label, a_event);
		return true;
	}

	const bool busy = player->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
	if (busy && cfg.strikeBufferTime > 0.0f) {
		_pending = { true, std::move(a_event), std::move(a_label), a_angle, 0.0f };
		logger::debug("Gest: {} -> poprzedni atak trwa, cios w buforze", _pending.label);
		return false;
	}

	logger::debug("Gest: {} -> '{}' odrzucony (brak staminy lub zły stan postaci)", a_label, a_event);
	return false;
}

void Combat::Update(float a_delta)
{
	if (!_pending.active) {
		return;
	}
	_pending.age += a_delta;

	if (!MeleeHere()) {
		_pending.active = false;  // schowana broń, menu itp.
		return;
	}

	if (TrySend(_pending.event)) {
		_pending.active = false;
		Commit(_pending.angle, _pending.event.find("Power") != std::string::npos);
		logger::debug("Gest: {} -> '{}' (ok, z bufora po {:.2f} s)", _pending.label, _pending.event, _pending.age);
	} else if (_pending.age > Config::Get().strikeBufferTime) {
		_pending.active = false;
		logger::debug("Gest: {} -> bufor wygasł po {:.2f} s", _pending.label, _pending.age);
	}
}

RangedKind Combat::CurrentRanged(bool a_left)
{
	const auto& cfg = Config::Get();
	if (!cfg.enabled || !cfg.rangedEnabled || (a_left && !cfg.leftEnabled)) {
		return RangedKind::None;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->AsActorState()->IsWeaponDrawn() || player->IsOnMount()) {
		return RangedKind::None;
	}
	auto* obj = player->GetEquippedObject(a_left);
	auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
	if (!weap) {
		return RangedKind::None;
	}
	if (weap->IsStaff() && cfg.rangedStaff) {
		return RangedKind::Staff;
	}
	if (a_left) {
		return RangedKind::None;  // łuk i kusza są obsługiwane przez prawą rękę (LPM)
	}
	if (weap->IsBow() && cfg.rangedBow) {
		return RangedKind::Bow;
	}
	if (weap->IsCrossbow() && cfg.rangedCrossbow) {
		return RangedKind::Crossbow;
	}
	return RangedKind::None;
}

ButtonAction Combat::OnRangedButton(RE::ButtonEvent* a_event, RangedKind a_kind)
{
	const auto& cfg = Config::Get();

	if (a_event->IsDown()) {
		_rangedKind = a_kind;
		_rangedPhase = RangedPhase::Waiting;
		_rangedAccum = 0.0f;
		_rangedFire = false;
		_rangedMoved = false;
		_rangedPress = Clock::now();
		s_active = this;
		logger::debug("Dystans: start ({})", a_kind == RangedKind::Bow ? "łuk" : a_kind == RangedKind::Crossbow ? "kusza" : "kostur");
		return ButtonAction::Swallow;
	}

	if (_rangedPhase == RangedPhase::Idle) {
		return ButtonAction::Pass;
	}

	if (a_event->IsUp()) {
		const RangedPhase phase = _rangedPhase;
		_rangedPhase = RangedPhase::Idle;
		if (s_active == this) {
			s_active = nullptr;
		}
		if (phase == RangedPhase::PassThrough) {
			return ButtonAction::Pass;  // np. puszczenie cięciwy = strzał
		}
		if (_rangedFire) {
			_rangedFire = false;
			return ButtonAction::PressThenPass;
		}
		if (!_rangedMoved && cfg.tapForNormalAttack && Seconds(_rangedPress) <= cfg.tapMaxTime) {
			logger::debug("Dystans: kliknięcie -> zwykłe użycie");
			return ButtonAction::PressThenPass;
		}
		logger::debug("Dystans: anulowano (puszczono bez gestu)");
		return ButtonAction::Swallow;
	}

	// trzymanie
	if (_rangedFire) {
		_rangedFire = false;
		_rangedPhase = RangedPhase::PassThrough;
		return ButtonAction::PassAsPress;
	}
	return _rangedPhase == RangedPhase::PassThrough ? ButtonAction::Pass : ButtonAction::Swallow;
}

bool Combat::OnRangedMotion(float a_up, bool a_stick)
{
	const auto& cfg = Config::Get();
	if (_rangedPhase != RangedPhase::Waiting && _rangedPhase != RangedPhase::Cocked) {
		return false;
	}
	if (_rangedFire) {
		return false;  // gest już wykonany – kamera wolna do celowania
	}
	if (std::abs(a_up) > (a_stick ? 0.2f : 0.0f)) {
		_rangedMoved = true;
	}

	const auto fire = [&](const char* a_what) {
		_rangedFire = true;
		StartSettle(a_stick);
		logger::debug("Dystans: {}", a_what);
	};

	const bool pullPhase = _rangedKind == RangedKind::Bow || (_rangedKind == RangedKind::Crossbow && _rangedPhase == RangedPhase::Waiting);

	if (pullPhase) {
		// ruch do siebie (w dół)
		bool done = false;
		if (a_stick) {
			done = a_up <= -cfg.rangedStickPull;
		} else {
			_rangedAccum = (std::max)(0.0f, _rangedAccum - a_up);
			done = _rangedAccum >= cfg.rangedPull;
		}
		if (done) {
			if (_rangedKind == RangedKind::Bow) {
				fire("naciągnięto cięciwę – celuj i puść LPM, by strzelić");
			} else {
				_rangedPhase = RangedPhase::Cocked;
				_rangedAccum = 0.0f;
				logger::debug("Dystans: kusza napięta – pchnij mysz od siebie, by strzelić");
			}
		}
	} else {
		// ruch od siebie (w górę): strzał z kuszy / czar z kostura
		bool done = false;
		if (a_stick) {
			done = a_up >= cfg.rangedStickPull;
		} else {
			_rangedAccum = (std::max)(0.0f, _rangedAccum + a_up);
			done = _rangedAccum >= cfg.rangedPush;
		}
		if (done) {
			fire(_rangedKind == RangedKind::Crossbow ? "strzał z kuszy" : "czar z kostura");
		}
	}

	return cfg.rangedFreezeCamera && !_rangedFire;
}

ButtonAction Combat::OnAttackButton(RE::ButtonEvent* a_event)
{
	const bool gamepad = a_event->GetDevice() == RE::INPUT_DEVICES::kGamepad;

	if (a_event->IsDown()) {
		if (gamepad && !Config::Get().gamepadEnabled) {
			return ButtonAction::Pass;
		}
		// Atak prawą ręką podczas bloku = uderzenie tarczą/bronią w vanilli – nie przeszkadzamy.
		if (auto* pl = RE::PlayerCharacter::GetSingleton(); !_left && pl && pl->IsBlocking()) {
			return ButtonAction::Pass;
		}
		if (const auto kind = CurrentRanged(_left); kind != RangedKind::None) {
			Reset();
			return OnRangedButton(a_event, kind);
		}
		_rangedPhase = RangedPhase::Idle;
		if (!MeleeHere()) {
			Reset();
			return ButtonAction::Pass;  // zwykła walka vanilla
		}
		Begin(gamepad);
		return ButtonAction::Swallow;
	}

	if (_rangedPhase != RangedPhase::Idle) {
		if (CurrentRanged(_left) != _rangedKind) {
			_rangedPhase = RangedPhase::Idle;  // broń schowana / zmieniona
			if (s_active == this) {
				s_active = nullptr;
			}
			return ButtonAction::Pass;
		}
		return OnRangedButton(a_event, _rangedKind);
	}

	if (_state == State::Idle) {
		return ButtonAction::Pass;
	}

	if (a_event->IsUp()) {
		const auto& cfg = Config::Get();
		if (_state == State::Armed && cfg.strikeOnRelease) {
			Strike();
		} else if (_state == State::Charging && !_struckOnce && !_movedAtAll && cfg.tapForNormalAttack &&
				   Seconds(_pressTime) <= cfg.tapMaxTime) {
			NormalAttack();
		}
		Reset();
		return ButtonAction::Swallow;
	}

	// trzymanie – połykamy, żeby gra nie zaczęła ładować ciosu mocy
	if (!MeleeHere()) {
		Reset();
		return ButtonAction::Pass;
	}
	return ButtonAction::Swallow;
}

bool Combat::OnMouseMove(std::int32_t a_dx, std::int32_t a_dy)
{
	if (_rangedPhase == RangedPhase::Waiting || _rangedPhase == RangedPhase::Cocked) {
		const auto& rc = Config::Get();
		const float up = (rc.invertY ? a_dy : -a_dy) * rc.mouseSensitivity;
		return OnRangedMotion(up, false);
	}

	if (_state == State::Idle || _gamepad) {
		return false;
	}

	const auto& cfg = Config::Get();
	const float s = cfg.mouseSensitivity;
	// Zakładamy dy > 0 = ruch myszy w dół (jak w DirectInput); InvertY odwraca.
	const Vec raw{ a_dx * s, (cfg.invertY ? a_dy : -a_dy) * s };

	const auto now = Clock::now();
	const auto prev = _lastMouse;
	_lastMouse = now;

	if (InCooldown()) {
		_dz = {};
		return cfg.freezeCamera;  // ruch "dokończenia" ciosu nie uzbraja kolejnego zamachu
	}

	// MARTWA STREFA: drobny ruch (drżenie ręki, mikrokorekty) jest zbierany i ignorowany,
	// dopóki nie wyjdzie poza promień DeadZone. Ruch tam i z powrotem w środku strefy się znosi.
	if (_dz.x == 0.0f && _dz.y == 0.0f) {
		_dzStart = (now - prev > std::chrono::milliseconds(50)) ? now - std::chrono::milliseconds(8) : prev;
	}
	_dz.x += raw.x;
	_dz.y += raw.y;
	if (_dz.Len() < cfg.mouseDeadZone) {
		return cfg.freezeCamera;
	}
	const Vec d = _dz;
	_dz = {};
	_movedAtAll = true;

	// czas, w którym zebrał się ten ruch (do prędkości szarpnięcia)
	const float dt = std::clamp(std::chrono::duration<float>(now - _dzStart).count(), 0.001f, 0.1f);

	// Kolejny zamach w serii wymaga wyraźniejszego ruchu – naturalne "odbicie" ręki po ciosie
	// nie uzbraja nowego cięcia.
	const float windupNeeded = cfg.mouseWindupThreshold * (_struckOnce ? cfg.chainWindupMult : 1.0f);

	switch (_state) {
	case State::Charging:
		_windup.x += d.x;
		_windup.y += d.y;
		if (_windup.Len() >= windupNeeded) {
			_reverse = 0.0f;
			Arm(AngleDeg(_windup));
		}
		break;

	case State::Armed:
		{
			const float along = d.Dot(FromAngle(_angle));
			// Cios = JEDNO ciągłe, szybkie szarpnięcie w drugą stronę o długości StrikeThreshold.
			// Każdy ruch w przód, zwolnienie albo przerwa kasuje odliczanie, więc drobne
			// ruchy myszy po wybraniu kierunku nie sumują się w cios.
			if (along >= 0.0f) {
				// dalej w stronę zamachu albo w bok – broń "jedzie" po okręgu
				_windup.x += d.x;
				_windup.y += d.y;
				_reverse = 0.0f;
				Arm(AngleDeg(_windup));
			} else if (-along / dt >= cfg.strikeMinSpeed) {
				if (std::chrono::duration<float>(now - _lastReverse).count() > cfg.strikeFlickGap) {
					_reverse = 0.0f;  // nowe szarpnięcie
				}
				_lastReverse = now;
				_reverse += -along;
				if (_reverse >= cfg.mouseStrikeThreshold) {
					Strike();
				}
			} else {
				// powolny ruch wstecz to nie cios, tylko cofanie zamachu
				_windup.x += d.x;
				_windup.y += d.y;
				_reverse = 0.0f;
				if (_windup.Len() < windupNeeded * 0.5f) {
					_state = State::Charging;  // zamach cofnięty prawie do środka
					logger::debug("Gest: zamach cofnięty");
				}
			}
		}
		break;

	default:
		break;
	}

	return cfg.freezeCamera;
}

bool Combat::OnRightStick(float a_x, float a_y)
{
	if (_rangedPhase == RangedPhase::Waiting || _rangedPhase == RangedPhase::Cocked) {
		return OnRangedMotion(ApplyStickDeadZone({ a_x, a_y }).y, true);
	}

	if (_state == State::Idle || !_gamepad) {
		return false;
	}

	const auto& cfg = Config::Get();
	const Vec   v = ApplyStickDeadZone({ a_x, a_y });
	_stick = v;

	if (v.Len() > 0.2f) {
		_movedAtAll = true;
	}

	if (InCooldown()) {
		return cfg.freezeCamera;
	}

	switch (_state) {
	case State::Charging:
		if (v.Len() >= cfg.stickWindup) {
			Arm(AngleDeg(v));
		}
		break;

	case State::Armed:
		{
			const float along = v.Dot(FromAngle(_angle));
			if (along <= -cfg.stickStrike) {
				Strike();
			} else if (v.Len() >= cfg.stickWindup && along > 0.0f) {
				Arm(AngleDeg(v));  // drążek po okręgu = płynna zmiana kąta
			}
		}
		break;

	default:
		break;
	}

	return cfg.freezeCamera;
}
