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
		void Get(const char* a_key, int& a_out) const
		{
			if (const auto it = ini.find(a_key); it != ini.end()) {
				try {
					a_out = std::stoi(it->second);
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
	r.Get("mouse.strikeminspeed", strikeMinSpeed);
	r.Get("mouse.chainwindupmult", chainWindupMult);
	r.Get("mouse.sensitivity", mouseSensitivity);
	r.Get("mouse.inverty", invertY);

	r.Get("gamepad.enabled", gamepadEnabled);
	r.Get("gamepad.stickwindup", stickWindup);
	r.Get("gamepad.stickstrike", stickStrike);

	r.Get("timing.strikecooldown", strikeCooldown);
	r.Get("timing.tapmaxtime", tapMaxTime);
	r.Get("timing.strikebuffertime", strikeBufferTime);
	r.Get("timing.gesturesettle", gestureSettle);

	r.Get("animations.windupright", windupRight);
	r.Get("animations.windupup", windupUp);
	r.Get("animations.windupleft", windupLeft);
	r.Get("animations.windupdown", windupDown);
	for (int i = 0; i < kMaxSectors; ++i) {
		const auto key = "animations." + Lower(SectorKey(i));
		r.Get(key.c_str(), sectorEvents[static_cast<std::size_t>(i)]);
	}
	r.Get("animations.normalattack", normalAttackEvent);
	r.Get("animations.fallback", fallbackEvent);
	r.Get("animations.swapleftright", swapLeftRight);

	const auto lookupAV = [](const std::string& a_name) {
		if (a_name.empty()) {
			return RE::ActorValue::kNone;
		}
		const auto av = RE::ActorValueList::LookupActorValueByName(a_name.c_str());
		if (av == RE::ActorValue::kNone) {
			logger::warn("Nieznana wartość aktora '{}' – ta integracja wyłączona", a_name);
		}
		return av;
	};
	r.Get("integration.directionactorvalue", directionActorValue);
	r.Get("integration.angleactorvalue", angleActorValue);
	directionAV = lookupAV(directionActorValue);
	angleAV = lookupAV(angleActorValue);

	r.Get("floatingweapon.hidearms", hideArms);
	r.Get("floatingweapon.hidearmsonlywhendrawn", hideArmsOnlyWhenDrawn);
	r.Get("general.livereload", liveReload);
	r.Get("activate.disableholdtograb", disableHoldToGrab);

	r.Get("lefthand.enabled", leftEnabled);
	r.Get("lefthand.attackevent", leftAttackEvent);
	r.Get("lefthand.powerattackevent", leftPowerEvent);

	r.Get("shield.enabled", shieldEnabled);
	r.Get("shield.bashevent", bashEvent);
	r.Get("shield.flickthreshold", bashFlick);
	r.Get("shield.flickwindow", bashWindow);
	r.Get("shield.cooldown", bashCooldown);
	r.Get("shield.stickpush", bashStick);
	r.Get("shield.swallowspeed", bashSwallowSpeed);
	r.Get("shield.freezecamerawhileblocking", freezeCameraWhileBlocking);
	bashWindow = (std::max)(0.02f, bashWindow);

	r.Get("ranged.enabled", rangedEnabled);
	r.Get("ranged.bow", rangedBow);
	r.Get("ranged.crossbow", rangedCrossbow);
	r.Get("ranged.staff", rangedStaff);
	r.Get("ranged.pullthreshold", rangedPull);
	r.Get("ranged.pushthreshold", rangedPush);
	r.Get("ranged.freezecamera", rangedFreezeCamera);
	r.Get("ranged.stickpull", rangedStickPull);
	r.Get("floatingweapon.hidearmsranged", hideArmsRanged);
	rangedPull = (std::max)(1.0f, rangedPull);
	rangedPush = (std::max)(1.0f, rangedPush);
	rangedStickPull = std::clamp(rangedStickPull, 0.1f, 1.0f);

	r.Get("procedural.enabled", procEnabled);
	r.Get("procedural.attackevent", procAttackEvent);
	r.Get("procedural.powerattackevent", procPowerEvent);
	r.Get("procedural.powerchargetime", powerChargeTime);
	r.Get("procedural.distance", procDistance);
	r.Get("procedural.centerx", procCenterX);
	r.Get("procedural.centerz", procCenterZ);
	r.Get("procedural.radius", procRadius);
	r.Get("procedural.pullback", procPullBack);
	r.Get("procedural.reach", procReach);
	r.Get("procedural.restx", procRestX);
	r.Get("procedural.restz", procRestZ);
	r.Get("procedural.resttilt", procRestTilt);
	r.Get("procedural.swingtimescale", swingTimeScale);
	r.Get("procedural.smoothing", procSmoothing);
	r.Get("procedural.bladeroll", procBladeRoll);
	r.Get("procedural.flipblade", procFlipBlade);
	r.Get("procedural.pivotatcenter", procPivotAtCenter);
	swingTimeScale = (std::max)(0.2f, swingTimeScale);

	r.Get("feel.weaponweight", weaponWeight);
	r.Get("feel.synctohits", syncToHits);
	r.Get("feel.overshoot", overshoot);
	r.Get("feel.walkbob", walkBob);
	r.Get("feel.camerasway", cameraSway);
	r.Get("feel.breathing", breathing);
	r.Get("feel.hitstop", hitStop);
	r.Get("feel.hitrecoil", hitRecoil);
	r.Get("feel.hitshake", hitShake);
	r.Get("feel.blockpose", blockPose);
	r.Get("feel.drawanimation", drawAnimation);
	overshoot = std::clamp(overshoot, 0.0f, 0.6f);
	procSmoothing = (std::max)(0.1f, procSmoothing);

	r.Get("general.directions", directions);
	if (directions != 4 && directions != 8 && directions != 16) {
		logger::warn("Directions={} nieobsługiwane (dozwolone 4/8/16) – ustawiam 16", directions);
		directions = 16;
	}

	mouseWindupThreshold = (std::max)(1.0f, mouseWindupThreshold);
	mouseStrikeThreshold = (std::max)(1.0f, mouseStrikeThreshold);
	stickWindup = std::clamp(stickWindup, 0.1f, 1.0f);
	stickStrike = std::clamp(stickStrike, 0.1f, 1.0f);

	if (debugLog) {
		spdlog::set_level(spdlog::level::debug);
		spdlog::flush_on(spdlog::level::debug);
	}

	logger::info("Konfiguracja: enabled={} directions={} freezeCamera={} chain={} windup={} strike={} gamepad={}",
		enabled, directions, freezeCamera, chainStrikes, mouseWindupThreshold, mouseStrikeThreshold, gamepadEnabled);
}

std::string Config::SectorKey(int a_sector16)
{
	// 0 -> "Windup_000", 1 -> "Windup_022", 2 -> "Windup_045", ... 15 -> "Windup_337"
	const int deg = static_cast<int>(a_sector16 * 22.5f);
	char buf[16]{};
	std::snprintf(buf, sizeof(buf), "Windup_%03d", deg);
	return buf;
}
