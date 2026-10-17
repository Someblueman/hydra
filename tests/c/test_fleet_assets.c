/* Provisioning building blocks: platform names and executable headers, the
 * package platform member, fleet-assets.tsv parsing, the private cache and
 * install packages from source trees and installed prefixes. Cache and
 * package cases run under umask 022 and 002. */
#include "fleet/setup/assets.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/bundle.h"
#include "fleet/transport/platform.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
const char *f_home, *f_hydra = "hydra";

static char base[F_PATH];

static void write_bytes(const char *path, const void *bytes, size_t size) {
    int fd;
    unlink(path);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, bytes, size) == (ssize_t)size);
    close(fd);
}
static void write_text(const char *path, const char *text) { write_bytes(path, text, strlen(text)); }
static void at(char path[F_PATH], const char *relative) { assert(!f_path(path, F_PATH, base, relative)); }
static void directory(const char *relative) {
    char path[F_PATH];
    at(path, relative);
    assert(!f_mkdirs(path));
}
static unsigned mode_of(const char *path) {
    struct stat st;
    assert(!lstat(path, &st));
    return (unsigned)(st.st_mode & 0777);
}

static void platform_names(void) {
    struct f_platform p, q; char text[F_PLATFORM_TEXT];
    assert(!f_platform_set(&p, "Linux", "amd64")); f_platform_text(&p, text); assert(!strcmp(text, "linux-x86_64"));
    assert(!f_platform_set(&p, "Darwin", "arm64")); f_platform_text(&p, text); assert(!strcmp(text, "darwin-aarch64"));
    assert(!f_platform_parse(&q, "darwin-aarch64") && f_platform_equal(&p, &q));
    assert(!f_platform_parse(&q, "Linux-aarch64") && !strcmp(q.os, "linux"));
    assert(f_platform_set(&p, "FreeBSD", "x86_64") && f_platform_set(&p, "linux", "riscv64") && f_platform_set(&p, NULL, "x86_64"));
    assert(f_platform_parse(&p, "linux") && f_platform_parse(&p, "linux-") && f_platform_parse(&p, NULL));
    assert(f_platform_parse(&p, "averyveryverylongos-x86_64"));
    assert(!f_platform_local(&p));
}
/* A minimal 64-bit little-endian header for machine (ELF) or cpu (Mach-O). */
static void header(const char *relative, bool elf, unsigned long machine, unsigned char elf_class) {
    unsigned char bytes[64] = {0}; char path[F_PATH];
    if (elf) {
        memcpy(bytes, "\x7f" "ELF", 4); bytes[4] = elf_class; bytes[5] = 1;
        bytes[18] = (unsigned char)(machine & 0xff); bytes[19] = (unsigned char)(machine >> 8);
    } else {
        bytes[0] = 0xcf; bytes[1] = 0xfa; bytes[2] = 0xed; bytes[3] = 0xfe;
        bytes[4] = (unsigned char)(machine & 0xff); bytes[7] = (unsigned char)(machine >> 24);
    }
    at(path, relative); write_bytes(path, bytes, sizeof(bytes));
}
static void expect_binary(const char *relative, const char *platform) {
    struct f_platform p; char path[F_PATH], text[F_PLATFORM_TEXT];
    at(path, relative);
    if (!platform) { assert(f_platform_binary(path, &p)); return; }
    assert(!f_platform_binary(path, &p));
    f_platform_text(&p, text); assert(!strcmp(text, platform));
}
static void executable_headers(void) {
    header("elf-x86", true, 62, 2); expect_binary("elf-x86", "linux-x86_64");
    header("elf-arm", true, 183, 2); expect_binary("elf-arm", "linux-aarch64");
    header("elf-32", true, 62, 1); expect_binary("elf-32", NULL);
    header("elf-riscv", true, 243, 2); expect_binary("elf-riscv", NULL);
    header("macho-x86", false, 0x01000007UL, 0); expect_binary("macho-x86", "darwin-x86_64");
    header("macho-arm", false, 0x0100000cUL, 0); expect_binary("macho-arm", "darwin-aarch64");
    { char path[F_PATH]; at(path, "script"); write_text(path, "#!/bin/sh\n"); }
    expect_binary("script", NULL);
    expect_binary("missing", NULL);
}
static int package_platform(const char *text) {
    struct f_platform p; json_object *package = f_parse(text); int status = f_package_platform(package, &p);
    json_object_put(package);
    return status;
}
static void package_platform_member(void) {
    assert(package_platform("{\"kind\":\"install\"}") == 1);
    assert(package_platform("{\"platform\":{\"os\":\"linux\",\"arch\":\"x86_64\"}}") == 0);
    assert(package_platform("{\"platform\":{\"os\":\"Linux\",\"arch\":\"x86_64\"}}") == -1);
    assert(package_platform("{\"platform\":{\"os\":\"linux\",\"arch\":\"amd64\"}}") == -1);
    assert(package_platform("{\"platform\":{\"os\":\"linux\",\"arch\":\"x86_64\",\"libc\":\"musl\"}}") == -1);
    assert(package_platform("{\"platform\":\"linux-x86_64\"}") == -1);
}

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
static int row(const char *table, const char *version, const char *platform, char sha[65], char name[128]) {
    char path[F_PATH];
    at(path, "fleet-assets.tsv"); write_text(path, table);
    return setup_asset_row(path, version, platform, sha, name);
}
#define HEADER SETUP_ASSET_HEADER "\n"
#define ROW_X86 "2.9.0\tlinux-x86_64\t" SHA_A "\thydra-fleet-2.9.0-linux-x86_64\n"
static const struct { const char *table, *version, *platform; int expected; } table_cases[] = {
    {HEADER "# pinned\n\n" ROW_X86 "2.9.0\tlinux-aarch64\t" SHA_B "\thydra-fleet-2.9.0-linux-aarch64\n", "2.9.0", "linux-aarch64", 0},
    {HEADER ROW_X86, "2.9.0", "darwin-aarch64", 1},
    {HEADER ROW_X86, "2.8.0", "linux-x86_64", 1},
    {HEADER, "2.9.0", "linux-x86_64", 1},
    {"version\tplatform\tsha256\n", "2.9.0", "linux-x86_64", -1},
    {SETUP_ASSET_HEADER, "2.9.0", "linux-x86_64", -1},
    {HEADER "2.9.0\tlinux-x86_64\t" SHA_A "\thydra-fleet-2.9.0-linux-aarch64\n", "2.9.0", "linux-x86_64", -1},
    {HEADER "2.9.0\tLinux-x86_64\t" SHA_A "\thydra-fleet-2.9.0-Linux-x86_64\n", "2.9.0", "linux-x86_64", -1},
    {HEADER "2.9.0\tlinux-x86_64\t" SHA_A "\thydra-fleet-2.9.0-linux-x86_64\textra\n", "2.9.0", "linux-x86_64", -1},
    {HEADER "2.9.0\tlinux-x86_64\tABC\thydra-fleet-2.9.0-linux-x86_64\n", "2.9.0", "linux-x86_64", -1},
    {HEADER "2..9\tlinux-x86_64\t" SHA_A "\thydra-fleet-2..9-linux-x86_64\n", "2.9.0", "linux-x86_64", -1},
    {HEADER "2.9.0\tlinux-x86_64\t" SHA_A "\thydra-fleet-2.9.0-linux-x86_64\r\n", "2.9.0", "linux-x86_64", -1},
    {HEADER ROW_X86 "2.9.0\tlinux-x86_64\t" SHA_B "\thydra-fleet-2.9.0-linux-x86_64\n", "2.9.0", "linux-x86_64", -1},
};
static void asset_table(void) {
    char sha[65], name[128]; size_t i;
    for (i = 0; i < sizeof(table_cases) / sizeof(table_cases[0]); i++)
        assert(row(table_cases[i].table, table_cases[i].version, table_cases[i].platform, sha, name) == table_cases[i].expected);
    assert(row(table_cases[0].table, "2.9.0", "linux-aarch64", sha, name) == 0);
    assert(!strcmp(sha, SHA_B) && !strcmp(name, "hydra-fleet-2.9.0-linux-aarch64"));
    assert(setup_asset_row(NULL, "2.9.0", "linux-x86_64", sha, name) == -1);
}
static void table_lookup(void) {
    char path[F_PATH], found[F_PATH], bin[F_PATH];
    at(path, "fleet-assets.tsv");
    setenv("HYDRA_FLEET_ASSETS_FILE", path, 1);
    assert(!setup_asset_table(found) && !strcmp(found, path));
    at(path, "absent.tsv"); setenv("HYDRA_FLEET_ASSETS_FILE", path, 1);
    assert(setup_asset_table(found) == -1);
    unsetenv("HYDRA_FLEET_ASSETS_FILE");
    directory("prefix/bin"); directory("prefix/share/hydra");
    at(bin, "prefix/bin"); setenv("HYDRA_BIN_DIR", bin, 1);
    assert(setup_asset_table(found) == -1);
    at(path, "prefix/share/hydra/fleet-assets.tsv"); write_text(path, SETUP_ASSET_HEADER "\n");
    assert(!setup_asset_table(found) && strstr(found, "/share/hydra/fleet-assets.tsv"));
    unsetenv("HYDRA_BIN_DIR");
}

static void cache_store(void) {
    char home[F_PATH], dir[F_PATH], digest[65], path[F_PATH], again[F_PATH];
    at(home, "hydra-home"); f_home = home;
    assert(!setup_private_dir("packages", dir));
    assert(mode_of(dir) == 0700);
    assert(!setup_cache_store(dir, ".json", "{\"a\":1}", 7, digest, path));
    assert(strlen(digest) == 64 && strstr(path, digest) && strstr(path, ".json") && mode_of(path) == 0600);
    assert(!setup_cache_store(dir, ".json", "{\"a\":1}", 7, digest, again) && !strcmp(path, again));
    assert(chmod(home, 0777) == 0);
    assert(setup_private_dir("assets", dir) == -1);
    assert(chmod(home, 0700) == 0);
}

/* A source tree (lib/NAME.sh) or an installed prefix (lib/hydra/NAME.sh). */
static void put(const char *root, const char *relative, const char *text) {
    char full[F_PATH], path[F_PATH], parent[F_PATH];
    assert(snprintf(full, sizeof(full), "%s/%s", root, relative) < (int)sizeof(full));
    at(path, full);
    assert(!f_copy(parent, sizeof(parent), path));
    *strrchr(parent, '/') = '\0';
    assert(!f_mkdirs(parent));
    write_text(path, text);
}
static void layout(const char *root, bool installed) {
    const char *lib = installed ? "lib/hydra" : "lib"; char relative[F_PATH];
    put(root, "bin/hydra", "#!/bin/sh\n");
    put(root, installed ? "share/licenses/hydra/LICENSE" : "LICENSE", "hydra license\n");
    put(root, installed ? "libexec/hydra/hydra-fleet.LICENSE" : "docs/licenses/json-c.txt", "json-c license\n");
    snprintf(relative, sizeof(relative), "%s/zeta.sh", lib); put(root, relative, "z\n");
    snprintf(relative, sizeof(relative), "%s/alpha.sh", lib); put(root, relative, "a\n");
    snprintf(relative, sizeof(relative), "%s/notes.txt", lib); put(root, relative, "skip\n");
}
static json_object *package(const char *root, const char *binary, const struct f_platform *platform) {
    char path[F_PATH], exe[F_PATH];
    at(path, root); at(exe, binary);
    return f_package_installation(path, exe, platform);
}
/* Files in order: shell, helper, sorted libraries, licenses. */
static void expect_package(const char *root, const struct f_platform *platform) {
    static const char *const expected[] = {
        "bin/hydra", "libexec/hydra/hydra-fleet", "lib/hydra/alpha.sh", "lib/hydra/zeta.sh",
        "share/licenses/hydra/LICENSE", "share/licenses/hydra/json-c.txt"
    };
    json_object *result = package(root, "elf-x86", platform), *files = f_field(f_field(result, "data"), "files"); size_t i;
    assert(json_object_get_boolean(f_field(result, "ok")) && json_object_array_length(files) == 6);
    for (i = 0; i < 6; i++) assert(!strcmp(f_string(json_object_array_get_idx(files, i), "path"), expected[i]));
    assert(!strcmp(f_string(f_field(f_field(result, "data"), "platform"), "arch"), "x86_64"));
    json_object_put(result);
}
static void expect_package_error(const char *root, const char *binary, const struct f_platform *platform, const char *code) {
    json_object *result = package(root, binary, platform);
    assert(!strcmp(f_string(f_field(result, "error"), "code"), code));
    json_object_put(result);
}
static void package_layouts(void) {
    struct f_platform linux_x86; json_object *result;
    layout("source", false); layout("installed", true);
    header("elf-x86", true, 62, 2);
    assert(!f_platform_parse(&linux_x86, "linux-x86_64"));
    expect_package("source", &linux_x86);
    expect_package("installed", &linux_x86);
    result = package("source", "script", NULL);
    assert(json_object_get_boolean(f_field(result, "ok")) && !f_field(f_field(result, "data"), "platform"));
    json_object_put(result);
    expect_package_error("source", "script", &linux_x86, "platform_mismatch");
    expect_package_error("source", "elf-arm", &linux_x86, "platform_mismatch");
    expect_package_error("missing-root", "elf-x86", &linux_x86, "package_failed");
}

int main(void) {
    char temp[] = "/tmp/hydra-assets-test.XXXXXX", root[F_PATH];
    const mode_t masks[] = {022, 002};
    size_t i;
    assert(mkdtemp(temp) && realpath(temp, root));
    assert(!f_copy(base, sizeof(base), root));
    platform_names();
    executable_headers();
    package_platform_member();
    asset_table();
    table_lookup();
    for (i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
        umask(masks[i]);
        assert(snprintf(base, sizeof(base), "%s/umask%o", root, (unsigned)masks[i]) < (int)sizeof(base) && !mkdir(base, 0700));
        executable_headers();
        cache_store();
        package_layouts();
    }
    assert(!f_remove_tree(root));
    puts("Fleet asset and package tests passed");
    return 0;
}
