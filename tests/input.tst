echo ===== input.p.t =====
$INVOKE $TESTEE -iv +m input.p.t
# TODO test of '!' in PS1
echo ===== input.t =====
$INVOKE $TESTEE -iv +m input.t
# TODO test of '\j' '\!' in PS1/PS2