#!/do/not/execute

DIFF=$(which diff)
GREP=$(which grep)
PTFTOOL=../../ptftool

run_test() {
	echo "$NAME"
	if [ "x$FILE" != "x" ] && [ ! -e $FILE ]; then
		echo "Cannot find test file"
		echo ""
		exit 1
	fi
	if [ "x$DIFF" != "x" ] && [ ! -e $DIFF ]; then
		echo "Cannot find diff"
		echo ""
		exit 1
	fi
	if [ "x$GREP" != "x" ] && [ ! -e $GREP ]; then
		echo "Cannot find grep"
		echo ""
		exit 1
	fi
	if [ ! -e $PTFTOOL ]; then
		echo "Cannot find ptftool"
		echo ""
		exit 1
	fi
	TMP1=$(mktemp)
	TMP2=$(mktemp)
	echo "$PTFTOOL $FILE"
	$PTFTOOL $FILE > $TMP1
	echo "$EXPECT" > $TMP2
	DIFFED=$($DIFF -U0 $TMP2 $TMP1 | $GREP -v -E '^\+\+\+ |^--- ')
	rm -f $TMP1 $TMP2
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
