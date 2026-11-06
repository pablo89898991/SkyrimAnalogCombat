#include "Hooks.h"

#include "Combat.h"

// Hooki vtable na handlerach wejścia gracza (PlayerControls):
//  - AttackBlockHandler::ProcessButton  -> przechwytuje LPM / prawy spust ("Right Attack/Block")
//  - LookHandler::ProcessMouseMove      -> zbiera ruch myszy i zamraża kamerę podczas gestu
//  - LookHandler::ProcessThumbstick     -> to samo dla prawego drążka pada
//
// Indeksy wirtualnych funkcji PlayerInputHandler (SE / AE < 1.7.99):
//   02 ProcessThumbstick, 03 ProcessMouseMove, 04 ProcessButton
// W AE 1.7.99+ Bethesda dodała dwie funkcje (ProcessMotionGesture/ProcessSixaxis) PRZED nimi,
// więc indeksy przesuwają się o 2. REL::VersionShift załatwia to w runtime.

namespace Hooks
{
	namespace
	{
		constexpr std::size_t kAddedIn1799 = 2;

		std::size_t Idx(std::size_t a_base)
		{
			return REL::VersionShift(a_base, kAddedIn1799, SKSE::RUNTIME_SSE_1_7_99);
		}

		bool IsRightAttack(const RE::ButtonEvent* a_event)
		{
			auto* ue = RE::UserEvents::GetSingleton();
			return ue && a_event->QUserEvent() == ue->rightAttack;
		}

		// ---- AttackBlockHandler::ProcessButton ----
		using ProcessButton_t = void(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
		ProcessButton_t* _AttackProcessButton = nullptr;

		void AttackProcessButton(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (a_event && IsRightAttack(a_event) && Combat::Get().OnAttackButton(a_event)) {
				return;  // połknięte
			}
			_AttackProcessButton(a_this, a_event, a_data);
		}

		// ---- LookHandler::ProcessMouseMove ----
		using ProcessMouseMove_t = void(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
		ProcessMouseMove_t* _LookProcessMouseMove = nullptr;

		void LookProcessMouseMove(RE::LookHandler* a_this, RE::MouseMoveEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (a_event && Combat::Get().OnMouseMove(a_event->mouseInputX, a_event->mouseInputY)) {
				return;  // kamera zamrożona podczas zamachu
			}
			_LookProcessMouseMove(a_this, a_event, a_data);
		}

		// ---- LookHandler::ProcessThumbstick ----
		using ProcessThumbstick_t = void(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
		ProcessThumbstick_t* _LookProcessThumbstick = nullptr;

		void LookProcessThumbstick(RE::LookHandler* a_this, RE::ThumbstickEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (a_event && a_event->IsRight() && Combat::Get().OnRightStick(a_event->xValue, a_event->yValue)) {
				return;
			}
			_LookProcessThumbstick(a_this, a_event, a_data);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> attackVtbl{ RE::VTABLE_AttackBlockHandler[0] };
		REL::Relocation<std::uintptr_t> lookVtbl{ RE::VTABLE_LookHandler[0] };

		_AttackProcessButton = reinterpret_cast<ProcessButton_t*>(attackVtbl.write_vfunc(Idx(0x4), &AttackProcessButton));
		_LookProcessMouseMove = reinterpret_cast<ProcessMouseMove_t*>(lookVtbl.write_vfunc(Idx(0x3), &LookProcessMouseMove));
		_LookProcessThumbstick = reinterpret_cast<ProcessThumbstick_t*>(lookVtbl.write_vfunc(Idx(0x2), &LookProcessThumbstick));

		logger::info("Hooki zainstalowane (indeksy vfunc: button={}, mouse={}, stick={})", Idx(0x4), Idx(0x3), Idx(0x2));
	}
}
