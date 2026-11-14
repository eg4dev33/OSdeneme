#!/usr/bin/env bash
# OSdeneme masaustunu tarayicida gosterir (GitHub Codespaces icin).
# QEMU ekrani VNC (5900) ile yayinlar, noVNC bunu tarayicida 6080 portunda gosterir.
set -e
cd "$(dirname "$0")/.."

if ! command -v websockify >/dev/null 2>&1 || [ ! -d /usr/share/novnc ]; then
  echo "noVNC kuruluyor (bir kerelik)..."
  sudo apt-get update -y
  sudo apt-get install -y novnc websockify
fi

pkill -f "websockify.*6080" 2>/dev/null || true
websockify --web /usr/share/novnc 6080 localhost:5900 >/tmp/websockify.log 2>&1 &

echo ""
echo "================================================================"
echo " Masaustu aciliyor."
echo " 1) Alttaki 'PORTS' sekmesinde 6080 portunu bul"
echo " 2) Yanindaki dunya simgesine (Open in Browser) tikla"
echo " 3) Acilan sayfada adresin sonuna  /vnc.html  ekle  ve Connect'e bas"
echo "    (ya da dogrudan: /vnc.html?autoconnect=true&resize=scale)"
echo " Cikmak icin bu terminalde: Ctrl+A sonra X"
echo "================================================================"
echo ""
exec qemu-system-x86_64 -m 256M -cdrom build/os.iso -serial mon:stdio -display none -vnc :0 -no-reboot
