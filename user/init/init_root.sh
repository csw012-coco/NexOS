#!/cmd/ush
/cmd/nexbox minfo
/cmd/nexbox cat /system/config/motd.scf
/cmd/nexbox getty /dev/tty2 &
/cmd/nexbox getty /dev/tty3 &
exec /cmd/nexbox getty /dev/tty1
