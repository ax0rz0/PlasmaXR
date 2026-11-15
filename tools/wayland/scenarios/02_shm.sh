# A bare shm + xdg-shell client with frame callbacks (weston-simple-shm).
cat > /tmp/b/s.txt <<'S'
wait 3
list
dump /out/shm.png
wait 1
dump /out/shm2.png
quit
S
$HARNESS --dir /run/wl --scale 2 /tmp/b/s.txt &
sleep 0.7
timeout 6 weston-simple-shm 2>&1 | head -20 &
wait %1
