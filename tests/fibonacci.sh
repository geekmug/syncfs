#!/usr/bin/env bash

echo -n 0 > a
echo -n 1 > b

while [ 1 ]
do
    stat clock > /dev/null

    a=$(cat a)
    b=$(cat b)
#    c=$((a + b))
    c=$(echo "$a + $b" | bc)

    echo $a
    echo -n $b > a
    echo -n $c > b
done
