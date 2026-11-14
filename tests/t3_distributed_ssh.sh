#!/bin/sh
# Sourced by test_t3_distributed.sh: two private loopback OpenSSH daemons, one
# per execution host, with ephemeral keys and explicit client configuration.
# Each daemon's forced command runs every request with the receiver's
# tmux-less PATH, exactly as a host without tmux would. Nothing touches ~/.ssh.
# ssh_config and sshd_pids are read by the driver.
# shellcheck disable=SC2034
: "${fixture:?}" "${receiver_path:?}"
t3_ssh_available() {
    [ -x /usr/sbin/sshd ] && command -v ssh >/dev/null 2>&1 && command -v ssh-keygen >/dev/null 2>&1
}
# Starts sshd for one host name on a free loopback port and appends the client
# stanza. Sets t3_port; records the daemon in sshd_pids for cleanup.
t3_ssh_host() {
    _tsh_name="$1" _tsh_port="$2" _tsh_dir="$fixture/ssh"
    ssh-keygen -q -t ed25519 -N '' -f "$_tsh_dir/$_tsh_name-host" >/dev/null
    cat > "$_tsh_dir/$_tsh_name-force" <<EOF
#!/bin/sh
# Every request to execution host $_tsh_name runs without tmux on PATH.
exec env PATH='$receiver_path' /bin/sh -c "\${SSH_ORIGINAL_COMMAND:?}"
EOF
    chmod 700 "$_tsh_dir/$_tsh_name-force"
    _tsh_tries=0
    while [ "$_tsh_tries" -lt 20 ]; do
        _tsh_tries=$((_tsh_tries + 1))
        cat > "$_tsh_dir/$_tsh_name-sshd_config" <<EOF
Port $_tsh_port
ListenAddress 127.0.0.1
HostKey $_tsh_dir/$_tsh_name-host
PidFile $_tsh_dir/$_tsh_name.pid
AuthorizedKeysFile $_tsh_dir/authorized_keys
StrictModes no
UsePAM no
PasswordAuthentication no
KbdInteractiveAuthentication no
AllowUsers $t3_user
LogLevel ERROR
ForceCommand $_tsh_dir/$_tsh_name-force
EOF
        /usr/sbin/sshd -D -e -f "$_tsh_dir/$_tsh_name-sshd_config" 2> "$_tsh_dir/$_tsh_name.log" &
        _tsh_pid=$!
        printf '[127.0.0.1]:%s %s\n' "$_tsh_port" "$(cat "$_tsh_dir/$_tsh_name-host.pub")" > "$_tsh_dir/$_tsh_name-known"
        cat > "$_tsh_dir/$_tsh_name-config" <<EOF
Host $_tsh_name
 HostName 127.0.0.1
 Port $_tsh_port
 User $t3_user
 IdentityFile $_tsh_dir/client
 IdentitiesOnly yes
 UserKnownHostsFile $_tsh_dir/$_tsh_name-known
 GlobalKnownHostsFile /dev/null
EOF
        _tsh_polls=0
        while [ "$_tsh_polls" -lt 50 ] && kill -0 "$_tsh_pid" 2>/dev/null; do
            if ssh -F "$_tsh_dir/$_tsh_name-config" -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=2 "$_tsh_name" 'exit 0' 2>/dev/null; then
                sshd_pids="${sshd_pids:-} $_tsh_pid"
                t3_port="$_tsh_port"
                return 0
            fi
            sleep 0.1
            _tsh_polls=$((_tsh_polls + 1))
        done
        # The port was taken or the daemon never answered: try the next port.
        kill "$_tsh_pid" 2>/dev/null || :
        wait "$_tsh_pid" 2>/dev/null || :
        _tsh_port=$((_tsh_port + 7))
    done
    cat "$_tsh_dir/$_tsh_name.log" >&2
    return 1
}
# Two daemons, one shared client identity, one combined client configuration.
t3_ssh_start() {
    t3_user="$(id -un)"
    mkdir -p "$fixture/ssh"
    chmod 700 "$fixture/ssh"
    [ -d /run/sshd ] || mkdir -p /run/sshd 2>/dev/null || :
    ssh-keygen -q -t ed25519 -N '' -f "$fixture/ssh/client" >/dev/null
    cp "$fixture/ssh/client.pub" "$fixture/ssh/authorized_keys"
    t3_ssh_host host-a $((20000 + $$ % 20000)) || return 1
    t3_ssh_host host-b $((t3_port + 1)) || return 1
    cat "$fixture/ssh/host-a-config" "$fixture/ssh/host-b-config" > "$fixture/ssh/config"
    cat "$fixture/ssh/host-a-known" "$fixture/ssh/host-b-known" > "$fixture/ssh/known_hosts"
    sed 's|UserKnownHostsFile .*|UserKnownHostsFile '"$fixture/ssh/known_hosts"'|' "$fixture/ssh/config" > "$fixture/ssh/config.tmp"
    mv "$fixture/ssh/config.tmp" "$fixture/ssh/config"
    ssh_config="$fixture/ssh/config"
}
