/* Exercise the public palette through a PTY and observe literal CLI argv.
 * The palette is kettle's: typing filters the commands, Enter runs the best
 * match, read-only commands open in the output viewer (Enter closes it). */
static void palette_query(struct session *session, const char *query) {
    write_input(session->master, ":", 1U);
    (void)wait_for_marker(session, "Commands", 1000);
    write_input(session->master, query, strlen(query));
    write_input(session->master, "\n", 1U);
}

static void test_palette(const char *tui, const char *hydra, const char *fake_bin) {
    static const struct { const char *query; const char *expected; } cases[] = {
        {"switch", "FAKE ACTION <switch> <feature-live>"},
        {"kill", "FAKE KILL kill feature-live"},
        {"regenerate", "FAKE ACTION <regenerate>"},
        {"status", "FAKE ACTION <status>"},
        {"claims", "FAKE ACTION <claim> <list>"},
        {"scopes", "FAKE ACTION <scope> <show> <feature-live>"},
        {"queue", "FAKE ACTION <queue>"},
        {"resources", "FAKE ACTION <resource> <status> <feature-live>"},
        {"diff", "FAKE ACTION <diff> <feature-live>"},
        {"approvals", "FAKE ACTION <gate> <status> <feature-live>"},
        {"recovery inspect", "FAKE ACTION <doctor>"},
        {"dashboard", "FAKE DASHBOARD"},
        {"sw", "FAKE ACTION <switch> <feature-live>"}
    };
    struct session session;
    size_t index;
    if (!result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0,
                "open palette test terminal")) return;
    (void)wait_for_raw(&session);
    (void)wait_for_model(&session);
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        /* Bound this regression to one queued form burst, not idle throughput. */
        session.slow_output = strcmp(cases[index].query, "recovery inspect") == 0;
        palette_query(&session, cases[index].query);
        if (strcmp(cases[index].query, "kill") == 0) {
            result(wait_for_marker(&session, "Remove feature-live?", 1000), "kill asks for an in-app confirmation");
            write_input(session.master, "y", 1U);
        }
        if (strcmp(cases[index].query, "kill") == 0) {
            /* A removal that succeeded moves focus to surviving work at once
             * (the result is a notice, not an overlay); the fake keeps the
             * head, so step back to it for the next cases. */
            result(wait_for_marker(&session, cases[index].expected, 2000), cases[index].expected);
            result(wait_for_selected(&session, "feature-stale", 1000), "a removed head hands focus to the next surviving head");
            write_input(session.master, "k", 1U);
            (void)wait_for_selected(&session, "feature-live", 1000);
            continue;
        }
        result(wait_for_marker(&session, cases[index].expected, 2000), cases[index].expected);
        if (!strcmp(cases[index].query, "diff"))
            result(!wait_for_marker(&session, "Press Enter to return", 300) && still_running(&session),
                   "a read-only command opens in the viewer without leaving the control centre");
        session.slow_output = false;
        write_input(session.master, "\n", 1U);
        (void)wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000);
    }
    write_input(session.master, ":", 1U);
    (void)wait_for_marker(&session, "Commands", 1000);
    write_input(session.master, "tatus\033[", 7U);
    sleep_ms(10);
    write_input(session.master, "Hs\n", 3U);
    result(wait_for_marker(&session, "FAKE ACTION <status>", 1000),
           "queued form input preserves a fragmented Home key and cursor insertion");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000);
    palette_query(&session, "collisions");
    (void)wait_for_marker(&session, "Compare with head", 1000);
    write_input(session.master, "other head;literal\n", 19U);
    result(wait_for_marker(&session, "FAKE ACTION <collision> <feature-live> <other head;literal>", 1000),
           "comparison target remains one literal argument");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000);
    write_input(session.master, ":", 1U);
    (void)wait_for_marker(&session, "Commands", 1000);
    write_input(session.master, "\033", 1U);
    sleep_ms(60);
    result(wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000) && still_running(&session), "Esc closes the palette and runs nothing");
    palette_query(&session, "UNKNOWN");
    result(wait_for_marker(&session, "No matching command", 1000), "unknown palette search is inert");
    write_input(session.master, "\033", 1U);
    sleep_ms(60); /* Escape stands alone after 20 ms; a key sooner would make it Alt */
    (void)wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000);
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Filter heads", 1000);
    write_input(session.master, "no-match\n", 9U);
    (void)wait_for_marker(&session, "No heads match", 1000);
    palette_query(&session, "switch");
    result(wait_for_marker(&session, "a head for that command", 1000), "palette requires a visible selected head");
    palette_query(&session, "collisions");
    result(wait_for_marker(&session, "a head for that command", 1000), "comparison requires a head before prompting");
    palette_query(&session, "status");
    result(wait_for_marker(&session, "FAKE ACTION <status>", 1000), "global actions work without a selected head");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000);
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0, "palette test exits cleanly");
    close_session(&session);
}

/* Recovery reviews a leftover worktree read-only and removes only that path
 * through the shell gc authority after an in-app confirmation. */
static void test_recovery_actions(const char *tui, const char *hydra, const char *fake_bin) {
    struct session session;
    if (!result(open_session(&session, tui, hydra, fake_bin, 80, 24) == 0, "open recovery action terminal")) return;
    (void)wait_for_raw(&session);
    (void)wait_for_model(&session);
    write_input(session.master, "\033[<0;20;1M", 10U);
    (void)wait_for_markers(&session, "Repairs 4", "Approve S-1: T-1", 3000);
    write_input(session.master, "jjjjjj", 6U);
    result(wait_for_selected(&session, "Leftover lock", 1000), "a repair is selectable in Inbox");
    drain_output(&session);
    write_input(session.master, "x", 1U);
    result(!wait_for_marker(&session, "Remove leftover worktree", 500) && wait_for_selected(&session, "Leftover lock", 500),
           "x on a non-worktree finding removes nothing");
    write_input(session.master, "j", 1U);
    result(wait_for_selected(&session, "Leftover worktree from a removed task", 1000), "leftover worktree finding is selectable");
    write_input(session.master, "c", 1U);
    result(wait_for_marker(&session, "FAKE GC <gc> <--policy> <orphaned> <--dry-run>", 2000), "c runs the read-only gc review");
    write_input(session.master, "\033", 1U);
    (void)wait_for_marker(&session, "\xe2\x97\x86 hydra", 1000);
    write_input(session.master, "x", 1U);
    result(wait_for_marker(&session, "Remove leftover worktree feature-old", 1000), "worktree removal asks inside the UI");
    write_input(session.master, "n", 1U);
    result(wait_for_marker(&session, "Removal cancelled; nothing changed", 1000), "declined worktree removal changes nothing");
    write_input(session.master, "x", 1U);
    (void)wait_for_marker(&session, "Remove leftover worktree feature-old", 1000);
    write_input(session.master, "y", 1U);
    result(wait_for_marker(&session, "Removed leftover worktree /tmp/wt/head_0123456789abcdef; the branch is kept", 3000),
           "confirmed removal passes only that worktree path to gc --apply");
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0, "recovery action test exits cleanly");
    close_session(&session);
}
