#!/cmd/ush
/cmd/nexbox minfo
/cmd/nexbox cat /system/config/motd.scf
/cmd/nexbox boot
/cmd/nexbox service supervise 1s &
exec /cmd/nexbox getty /dev/tty1
