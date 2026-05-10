#!/cmd/ush
echo "Hello form NexOS!"
echo "Switching root to disk0p2..."
echo "안녕하세요!"
switch_root /dev/disk0p2
service boot
exec /cmd/ush
