# Pointer and keyboard events as a client sees them (wev prints everything it receives).
cat > /tmp/b/s.txt <<'S'
wait 2.5
focus
motion 120 90
wait 0.2
motion 160 110
wait 0.2
button 272 1
wait 0.15
button 272 0
wait 0.2
axis 15
wait 0.2
key 42 1
key 30 1
key 30 0
key 42 0
wait 0.3
key 29 1
key 46 1
key 46 0
key 29 0
wait 0.5
configure 500 300
wait 1
dump /out/wev.png
close
wait 1
quit
S
$HARNESS --dir /run/wl --scale 2 /tmp/b/s.txt &
sleep 0.7
timeout 12 wev > /out/wev.txt 2>&1 &
wait %1
echo "--- wev saw:"
grep -E "wl_pointer|wl_keyboard|key:|button|axis|enter|leave|motion|modifiers|state:" /out/wev.txt | sed 's/^ *//' | head -60
