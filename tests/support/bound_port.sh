# Prints the port a daemon bound after it was started on port 0, read from its log line
# "listening on ...:<port>" (detentd) or "ready on ...:<port>" (detent-relay).
#   bound_port LOG PID
bound_port() {
    local log=$1 pid=$2 port deadline=$((SECONDS + 30))
    while ((SECONDS < deadline)); do
        port=$(sed -n -E 's/.*(listening|ready)( with [A-Z]+)? on [^ ]*:([0-9]+).*/\3/p' "$log" 2>/dev/null | head -n 1)
        if [[ -n "$port" && "$port" != 0 ]]; then printf '%s\n' "$port"; return 0; fi
        if ! kill -0 "$pid" 2>/dev/null; then cat "$log" >&2; return 1; fi
        sleep 0.02
    done
    cat "$log" >&2
    return 1
}
