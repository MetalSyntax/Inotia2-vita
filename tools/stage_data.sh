#!/bin/sh
# Arma ux0_data/inotia2/ (lo que va en ux0:data/inotia2/ de la Vita) desde la Inotia2.app del IPA:
# todos los recursos + el ejecutable + rebase.bin. Nada de esto se commitea.
# Uso: tools/stage_data.sh [ruta/a/Inotia2.app]
set -e
cd "$(dirname "$0")/.."
APP="${1:-game_app}"
OUT=ux0_data/inotia2
[ -f "$APP/Inotia2" ] || { echo "No encuentro $APP/Inotia2"; exit 1; }
mkdir -p "$OUT"
rsync -a --exclude '._*' --exclude '_CodeSignature' --exclude 'SC_Info' "$APP/" "$OUT/"
python3 -I tools/macho_prep.py "$OUT/Inotia2" "$OUT/rebase.bin" --report ux0_data/macho_report.txt
echo "Listo: copiá el contenido de $OUT/ a ux0:data/inotia2/ en la Vita."
