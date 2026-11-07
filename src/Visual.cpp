#include "Visual.h"

#include "Combat.h"
#include "Config.h"

namespace Visual
{
	namespace
	{
		constexpr float kDegToRad = 3.14159265358979f / 180.0f;

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

		// Czy geometria wisi pod węzłem broni/tarczy (tych NIE ukrywamy).
		bool IsUnderWeaponNode(const RE::NiAVObject* a_obj)
		{
			for (auto* p = a_obj ? a_obj->parent : nullptr; p; p = p->parent) {
				if (NameIs(p, "WEAPON"sv) || NameIs(p, "SHIELD"sv)) {
					return true;
				}
			}
			return false;
		}

		RE::NiPoint3 Column(const RE::NiMatrix3& a_m, int a_c)
		{
			return { a_m.entry[0][a_c], a_m.entry[1][a_c], a_m.entry[2][a_c] };
		}

		// Macierz obrotu wokół osi (Rodrigues).
		RE::NiMatrix3 AxisAngle(RE::NiPoint3 a_axis, float a_rad)
		{
			const float len = std::sqrt(a_axis.x * a_axis.x + a_axis.y * a_axis.y + a_axis.z * a_axis.z);
			RE::NiMatrix3 r;  // jednostkowa
			if (len < 1e-6f || std::abs(a_rad) < 1e-6f) {
				return r;
			}
			const float x = a_axis.x / len, y = a_axis.y / len, z = a_axis.z / len;
			const float c = std::cos(a_rad), s = std::sin(a_rad), t = 1.0f - c;
			r.entry[0][0] = t * x * x + c;
			r.entry[0][1] = t * x * y - s * z;
			r.entry[0][2] = t * x * z + s * y;
			r.entry[1][0] = t * x * y + s * z;
			r.entry[1][1] = t * y * y + c;
			r.entry[1][2] = t * y * z - s * x;
			r.entry[2][0] = t * x * z - s * y;
			r.entry[2][1] = t * y * z + s * x;
			r.entry[2][2] = t * z * z + c;
			return r;
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

			// Zawsze przechodzimy po ŻYWYM drzewie, więc nie dotykamy zwolnionych obiektów.
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

		// ---------------- broń podążająca za zamachem ----------------
		struct WeaponState
		{
			RE::NiAVObject* node = nullptr;
			RE::NiTransform base{};
			RE::NiTransform written{};
			bool            applied = false;
			float           curX = 0.0f;
			float           curY = 0.0f;
		} g_weap;

		void RestoreWeapon()
		{
			if (g_weap.applied && g_weap.node && Near(g_weap.node->local, g_weap.written)) {
				g_weap.node->local = g_weap.base;
			}
			g_weap.applied = false;
		}

		void UpdateWeapon(RE::NiAVObject* a_root, float a_delta, bool a_active)
		{
			const auto& cfg = Config::Get();

			RE::NiAVObject* node = a_root ? a_root->GetObjectByName("WEAPON") : nullptr;
			if (node != g_weap.node) {
				g_weap = {};  // inny model – zapominamy stan (bez dotykania starego węzła)
				g_weap.node = node;
			}
			if (!node || !node->parent) {
				return;
			}

			// wygładzanie w stronę celu
			Combat::Aim target{};
			if (a_active) {
				target = Combat::Get().GetAim();
			}
			const float k = std::clamp(a_delta * cfg.smoothing, 0.0f, 1.0f);
			g_weap.curX += (target.x - g_weap.curX) * k;
			g_weap.curY += (target.y - g_weap.curY) * k;

			if (std::abs(g_weap.curX) < 0.002f && std::abs(g_weap.curY) < 0.002f) {
				g_weap.curX = g_weap.curY = 0.0f;
				RestoreWeapon();
				return;
			}

			// Baza = to, co ustawiła gra/animacja. Jeśli od zeszłej klatki nikt nie nadpisał
			// naszej wartości, bazą jest dalej zapamiętany oryginał.
			if (!(g_weap.applied && Near(node->local, g_weap.written))) {
				g_weap.base = node->local;
			}

			auto* camera = RE::PlayerCamera::GetSingleton();
			RE::NiNode* camRoot = camera ? camera->cameraRoot.get() : nullptr;
			if (!camRoot) {
				return;
			}

			// Osie kamery w świecie (Skyrim: X = prawo, Y = przód, Z = góra).
			const auto& camRot = camRoot->world.rotate;
			const RE::NiPoint3 right = Column(camRot, 0);
			const RE::NiPoint3 fwd = Column(camRot, 1);
			const RE::NiPoint3 up = Column(camRot, 2);

			// Przesunięcie w świecie -> do przestrzeni rodzica węzła WEAPON.
			const auto&  parent = node->parent->world;
			const auto   pInv = parent.rotate.Transpose();
			const float  pScale = parent.scale > 1e-4f ? parent.scale : 1.0f;
			const float  ox = g_weap.curX * cfg.maxOffset;
			const float  oy = g_weap.curY * cfg.maxOffset;
			RE::NiPoint3 worldOff{
				right.x * ox + up.x * oy,
				right.y * ox + up.y * oy,
				right.z * ox + up.z * oy
			};
			RE::NiPoint3 localOff = pInv * worldOff;
			localOff.x /= pScale;
			localOff.y /= pScale;
			localOff.z /= pScale;

			// Przechył: w bok wokół osi "przód", w górę/dół wokół osi "prawo".
			const RE::NiMatrix3 tiltWorld =
				AxisAngle(fwd, -g_weap.curX * cfg.maxTilt * kDegToRad) *
				AxisAngle(right, g_weap.curY * cfg.maxTilt * kDegToRad);
			const RE::NiMatrix3 tiltLocal = pInv * tiltWorld * parent.rotate;

			RE::NiTransform t = g_weap.base;
			t.translate.x += localOff.x;
			t.translate.y += localOff.y;
			t.translate.z += localOff.z;
			t.rotate = tiltLocal * g_weap.base.rotate;

			node->local = t;
			g_weap.written = t;
			g_weap.applied = true;
		}

		void OnFrame(RE::PlayerCharacter* a_player, float a_delta)
		{
			const auto& cfg = Config::Get();
			auto*       camera = RE::PlayerCamera::GetSingleton();
			const bool  firstPerson = camera && camera->IsInFirstPerson();
			RE::NiAVObject* root = a_player->Get3D(true);

			const bool meleeReady = Combat::MeleeReady();

			const bool hide = cfg.enabled && cfg.hideArms && firstPerson && (!cfg.hideArmsOnlyWhenDrawn || meleeReady);
			UpdateArms(root, hide);

			const bool follow = cfg.enabled && cfg.followAim && firstPerson;
			if (follow || g_weap.applied) {
				UpdateWeapon(root, a_delta, follow && meleeReady);
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

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		_Update = reinterpret_cast<Update_t*>(vtbl.write_vfunc(0xAD, &PlayerUpdate));
		logger::info("Hook PlayerCharacter::Update zainstalowany (efekt latającej broni)");
	}
}
