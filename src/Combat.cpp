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

bool Combat::CanUseDirectionalCombat()
{
	const auto& cfg = Config::Get();
	if (!cfg.enabled) {
		return false;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}

	if (!player->AsActorState()->IsWeaponDrawn() || player->IsOnMount()) {
		return false;
	}

	// Tylko broń biała w prawej ręce (miecze, topory, buławy, sztylety, dwuręczne).
	auto* right = player->GetEquippedObject(false);
	auto* weap = right ? right->As<RE::TESObjectWEAP>() : nullptr;
	if (!weap || !weap->IsMelee() || weap->IsHandToHandMelee()) {
		return false;
	}

	return true;
}

bool Combat::InCooldown() const
{
	return _struckOnce && Seconds(_lastStrike) < Config::Get().strikeCooldown;
}

void Combat::Begin(bool a_gamepad)
{
	_state = State::Charging;
	_gamepad = a_gamepad;
	_movedAtAll = false;
	_windup = {};
	_stick = {};
	_reverse = 0.0f;
	_pressTime = Clock::now();
	_struckOnce = false;
	logger::debug("Gest: start ({})", a_gamepad ? "pad" : "mysz");
}

void Combat::Reset()
{
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
	const bool procedural = Visual::ProceduralActive();

	// 1. osoba + tryb proceduralny: wygląd rysuje mod, gra dostaje tylko atak od obrażeń.
	// Inaczej: kierunkowe animacje z [Animations].
	std::string ev;
	if (procedural) {
		const bool charged = cfg.powerChargeTime > 0.0f && Seconds(_armStart) >= cfg.powerChargeTime;
		ev = (charged && stamina) ? cfg.procPowerEvent : cfg.procAttackEvent;
	} else if (stamina) {
		ev = EventFor(_sector);
	}
	if (ev.empty()) {
		ev = cfg.fallbackEvent;
	}

	Dispatch(ev, _angle, "cios z " + Describe(_sector) + (procedural ? " [proceduralny]" : ""));

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
	if (!cfg.normalAttackEvent.empty()) {
		// zwykłe kliknięcie = cięcie z prawej w lewo (dla animacji proceduralnej)
		Dispatch(cfg.normalAttackEvent, 20.0f, "kliknięcie");
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
	if (!ok && !cfg.fallbackEvent.empty() && cfg.fallbackEvent != a_event) {
		ok = player->NotifyAnimationGraph(cfg.fallbackEvent.c_str());
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

	if (!CanUseDirectionalCombat()) {
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

bool Combat::OnAttackButton(RE::ButtonEvent* a_event)
{
	const bool gamepad = a_event->GetDevice() == RE::INPUT_DEVICES::kGamepad;

	if (a_event->IsDown()) {
		if (gamepad && !Config::Get().gamepadEnabled) {
			return false;
		}
		if (!CanUseDirectionalCombat()) {
			Reset();
			return false;  // zwykła walka vanilla
		}
		Begin(gamepad);
		return true;
	}

	if (_state == State::Idle) {
		return false;
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
		return true;
	}

	// trzymanie – połykamy, żeby gra nie zaczęła ładować ciosu mocy
	if (!CanUseDirectionalCombat()) {
		Reset();
		return false;
	}
	return true;
}

bool Combat::OnMouseMove(std::int32_t a_dx, std::int32_t a_dy)
{
	if (_state == State::Idle || _gamepad) {
		return false;
	}

	const auto& cfg = Config::Get();
	const float s = cfg.mouseSensitivity;
	// Zakładamy dy > 0 = ruch myszy w dół (jak w DirectInput); InvertY odwraca.
	const Vec d{ a_dx * s, (cfg.invertY ? a_dy : -a_dy) * s };

	if (d.x != 0.0f || d.y != 0.0f) {
		_movedAtAll = true;
	}

	if (InCooldown()) {
		return cfg.freezeCamera;  // ruch "dokończenia" ciosu nie uzbraja kolejnego zamachu
	}

	switch (_state) {
	case State::Charging:
		_windup.x += d.x;
		_windup.y += d.y;
		if (_windup.Len() >= cfg.mouseWindupThreshold) {
			_reverse = 0.0f;
			Arm(AngleDeg(_windup));
		}
		break;

	case State::Armed:
		{
			const float along = d.Dot(FromAngle(_angle));
			if (along >= 0.0f) {
				// dalej w stronę zamachu albo w bok – broń "jedzie" po okręgu
				_windup.x += d.x;
				_windup.y += d.y;
				_reverse = (std::max)(0.0f, _reverse - along);
				Arm(AngleDeg(_windup));
			} else {
				_reverse += -along;
				if (_reverse >= cfg.mouseStrikeThreshold) {
					Strike();
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
	if (_state == State::Idle || !_gamepad) {
		return false;
	}

	const auto& cfg = Config::Get();
	const Vec   v{ a_x, a_y };
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
