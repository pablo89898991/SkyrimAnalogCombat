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

| Zamach   | Cios                      | Domyślna animacja (vanilla) |
|----------|---------------------------|-----------------------------|
| w lewo   | cięcie z lewej w prawo    | `attackPowerStartLeft`      |
| w prawo  | cięcie z prawej w lewo    | `attackPowerStartRight`     |
| w górę   | cios z góry               | `attackPowerStartForward`   |
| w dół    | cios od dołu / pchnięcie  | `attackPowerStartInPlace`   |

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
  i użyj warunku na tę wartość aktora (1 = lewo, 2 = prawo, 3 = góra, 4 = dół).
- Brak wskaźnika kierunku na HUD. Feedbackiem jest zatrzymana kamera.
- Tylko prawa ręka. Dual-wield i lewa ręka działają po staremu.
- Możliwe konflikty z frameworkami ataków (MCO/ADXP, SCAR, Elden Rim itp.), które też przejmują LPM.
- Mod nie był jeszcze testowany w grze.

## Technicznie

Wtyczka SKSE (C++23, CommonLibSSE-NG). Hooki vtable na `AttackBlockHandler::ProcessButton` oraz
`LookHandler::ProcessMouseMove/ProcessThumbstick`. Uwzględnia przesunięcie indeksów vtable w AE 1.7.99+.
Ciosy są wysyłane przez `NotifyAnimationGraph`.
