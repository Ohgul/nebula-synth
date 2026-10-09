#!/bin/bash
# Crea Nebula-Instalador.pkg con el VST3, el AU y la app independiente.
set -euo pipefail

VERSION="${1:-1.0.0}"
ART="build/NebulaSynth_artefacts/Release"
WORK="build/pkg"
OUT="dist"

rm -rf "$WORK" "$OUT"
mkdir -p "$WORK/vst3" "$WORK/au" "$WORK/app" "$WORK/scripts" "$OUT"

cp -R "$ART/VST3/Nebula.vst3"       "$WORK/vst3/"
cp -R "$ART/AU/Nebula.component"    "$WORK/au/"
cp -R "$ART/Standalone/Nebula.app"  "$WORK/app/"

# Script post-instalación: refresca la caché de Audio Units
cat > "$WORK/scripts/postinstall" <<'EOS'
#!/bin/bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOS
chmod +x "$WORK/scripts/postinstall"

# Evita que el instalador "reubique" el plugin si encuentra otra copia en el disco
make_component () {
    local name="$1" root="$2" dest="$3" extra="${4:-}"
    pkgbuild --analyze --root "$root" "$WORK/$name.plist"
    local i=0
    while plutil -extract "$i" xml1 -o /dev/null "$WORK/$name.plist" 2>/dev/null; do
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$WORK/$name.plist"
        i=$((i + 1))
    done
    pkgbuild --root "$root" \
             --component-plist "$WORK/$name.plist" \
             --identifier "com.remacdent.nebulasynth.$name" \
             --version "$VERSION" \
             --install-location "$dest" \
             $extra \
             "$WORK/$name.pkg"
}

make_component vst3 "$WORK/vst3" "/Library/Audio/Plug-Ins/VST3"
make_component au   "$WORK/au"   "/Library/Audio/Plug-Ins/Components" "--scripts $WORK/scripts"
make_component app  "$WORK/app"  "/Applications"

cat > "$WORK/distribution.xml" <<EOX
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Nebula ${VERSION}</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="Nebula VST3" description="Plugin VST3 (Ableton, etc.)">
        <pkg-ref id="com.remacdent.nebulasynth.vst3"/>
    </choice>
    <choice id="au" title="Nebula Audio Unit" description="Plugin AU (Ableton, Logic, GarageBand)">
        <pkg-ref id="com.remacdent.nebulasynth.au"/>
    </choice>
    <choice id="app" title="Nebula (app independiente)" description="Para probar el sinte sin abrir un DAW">
        <pkg-ref id="com.remacdent.nebulasynth.app"/>
    </choice>
    <pkg-ref id="com.remacdent.nebulasynth.vst3" version="${VERSION}">vst3.pkg</pkg-ref>
    <pkg-ref id="com.remacdent.nebulasynth.au"   version="${VERSION}">au.pkg</pkg-ref>
    <pkg-ref id="com.remacdent.nebulasynth.app"  version="${VERSION}">app.pkg</pkg-ref>
</installer-gui-script>
EOX

productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK" \
             "$OUT/Nebula-Instalador-${VERSION}.pkg"

echo "Listo: $OUT/Nebula-Instalador-${VERSION}.pkg"
