#!/bin/sh
# Roda todos os scripts de teste no emulador e na reescrita e compara estado, som e frames.
# uso: tools/validate.sh [padrão]   (ex.: tools/validate.sh t_care)
cd "$(dirname "$0")/.." || exit 1
make -s -C harness >/dev/null && make -s -C src tama-headless >/dev/null || exit 1
for s in scripts/t_*${1}*.txt; do
	n=$(basename "$s" .txt)
	mkdir -p "out/v/$n/ref" "out/v/$n/nat"
	harness/trace -C -W -s "$s" -o "out/v/$n/ref" >/dev/null 2>&1
	src/tama-headless -s "$s" -o "out/v/$n/nat" 2>/dev/null
	ram=$(tools/cmpram.py "out/v/$n/ref/ram.txt" "out/v/$n/nat/ram.txt" >/dev/null 2>&1 && echo ok || echo DIF)
	[ -s "out/v/$n/ref/ram.txt" ] || ram="-"
	snd=$(tools/cmpsnd.py "out/v/$n/ref/snd.txt" "out/v/$n/nat/snd.txt" >/dev/null 2>&1 && echo ok || echo DIF)
	strict=$(tools/cmpframes.py --min 0 "out/v/$n/ref/frames.txt" "out/v/$n/nat/frames.txt" 2>&1 | head -1 | cut -c1-40)
	tol=$(tools/cmpframes.py --min 0 --tol 15 "out/v/$n/ref/frames.txt" "out/v/$n/nat/frames.txt" >/dev/null 2>&1 && echo ok || echo DIF)
	printf '%-26s ram=%-3s som=%-3s frames±15s=%-3s estrito: %s\n' "$n" "$ram" "$snd" "$tol" "$strict"
done
