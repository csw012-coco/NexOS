#!/cmd/ush
echo "Hello form NexOS!"
echo "Switching root to disk0p2..."
echo "안녕하세요"
echo "한글 UTF-8 테스트"
echo "가나다라마바사"
echo "你好"
echo "こんにちは"
echo "┌───┬───┐"
echo "│ NexOS │"
echo "└───┴───┘ "
switch_root /dev/disk0p2
service boot
exec /cmd/ush --tty /dev/tty
