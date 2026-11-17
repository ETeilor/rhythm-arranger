#!/bin/bash
# Builds the macOS installer from the Release artefacts:
#   Rhythm Arranger.component -> /Library/Audio/Plug-Ins/Components
#   Rhythm Arranger.vst3      -> /Library/Audio/Plug-Ins/VST3
#   Rhythm Arranger.app       -> /Applications
# Result: dist/RhythmArranger-<ver>.pkg and dist/RhythmArranger-<ver>.dmg (the dmg holds the pkg and a readme).
set -euo pipefail
VER="${1:-0.1.0}"
ART="build/RhythmArranger_artefacts/Release"
OUT="dist"
WORK="$(mktemp -d)"
HERE="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$OUT" && mkdir -p "$OUT"

stage() {   # stage <bundle> <subdir>
    mkdir -p "$WORK/$2"
    cp -R "$1" "$WORK/$2/"
    # Apple Silicon refuses unsigned code: ad-hoc signature (no Apple certificate needed)
    codesign --force --deep --sign - "$WORK/$2/$(basename "$1")"
    codesign --verify --deep --strict "$WORK/$2/$(basename "$1")"
}
LIVE="build/RhythmArrangerLive_artefacts/Release"
stage "$ART/AU/Rhythm Arranger.component" au
stage "$LIVE/AU/Rhythm Arranger Live.component" au
stage "$ART/VST3/Rhythm Arranger.vst3" vst3
stage "$LIVE/VST3/Rhythm Arranger Live.vst3" vst3
stage "$ART/Standalone/Rhythm Arranger.app" app

# the same postinstall in every component package: it only touches paths that exist
mkdir -p "$WORK/scripts"
cp "$HERE/postinstall" "$WORK/scripts/postinstall"
chmod +x "$WORK/scripts/postinstall"

for part in au:Components vst3:VST3 app:; do
    name="${part%%:*}"
    dir="${part#*:}"
    loc="/Library/Audio/Plug-Ins/$dir"
    [ "$name" = app ] && loc="/Applications"
    pkgbuild --root "$WORK/$name" --install-location "$loc" --scripts "$WORK/scripts" \
             --identifier "com.teilormusic.rhythmarranger.$name" --version "$VER" "$WORK/$name.pkg"
done

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Rhythm Arranger $VER</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/><line choice="vst3"/><line choice="app"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro)"><pkg-ref id="com.teilormusic.rhythmarranger.au"/></choice>
    <choice id="vst3" title="VST3"><pkg-ref id="com.teilormusic.rhythmarranger.vst3"/></choice>
    <choice id="app" title="Standalone app"><pkg-ref id="com.teilormusic.rhythmarranger.app"/></choice>
    <pkg-ref id="com.teilormusic.rhythmarranger.au">au.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.rhythmarranger.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.rhythmarranger.app">app.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/RhythmArranger-$VER.pkg"

mkdir -p "$WORK/dmg"
cp "$OUT/RhythmArranger-$VER.pkg" "$WORK/dmg/Install Rhythm Arranger.pkg"
cat > "$WORK/dmg/Read me.txt" <<'EOF'
Rhythm Arranger: установка

1. Дважды щёлкните «Install Rhythm Arranger.pkg».
   Если macOS пишет, что разработчик не установлен: System Settings → Privacy & Security →
   внизу «Open Anyway», затем снова откройте установщик.
2. Установщик положит плагины в /Library/Audio/Plug-Ins (AU и VST3) и приложение в Applications.
3. Перезапустите Logic Pro.
   Rhythm Arranger: дорожка программного инструмента →
   AU Instruments → Teilormusic → Rhythm Arranger.
   Выберите ритм, нажмите «Слушать» (простой клик) и потяните карточку «Ритм в трек»
   на дорожку Logic. Нота везде C3, слои полиритма на соседних октавах.
   Rhythm Arranger Live: на дорожке с любым инструментом слот MIDI FX →
   Audio Units → Teilormusic → Rhythm Arranger Live. Зажмите аккорд.
   Если Logic плагин не показал: Logic Pro → Settings → Plug-in Manager, выберите плагин,
   нажмите «Reset & Rescan Selection».
EOF
hdiutil create -volname "Rhythm Arranger $VER" -srcfolder "$WORK/dmg" -ov -format UDZO "$OUT/RhythmArranger-$VER.dmg"
ls -la "$OUT"
