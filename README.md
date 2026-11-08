# Directional Combat (Skyrim SE / AE)

Analogowy / kierunkowy tryb walki bronią białą w stylu *Dead Island (Analog Combat)* i *Mordhau*.

## Jak to działa w grze

1. Wyjmij broń białą (miecz, topór, buława, sztylet, dwuręczna).
2. **Przytrzymaj LPM**. Kamera się zatrzymuje, a ruch myszy steruje bronią.
3. **Odciągnij mysz** w jedną stronę: to zamach (np. w prawo).
4. **Szarpnij w przeciwną stronę.** Cios leci z wybranej strony (zamach w prawo = cięcie z prawej do lewej).
5. Bez puszczania LPM możesz machać tam i z powrotem (`ChainStrikes`).
   Puszczenie LPM po zamachu też zadaje cios (`StrikeOnRelease`).
6. Szybkie kliknięcie bez ruchu myszy daje zwykły atak.

**Pad:** przytrzymaj prawy spust, wychyl prawy drążek (zamach), potem przerzuć go na drugą stronę.
Steam Controller z żyroskopem zmapowanym na mysz działa tak samo jak mysz.

**16 kierunków na całym okręgu** (jak w Dead Island). Kąt zamachu jest liczony płynnie,
a potem zatrzaskiwany do 4, 8 albo 16 sektorów (`Directions` w `.ini`). Zamach w prawo-górę daje
cięcie ukośne w lewo-dół i tak dalej. Trzymając LPM, możesz wodzić bronią po okręgu i korygować
kąt przed ciosem.

Każdy z 16 kątów może mieć własną animację (`Windup_000` … `Windup_337`). Kąt bez własnej animacji
dostaje najbliższą z 4 bazowych (vanilla: ciosy mocy w lewo/prawo/do przodu/w miejscu).

**Latająca broń i animacja proceduralna (1. osoba):** ręce są ukryte, a broń animuje sam mod.
W spoczynku wisi z boku, podczas zamachu jedzie po okręgu za myszą, a cios to łuk przez środek
ekranu pod dowolnym z 16 kątów. Gra dostaje zwykły atak, który liczy trafienia i obrażenia.
Ruch ma wagę zależną od broni, łuk z rozpędem i dokończeniem, kołysanie przy chodzeniu,
bezwładność przy obrocie kamery, zatrzymanie i odbicie przy trafieniu, pozę bloku i animację
wyjęcia broni. Tempo cięcia synchronizuje się z momentem trafienia w grze.
Ustawienia są w sekcjach `[FloatingWeapon]`, `[Procedural]` i `[Feel]`. `.ini` przeładowuje się w trakcie gry.

## Wymagania

- Skyrim SE 1.5.97 albo AE 1.6.x / 1.7.x
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

VR nie jest obsługiwany.

## Budowanie (bez Visual Studio, przez GitHub Actions)

1. Utwórz nowe repo na GitHubie i wrzuć do niego zawartość tego folderu (razem z `.github/`).
2. Workflow **Build DirectionalCombat** uruchomi się sam (albo ręcznie: *Actions → Run workflow*).
   Pierwszy build trwa ok. 15–25 min, bo kompiluje CommonLibSSE-NG. Kolejne idą szybciej dzięki cache.
3. Pobierz artefakt **DirectionalCombat**. W środku jest `SKSE/Plugins/DirectionalCombat.dll` i `.ini`.
4. Spakuj go do zipa i zainstaluj jako moda w MO2/Vortex
   (albo skopiuj `SKSE` do `Skyrim Special Edition/Data/`).
5. Tag `v0.1.0` wypchnięty do repo sam utworzy Release z gotowym zipem.

Lokalnie (Windows + MSVC + xmake):
```
git clone --depth 1 -b v15.6.0 --recurse-submodules https://github.com/alandtse/CommonLibSSE-NG lib/commonlibsse-ng
xmake f -m releasedbg -y
xmake -y
```

## Konfiguracja

`Data/SKSE/Plugins/DirectionalCombat.ini`. Wszystkie opcje są opisane w pliku. Najważniejsze:

- `WindupThreshold` / `StrikeThreshold`: jak daleko trzeba ruszyć mysz (zależy od DPI).
- `InvertY`: włącz, jeśli góra i dół są odwrócone.
- `SwapLeftRight`: włącz, jeśli cięcia poziome lecą w złą stronę.
- `DebugLog = true`: każdy gest trafia do
  `Documents/My Games/Skyrim Special Edition/SKSE/DirectionalCombat.log`. Przydaje się przy strojeniu.

## Ograniczenia

- **Animacje:** vanilla ma kierunkowe tylko ciosy mocy, więc domyślnie każdy cios kierunkowy jest
  ciosem mocy (z kosztem staminy). Bez staminy leci zwykły atak. Prawdziwe kierunkowe cięcia
  wymagają własnych animacji. Można je podpiąć przez OAR: ustaw `DirectionActorValue = Variable09`
  (sektor 1..16: 1 = prawo, 5 = góra, 9 = lewo, 13 = dół) albo `AngleActorValue` (kąt 0–360°)
  i użyj warunku na tę wartość aktora.
- Brak wskaźnika kierunku na HUD. Feedbackiem jest zatrzymana kamera.
- Tylko prawa ręka. Dual-wield i lewa ręka działają po staremu.
- Możliwe konflikty z frameworkami ataków (MCO/ADXP, SCAR, Elden Rim itp.), które też przejmują LPM.
- Mod nie był jeszcze testowany w grze.

## Technicznie

Wtyczka SKSE (C++23, CommonLibSSE-NG). Hooki vtable na `AttackBlockHandler::ProcessButton` oraz
`LookHandler::ProcessMouseMove/ProcessThumbstick`. Uwzględnia przesunięcie indeksów vtable w AE 1.7.99+.
Ciosy są wysyłane przez `NotifyAnimationGraph`.
