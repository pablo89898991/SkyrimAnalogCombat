#include "Config.h"
#include "Hooks.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			// Config po załadowaniu danych – ActorValueList jest już dostępny.
			Config::Get().Load();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logger::info("DirectionalCombat {} – ładowanie", SKSE::GetPluginVersion().string());

	Hooks::Install();

	if (auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	}

	return true;
}
