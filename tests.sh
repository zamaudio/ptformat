#!/do/not/execute

DIFF=diff
PTFTOOL=../../ptftool
FAIL=$(if [ "x$BROKEN" == "x1" ]; then echo "BROKEN"; else echo "FAIL"; fi)
FAILRET=$(if [ "x$BROKEN" == "x1" ]; then echo 0; else echo 1; fi)

run_test() {
	echo "$NAME"
	if [ ! -e $FILE ]; then
		echo "Can't find binary"
		echo ""
		exit 2
	fi
	$PTFTOOL $FILE 2> /dev/null > .tmp1
	echo "$EXPECT" > .tmp2
	DIFFED=$($DIFF -U0 .tmp2 .tmp1 | grep -v -E '^\+\+\+ |^--- ')
	rm -f .tmp1 .tmp2
	if [ -z "$DIFFED" ]; then
		echo "[ OK ]"
		echo ""
		exit 0
	else
		echo "$DIFFED"
		echo "[$FAIL]"
		echo ""
		exit $FAILRET
	fi
}
