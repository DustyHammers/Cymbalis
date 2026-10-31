#!/usr/bin/env bash
# Report a failed step's error lines as annotations, which can be read without the job's log.
# Each error line, with the four lines after it, becomes one annotation (GitHub shows at most
# ten error annotations per step). A log without error lines gives its last 15 lines instead.
#   annotate_errors.sh <log file> <title>
log="$1"; title="$2"
pattern='error:|FAILED:|CMake Error|Undefined symbols|ld: '
if grep -q -E "$pattern" "$log"; then
    lines() { grep -n -E -A4 "$pattern" "$log"; }
else
    lines() { tail -n 15 "$log"; }
fi

block=""; n=0
emit() {
    if [ -n "$block" ]; then echo "::error title=${title}::${block}"; n=$((n + 1)); fi
    block=""
}
while IFS= read -r line; do
    if [ "$line" = "--" ]; then
        emit
        [ "$n" -ge 10 ] && break
    else
        line=${line//%/%25}; line=${line//$'\r'/}
        block="${block:+${block}%0A}${line}"
    fi
done < <(lines)
emit
exit 0
