#!/do/not/execute

DIFF=diff
PTFTOOL=../../ptftool

run_test() {
	echo "$NAME"
	if [ ! -e $FILE ]; then
		echo "Can't find binary"
		exit 2
	fi
	$PTFTOOL $FILE 2> /dev/null > .tmp1
	echo "$EXPECT" > .tmp2
	DIFFED=$($DIFF -U0 .tmp1 .tmp2 | grep -v -E '^\+\+\+ |^--- ')
	rm -f .tmp1 .tmp2
	if [ -z "$DIFFED" ]; then
		echo "[ OK ]"
		echo ""
		exit 0
	else
		echo "$DIFFED"
		echo "[FAIL]"
		echo ""
		exit 1
	fi
}
