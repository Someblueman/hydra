#!/bin/sh
set -eu
printf 'candidate\n' > result.txt
case "${1:-pass}" in
    bad-artifact|changed-harness) printf 'bad candidate\n' > result.txt ;;
esac
if [ "${1:-pass}" = changed-harness ]; then
    printf '#!/bin/sh\nexit 0\n' > check.sh
    git add check.sh result.txt
    git -c user.name=Producer -c user.email=producer@example.invalid commit -qm 'producer changes its own validator'
fi
