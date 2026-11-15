# The real plasmashell as a client of our compositor: which protocols does it bind, and what does it
# put on screen? Uses the image with Plasma installed (plasmaxr-plasma-test).
cat > /tmp/b/s.txt <<'S'
wait 30
list
dumpall /out/plasma
wait 5
quit
S
export XDG_RUNTIME_DIR=/run/wl WAYLAND_DISPLAY=wayland-0
$HARNESS --dir /run/wl --scale 1 /tmp/b/s.txt > /out/harness.log 2>&1 &
sleep 0.8
dbus-run-session -- sh -c '
  WAYLAND_DEBUG=1 timeout 40 plasmashell --no-respawn > /out/plasmashell.log 2>&1
' &
wait %1
echo "--- harness:"; grep -E "toplevel|Wayland:|dump" /out/harness.log | head -30
echo "--- globals plasmashell bound:"; grep -oE "wl_registry@[0-9]+\.bind\([0-9]+, \"[a-z_0-9]+\"" /out/plasmashell.log | sed 's/.*"\(.*\)"/\1/' | sort | uniq -c | sort -rn | head -30
echo "--- plasmashell messages (non-protocol):"; grep -vE "^\[ *[0-9.]+\] ( -> |[a-z_0-9]+@)|^\[ *[0-9.]+\]" /out/plasmashell.log | head -40
