# Rhythm Arranger

Плагины Teilormusic для Logic Pro (AU), VST3 и отдельное приложение. Версия 0.1.0.

- **Rhythm Arranger** (инструмент): каталог из 164 ритмов мировой практики, евклидовы ритмы, полиритм, полиметр,
  случайный ритм по зерну, тихай, ритм из MIDI. Преобразования: поворот, раком, дополнение, увеличение и уменьшение,
  плотность, сдвиг акцента, фазовая копия, свинг. Прослушивание обычным кликом. Карточки перетаскиваются на дорожку Logic.
- **Rhythm Arranger Live** (MIDI FX): играет зажатый аккорд выбранным ритмом в трёх режимах (гейт, арпеджио, расщепление слоёв по голосам аккорда).

Нота на выходе всегда C3 (60). Слои полиритма лежат на октавах: первый слой C3, затем C2, C4, C5. Ноты всегда стаккато (60 тиков при 480 на четверть),
динамика только скоростью по акценту (64, 90, 112).

## Структура

- `core/` ядро без JUCE: ритмы, генераторы, преобразования, MIDI. Целые числа, один и тот же результат на любой машине.
- `data/catalog.json` и `tools/gen_catalog.py` каталог. `core/CatalogData.cpp` создаётся скриптом. Id стабильны: категория * 1000 + номер, новые ритмы только в конец.
- `plugin/` оболочка JUCE 8.0.4.
- `tests/` проверки ядра и плагинов без хоста.
- `scripts/` установщик (pkg и dmg), `.github/workflows/build-macos.yml` сборка на GitHub Actions.

## Версия

Номер версии хранится в трёх местах: `CMakeLists.txt` (project VERSION), аргумент `scripts/make_installer.sh` в workflow и этот файл.

## Проверка на Linux без хоста

    cmake -S . -B build -G Ninja -DRHYTHM_JUCE_PATH=/path/to/JUCE -DRHYTHM_BUILD_TESTS=ON
    cmake --build build
    build/rhythm_tests
    xvfb-run -a build/RhythmPluginTests_artefacts/Release/RhythmPluginTests snapshots/
