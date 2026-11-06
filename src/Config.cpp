#include "Config.h"

namespace
{
	std::string Trim(std::string s)
	{
		const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
		s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
		return s;
	}

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// Klucze w postaci "sekcja.klucz", małymi literami.
	std::unordered_map<std::string, std::string> ParseIni(const std::filesystem::path& a_path)
	{
		std::unordered_map<std::string, std::string> out;
		std::ifstream file(a_path);
		if (!file) {
			return out;
		}

		std::string section;
		std::string line;
		while (std::getline(file, line)) {
			if (const auto c = line.find_first_of(";#"); c != std::string::npos) {
				line.erase(c);
			}
			line = Trim(line);
			if (line.empty()) {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = Lower(Trim(line.substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			out[section + "." + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
		}
		return out;
	}

	struct Reader
	{
		const std::unordered_map<std::string, std::string>& ini;

		void Get(const char* a_key, bool& a_out) const
		{
			if (const auto it = ini.find(a_key); it != ini.end()) {
				const auto v = Lower(it->second);
				a_out = (v == "1" || v == "true" || v == "yes" || v == "on");
			}
		}
		void Get(const char* a_key, float& a_out) const
		{
			if (const auto it = ini.find(a_key); it != ini.end()) {
				try {
					a_out = std::stof(it->second);
				} catch (...) {
					logger::warn("Niepoprawna liczba dla {}: '{}'", a_key, it->second);
				}
			}
		}
		void Get(const char* a_key, std::string& a_out) const
		{
			if (const auto it = ini.find(a_key); it != ini.end()) {
				a_out = it->second;
			}
		}
	};
}

void Config::Load()
{
	const std::filesystem::path path = "Data/SKSE/Plugins/DirectionalCombat.ini";
	const auto ini = ParseIni(path);
	if (ini.empty()) {
		logger::warn("Brak lub pusty plik {}, używam ustawień domyślnych", path.string());
	}
	const Reader r{ ini };

	r.Get("general.enabled", enabled);
	r.Get("general.freezecamera", freezeCamera);
	r.Get("general.chainstrikes", chainStrikes);
	r.Get("general.strikeonrelease", strikeOnRelease);
	r.Get("general.tapfornormalattack", tapForNormalAttack);
	r.Get("general.debuglog", debugLog);

	r.Get("mouse.windupthreshold", mouseWindupThreshold);
	r.Get("mouse.strikethreshold", mouseStrikeThreshold);
	r.Get("mouse.sensitivity", mouseSensitivity);
	r.Get("mouse.inverty", invertY);

	r.Get("gamepad.enabled", gamepadEnabled);
	r.Get("gamepad.stickwindup", stickWindup);
	r.Get("gamepad.stickstrike", stickStrike);

	r.Get("timing.strikecooldown", strikeCooldown);
	r.Get("timing.tapmaxtime", tapMaxTime);

	r.Get("animations.windupleft", events[static_cast<std::size_t>(Dir::Left)]);
	r.Get("animations.windupright", events[static_cast<std::size_t>(Dir::Right)]);
	r.Get("animations.windupup", events[static_cast<std::size_t>(Dir::Up)]);
	r.Get("animations.windupdown", events[static_cast<std::size_t>(Dir::Down)]);
	r.Get("animations.normalattack", normalAttackEvent);
	r.Get("animations.fallback", fallbackEvent);
	r.Get("animations.swapleftright", swapLeftRight);

	r.Get("integration.directionactorvalue", directionActorValue);
	directionAV = RE::ActorValue::kNone;
	if (!directionActorValue.empty()) {
		directionAV = RE::ActorValueList::LookupActorValueByName(directionActorValue.c_str());
		if (directionAV == RE::ActorValue::kNone) {
			logger::warn("Nieznana wartość aktora '{}' – integracja wyłączona", directionActorValue);
		}
	}

	mouseWindupThreshold = std::max(1.0f, mouseWindupThreshold);
	mouseStrikeThreshold = std::max(1.0f, mouseStrikeThreshold);
	stickWindup = std::clamp(stickWindup, 0.1f, 1.0f);
	stickStrike = std::clamp(stickStrike, 0.1f, 1.0f);

	if (debugLog) {
		spdlog::set_level(spdlog::level::debug);
		spdlog::flush_on(spdlog::level::debug);
	}

	logger::info("Konfiguracja: enabled={} freezeCamera={} chain={} windup={} strike={} gamepad={}",
		enabled, freezeCamera, chainStrikes, mouseWindupThreshold, mouseStrikeThreshold, gamepadEnabled);
}
