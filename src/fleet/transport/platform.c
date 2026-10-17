#include "fleet/transport/platform.h"
#include "fleet/support/json.h"
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static const char *const os_names[][2] = {{"linux", "linux"}, {"Linux", "linux"}, {"darwin", "darwin"}, {"Darwin", "darwin"}};
static const char *const arch_names[][2] = {
    {"x86_64", "x86_64"}, {"amd64", "x86_64"}, {"aarch64", "aarch64"}, {"arm64", "aarch64"}
};

static const char *normalized(const char *value, const char *const (*names)[2], size_t count) {
    size_t i;
    for (i = 0; value && i < count; i++) if (!strcmp(value, names[i][0])) return names[i][1];
    return NULL;
}
int f_platform_set(struct f_platform *platform, const char *os, const char *arch) {
    const char *o = normalized(os, os_names, sizeof(os_names) / sizeof(os_names[0]));
    const char *a = normalized(arch, arch_names, sizeof(arch_names) / sizeof(arch_names[0]));
    if (!o || !a) return -1;
    snprintf(platform->os, sizeof(platform->os), "%s", o);
    snprintf(platform->arch, sizeof(platform->arch), "%s", a);
    return 0;
}
int f_platform_parse(struct f_platform *platform, const char *text) {
    char os[16]; const char *dash = text ? strchr(text, '-') : NULL;
    size_t length = dash ? (size_t)(dash - text) : 0;
    if (!dash || length >= sizeof(os)) return -1;
    memcpy(os, text, length); os[length] = '\0';
    return f_platform_set(platform, os, dash + 1);
}
int f_platform_local(struct f_platform *platform) {
    struct utsname name;
    if (uname(&name) < 0) return -1;
    return f_platform_set(platform, name.sysname, name.machine);
}
static unsigned little16(const unsigned char *bytes) { return (unsigned)bytes[0] | (unsigned)bytes[1] << 8; }
static unsigned long little32(const unsigned char *bytes) {
    return (unsigned long)little16(bytes) | (unsigned long)little16(bytes + 2) << 16;
}
/* ELFCLASS64, little-endian, System V or GNU/Linux ABI; x86-64 or AArch64. */
static int elf_platform(const unsigned char *h, struct f_platform *platform) {
    unsigned machine = little16(h + 18);
    if (h[4] != 2 || h[5] != 1 || (h[7] != 0 && h[7] != 3)) return -1;
    if (machine == 62) return f_platform_set(platform, "linux", "x86_64");
    return machine == 183 ? f_platform_set(platform, "linux", "aarch64") : -1;
}
static int macho_platform(const unsigned char *h, struct f_platform *platform) {
    unsigned long cpu = little32(h + 4);
    if (cpu == 0x01000007UL) return f_platform_set(platform, "darwin", "x86_64");
    return cpu == 0x0100000cUL ? f_platform_set(platform, "darwin", "aarch64") : -1;
}
int f_platform_binary(const char *path, struct f_platform *platform) {
    static const unsigned char elf[] = {0x7f, 'E', 'L', 'F'}, macho[] = {0xcf, 0xfa, 0xed, 0xfe};
    unsigned char header[20]; FILE *fp = path ? fopen(path, "rb") : NULL; size_t n;
    if (!fp) return -1;
    n = fread(header, 1, sizeof(header), fp);
    fclose(fp);
    if (n != sizeof(header)) return -1;
    if (!memcmp(header, elf, sizeof(elf))) return elf_platform(header, platform);
    return memcmp(header, macho, sizeof(macho)) ? -1 : macho_platform(header, platform);
}
bool f_platform_equal(const struct f_platform *left, const struct f_platform *right) {
    return !strcmp(left->os, right->os) && !strcmp(left->arch, right->arch);
}
void f_platform_text(const struct f_platform *platform, char out[F_PLATFORM_TEXT]) {
    snprintf(out, F_PLATFORM_TEXT, "%s-%s", platform->os, platform->arch);
}
json_object *f_platform_json(const struct f_platform *platform) {
    json_object *value = json_object_new_object();
    f_string_add(value, "os", platform->os); f_string_add(value, "arch", platform->arch);
    return value;
}
int f_package_platform(json_object *package, struct f_platform *platform) {
    json_object *value = f_field(package, "platform"); const char *os, *arch;
    if (!value) return 1;
    os = f_string(value, "os"); arch = f_string(value, "arch");
    /* Only the normalized spelling is valid inside a package. */
    if (!json_object_is_type(value, json_type_object) || json_object_object_length(value) != 2 || f_platform_set(platform, os, arch) ||
        strcmp(os, platform->os) || strcmp(arch, platform->arch)) return -1;
    return 0;
}
