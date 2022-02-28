set -ex
export SKIP_COMPILE=1
retry_max=100
for ((i=0;i<retry_max;i++)); do
    echo "Try NO.${i}"
    mix test test/otter_test.exs:106
done
