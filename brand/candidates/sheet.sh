#!/usr/bin/env bash
# Render the comparison sheet: each candidate beside the Kronuz and Xapiand logos,
# 512 px tall on light and dark, and at 16 and 32 px scaled up so the pixels show.
# Needs rsvg-convert and ImageMagick. Usage: brand/candidates/sheet.sh [out.png]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/sheet.png}
kronuz=${KRONUZ_LOGO:-$HOME/code/KronuzBlog/Kronuz.github.io/src/assets/logo.svg}
xapiand=${XAPIAND_LOGO:-}
if [ -z "$xapiand" ]; then  # Xapiand's vector master: landed, or still on its brand branch
  for x in "$HOME/code/Kronuz/Xapiand/brand/logo.svg" "$HOME/code/Kronuz/Xapiand-wt/xapiand/xapiand-brand/brand/logo.svg"; do
    [ -f "$x" ] && { xapiand=$x; break; }
  done
fi
light='#fdfdfc' dark='#1a1a1a' ink='#333333'
tmp=$(mktemp -d "${TMPDIR:-/tmp}/detent-sheet.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

names=(kronuz xapiand a b c)
labels=("Kronuz" "Xapiand" "A: traces" "B: solid hexagon" "C: notch ring")
srcs=("$kronuz" "$xapiand" "$here/a.svg" "$here/b.svg" "$here/c.svg")

cell=576 pad=32 gap=40 head=72 small=288
for i in "${!names[@]}"; do
  n=${names[$i]} s=${srcs[$i]}
  for bg in light dark; do
    color=${!bg}
    rsvg-convert -h $((cell - 2 * pad)) -b "$color" "$s" -o "$tmp/$n-big-$bg.png"
    magick "$tmp/$n-big-$bg.png" -gravity center -background "$color" -extent "${cell}x${cell}" "$tmp/$n-big-$bg.png"
    for px in 16 32; do
      rsvg-convert -h "$px" -b "$color" "$s" -o "$tmp/$n-$px-$bg.png"
      magick "$tmp/$n-$px-$bg.png" -gravity center -background "$color" -extent "${px}x${px}" \
        -filter point -resize "${small}x${small}" "$tmp/$n-$px-$bg.png"
    done
  done
done

cols=${#names[@]}
width=$((cols * cell + (cols + 1) * gap))
y1=$head y2=$((y1 + cell + gap / 2)) y3=$((y2 + cell + gap)) y4=$((y3 + small + gap))
height=$((y4 + small + gap))
{
  echo "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"$width\" height=\"$height\">"
  echo "<rect width=\"$width\" height=\"$height\" fill=\"#eeeeec\"/>"
  for i in "${!names[@]}"; do
    n=${names[$i]} x=$((gap + i * (cell + gap)))
    echo "<text x=\"$((x + cell / 2))\" y=\"$((head - 24))\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"32\" font-weight=\"bold\" fill=\"$ink\" text-anchor=\"middle\">${labels[$i]}</text>"
    echo "<image x=\"$x\" y=\"$y1\" width=\"$cell\" height=\"$cell\" xlink:href=\"$n-big-light.png\"/>"
    echo "<image x=\"$x\" y=\"$y2\" width=\"$cell\" height=\"$cell\" xlink:href=\"$n-big-dark.png\"/>"
    echo "<image x=\"$x\" y=\"$y3\" width=\"$small\" height=\"$small\" xlink:href=\"$n-16-light.png\"/>"
    echo "<image x=\"$((x + small))\" y=\"$y3\" width=\"$small\" height=\"$small\" xlink:href=\"$n-16-dark.png\"/>"
    echo "<image x=\"$x\" y=\"$y4\" width=\"$small\" height=\"$small\" xlink:href=\"$n-32-light.png\"/>"
    echo "<image x=\"$((x + small))\" y=\"$y4\" width=\"$small\" height=\"$small\" xlink:href=\"$n-32-dark.png\"/>"
  done
  for row in "$y3:16 px" "$y4:32 px"; do
    echo "<text x=\"$((gap / 2))\" y=\"$((${row%%:*} + small / 2))\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"22\" fill=\"$ink\" text-anchor=\"middle\" transform=\"rotate(-90 $((gap / 2)) $((${row%%:*} + small / 2)))\">${row#*:}</text>"
  done
  echo "</svg>"
} > "$tmp/sheet.svg"
(cd "$tmp" && rsvg-convert sheet.svg -o sheet.png)
cp "$tmp/sheet.png" "$out"
echo "$out"
