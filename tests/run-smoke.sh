#!/bin/sh
# Minimal darling smoke test: mldr → darlingserver checkin → hello-static-macho
# Must run as root on FreeBSD 15.1 dev VM.
# Usage: sh /path/to/darling/tests/run-smoke.sh
set -e

PREFIX=/tmp/darling-smoke-prefix
SOCK="${PREFIX}/.darlingserver.sock"
MLDR=/var/darling-build/dserver/mldr-real/mldr
DSERVER=/var/darling-build/dserver/darlingserver
BINARY=/path/to/darling/tests/hello-static-macho

if [ "$(id -u)" != "0" ]; then
    echo "ERROR: must run as root" >&2
    exit 1
fi

# Clean up from previous runs
rm -rf "${PREFIX}"
mkdir -p "${PREFIX}"
pkill -9 darlingserver 2>/dev/null || true

# darlingserver protocol: expects argc >= 6:
#   argv[1]=prefix  argv[2]=uid  argv[3]=gid  argv[4]=pipefd  argv[5]=fix_permissions
# We create a pipe, pass the write end, wait for darlingserver to signal ready.
pipe_r=/tmp/darling-smoke-pipe-r
pipe_w=/tmp/darling-smoke-pipe-w
rm -f "${pipe_r}" "${pipe_w}"
mkfifo "${pipe_r}"

# Use a subshell that holds the fifo open so darlingserver can write to it
(
    # exec 3>"${pipe_r}"  -- can't redirect to fifo for write in sh easily
    # Use the fd trick: open the fifo for writing in background cat
    cat "${pipe_r}" > /dev/null &
    CAT_PID=$!
    exec 4>"${pipe_r}"
    "${DSERVER}" "${PREFIX}" "$(id -u)" "$(id -g)" 4 0 &
    DSERVER_PID=$!
    echo "${DSERVER_PID}" > /tmp/darling-dserver-pid
    wait "${DSERVER_PID}" 2>/dev/null || true
    kill "${CAT_PID}" 2>/dev/null || true
) &
SUBSHELL_PID=$!

# Wait for darlingserver to write "." to the pipe (readiness signal)
# Poll for the socket file appearing (simpler than pipe dance in shell)
echo "Waiting for darlingserver socket at ${SOCK} ..."
WAITED=0
while [ ! -S "${SOCK}" ]; do
    sleep 0.2
    WAITED=$((WAITED + 1))
    if [ "${WAITED}" -gt 25 ]; then
        echo "ERROR: darlingserver socket did not appear after 5s" >&2
        pkill -9 darlingserver 2>/dev/null || true
        exit 1
    fi
done
echo "darlingserver socket ready: ${SOCK}"

# Run mldr with the smoke test Mach-O
echo "Running: ${MLDR} ${BINARY}"
__mldr_sockpath="${SOCK}" \
    __mldr_DYLD_ROOT_PATH="${PREFIX}" \
    "${MLDR}" "${BINARY}" 2>&1
STATUS=$?

echo "mldr exit status: ${STATUS}"

# Cleanup
pkill -9 darlingserver 2>/dev/null || true
rm -rf "${PREFIX}"
rm -f "${pipe_r}" /tmp/darling-dserver-pid

exit "${STATUS}"
