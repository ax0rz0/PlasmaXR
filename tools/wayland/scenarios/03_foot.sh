# A real terminal: rendering at scale 2, keyboard input, resize.
cat > /tmp/b/s.txt <<'S'
wait 3
list
dump /out/foot-start.png
focus
wait 0.3
type echo hello from plasma xr
wait 0.3
key 28 1
key 28 0
wait 0.8
dump /out/foot-typed.png
configure 900 520
wait 1.5
dump /out/foot-resized.png
quit
S
$HARNESS --dir /run/wl --scale 2 /tmp/b/s.txt &
sleep 0.7
timeout 12 foot -e sh -c 'echo ready; exec sh' 2>&1 | head -20 &
wait %1
