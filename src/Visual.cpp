#include "Visual.h"

#include "Combat.h"
#include "Config.h"

namespace Visual
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		constexpr float kDegToRad = kPi / 180.0f;

		using P3 = RE::NiPoint3;

		P3    Add(const P3& a, const P3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		P3    Sub(const P3& a, const P3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		P3    Mul(const P3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
		float Dot(const P3& a, const P3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		P3    Cross(const P3& a, const P3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		float Len(const P3& a) { return std::sqrt(Dot(a, a)); }
		P3    Norm(const P3& a, const P3& fallback = { 0.0f, 0.0f, 1.0f })
		{
			const float l = Len(a);
			return l > 1e-5f ? Mul(a, 1.0f / l) : fallback;
		}
		P3 Lerp(const P3& a, const P3& b, float t) { return Add(a, Mul(Sub(b, a), t)); }

		P3 Column(const RE::NiMatrix3& m, int c) { return { m.entry[0][c], m.entry[1][c], m.entry[2][c] }; }
		void SetColumn(RE::NiMatrix3& m, int c, const P3& v)
		{
			m.entry[0][c] = v.x;
			m.entry[1][c] = v.y;
			m.entry[2][c] = v.z;
		}

		// Macierz obrotu wokół osi (Rodrigues).
		RE::NiMatrix3 AxisAngle(P3 a_axis, float a_rad)
		{
			RE::NiMatrix3 r;  // jednostkowa
			if (Len(a_axis) < 1e-6f || std::abs(a_rad) < 1e-6f) {
				return r;
			}
			const P3    n = Norm(a_axis);
			const float c = std::cos(a_rad), s = std::sin(a_rad), t = 1.0f - c;
			r.entry[0][0] = t * n.x * n.x + c;
			r.entry[0][1] = t * n.x * n.y - s * n.z;
			r.entry[0][2] = t * n.x * n.z + s * n.y;
			r.entry[1][0] = t * n.x * n.y + s * n.z;
			r.entry[1][1] = t * n.y * n.y + c;
			r.entry[1][2] = t * n.y * n.z - s * n.x;
			r.entry[2][0] = t * n.x * n.z - s * n.y;
			r.entry[2][1] = t * n.y * n.z + s * n.x;
			r.entry[2][2] = t * n.z * n.z + c;
			return r;
		}

		bool NameIs(const RE::NiAVObject* a_obj, std::string_view a_name)
		{
			const char* n = a_obj ? a_obj->name.c_str() : nullptr;
			if (!n) {
				return false;
			}
			const std::string_view s{ n };
			return s.size() == a_name.size() &&
			       std::equal(s.begin(), s.end(), a_name.begin(), [](char a, char b) {
					   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
				   });
		}

		bool IsUnderWeaponNode(const RE::NiAVObject* a_obj)
		{
			for (auto* p = a_obj ? a_obj->parent : nullptr; p; p = p->parent) {
				if (NameIs(p, "WEAPON"sv) || NameIs(p, "SHIELD"sv)) {
					return true;
				}
			}
			return false;
		}

		bool Near(const RE::NiTransform& a, const RE::NiTransform& b)
		{
			constexpr float eps = 1e-3f;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					if (std::abs(a.rotate.entry[i][j] - b.rotate.entry[i][j]) > eps) {
						return false;
					}
				}
			}
			return std::abs(a.translate.x - b.translate.x) < eps &&
			       std::abs(a.translate.y - b.translate.y) < eps &&
			       std::abs(a.translate.z - b.translate.z) < eps;
		}

		bool g_proceduralActive = false;

		// ---------------- przeładowanie .ini na żywo ----------------
		void CheckLiveReload(float a_delta)
		{
			static float                           timer = 0.0f;
			static std::filesystem::file_time_type lastWrite{};
			static bool                            init = false;

			timer += a_delta;
			if (timer < 1.0f) {
				return;
			}
			timer = 0.0f;

			std::error_code ec;
			const auto      t = std::filesystem::last_write_time("Data/SKSE/Plugins/DirectionalCombat.ini", ec);
			if (ec) {
				return;
			}
			if (!init) {
				init = true;
				lastWrite = t;
				return;
			}
			if (t != lastWrite) {
				lastWrite = t;
				if (Config::Get().liveReload) {
					Config::Get().Load();
					logger::info("Plik .ini przeładowany");
				}
			}
		}

		// ---------------- ukrywanie rąk ----------------
		struct ArmsState
		{
			RE::NiAVObject*                     root = nullptr;
			std::unordered_set<RE::BSGeometry*> culled;
		} g_arms;

		void UpdateArms(RE::NiAVObject* a_root, bool a_hide)
		{
			if (a_root != g_arms.root) {
				g_arms.root = a_root;  // nowy model 1. osoby – stare wskaźniki są nieważne
				g_arms.culled.clear();
			}
			if (!a_root || (!a_hide && g_arms.culled.empty())) {
				return;
			}

			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geom) {
				if (a_hide) {
					if (a_geom->GetGeometryRuntimeData().skinInstance && !IsUnderWeaponNode(a_geom) && !a_geom->GetAppCulled()) {
						a_geom->SetAppCulled(true);
						g_arms.culled.insert(a_geom);
					}
				} else if (g_arms.culled.contains(a_geom)) {
					a_geom->SetAppCulled(false);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			if (!a_hide) {
				g_arms.culled.clear();
			}
		}

		// ---------------- animacja proceduralna ----------------
		// Poza broni w przestrzeni kamery: X = prawo, Y = przód, Z = góra.
		struct Pose
		{
			P3 pos{};
			P3 blade{ 0.0f, 0.0f, 1.0f };   // kierunek ostrza
			P3 plane{ 1.0f, 0.0f, 0.0f };   // normalna płaszczyzny cięcia (płaz ostrza)
		};

		struct WeaponState
		{
			RE::NiAVObject* node = nullptr;
			RE::NiAVObject* weaponObj = nullptr;  // aktualny model broni (dziecko WEAPON)
			P3              bladeLocal{ 0.0f, 1.0f, 0.0f };
			float           halfLen = 0.0f;
			bool            bladeKnown = false;

			RE::NiTransform base{};
			RE::NiTransform written{};
			bool            applied = false;

			Pose          cur{};
			bool          curValid = false;
			std::uint32_t seenStrike = 0;
			bool          sweeping = false;
			float         sweepTime = 0.0f;
			float         sweepAngle = 0.0f;
		} g_weap;

		void ReleaseWeapon()
		{
			if (g_weap.applied && g_weap.node && Near(g_weap.node->local, g_weap.written)) {
				g_weap.node->local = g_weap.base;
			}
			g_weap.applied = false;
			g_weap.curValid = false;
			g_weap.sweeping = false;
		}

		// Kierunek ostrza w przestrzeni węzła WEAPON = środek bryły broni względem uchwytu.
		void DetectBlade(RE::NiAVObject* a_node)
		{
			auto*           asNode = a_node->AsNode();
			RE::NiAVObject* weapon = nullptr;
			if (asNode) {
				for (auto& child : asNode->GetChildren()) {
					if (child) {
						weapon = child.get();
						break;
					}
				}
			}
			if (weapon != g_weap.weaponObj) {
				g_weap.weaponObj = weapon;
				g_weap.bladeKnown = false;
			}
			if (!weapon || g_weap.bladeKnown) {
				return;
			}

			const auto& b = weapon->worldBound;
			const auto& w = a_node->world;
			if (b.radius <= 0.0f || w.scale <= 1e-4f) {
				return;  // jeszcze nie policzone przez silnik – spróbujemy w następnej klatce
			}
			const P3 rel = Sub(b.center, w.translate);
			const P3 local = Mul(w.rotate.Transpose() * rel, 1.0f / w.scale);
			if (Len(local) < 1.0f) {
				return;
			}
			g_weap.bladeLocal = Norm(local);
			g_weap.halfLen = Len(local);
			g_weap.bladeKnown = true;
			logger::debug("Broń: oś ostrza ({:.2f}, {:.2f}, {:.2f}), pół długości {:.1f}",
				g_weap.bladeLocal.x, g_weap.bladeLocal.y, g_weap.bladeLocal.z, g_weap.halfLen);
		}

		// ---------------- profil broni (waga / tempo) ----------------
		struct Profile
		{
			float duration = 0.24f;  // czas cięcia (s)
			float radius = 1.0f;     // mnożnik zasięgu łuku
			float agility = 1.0f;    // jak szybko broń wraca / reaguje
			float recoil = 1.0f;     // siła odbicia przy trafieniu
			int   type = -1;         // typ broni (klucz synchronizacji)
		};

		// Zmierzony czas od ciosu do trafienia, osobno dla każdego typu broni.
		std::array<float, 16> g_hitDelay{};  // 0 = jeszcze nie zmierzono

		Profile GetProfile(RE::PlayerCharacter* a_player, const Config& c)
		{
			Profile pr;
			auto*   obj = a_player->GetEquippedObject(false);
			auto*   weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
			if (c.weaponWeight && weap) {
				switch (weap->GetWeaponType()) {
				case RE::WeaponTypes::kOneHandDagger:
					pr = { 0.16f, 0.80f, 1.40f, 0.6f };
					break;
				case RE::WeaponTypes::kOneHandSword:
					pr = { 0.24f, 1.00f, 1.00f, 0.9f };
					break;
				case RE::WeaponTypes::kOneHandAxe:
					pr = { 0.27f, 0.95f, 0.90f, 1.1f };
					break;
				case RE::WeaponTypes::kOneHandMace:
					pr = { 0.30f, 0.95f, 0.85f, 1.3f };
					break;
				case RE::WeaponTypes::kTwoHandSword:
					pr = { 0.36f, 1.20f, 0.65f, 1.3f };
					break;
				case RE::WeaponTypes::kTwoHandAxe:
					pr = { 0.42f, 1.25f, 0.55f, 1.6f };
					break;
				default:
					break;
				}
				pr.type = static_cast<int>(weap->GetWeaponType());
				// prędkość broni z gry (sztylety ~1.3, młoty ~0.7)
				const float speed = std::clamp(weap->GetSpeed(), 0.5f, 1.6f);
				pr.duration /= speed;
			}
			// Synchronizacja: ostrze ma przejść przez środek ekranu w chwili, gdy gra liczy trafienie.
			if (c.syncToHits && pr.type >= 0 && pr.type < 16 && g_hitDelay[pr.type] > 0.0f) {
				const float e = 0.5f / (1.0f + c.overshoot);            // ułamek łuku = środek ekranu
				const float shaped = std::acos(1.0f - 2.0f * e) / kPi;  // odwrotność krzywej czasu
				const float tCenter = std::pow(shaped, 1.0f / 1.35f);
				const float synced = g_hitDelay[pr.type] / tCenter;
				pr.duration = std::clamp(synced, pr.duration * 0.6f, pr.duration * 1.8f);
			}
			pr.duration *= c.swingTimeScale;
			return pr;
		}

		// ---------------- efekty "naturalności" ----------------
		struct Feel
		{
			float time = 0.0f;
			float bobPhase = 0.0f;
			float bobAmount = 0.0f;
			P3    sway{};          // spóźnienie przy obrocie kamery (wygładzone)
			P3    prevFwd{};
			bool  prevValid = false;
			float recoil = 0.0f;   // 0..1, zanika
			float hitStop = 0.0f;  // pozostały czas zatrzymania cięcia
			bool  sprint = false;
		} g_feel;

		std::atomic<int> g_pendingHit{ 0 };  // 1 = trafienie, 2 = mocne / zablokowane

		class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			static HitSink* Get()
			{
				static HitSink s;
				return &s;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (a_event && player && a_event->cause.get() == player && a_event->projectile == 0 &&
					!a_event->flags.any(RE::TESHitEvent::Flag::kBashAttack)) {
					const bool strong = a_event->flags.any(RE::TESHitEvent::Flag::kPowerAttack, RE::TESHitEvent::Flag::kHitBlocked);
					int        v = strong ? 2 : 1;
					int        cur = g_pendingHit.load();
					while (v > cur && !g_pendingHit.compare_exchange_weak(cur, v)) {}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		Pose RestPose(const Config& c)
		{
			const float t = (c.procRestTilt + (g_feel.sprint ? 30.0f : 0.0f)) * kDegToRad;
			const float drop = g_feel.sprint ? -8.0f : 0.0f;
			return { { c.procRestX, c.procDistance, c.procRestZ + drop }, Norm({ 0.0f, std::sin(t), std::cos(t) }), { 1.0f, 0.0f, 0.0f } };
		}

		Pose BlockPose(const Config& c)
		{
			// broń w poprzek ekranu, lekko uniesiona, ostrze w lewo
			return { { c.procCenterX, c.procDistance * 0.8f, c.procCenterZ + 6.0f }, Norm({ -1.0f, 0.25f, 0.2f }), { 0.0f, 0.3f, 1.0f } };
		}

		// Zamach: broń odciągnięta na okręgu pod kątem a_deg, a_amount 0..1.
		Pose WindupPose(const Config& c, const Profile& pr, float a_deg, float a_amount)
		{
			const Pose  rest = RestPose(c);
			const float r = a_deg * kDegToRad;
			const P3    d{ std::cos(r), 0.0f, std::sin(r) };
			const P3    fwd{ 0.0f, 1.0f, 0.0f };
			const P3    center{ c.procCenterX, c.procDistance, c.procCenterZ };

			Pose p;
			p.pos = Lerp(rest.pos, Sub(Add(center, Mul(d, c.procRadius * pr.radius)), Mul(fwd, c.procPullBack)), a_amount);
			p.blade = Norm(Lerp(rest.blade, Norm(Sub(d, Mul(fwd, 0.35f))), a_amount));
			p.plane = Norm(Cross(d, fwd), rest.plane);
			return p;
		}

		// Cięcie: łuk (wokół "barku") z kąta a_deg przez przód na przeciwną stronę i trochę dalej.
		// Krzywa czasu: powolny start, maksymalna prędkość w 2/3, ostre wyhamowanie – jak prawdziwy zamach.
		Pose SwingPose(const Config& c, const Profile& pr, float a_deg, float a_t, bool a_power)
		{
			const float t = std::clamp(a_t, 0.0f, 1.0f);
			const float shaped = std::pow(t, 1.35f);
			const float e = 0.5f - 0.5f * std::cos(kPi * shaped);
			const float phi = kPi * (1.0f + c.overshoot) * e;  // kąt na łuku

			const float r = a_deg * kDegToRad;
			const P3    d{ std::cos(r), 0.0f, std::sin(r) };
			const P3    fwd{ 0.0f, 1.0f, 0.0f };
			const P3    center{ c.procCenterX, c.procDistance, c.procCenterZ };
			const float radius = c.procRadius * pr.radius * (a_power ? 1.15f : 1.0f);
			const float reach = c.procReach * (a_power ? 1.3f : 1.0f);

			Pose p;
			p.pos = Add(Add(center, Mul(d, radius * std::cos(phi))),
				Mul(fwd, reach * std::sin((std::min)(phi, kPi)) - c.procPullBack * (std::max)(0.0f, std::cos(phi))));
			p.blade = Norm(Add(Mul(d, std::cos(phi)), Mul(fwd, std::sin(phi))));
			p.plane = Norm(Cross(d, fwd));
			return p;
		}

		Pose TargetPose(RE::PlayerCharacter* a_player, const Config& c, const Profile& pr, float a_delta)
		{
			const auto& combat = Combat::Get();

			if (combat.StrikeId() != g_weap.seenStrike) {
				g_weap.seenStrike = combat.StrikeId();
				g_weap.sweeping = true;
				g_weap.sweepTime = 0.0f;
				g_weap.sweepAngle = combat.StrikeAngle();
				g_weap.sweepPower = combat.StrikePower();
				g_weap.sweepStartTime = g_feel.time;
				g_weap.hitMeasured = false;
			}

			if (g_weap.sweeping) {
				if (g_feel.hitStop > 0.0f) {
					g_feel.hitStop -= a_delta;  // "hit-stop": cięcie na moment staje przy trafieniu
				} else {
					g_weap.sweepTime += a_delta;
				}
				const float dur = pr.duration * (g_weap.sweepPower ? 1.3f : 1.0f);
				const float t = g_weap.sweepTime / dur;
				if (t < 1.0f) {
					return SwingPose(c, pr, g_weap.sweepAngle, t, g_weap.sweepPower);
				}
				g_weap.sweeping = false;
			}

			const auto  aim = combat.GetAim();
			const float amount = std::sqrt(aim.x * aim.x + aim.y * aim.y);
			if (amount >= 0.01f) {
				const float deg = std::atan2(aim.y, aim.x) / kDegToRad;
				return WindupPose(c, pr, deg, (std::min)(amount, 1.0f));
			}
			if (c.blockPose && a_player->IsBlocking()) {
				return BlockPose(c);
			}
			return RestPose(c);
		}

		// Dodatkowe przesunięcie: kołysanie przy ruchu, oddech, spóźnienie za kamerą, odbicie po trafieniu.
		P3 FeelOffset(RE::PlayerCharacter* a_player, const Config& c, const Profile& pr, const RE::NiTransform& a_cam, float a_delta)
		{
			g_feel.time += a_delta;
			const float k = 1.0f - std::exp(-8.0f * a_delta);

			// --- ruch postaci ---
			RE::NiPoint3 vel;
			a_player->GetLinearVelocity(vel);
			const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
			auto*       st = a_player->AsActorState();
			g_feel.sprint = st && st->IsSprinting();
			const float targetBob = (std::min)(speed / 350.0f, 1.0f);
			g_feel.bobAmount += (targetBob - g_feel.bobAmount) * k;
			g_feel.bobPhase += a_delta * (4.0f + 6.0f * targetBob);

			const float bobA = c.walkBob * g_feel.bobAmount * (g_feel.sprint ? 3.5f : 2.0f);
			P3 off{
				std::cos(g_feel.bobPhase) * bobA,
				0.0f,
				-std::abs(std::sin(g_feel.bobPhase)) * bobA * 0.8f
			};

			// --- oddech w spoczynku ---
			off.z += std::sin(g_feel.time * 1.7f) * 0.35f * c.breathing;
			off.x += std::sin(g_feel.time * 0.9f) * 0.2f * c.breathing;

			// --- spóźnienie za obrotem kamery (cięższa broń = większe) ---
			const P3 fwdNow = Column(a_cam.rotate, 1);
			if (g_feel.prevValid && a_delta > 1e-4f) {
				const P3    right = Column(a_cam.rotate, 0);
				const P3    up = Column(a_cam.rotate, 2);
				const float yawVel = Dot(g_feel.prevFwd, right) / a_delta;  // rad/s (w przybliżeniu)
				const float pitchVel = Dot(g_feel.prevFwd, up) / a_delta;
				const float amt = 2.5f * c.cameraSway / pr.agility;
				const P3    target{ std::clamp(yawVel * amt, -6.0f, 6.0f), 0.0f, std::clamp(pitchVel * amt, -4.0f, 4.0f) };
				g_feel.sway = Lerp(g_feel.sway, target, 1.0f - std::exp(-10.0f * a_delta));
			}
			g_feel.prevFwd = fwdNow;
			g_feel.prevValid = true;
			off = Add(off, g_feel.sway);

			// --- trafienie: hit-stop, odbicie, drżenie kamery ---
			if (const int hit = g_pendingHit.exchange(0); hit > 0) {
				// pomiar opóźnienia trafienia względem startu cięcia (do synchronizacji)
				if (!g_weap.hitMeasured && pr.type >= 0 && pr.type < 16 && !g_weap.sweepPower) {
					const float d = g_feel.time - g_weap.sweepStartTime;
					if (d > 0.05f && d < 1.0f) {
						float& avg = g_hitDelay[pr.type];
						avg = avg > 0.0f ? avg + (d - avg) * 0.3f : d;
						logger::debug("Synchronizacja: trafienie po {:.3f} s, średnia dla typu {} = {:.3f} s", d, pr.type, avg);
					}
					g_weap.hitMeasured = true;
				}
				const float strength = (hit == 2 ? 1.6f : 1.0f) * pr.recoil;
				if (g_weap.sweeping) {
					g_feel.hitStop = c.hitStop * strength;
				}
				g_feel.recoil = (std::min)(1.0f, g_feel.recoil + 0.7f * c.hitRecoil * strength);
				if (c.hitShake > 0.0f) {
					RE::ShakeCamera(c.hitShake * strength, a_player->GetPosition(), 0.15f);
				}
			}
			g_feel.recoil = (std::max)(0.0f, g_feel.recoil - a_delta * 4.0f);
			off.y -= g_feel.recoil * 6.0f;
			off.z += g_feel.recoil * 2.0f;

			// podczas cięcia efekty słabsze, żeby nie psuły łuku
			return g_weap.sweeping ? Mul(off, 0.3f) : off;
		}

		// Macierz obrotu z bazy (ostrze, płaz) w przestrzeni kamery.
		RE::NiMatrix3 PoseRotation(const Config& c, const Pose& a_pose)
		{
			// lokalnie (model broni)
			P3 bl = g_weap.bladeLocal;
			if (c.procFlipBlade) {
				bl = Mul(bl, -1.0f);
			}
			// oś lokalna najmniej zgodna z ostrzem -> prostopadła "płaz"
			P3 pick{ 1.0f, 0.0f, 0.0f };
			if (std::abs(bl.y) <= std::abs(bl.x) && std::abs(bl.y) <= std::abs(bl.z)) {
				pick = { 0.0f, 1.0f, 0.0f };
			} else if (std::abs(bl.z) <= std::abs(bl.x) && std::abs(bl.z) <= std::abs(bl.y)) {
				pick = { 0.0f, 0.0f, 1.0f };
			}
			const P3 el = Norm(Sub(pick, Mul(bl, Dot(pick, bl))));
			const P3 tl = Cross(bl, el);

			// w kamerze
			const P3 bc = Norm(a_pose.blade);
			const P3 ec = Norm(Sub(a_pose.plane, Mul(bc, Dot(a_pose.plane, bc))), Norm(Cross(bc, { 0.0f, 1.0f, 0.0f }), { 1.0f, 0.0f, 0.0f }));
			const P3 tc = Cross(bc, ec);

			RE::NiMatrix3 cam, loc;
			SetColumn(cam, 0, bc);
			SetColumn(cam, 1, ec);
			SetColumn(cam, 2, tc);
			SetColumn(loc, 0, bl);
			SetColumn(loc, 1, el);
			SetColumn(loc, 2, tl);

			RE::NiMatrix3 r = cam * loc.Transpose();
			if (std::abs(c.procBladeRoll) > 0.01f) {
				r = AxisAngle(bc, c.procBladeRoll * kDegToRad) * r;
			}
			return r;
		}

		// Osie kamery. Które kolumny macierzy cameraRoot to "przód" i "góra", wykrywamy raz,
		// gdy gracz patrzy mniej więcej poziomo (porównując z kierunkiem z kąta obrotu gracza),
		// i zapamiętujemy – działa niezależnie od konwencji osi silnika.
		struct CamAxes
		{
			bool  known = false;
			int   fwdCol = 1;
			float fwdSign = 1.0f;
			int   upCol = 2;
			float upSign = 1.0f;
		} g_camAxes;

		bool CameraFrame(RE::PlayerCharacter* a_player, RE::NiTransform& a_out)
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* camRoot = camera ? camera->cameraRoot.get() : nullptr;
			if (!camRoot) {
				return false;
			}
			const auto& m = camRoot->world.rotate;

			if (!g_camAxes.known) {
				if (std::abs(a_player->data.angle.x) > 0.35f) {
					return false;  // poczekaj, aż gracz spojrzy w miarę poziomo
				}
				const float yaw = a_player->data.angle.z;
				const P3    fwdRef{ std::sin(yaw), std::cos(yaw), 0.0f };
				const P3    upRef{ 0.0f, 0.0f, 1.0f };
				auto        best = [&](const P3& ref, int& col, float& sign) {
					float bd = 0.0f;
					for (int i = 0; i < 3; ++i) {
						const float d = Dot(Column(m, i), ref);
						if (std::abs(d) > std::abs(bd)) {
							bd = d;
							col = i;
						}
					}
					sign = bd >= 0.0f ? 1.0f : -1.0f;
				};
				best(fwdRef, g_camAxes.fwdCol, g_camAxes.fwdSign);
				best(upRef, g_camAxes.upCol, g_camAxes.upSign);
				if (g_camAxes.fwdCol == g_camAxes.upCol) {
					return false;
				}
				g_camAxes.known = true;
				logger::info("Osie kamery: przód = kolumna {} ({:+}), góra = kolumna {} ({:+})",
					g_camAxes.fwdCol, g_camAxes.fwdSign, g_camAxes.upCol, g_camAxes.upSign);
			}

			const P3 fwd = Norm(Mul(Column(m, g_camAxes.fwdCol), g_camAxes.fwdSign));
			P3       up = Mul(Column(m, g_camAxes.upCol), g_camAxes.upSign);
			up = Norm(Sub(up, Mul(fwd, Dot(up, fwd))));
			const P3 right = Cross(fwd, up);

			a_out.rotate = RE::NiMatrix3();
			SetColumn(a_out.rotate, 0, right);
			SetColumn(a_out.rotate, 1, fwd);
			SetColumn(a_out.rotate, 2, up);
			a_out.translate = camRoot->world.translate;
			a_out.scale = 1.0f;
			return true;
		}

		void UpdateProcedural(RE::PlayerCharacter* a_player, RE::NiAVObject* a_root, float a_delta)
		{
			const auto& c = Config::Get();

			RE::NiAVObject* node = a_root ? a_root->GetObjectByName("WEAPON") : nullptr;
			if (node != g_weap.node) {
				g_weap = {};
				g_weap.node = node;
			}
			if (!node || !node->parent) {
				return;
			}

			if (!(g_weap.applied && Near(node->local, g_weap.written))) {
				g_weap.base = node->local;
			}

			RE::NiAVObject* prevWeapon = g_weap.weaponObj;
			DetectBlade(node);
			const bool newWeapon = prevWeapon != g_weap.weaponObj;

			RE::NiTransform cam;
			if (!CameraFrame(a_player, cam)) {
				return;
			}

			const Profile pr = GetProfile(a_player, c);

			// wygładzona poza
			const Pose  target = TargetPose(a_player, c, pr, a_delta);
			const float speed = (g_weap.sweeping ? c.procSmoothing * 4.0f : c.procSmoothing * 0.75f) * pr.agility;
			const float k = 1.0f - std::exp(-speed * a_delta);
			if (!g_weap.curValid || newWeapon) {
				g_weap.cur = target;
				if (c.drawAnimation) {
					// wyjęcie broni: startuje nisko i z boku, potem "wjeżdża" na miejsce
					g_weap.cur.pos = Add(target.pos, { 6.0f, -12.0f, -35.0f });
					g_weap.cur.blade = Norm({ 0.3f, 0.6f, 0.4f });
				}
				g_weap.curValid = true;
			} else {
				g_weap.cur.pos = Lerp(g_weap.cur.pos, target.pos, k);
				g_weap.cur.blade = Norm(Lerp(g_weap.cur.blade, target.blade, k));
				g_weap.cur.plane = Norm(Lerp(g_weap.cur.plane, target.plane, k), target.plane);
			}

			const P3 feel = FeelOffset(a_player, c, pr, cam, a_delta);

			// Transformacja broni w przestrzeni kamery.
			RE::NiTransform pose;
			pose.rotate = PoseRotation(c, g_weap.cur);
			P3 pos = Add(g_weap.cur.pos, feel);
			if (c.procPivotAtCenter && g_weap.bladeKnown) {
				const float scale = node->world.scale > 1e-4f ? node->world.scale : 1.0f;
				pos = Sub(pos, Mul(Norm(g_weap.cur.blade), g_weap.halfLen * scale));
			}
			pose.translate = pos;
			pose.scale = 1.0f;

			const RE::NiTransform desiredWorld = cam * pose;

			// Świat rodzica liczony z korzenia 1. osoby i lokalnych transformacji łańcucha
			// (lokalne są już z tej klatki animacji, korzeń i kamera z tej samej "starej" klatki,
			// więc różnica się znosi i broń nie drga przy ruchach ręki).
			RE::NiTransform chain;
			bool            reachedRoot = false;
			for (auto* n = static_cast<RE::NiAVObject*>(node->parent); n; n = n->parent) {
				if (n == a_root) {
					reachedRoot = true;
					break;
				}
				chain = n->local * chain;
			}
			const RE::NiTransform parentWorld = reachedRoot ? a_root->world * chain : node->parent->world;

			RE::NiTransform local = parentWorld.Invert() * desiredWorld;
			local.scale = g_weap.base.scale;

			node->local = local;
			g_weap.written = local;
			g_weap.applied = true;
		}

		void OnFrame(RE::PlayerCharacter* a_player, float a_delta)
		{
			CheckLiveReload(a_delta);
			Combat::Get().Update(a_delta);

			const auto&     cfg = Config::Get();
			auto*           camera = RE::PlayerCamera::GetSingleton();
			const bool      firstPerson = camera && camera->IsInFirstPerson();
			RE::NiAVObject* root = a_player->Get3D(true);
			const bool      meleeReady = Combat::MeleeReady();

			g_proceduralActive = cfg.enabled && cfg.procEnabled && firstPerson && meleeReady && root;

			const bool hide = cfg.enabled && firstPerson &&
			                  (g_proceduralActive || (cfg.hideArms && (!cfg.hideArmsOnlyWhenDrawn || meleeReady)));
			UpdateArms(root, hide);

			if (g_proceduralActive) {
				UpdateProcedural(a_player, root, a_delta);
			} else if (g_weap.applied || g_weap.curValid) {
				ReleaseWeapon();
			}
		}

		// ---- hook PlayerCharacter::Update (vfunc 0xAD) ----
		using Update_t = void(RE::PlayerCharacter*, float);
		Update_t* _Update = nullptr;

		void PlayerUpdate(RE::PlayerCharacter* a_this, float a_delta)
		{
			_Update(a_this, a_delta);
			OnFrame(a_this, a_delta);
		}
	}

	bool ProceduralActive()
	{
		return g_proceduralActive;
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		_Update = reinterpret_cast<Update_t*>(vtbl.write_vfunc(0xAD, &PlayerUpdate));
		logger::info("Hook PlayerCharacter::Update zainstalowany (animacja proceduralna)");
	}

	void RegisterEvents()
	{
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESHitEvent>(HitSink::Get());
			logger::info("Nasłuch trafień zarejestrowany");
		}
	}
}
