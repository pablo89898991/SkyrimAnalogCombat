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

		// 0 = nie atak, 1 = prawa ręka (LPM), 2 = lewa ręka (PPM)
		int AttackHand(const RE::ButtonEvent* a_event)
		{
			auto* ue = RE::UserEvents::GetSingleton();
			if (!ue) {
				return 0;
			}
			const auto& ev = a_event->QUserEvent();
			if (ev == ue->rightAttack) {
				return 1;
			}
			if (ev == ue->leftAttack) {
				return 2;
			}
			return 0;
		}

		// ---- AttackBlockHandler::ProcessButton ----
		using ProcessButton_t = void(RE::AttackBlockHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
		ProcessButton_t* _AttackProcessButton = nullptr;

		void AttackProcessButton(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			const int hand = a_event ? AttackHand(a_event) : 0;
			if (hand == 0) {
				_AttackProcessButton(a_this, a_event, a_data);
				return;
			}

			auto& rt = a_event->GetRuntimeData();
			switch (Combat::Get(hand == 2).OnAttackButton(a_event)) {
			case ButtonAction::Swallow:
				return;
			case ButtonAction::PassAsPress:
				{
					// to samo zdarzenie, ale jako świeże wciśnięcie; potem przywracamy oryginał
					const float value = rt.value, held = rt.heldDownSecs;
					rt.value = 1.0f;
					rt.heldDownSecs = 0.0f;
					_AttackProcessButton(a_this, a_event, a_data);
					rt.value = value;
					rt.heldDownSecs = held;
					return;
				}
			case ButtonAction::PressThenPass:
				{
					const float value = rt.value, held = rt.heldDownSecs;
					rt.value = 1.0f;
					rt.heldDownSecs = 0.0f;
					_AttackProcessButton(a_this, a_event, a_data);
					rt.value = value;
					rt.heldDownSecs = held;
					_AttackProcessButton(a_this, a_event, a_data);
					return;
				}
			case ButtonAction::Pass:
			default:
				_AttackProcessButton(a_this, a_event, a_data);
				return;
			}
		}

		// ---- ActivateHandler::ProcessButton ----
		// Przytrzymanie "Aktywuj" (E) normalnie podnosi przedmiot/ciało. Wyłączamy to:
		// gra nie widzi trzymania, więc podnoszenie zostaje tylko pod akcją "Grab"
		// (do przypisania w ustawieniach sterowania).
		using ActivateButton_t = void(RE::ActivateHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
		ActivateButton_t* _ActivateProcessButton = nullptr;
		bool              g_grabInjecting = false;  // zdarzenie z naszego klawisza podnoszenia – przepuść

		void ActivateProcessButton(RE::ActivateHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			auto* ue = RE::UserEvents::GetSingleton();
			if (a_event && ue && !g_grabInjecting && Config::Get().enabled && Config::Get().disableHoldToGrab && a_event->QUserEvent() == ue->activate) {
				if (a_event->IsHeld()) {
					return;  // trzymanie E – ignorujemy (bez podnoszenia)
				}
				if (a_event->IsUp()) {
					// puszczenie: gra widzi je jak po krótkim kliknięciu
					auto&       rt = a_event->GetRuntimeData();
					const float held = rt.heldDownSecs;
					rt.heldDownSecs = (std::min)(held, 0.05f);
					_ActivateProcessButton(a_this, a_event, a_data);
					rt.heldDownSecs = held;
					return;
				}
			}
			_ActivateProcessButton(a_this, a_event, a_data);
		}

		// ---- własny klawisz podnoszenia ----
		// Gra (PC) nie ma osobnej akcji podnoszenia w menu sterowania – podnoszenie to
		// "przytrzymane E". Klawisz z [Activate] GrabKey udaje przed grą przytrzymane E
		// (tylko dla podnoszenia), więc działa jej własny mechanizm chwytania.
		void HandleGrabKey(RE::ButtonEvent* a_event)
		{
			const auto& cfg = Config::Get();
			auto*       ui = RE::UI::GetSingleton();
			auto*       controls = RE::PlayerControls::GetSingleton();
			auto*       ue = RE::UserEvents::GetSingleton();
			auto*       cmap = RE::ControlMap::GetSingleton();
			if (!cfg.enabled || !cfg.disableHoldToGrab || cfg.grabCode == 0 || !ui || ui->GameIsPaused() || !controls || !ue ||
				!_ActivateProcessButton || (cmap && !cmap->IsActivateControlsEnabled())) {
				return;
			}
			auto* handler = controls->GetActivateHandler();
			if (!handler) {
				return;
			}

			const RE::BSFixedString saved = a_event->QUserEvent();
			a_event->SetUserEvent(ue->activate);
			g_grabInjecting = true;
			_ActivateProcessButton(handler, a_event, &controls->data);
			g_grabInjecting = false;
			a_event->SetUserEvent(saved);
		}

		class GrabKeySink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static GrabKeySink* Get()
			{
				static GrabKeySink s;
				return &s;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				const auto& cfg = Config::Get();
				for (auto* e = a_event ? *a_event : nullptr; e; e = e->next) {
					if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
						continue;
					}
					auto*      b = e->AsButtonEvent();
					const auto device = b ? b->GetDevice() : RE::INPUT_DEVICES::kNone;
					const bool deviceOk = cfg.grabIsMouse ? device == RE::INPUT_DEVICES::kMouse : device == RE::INPUT_DEVICES::kKeyboard;
					if (b && deviceOk && b->GetIDCode() == cfg.grabCode) {
						HandleGrabKey(b);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---- LookHandler::ProcessMouseMove ----
		using ProcessMouseMove_t = void(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
		ProcessMouseMove_t* _LookProcessMouseMove = nullptr;

		void LookProcessMouseMove(RE::LookHandler* a_this, RE::MouseMoveEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (a_event) {
				// 1) gest w toku (np. kolejny zamach w serii) – obsługuje go ręka
				if (auto* hand = Combat::ActiveHand(); hand && hand->OnMouseMove(a_event->mouseInputX, a_event->mouseInputY)) {
					return;  // kamera zamrożona podczas gestu
				}
				// 2) chwilę po geście – połknij końcówkę ruchu
				if (Combat::SwallowMouse()) {
					return;
				}
				// 3) tarcza: szybkie pchnięcie w górę podczas bloku
				if (Combat::OnShieldMotion(a_event->mouseInputX, a_event->mouseInputY)) {
					return;
				}
			}
			_LookProcessMouseMove(a_this, a_event, a_data);
		}

		// ---- LookHandler::ProcessThumbstick ----
		using ProcessThumbstick_t = void(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
		ProcessThumbstick_t* _LookProcessThumbstick = nullptr;

		void LookProcessThumbstick(RE::LookHandler* a_this, RE::ThumbstickEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (a_event && a_event->IsRight()) {
				if (auto* hand = Combat::ActiveHand(); hand && hand->OnRightStick(a_event->xValue, a_event->yValue)) {
					return;
				}
				if (Combat::SwallowStick(a_event->xValue, a_event->yValue)) {
					return;  // po geście: dopóki drążek nie wróci do środka
				}
				if (Combat::OnShieldStick(a_event->xValue, a_event->yValue)) {
					return;
				}
			}
			_LookProcessThumbstick(a_this, a_event, a_data);
		}
	}

	void RegisterInput()
	{
		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(GrabKeySink::Get());
			logger::info("Klawisz podnoszenia: '{}' (kod {:#x}{})", Config::Get().grabKey, Config::Get().grabCode,
				Config::Get().grabIsMouse ? ", mysz" : "");
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> attackVtbl{ RE::VTABLE_AttackBlockHandler[0] };
		REL::Relocation<std::uintptr_t> lookVtbl{ RE::VTABLE_LookHandler[0] };
		REL::Relocation<std::uintptr_t> activateVtbl{ RE::VTABLE_ActivateHandler[0] };
		_ActivateProcessButton = reinterpret_cast<ActivateButton_t*>(activateVtbl.write_vfunc(Idx(0x4), &ActivateProcessButton));

		_AttackProcessButton = reinterpret_cast<ProcessButton_t*>(attackVtbl.write_vfunc(Idx(0x4), &AttackProcessButton));
		_LookProcessMouseMove = reinterpret_cast<ProcessMouseMove_t*>(lookVtbl.write_vfunc(Idx(0x3), &LookProcessMouseMove));
		_LookProcessThumbstick = reinterpret_cast<ProcessThumbstick_t*>(lookVtbl.write_vfunc(Idx(0x2), &LookProcessThumbstick));

		logger::info("Hooki zainstalowane (indeksy vfunc: button={}, mouse={}, stick={})", Idx(0x4), Idx(0x3), Idx(0x2));
	}
}
