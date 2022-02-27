set -ex
mix test
export SKIP_COMPILE=1
retry_max=100
for ((i=0;i<retry_max;i++)); do
    echo "Try NO.${i}"
    mix test
done
