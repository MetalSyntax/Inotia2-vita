#!/bin/sh
# Arma ux0_data/inotia2/ (= ux0:data/inotia2/ en la Vita) desde la Inotia2.app del IPA. Imita el
# sandbox de iOS para que las rutas del juego se traduzcan 1:1:
#   Inotia2.app/   bundle tal cual (ejecutable + recursos), sin firma ni basura de macOS
#   rebase.bin     generado para ese ejecutable (tools/macho_prep.py)
#   saves/         partidas (el Documents/ de iOS): vacía acá; el deploy nunca sube ni borra nada adentro
#   logs/          log_001.log .. log_999.log, uno por corrida (los escribe el loader)
# Los crash dumps (.psp2dmp) los escribe el sistema en ux0:data/, no acá.
# Nada de esto se commitea.
# Uso: tools/stage_data.sh [ruta/a/Inotia2.app]
set -e
cd "$(dirname "$0")/.."
APP="${1:-game_app}"
OUT=ux0_data/inotia2
[ -f "$APP/Inotia2" ] || { echo "No encuentro $APP/Inotia2"; exit 1; }
mkdir -p "$OUT/Inotia2.app"
# Migración desde la estructura plana anterior (todo suelto en $OUT/).
find "$OUT" -mindepth 1 -maxdepth 1 ! -name Inotia2.app ! -name saves ! -name logs ! -name rebase.bin -exec rm -rf {} +
mkdir -p "$OUT/saves" "$OUT/logs"
rsync -a --delete --exclude '._*' --exclude '.DS_Store' --exclude '*.webp' --exclude '_CodeSignature' \
  --exclude 'SC_Info' --exclude 'embedded.mobileprovision' --exclude 'CodeResources' "$APP/" "$OUT/Inotia2.app/"
python3 -I tools/macho_prep.py "$OUT/Inotia2.app/Inotia2" "$OUT/rebase.bin" --report ux0_data/macho_report.txt
echo "Listo. En la Vita: ux0:data/inotia2/{Inotia2.app/, rebase.bin, saves/, logs/} (tools/deploy_data.py)."
