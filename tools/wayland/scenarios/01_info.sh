# Globals and output information as a client sees them.
cat > /tmp/b/s.txt <<'S'
wait 2
quit
S
$HARNESS --dir /run/wl --scale 2 /tmp/b/s.txt &
sleep 0.7
wayland-info 2>&1 | grep -E "interface|name:|version:|physical|logical|scale|mode|refresh|width|height|capabilities|description" | head -60
wait
