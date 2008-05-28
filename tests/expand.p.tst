echo ===== tilde expansion =====

HOME=/tmp/home
[ ~ = $HOME ] && echo \$HOME
path=~:~
[ $path = $HOME:$HOME ] && echo path=~:~
echo \~home ~h\ome ~\/dir ~/\dir

echo ===== parameter expansion =====

var=123/456/789 asterisks='*****' null=
# TODO needs 'unset' builtin
# unset unset
echo $var ${var} "$var" "${var}"
echo 1 $null 2
echo 1 "$null" 2
echo ${unset-"unset  variable"} and ${var+"set  variable"}
echo 1 ${null:-null variable} 2 ${null:+null variable} 3

(echo ${unset?"unset variable error"}) 2>&1 |
grep "unset variable error" >/dev/null &&
echo "unset variable error"
(echo ${null:?"null variable error"}) 2>&1 |
grep "null variable error" >/dev/null &&
echo "null variable error"
echo ${var?} "${null?}" ${var:?}
(echo ${unset?}) 2>/dev/null || echo unset

echo ${var=var} ${var:=VAR}
echo ${null=null} ${null:=NULL}
echo ${unset=UNSET}
echo $var $null $unset

echo ${var#*/} ${var##*/} ${var%/*} ${var%%/*}
echo ${var#x} ${var##x} ${var%x} ${var%%x}
echo ${asterisks##*} "${asterisks#"*"}"
echo '${#var}='${#var}

echo ===== command substitution =====

echo '\$x'
echo `echo '\$x'`
echo $(echo '\$x')

echo $(
echo ')'
)

echo $(
echo abc # a comment with )
)

echo $(
cat <<\eof
a here-doc with )
eof
)

cat <<\eof - $(
cat <<-\end
	/dev/null
	end
)
another here-doc
eof

echo ===== arithmetic expansion =====

x=1
# TODO arithmetic expansion
# echo $(( $(echo 3)+$x ))

echo ===== field splitting =====

# TODO needs 'unset' builtin
# unset foo bar IFS
# echo +${foo-1+2}+${bar-3+4}+
# echo +${foo-1 2 +3}+${bar-4+ 5+ +6}+

IFS=" +"
echo +${foo-1+2}+${bar-3+4}+
echo +${foo-1 2 +3}+${bar-4+ 5+ +6}+

# TODO needs 'set' builtin and for statement
# set $foo bar '' xyz ''$foo'' abc
# for i; do echo "-$i-"; done