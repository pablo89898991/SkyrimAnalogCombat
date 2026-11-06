#include "Combat.h"

Dir Combat::Snap(const Vec& a_v)
{
	if (std::abs(a_v.x) >= std::abs(a_v.y)) {
		return a_v.x >= 0.0f ? Dir::Right : Dir::Left;
	}
	return a_v.y >= 0.0f ? Dir::Up : Dir::Down;
}

Combat::Vec Combat::Unit(Dir a_d)
{
	switch (a_d) {
	case Dir::Left:
		return { -1.0f, 0.0f };
	case Dir::Right:
		return { 1.0f, 0.0f };
	case Dir::Up:
		return { 0.0f, 1.0f };
	case Dir::Down:
	default:
		return { 0.0f, -1.0f };
	}
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

void Combat::Arm(Dir a_dir)
{
	if (_state != State::Armed || _dir != a_dir) {
		logger::debug("Gest: zamach {}", DirName(a_dir));
	}
	_state = State::Armed;
	_dir = a_dir;
}

void Combat::Strike()
{
	const auto& cfg = Config::Get();
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		Reset();
		return;
	}

	Dir animDir = _dir;
	if (cfg.swapLeftRight) {
		if (animDir == Dir::Left) {
			animDir = Dir::Right;
		} else if (animDir == Dir::Right) {
			animDir = Dir::Left;
		}
	}

	auto* av = player->AsActorValueOwner();
	if (cfg.directionAV != RE::ActorValue::kNone) {
		// 1=Left 2=Right 3=Up 4=Down (0 = brak) – do użycia w warunkach OAR/DAR
		av->SetActorValue(cfg.directionAV, static_cast<float>(static_cast<int>(_dir) + 1));
	}

	const auto& ev = cfg.events[static_cast<std::size_t>(animDir)];
	const bool  hasStamina = av->GetActorValue(RE::ActorValue::kStamina) > 0.0f;

	bool ok = false;
	if (hasStamina && !ev.empty()) {
		ok = player->NotifyAnimationGraph(ev.c_str());
	}
	if (!ok && !cfg.fallbackEvent.empty()) {
		ok = player->NotifyAnimationGraph(cfg.fallbackEvent.c_str());
		logger::debug("Gest: cios {} -> fallback '{}' ({})", DirName(_dir), cfg.fallbackEvent, ok ? "ok" : "odrzucony");
	} else {
		logger::debug("Gest: cios {} -> '{}' ({})", DirName(_dir), ev, ok ? "ok" : "odrzucony");
	}

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
	if (auto* player = RE::PlayerCharacter::GetSingleton(); player && !cfg.normalAttackEvent.empty()) {
		const bool ok = player->NotifyAnimationGraph(cfg.normalAttackEvent.c_str());
		logger::debug("Gest: kliknięcie -> '{}' ({})", cfg.normalAttackEvent, ok ? "ok" : "odrzucony");
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
			Arm(Snap(_windup));
			_reverse = 0.0f;
		}
		break;

	case State::Armed:
		{
			const float along = d.Dot(Unit(_dir));
			if (along >= 0.0f) {
				// dalej w stronę zamachu (albo w bok) – pozwala korygować kierunek
				_windup.x += d.x;
				_windup.y += d.y;
				_reverse = std::max(0.0f, _reverse - along);
				Arm(Snap(_windup));
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

	if (v.Len() > 0.2f) {
		_movedAtAll = true;
	}

	if (InCooldown()) {
		return cfg.freezeCamera;
	}

	switch (_state) {
	case State::Charging:
		if (v.Len() >= cfg.stickWindup) {
			Arm(Snap(v));
		}
		break;

	case State::Armed:
		{
			const float along = v.Dot(Unit(_dir));
			if (along <= -cfg.stickStrike) {
				Strike();
			} else if (v.Len() >= cfg.stickWindup && along > 0.0f) {
				Arm(Snap(v));
			}
		}
		break;

	default:
		break;
	}

	return cfg.freezeCamera;
}
