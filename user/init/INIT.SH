#!/cmd/ush
echo "Hello form NexOS!"
echo "Switching root to disk0p2..."
switch_root /dev/disk0p2
service boot
exec /cmd/ush
