# fg-y.tst: yash-specific test of the fg built-in
../checkfg || skip="true" # %SEQUENTIAL%

# POSIX requires that "fg" should return 0 on success. Yash, however, returns
# the exit status of the resumed job, which is not always 0. Many other shells
# behave this way.
test_x -e 17 'exit status of resumed command' -m
sh -c 'kill -s STOP $$; exit 17'
fg
__IN__

test_oE 'resuming more than one job' -m
"$TESTEE" -c 'suspend; echo 1'
"$TESTEE" -c 'suspend; echo 2'
"$TESTEE" -c 'suspend; echo 3'
fg %2 %3 '%? echo 1' >/dev/null
__IN__
2
3
1
__OUT__

test_oE 'omitting % in job ID' -m
"$TESTEE" -c 'suspend; echo x'
"$TESTEE" -c 'suspend; echo y'
fg 2 >/dev/null
fg \?x >/dev/null
__IN__
y
x
__OUT__

test_Oe -e 1 'non-job-controlled job (default operand)'
:&
set -m
fg
__IN__
fg: the current job is not a job-controlled job
__ERR__

test_Oe -e 1 'non-job-controlled job (job ID operand)'
:&
set -m
fg %:
__IN__
fg: `%:' is not a job-controlled job
__ERR__
#`

test_Oe -e 1 'no such job (name)' -m
: _no_such_job_&
fg %_no_such_job_
__IN__
fg: no such job `%_no_such_job_'
__ERR__
#`

test_Oe -e 1 'no such job (number)' -m
fg %2
__IN__
fg: no such job `%2'
__ERR__
#`

test_O -d -e 1 'printing to closed stream' -m
:&
fg >&-
__IN__

test_Oe -e 2 'invalid option' -m
fg --no-such-option
__IN__
fg: `--no-such-option' is not a valid option
__ERR__
#`

(
posix="true"

test_Oe -e 2 'too many operands' -m
:&:&
fg %+ %-
__IN__
fg: too many operands are specified
__ERR__

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:
