# A GTK3 app: subsurfaces, popups (menus), data device, client-side decorations.
cat > /tmp/b/s.txt <<'S'
wait 6
list
dump /out/gtk-start.png
focus
motion 600 300
wait 0.3
motion 640 320
wait 0.3
button 272 1
wait 0.1
button 272 0
wait 1.5
dump /out/gtk-click.png
quit
S
$HARNESS --dir /run/wl --scale 2 /tmp/b/s.txt &
sleep 0.7
timeout 14 gtk3-widget-factory 2>&1 | grep -v -E "^$|dconf|Gtk-WARNING|GLib-GIO|Failed to load module|atk-bridge|libEGL|MESA" | head -20 &
wait %1
