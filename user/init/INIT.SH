#!/cmd/ush
echo "Hello form NexOS!"
echo "Switching root..."
echo "안녕하세요"
echo "한글 UTF-8 테스트"
echo "가나다라마바사"
echo "你好"
echo "こんにちは"
echo "┌───┬───┐"
echo "│ NexOS │"
echo "└───┴───┘ "
echo "一	일	하나"
echo "二	이	둘"
echo "三	삼	셋"
echo "四	사	넷"
echo "五	오	다섯"
echo "六	육	여섯"
echo "七	칠	일곱"
echo "八	팔	여덟"
echo "九	구	아홉"
echo "十	십	열"
switch_root /dev/root
service boot
cd home
exec /cmd/ush --tty /dev/tty
