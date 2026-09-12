/* Exercise the public palette through a PTY and observe literal CLI argv. */
static void palette_query(struct session *session, const char *query) {
    write_input(session->master, ":", 1U);
    (void)wait_for_marker(session, "Action search:", 1000);
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
        {"git diff", "FAKE ACTION <diff> <feature-live>"},
        {"approvals", "FAKE ACTION <gate> <status> <feature-live>"},
        {"recovery inspect", "FAKE ACTION <doctor>"},
        {"dashboard", "FAKE DASHBOARD"},
        {"s", "FAKE ACTION <switch> <feature-live>"}
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
        result(wait_for_marker(&session, cases[index].expected, 1000), cases[index].expected);
        session.slow_output = false;
        write_input(session.master, "\n", 1U);
        (void)wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000);
    }
    write_input(session.master, ":", 1U);
    (void)wait_for_marker(&session, "Action search:", 1000);
    write_input(session.master, "tatus\033[", 7U);
    sleep_ms(10);
    write_input(session.master, "Hs\n", 3U);
    result(wait_for_marker(&session, "FAKE ACTION <status>", 1000),
           "queued form input preserves a fragmented Home key and cursor insertion");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000);
    palette_query(&session, "collisions");
    (void)wait_for_marker(&session, "Compare with head:", 1000);
    write_input(session.master, "other head;literal\n", 19U);
    result(wait_for_marker(&session, "FAKE ACTION <collision> <feature-live> <other head;literal>", 1000),
           "comparison target remains one literal argument");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000);
    palette_query(&session, "");
    result(wait_for_marker(&session, "action search canceled", 1000), "empty palette search cancels");
    palette_query(&session, "UNKNOWN");
    result(wait_for_marker(&session, "no explicit local action matched", 1000), "unknown palette search is inert");
    write_input(session.master, "/", 1U);
    (void)wait_for_marker(&session, "Search heads:", 1000);
    write_input(session.master, "no-match\n", 9U);
    (void)wait_for_marker(&session, "Search: no-match", 1000);
    palette_query(&session, "switch");
    result(wait_for_marker(&session, "select a head for that action", 1000), "palette requires a visible selected head");
    palette_query(&session, "collisions");
    result(wait_for_marker(&session, "select a head for that action", 1000), "comparison requires a head before prompting");
    palette_query(&session, "status");
    result(wait_for_marker(&session, "FAKE ACTION <status>", 1000), "global actions work without a selected head");
    write_input(session.master, "\n", 1U);
    (void)wait_for_marker(&session, "HYDRA MISSION CONTROL", 1000);
    write_input(session.master, "q", 1U);
    result(wait_for_exit(&session) == 0, "palette test exits cleanly");
    close_session(&session);
}
