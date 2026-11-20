/* Directory syncs after a rename or a removal. RLIMIT_FSIZE cannot make a
 * directory sync fail, so this test binary carries its own fsync: it records
 * which directories were synced (by device and inode) and, while sync_fail
 * is set, fails those syncs with that errno. A file sync succeeds without
 * reaching the disk: no case here needs file durability, and macOS declares
 * no fdatasync to pass it to. The
 * code under test reaches it because the helper library is linked into the
 * test statically. A case asserts that its directory was synced exactly once
 * more, that a failed sync is reported as the caller promises, and that a
 * file system without directory sync (EINVAL) is not a failure. */
static struct { dev_t dev; ino_t ino; } sync_log[256];
static size_t sync_logged;
static struct { dev_t dev; ino_t ino; int error; } sync_target;
int fsync(int fd) {
    struct stat st;
    if (!fstat(fd, &st) && S_ISDIR(st.st_mode)) {
        if (sync_logged < sizeof(sync_log) / sizeof(sync_log[0])) { sync_log[sync_logged].dev = st.st_dev; sync_log[sync_logged].ino = st.st_ino; sync_logged++; }
        if (sync_target.error && sync_target.dev == st.st_dev && sync_target.ino == st.st_ino) { errno = sync_target.error; return -1; }
        return 0;
    }
    return 0;
}
/* From now on the directory at path fails its syncs with error (0: none).
 * Only that directory: staging directories are synced by the writes that
 * fill them, and those must keep working. */
static void sync_fail_in(const char *path, int error) {
    struct stat st;
    assert(!stat(path, &st));
    sync_target.dev = st.st_dev; sync_target.ino = st.st_ino; sync_target.error = error;
}
static void sync_fail_off(void) { sync_target.error = 0; }
/* How many times the directory at path has been synced so far. */
static size_t synced(const char *path) {
    struct stat st; size_t i, count = 0;
    assert(!stat(path, &st));
    for (i = 0; i < sync_logged; i++) if (sync_log[i].dev == st.st_dev && sync_log[i].ino == st.st_ino) count++;
    return count;
}
static char *sync_hex(const char *text) {
    size_t i, length = strlen(text); char *hex = malloc(length * 2 + 1);
    assert(hex);
    for (i = 0; i < length; i++) (void)snprintf(hex + i * 2, 3, "%02x", (unsigned char)text[i]);
    return hex;
}
static void sync_code(json_object *result, bool ok, const char *code) {
    const char *got = f_string(f_field(result, "error"), "code");
    if (json_object_get_boolean(f_field(result, "ok")) != ok || (code && (!got || strcmp(got, code))))
        fprintf(stderr, "sync: ok %d code %s (want ok %d code %s)\n", json_object_get_boolean(f_field(result, "ok")), got ? got : "-", ok, code ? code : "-");
    assert(json_object_get_boolean(f_field(result, "ok")) == ok && (!code || (got && !strcmp(got, code))));
}
static void sync_helper(const char *home) {
    char dir[F_PATH], absent[F_PATH]; size_t before;
    assert(!f_path(dir, sizeof(dir), home, "sync-helper") && !mkdir(dir, 0700));
    before = synced(dir);
    assert(!f_sync_dir(dir) && synced(dir) == before + 1);
    sync_fail_in(dir, EINVAL); assert(!f_sync_dir(dir) && !task_sync_dir(dir));
    sync_fail_in(dir, EIO); errno = 0; assert(f_sync_dir(dir) && errno == EIO); errno = 0; assert(task_sync_dir(dir) && errno == EIO);
    sync_fail_off();
    assert(!f_path(absent, sizeof(absent), dir, "absent") && f_sync_dir(absent) && task_sync_dir(absent));
    assert(!f_remove_tree(dir));
}
/* An install is in place once its parent is synced. A parent that cannot be
 * synced leaves the install where it is and answers outcome_unknown; running
 * the install again finds it, syncs and succeeds. */
static void sync_install(const char *home) {
    char dir[F_PATH], prefix[F_PATH], other[F_PATH], *version, *protocol, *saved = getenv("HYDRA_HOME") ? strdup(getenv("HYDRA_HOME")) : NULL;
    const char *names[] = {"bin/hydra", "libexec/hydra/hydra-fleet"}, *zero = "0000000000000000000000000000000000000000000000000000000000000000";
    json_object *package = json_object_new_object(), *files = json_object_new_array(), *result; size_t i, before;
    assert(!f_path(dir, sizeof(dir), home, "sync-install") && !mkdir(dir, 0700));
    assert(!f_path(prefix, sizeof(prefix), dir, "one") && !f_path(other, sizeof(other), dir, "two"));
    version = sync_hex("#!/bin/sh\ncase \"$1\" in\n--version) echo 'Hydra version " F_VERSION "' ;;\n"
        "fleet) echo '{\"ok\":true,\"data\":{\"hydra_version\":\"" F_VERSION "\",\"fleet_protocol\":1,\"state_schema\":2,\"event_schema\":1,\"json_schema\":1,\"capabilities\":[]}}' ;;\nesac\n");
    protocol = sync_hex("#!/bin/sh\necho 'Hydra fleet protocol 1'\n");
    json_object_object_add(package, "schema_version", json_object_new_int(1)); f_string_add(package, "kind", "install");
    for (i = 0; i < 2; i++) {
        json_object *file = json_object_new_object();
        f_string_add(file, "path", names[i]); f_string_add(file, "hex", i ? protocol : version);
        json_object_array_add(files, file);
    }
    json_object_object_add(package, "files", files);

    before = synced(dir);
    result = f_install(package, zero, prefix); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(dir) > before);
    /* The same install again finds the directory and syncs it once more. */
    before = synced(dir);
    result = f_install(package, zero, prefix); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(dir) == before + 1);

    sync_fail_in(dir, EIO);
    result = f_install(package, zero, other); sync_code(result, false, "outcome_unknown"); json_object_put(result);
    assert(!access(other, F_OK));
    result = f_install(package, zero, other); sync_code(result, false, "outcome_unknown"); json_object_put(result);
    sync_fail_off();
    before = synced(dir);
    result = f_install(package, zero, other); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(dir) == before + 1);
    assert(!f_remove_tree(other));
    sync_fail_in(dir, EINVAL);
    result = f_install(package, zero, other); sync_code(result, true, NULL); json_object_put(result);
    sync_fail_off();

    /* The qualification of the staged shell sets HYDRA_HOME for itself. */
    if (saved) { assert(!setenv("HYDRA_HOME", saved, 1)); free(saved); } else unsetenv("HYDRA_HOME");
    json_object_put(package); free(version); free(protocol);
    assert(!f_remove_tree(dir));
}
/* An import is in place once the directory it was renamed into is synced: a
 * configuration import beside the project, a history import under
 * fleet/history. It cannot be repeated over itself, so a sync that fails is
 * reported with the place the files are. */
static void sync_import(const char *home) {
    char project[F_PATH], failing[F_PATH], history[F_PATH], settled[F_PATH]; const char *message;
    json_object *bundle = json_object_new_object(), *files = json_object_new_array(), *file = json_object_new_object(), *result; size_t before;
    f_home = home;
    assert(!f_path(project, sizeof(project), home, "sync-import") && !mkdir(project, 0700));
    assert(!f_path(failing, sizeof(failing), home, "sync-import-eio") && !mkdir(failing, 0700));
    assert(!f_path(settled, sizeof(settled), home, "sync-import-einval") && !mkdir(settled, 0700));
    assert(!f_path(history, sizeof(history), home, "fleet/history"));
    json_object_object_add(bundle, "schema_version", json_object_new_int(1)); f_string_add(bundle, "kind", "config");
    f_string_add(file, "path", "config.yml"); f_string_add(file, "hex", "6162");
    json_object_array_add(files, file); json_object_object_add(bundle, "files", files);

    before = synced(project);
    result = f_bundle_import(project, bundle); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(project) == before + 1);
    sync_fail_in(failing, EIO);
    result = f_bundle_import(failing, bundle); sync_code(result, false, "outcome_unknown");
    message = f_string(f_field(result, "error"), "message");
    assert(message && strstr(message, failing) && strstr(message, ".hydra"));
    json_object_put(result);
    sync_fail_in(settled, EINVAL);
    result = f_bundle_import(settled, bundle); sync_code(result, true, NULL); json_object_put(result);
    sync_fail_off();
    assert(!f_path(settled, sizeof(settled), failing, ".hydra/config.yml") && !access(settled, F_OK));

    f_string_add(bundle, "kind", "history"); f_string_add(file, "path", "run-id");
    assert(!f_mkdirs(history)); before = synced(history);
    result = f_bundle_import(project, bundle); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(history) == before + 1);
    sync_fail_in(history, EIO);
    result = f_bundle_import(project, bundle); sync_code(result, false, "outcome_unknown"); json_object_put(result);
    sync_fail_off();
    json_object_put(bundle);
}
/* A retained provider payload is published into agent-payloads, which is
 * synced after pruning; a directory this call made is also synced in its
 * parent. A failure is reported as before: the caller records the payload as
 * not retained. */
static void sync_retain(const char *home) {
    char head[F_PATH], root[F_PATH], kept[F_PATH]; struct f_capture cap; size_t before_head, before_root;
    memset(&cap, 0, sizeof(cap)); cap.out = (char *)"out"; cap.out_bytes = 3; cap.err = (char *)""; cap.err_bytes = 0;
    assert(!f_path(head, sizeof(head), home, "sync-retain") && !mkdir(head, 0700));
    assert(!f_path(root, sizeof(root), head, "agent-payloads"));
    before_head = synced(head);
    assert(!agent_retain(head, "run_one", &cap));
    assert(synced(head) == before_head + 1 && synced(root) >= 1);
    before_head = synced(head); before_root = synced(root);
    assert(!agent_retain(head, "run_two", &cap));
    assert(synced(head) == before_head && synced(root) == before_root + 1);
    sync_fail_in(root, EIO);
    assert(agent_retain(head, "run_three", &cap) == -1);
    sync_fail_off();
    assert(!f_path(kept, sizeof(kept), root, "run_three/stdout") && !access(kept, F_OK));
    sync_fail_in(root, EINVAL);
    assert(!agent_retain(head, "run_four", &cap));
    sync_fail_off();
    /* A payload directory made by this call has its own entry in head. */
    assert(!f_path(head, sizeof(head), home, "sync-retain-fresh") && !mkdir(head, 0700));
    sync_fail_in(head, EIO);
    assert(agent_retain(head, "run_one", &cap) == -1);
    sync_fail_off();
    assert(!f_path(kept, sizeof(kept), head, "agent-payloads/run_one/stdout") && !access(kept, F_OK));
    assert(!f_remove_tree(head));
}
/* Removing a record is a directory change too: the alias file of remote
 * remove and the marker of placement undrain are gone for good only once
 * their directory is synced. */
static void sync_removals(const char *home) {
    char private[F_PATH], remotes[F_PATH], drained[F_PATH]; struct f_remote remote; json_object *result; size_t before;
    char *remove[] = {(char *)"remove", (char *)"alias-r"}, *drain[] = {(char *)"drain", (char *)"alias-a"}, *undrain[] = {(char *)"undrain", (char *)"alias-a"};
    assert(!f_path(private, sizeof(private), home, "sync-removals") && !mkdir(private, 0700));
    f_home = private;
    memset(&remote, 0, sizeof(remote));
    assert(!f_copy(remote.name, sizeof(remote.name), "alias-r") && !f_copy(remote.target, sizeof(remote.target), "host-r") && !f_copy(remote.hydra, sizeof(remote.hydra), "hydra"));
    assert(!f_remote_save(&remote) && !f_path(remotes, sizeof(remotes), private, "fleet/remotes"));
    before = synced(remotes);
    sync_fail_in(remotes, EIO);
    result = f_remote_cli(2, remove); sync_code(result, false, "io_failed"); json_object_put(result);
    sync_fail_off();
    assert(synced(remotes) == before + 1);
    assert(!f_remote_save(&remote));
    before = synced(remotes);
    result = f_remote_cli(2, remove); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(remotes) == before + 1);

    result = placement_cli(2, drain); sync_code(result, true, NULL); json_object_put(result);
    assert(!f_path(drained, sizeof(drained), private, "fleet/placement-drained"));
    before = synced(drained);
    sync_fail_in(drained, EIO);
    result = placement_cli(2, undrain); sync_code(result, false, "io_failed"); json_object_put(result);
    sync_fail_off();
    assert(synced(drained) == before + 1);
    before = synced(drained);
    result = placement_cli(2, undrain); sync_code(result, true, NULL); json_object_put(result);
    assert(synced(drained) == before + 1 && !placement_drained("alias-a"));
    f_home = home;
}
