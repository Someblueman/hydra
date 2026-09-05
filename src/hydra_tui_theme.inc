/* Semantic terminal and fixed 256-color palettes; terminal mode preserves the user's base colors. */
enum tone { TONE_BASE, TONE_BORDER, TONE_TITLE, TONE_SELECTED, TONE_WARNING, TONE_STRONG };
static const char *theme_names[] = {"terminal", "dark", "light"};

/* Caller owns the output integer; names are fixed internal strings. */
static bool parse_theme(const char *name, int *theme) {
    int index;
    for (index = 0; index < 3; index++) {
        if (strcmp(name, theme_names[index]) == 0) { *theme = index; return true; }
    }
    return false;
}

static void style(const struct app *app, enum tone tone) {
    static const char *palettes[3][6] = {
        {"\033[0m", "\033[0;36m", "\033[0;1;7m", "\033[0;1;7m", "\033[0;33m", "\033[0;1m"},
        {"\033[0;38;5;252;48;5;234m", "\033[0;38;5;117;48;5;234m", "\033[0;38;5;16;48;5;117m", "\033[0;38;5;16;48;5;117m", "\033[0;38;5;220;48;5;234m", "\033[0;1;38;5;252;48;5;234m"},
        {"\033[0;38;5;16;48;5;255m", "\033[0;38;5;24;48;5;255m", "\033[0;1;38;5;231;48;5;24m", "\033[0;1;38;5;231;48;5;24m", "\033[0;38;5;124;48;5;255m", "\033[0;1;38;5;16;48;5;255m"}
    };
    if (app->paint) fputs(palettes[app->theme][tone], stdout);
}
