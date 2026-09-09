#!/cmd/ush
/cmd/nexbox echo "Switching root..."
/cmd/nexbox mount -a && exec /system/init
/cmd/nexbox echo "Root switch failed; entering recovery shell."
exec /cmd/ush
