#!/bin/bash
# usage: dev/run_regress.sh t_a.py t_b.py ...   one after another: the exit code, the count of FAILED lines and the last line of each log
cd /c/Users/tinmi/OneDrive/FielDes/FielDes
for t in "$@"; do
  tag=${t%.py}
  rc=$(bash dev/run_test.sh $tag $t 2>&1 | grep -o "rc=[0-9]*" | head -1)
  bad=$(grep -ciE "FAILED|FAIL |Traceback" dev/logs/$tag.log)
  last=$(grep -v "^\[bar" dev/logs/$tag.log | tail -1 | cut -c1-150)
  echo "$t : $rc bad=$bad | $last"
done
echo REGRESS_DONE
