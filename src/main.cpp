#include "Config.h"
#include "Hooks.h"
#include "Visual.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			// Config po załadowaniu danych – ActorValueList jest już dostępny.
			Config::Get().Load();
			Visual::RegisterEvents();
			Hooks::RegisterInput();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logger::info("DirectionalCombat {} – ładowanie", SKSE::GetPluginVersion().string());

	if (REL::Module::IsVR()) {
		logger::warn("Skyrim VR nie jest obsługiwany – wtyczka nic nie robi");
		return true;
	}

	Hooks::Install();
	Visual::Install();

	if (auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	}

	return true;
}
