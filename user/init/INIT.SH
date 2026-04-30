#!/cmd/ush
echo "Hello form NexOS!"
mkdir new_root
mount boot new_root
echo "Switching root to new_root..."
switch_root new_root
service boot
exec /ram/cmd/ush
