/* SHA-256 against the FIPS 180-2 examples and NIST's one-million-'a' vector,
 * the padding boundaries around one block, incremental input and the file
 * readers. Every digest Hydra records goes through these functions. */
#define SHA256_EMPTY "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define SHA256_MILLION "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
static void sha256_expect(const char *text, size_t size, const char *expected) {
    char digest[65];
    f_sha256_text(text, size, digest);
    assert(!strcmp(digest, expected));
}
static void sha256_published(const char *as) {
    static const char two_blocks[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    sha256_expect("", 0, SHA256_EMPTY);
    sha256_expect("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256_expect("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    sha256_expect(two_blocks, strlen(two_blocks), "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    sha256_expect(as, 1000000, SHA256_MILLION);
}
static void sha256_boundaries(const char *as) {
    static const struct { size_t size; const char *digest; } cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) sha256_expect(as, cases[i].size, cases[i].digest);
}
/* Uneven chunks, crossing block edges, give the one-shot digest. */
static void sha256_chunks(const char *as) {
    struct f_sha256 s;
    char digest[65];
    size_t at, step = 1, take;
    f_sha256_init(&s);
    for (at = 0; at < 1000000; at += take) {
        take = at + step > 1000000 ? 1000000 - at : step;
        f_sha256_update(&s, as + at, take);
        step = step % 97 + 1;
    }
    f_sha256_final(&s, digest);
    assert(!strcmp(digest, SHA256_MILLION));
}
/* Files: the reader that refuses a final symlink and the one that follows it. */
static void sha256_files(const char *dir, const char *as) {
    char digest[65], path[F_PATH], link_path[F_PATH];
    assert(!f_path(path, sizeof(path), dir, "million"));
    assert(!f_path(link_path, sizeof(link_path), dir, "million-link"));
    assert(!f_write(path, as, 1000000, true));
    assert(!symlink(path, link_path));
    assert(!f_sha256_file(path, digest) && !strcmp(digest, SHA256_MILLION));
    assert(f_sha256_file(link_path, digest) == -1);
    assert(!f_hash(link_path, digest) && !strcmp(digest, SHA256_MILLION));
    assert(!unlink(link_path) && !unlink(path));
}
/* f_hash reads devices like shasum and refuses directories and missing files. */
static void sha256_refusals(const char *dir) {
    char digest[65], missing[F_PATH];
    assert(!f_path(missing, sizeof(missing), dir, "missing"));
    assert(f_hash(dir, digest) == -1);
    assert(f_hash(missing, digest) == -1);
    assert(!f_hash("/dev/null", digest) && !strcmp(digest, SHA256_EMPTY));
}
static void sha256_vectors(const char *dir) {
    char *as = malloc(1000000);
    assert(as);
    memset(as, 'a', 1000000);
    sha256_published(as);
    sha256_boundaries(as);
    sha256_chunks(as);
    sha256_files(dir, as);
    sha256_refusals(dir);
    free(as);
}
