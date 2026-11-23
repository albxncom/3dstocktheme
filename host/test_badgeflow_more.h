/* The rest of test_badgeflow.c's cases and its two commands: the IDs, the texts, the download names, the open's
 * mode and why an open failed, the download as one JSON line, the card folder for pictures. Included once, by
 * test_badgeflow.c. */

/* ---------------------------------------------------------------- the IDs */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Gives set `slot` of the store's collection the set ID set_id and its badges the IDs from first on, as another
 * tool could have; the original becomes that. */
static void retag(uint32_t slot, uint32_t set_id, uint32_t first)
{
    tree t;
    store_now(&t);
    uint8_t *si = t.mng + BM_SET_INFO + slot * BM_SET_INFO_SIZE;
    uint32_t old = get32(si + 0x10), badges = get32(t.mng + BM_UNIQUE);
    put32(si + 0x10, set_id);
    for (uint32_t i = 0; i < badges; i++) {
        uint8_t *b = t.mng + BM_BADGE_INFO + i * BM_BADGE_INFO_SIZE;
        if (get32(b + 8) == old) {
            put32(b + 8, set_id);
            put32(b + 4, first++);
        }
    }
    for (uint32_t n = 0; n < (BADGE_MNG_SIZE - BM_LAYOUT) / BM_LAYOUT_RECORD; n++) {
        uint8_t *r = t.mng + BM_LAYOUT + n * BM_LAYOUT_RECORD;
        if (get32(r + 8) == old)
            put32(r + 8, set_id);
    }
    put_file(store.dir, STORE_FILE[1], t.mng, t.mng_size);
    free_tree(&original);
    original = t;
}

/* The new set's badge IDs in the store, from its first badge on. */
static uint32_t badge_id_at(uint32_t index)
{
    tree t;
    store_now(&t);
    uint32_t id = t.mng ? get32(t.mng + BM_BADGE_INFO + index * BM_BADGE_INFO_SIZE + 4) : 0;
    free_tree(&t);
    return id;
}

/* A manifest file with a valid sum around `body` (the lines after the head). */
static void put_manifest(const char *body)
{
    char text[1024], hex[65];
    uint8_t sum[32];
    int n = snprintf(text, sizeof text, "3DStockTheme badges 1\n%s", body);
    sha256(text, (size_t)n, sum);
    for (int i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    n += snprintf(text + n, sizeof text - (size_t)n, "end %s\n", hex);
    mkdir(card_dir, 0755);
    put_file(card_dir, "badge-manifest.txt", text, (size_t)n);
}

static void test_id_ranges(void)
{
    badge_flow_result res;
    badge_listing *l = malloc(sizeof *l);
    /* IDs of the application's halves that are in the collection already, from another tool: passed over */
    world(THREE, 3);
    retag(0, 0x7800u, 0x5800u);                 /* 4 badges: 0x5800..0x5803 */
    retag(2, 0x7802u, 0x5805u);                 /* 5 badges: 0x5805..0x5809; 0x5804 alone is too short a gap */
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7801u);
    CHECK_EQ(badge_id_at(res.start), 0x580Au);
    CHECK_EQ(badge_id_at(res.start + 2), 0x580Cu);
    CHECK(strstr(log_text, "IDs in the collection passed over") != NULL);
    CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7803u);              /* 0x7802 is the other tool's */
    CHECK_EQ(badge_id_at(res.start), 0x580Du);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK(store_is(&original));
    /* the Mac tool's IDs (the lower halves) are never handed out, also when free */
    world(THREE, 3);
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7800u);
    CHECK_EQ(badge_id_at(res.start), 0x5800u);
    /* no room left in the application's half: refused before any backup */
    world(THREE, 3);
    put_manifest("next_set_id 32767\nnext_badge_id 28670\n");  /* 0x7FFF, and two badge IDs left */
    store.writes = 0;
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_REFUSED);
    check_refused(&res, BSTAGE_CHANGE, BADGE_E_IDS);
    put_manifest("next_set_id 32768\nnext_badge_id 22528\n");  /* the set IDs are used up */
    CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_REFUSED);
    check_refused(&res, BSTAGE_CHANGE, BADGE_E_IDS);
    put_manifest("next_set_id 32767\nnext_badge_id 22528\n");  /* the last set ID */
    CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_DONE);
    CHECK_EQ(res.set_id, 0x7FFFu);
    /* a manifest with an ID or a counter of the lower halves is not this application's: damaged */
    static const char *const LOWER[] = {
        "next_set_id 28673\nnext_badge_id 22529\nset 28672\tX\tX\nbadge 0\t22528\tX.png\n",
        "next_set_id 30721\nnext_badge_id 16385\nset 30720\tX\tX\nbadge 0\t16384\tX.png\n",
        "next_set_id 28672\nnext_badge_id 22528\n",
        "next_set_id 30720\nnext_badge_id 16384\n",
    };
    for (size_t k = 0; k < sizeof LOWER / sizeof *LOWER; k++) {
        world(THREE, 3);
        put_manifest(LOWER[k]);
        CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
        CHECK_EQ(l->manifest_bad, 1);
        badge_listing_free(l);
        store.writes = 0;
        CHECK_EQ(append("Stern.png", BADGE_SRC_PNG, &res), BFLOW_REFUSED);
        check_refused(&res, BSTAGE_MANIFEST, BADGE_E_MANIFEST);
    }
    /* the same manifest in the upper halves reads */
    world(THREE, 3);
    put_manifest("next_set_id 30721\nnext_badge_id 22529\nset 30720\tX\tX\nbadge 0\t22528\tX.png\n");
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    CHECK_EQ(l->manifest_bad, 0);
    badge_listing_free(l);
    free(l);
}

/* ---------------------------------------------------------------- the texts */

static int text_clean(const char *s)
{
    return s && *s && !strstr(s, "\xc3\x9f") && !strstr(s, "\xc2\xb7") && !strstr(s, "\xc2\xab") &&
           !strstr(s, "\xc2\xbb");
}

static void test_texts(void)
{
    char a[512], b[1024];
    for (int lang = 0; lang < 2; lang++) {
        for (int id = 0; id < BT_COUNT; id++)
            CHECK(text_clean(badge_text(lang, id)));
        for (int r = BADGE_OK; r <= BADGE_E_ZIP; r++)
            CHECK(text_clean(badge_text_result(lang, r)));
        for (int plan = BPLAN_SET; plan <= BPLAN_FULL; plan++) {
            badge_listing l;
            badge_peek peek;
            memset(&l, 0, sizeof l);
            memset(&peek, 0, sizeof peek);
            l.state = BLIST_NO_DATA;
            peek.result = BADGE_E_PNG;
            badge_text_plan(lang, plan, &l, &peek, a, sizeof a);
            CHECK(text_clean(a));
        }
        static const int OUTCOMES[3] = {BFLOW_REFUSED, BFLOW_RESTORED, BFLOW_UNKNOWN};
        for (int o = 0; o < 3; o++)
            for (int stage = BSTAGE_NONE; stage <= BSTAGE_VERIFY; stage++) {
                badge_flow_result res;
                memset(&res, 0, sizeof res);
                res.outcome = OUTCOMES[o];
                res.stage = stage;
                res.badge = BADGE_E_FULL;
                snprintf(res.backup, sizeof res.backup, "sdmc:/theme-pack/badge-backup/0001");
                badge_text_outcome(lang, &res, a, sizeof a, b, sizeof b);
                CHECK(text_clean(a));
                CHECK(text_clean(b));
                if (o)
                    CHECK(strstr(b, "/theme-pack/badge-backup/0001") && !strstr(b, "sdmc:"));
            }
        badge_listing *l = calloc(1, sizeof *l);
        l->col.sets = 3;
        l->col.badges = 288;
        CHECK_EQ(badge_text_status(lang, l, a, sizeof a), 1);
        CHECK(!strcmp(a, "3 Sets, 288 Badges"));
        l->col.sets = 1;
        l->col.badges = 1;
        badge_text_status(lang, l, a, sizeof a);
        CHECK(!strcmp(a, "1 Set, 1 Badge"));
        l->state = BLIST_READ;
        CHECK_EQ(badge_text_status(lang, l, a, sizeof a), 0);
        CHECK(text_clean(a));
        free(l);
        badge_text_count(lang, 12, a, sizeof a);
        CHECK(!strcmp(a, "12 Badges"));
    }
    CHECK(!strcmp(badge_text(1, BT_INSTALLED_BADGES), "Installierte Badges"));
    CHECK(strstr(badge_text(0, BT_ASK_REMOVE), "%s") != NULL);
    CHECK(strstr(badge_text(1, BT_ASK_REMOVE_NOTE), "%u") != NULL);
}

/* ---------------------------------------------------------------- download names */

static void test_download_names(void)
{
    char out[DOWNLOAD_NAME], *dir = join(root, "names");
    rm_rf(dir);
    mkdir(dir, 0755);
    badge_download_name(NULL, "http://example.invalid/sets/Tiere.zip", BADGE_SRC_ZIP, dir, out, sizeof out);
    CHECK(!strcmp(out, "Tiere.zip"));
    char *sub = join(dir, "Tiere");
    mkdir(sub, 0755);                           /* a folder of that name is a set of that name */
    badge_download_name(NULL, "http://example.invalid/sets/Tiere.zip", BADGE_SRC_ZIP, dir, out, sizeof out);
    CHECK(!strcmp(out, "Tiere (2).zip"));
    put_file(dir, "Tiere (2).png", "x", 1);
    badge_download_name(NULL, "http://example.invalid/sets/Tiere.png", BADGE_SRC_PNG, dir, out, sizeof out);
    CHECK(!strcmp(out, "Tiere (3).png"));
    badge_download_name("attachment; filename=\"Sterne.png\"", "http://example.invalid/get?id=7", BADGE_SRC_PNG, dir,
                        out, sizeof out);
    CHECK(!strcmp(out, "Sterne.png"));
    badge_download_name(NULL, "http://example.invalid/", BADGE_SRC_ZIP, dir, out, sizeof out);
    CHECK(!strcmp(out, "Badges.zip"));
    badge_download_name(NULL, "http://example.invalid/theme", BADGE_SRC_ZIP, dir, out, sizeof out);
    CHECK(!strcmp(out, "theme.zip"));
    badge_download_name(NULL, "http://example.invalid/a/b/c/../x%2Fy.zip", BADGE_SRC_ZIP, dir, out, sizeof out);
    CHECK(!strchr(out, '/'));
    char url[400];
    snprintf(url, sizeof url, "http://example.invalid/%0100d.zip", 7);
    badge_download_name(NULL, url, BADGE_SRC_ZIP, dir, out, sizeof out);
    CHECK_EQ(strlen(out), BADGE_DOWNLOAD_BASE + 4);
    free(sub);
    free(dir);
}

/* ---------------------------------------------------------------- the commands */

/* test_badgeflow download URL PART DIR [CA]: one JSON line with the result and whether the part file is left. */
static int cmd_badge_download(int argc, char **argv)
{
    if (argc < 3)
        return 3;
    if (curl_global_init(CURL_GLOBAL_DEFAULT))
        return 3;
    download_result res;
    badge_download(argv[0], argv[1], argv[2], argc >= 4 ? argv[3] : NULL, "3DStockTheme/0.0.0-test", NULL, &res);
    struct stat st;
    printf("{\"status\": %d, \"http\": %ld, \"kind\": %d, \"detail\": \"%s\", \"file\": \"%s\", \"part_left\": %d, "
           "\"bytes\": %llu}\n",
           res.status, res.http, res.status == DL_OK ? res.kind : -1,
           res.status == DL_BAD_THEME ? badge_result_name(res.detail) : "", res.file, stat(argv[1], &st) == 0,
           (unsigned long long)res.bytes);
    curl_global_cleanup();
    return 0;
}

/* test_badgeflow fixture DIR: DIR/Badges with generated sets (a folder, a zip, a PNG, a subfolder with a set in it,
 * a file that is no set, a set that does not read) and DIR/extdata with a generated collection of three sets. */
static int cmd_fixture(const char *dir)
{
    char *saved_root = strdup(root);
    snprintf(root, sizeof root, "%s", dir);
    world(THREE, 3);
    snprintf(root, sizeof root, "%s", saved_root);
    free(saved_root);
    char *mehr = join(badges_dir, "Mehr");
    mkdir(mehr, 0755);
    put_png(mehr, "Mond.png", 128, 128, 7);
    put_file(mehr, "liesmich.txt", "x", 1);     /* makes Mehr a folder to open, not a set */
    char *planets = join(mehr, "Planeten");
    mkdir(planets, 0755);
    put_png(planets, "Saturn.png", 64, 64, 8);
    put_file(badges_dir, "Kaputt.png", "not a png", 9);
    put_png(badges_dir, "Schief.png", 60, 64, 9);
    free(planets);
    free(mehr);
    free_tree(&original);
    printf("%s\n", failures ? "fixture: failed" : "fixture: ok");
    return failures ? 1 : 0;
}

/* ---------------------------------------------------------------- the folder store (DEMO=1 and the host) */

static void test_dir_store(void)
{
    badge_store dir_store;
    badge_dir d;
    badge_listing *l = malloc(sizeof *l);
    badge_flow_result res;
    world(THREE, 3);
    char *home = join(root, "pretend");
    badge_env pretend = env;
    pretend.store = &dir_store;
    badge_dir_store(&dir_store, &d, home, 0);   /* not made: no badge data */
    CHECK_EQ(badge_flow_list(&pretend, l), BLIST_NO_DATA);
    badge_listing_free(l);
    CHECK(!exists(root, "pretend"));
    badge_dir_store(&dir_store, &d, home, 1);   /* made on first use, as an empty collection */
    CHECK_EQ(badge_flow_list(&pretend, l), BLIST_OK);
    CHECK_EQ(l->col.sets, 0);
    CHECK_EQ(l->col.badges, 0);
    badge_listing_free(l);
    size_t size;
    uint8_t *data = get_file(home, "BadgeData.dat", &size), *mng;
    CHECK_EQ(size, BADGE_DATA_SIZE);
    free(data);
    badge_source src;
    CHECK_EQ(load("Tiere", BADGE_SRC_FOLDER, &src), BADGE_OK);
    CHECK_EQ(badge_flow_append(&pretend, &src, &res), BFLOW_DONE);
    badge_source_free(&src);
    CHECK_EQ(res.sets, 1);
    CHECK_EQ(res.set_id, 0x7800u);
    CHECK_EQ(badge_flow_list(&pretend, l), BLIST_OK);
    CHECK(l->col.sets == 1 && l->col.set[0].mine && l->col.set[0].removable && l->undo);
    badge_listing_free(l);
    CHECK(store_is(&original));                 /* the other store was not touched */
    CHECK_EQ(badge_flow_remove_last(&pretend, &res), BFLOW_DONE);
    CHECK_EQ(res.identical, 1);
    data = get_file(home, "BadgeData.dat", &size);
    mng = get_file(home, "BadgeMngFile.dat", &size);
    int zero = data && mng;
    for (uint32_t i = 0; zero && i < BADGE_DATA_SIZE; i++)
        zero = !data[i];
    for (uint32_t i = 0; zero && i < BADGE_MNG_SIZE; i++)
        zero = !mng[i];
    CHECK(zero);
    free(data);
    free(mng);
    free(home);
    free(l);
}

/* ---------------------------------------------------------------- the open's mode, and why an open failed (0.9.4) */

/* Console finding 1 of 0.9.3: started as a .3dsx, the listing said "Keine Badge-Daten" for C92044E6 (in use) while
 * HOME held its files. A listing opens to read only; a failed open is told by its code; a write that meets a lock
 * at its open has written nothing; the writes open to read and write as before. */
static const struct {
    unsigned code;
    int kind, state;
    const char *status[2], *text[2];
} KINDS[3] = {
    {CODE_LOCKED, BOPEN_LOCKED, BLIST_LOCKED, {"The badge data is locked", "Die Badge-Daten sind gesperrt"},
     {"The badge data is locked. The HOME Menu is holding the badge data open. Press HOME and return to the "
      "application, or start 3DStockTheme as an installed title from the HOME Menu. (Code C92044E6)",
      "Die Badge-Daten sind gesperrt. Das HOME-Menü hält die Badge-Daten gerade offen. HOME drücken und zur "
      "Anwendung zurückkehren, oder 3DStockTheme als installierten Titel aus dem HOME-Menü starten. (Code C92044E6)"}},
    {CODE_OPEN, BOPEN_MISSING, BLIST_NO_DATA, {"No badge data on this console", "Keine Badge-Daten auf dieser Konsole"},
     {"No badge data on this console. The HOME Menu makes it once badges were used there. (Code C8804478)",
      "Keine Badge-Daten auf dieser Konsole. Das HOME-Menü legt sie an, sobald dort Badges benutzt wurden. (Code "
      "C8804478)"}},
    {CODE_IO, BOPEN_OTHER, BLIST_OPEN, {"The badge data cannot be read", "Die Badge-Daten sind nicht lesbar"},
     {"The badge data could not be opened. (Code C8A04555)",
      "Die Badge-Daten konnten nicht geöffnet werden. (Code C8A04555)"}},
};

/* The manifest, the undo records and the backups on the card, as one string of their sizes and sums. */
static void card_state(char *out, size_t size)
{
    char *backups = backup_root(), *undo = join(card_dir, "badge-undo");
    size_t n = 0;
    uint8_t *mf = get_file(card_dir, "badge-manifest.txt", &n), sum[32];
    sha256(mf ? mf : (const uint8_t *)"", n, sum);
    snprintf(out, size, "manifest %zu %02x%02x%02x%02x, %d backups, %d undo records", n, sum[0], sum[1], sum[2],
             sum[3], count_in(backups, NULL), count_in(undo, NULL));
    free(mf);
    free(backups);
    free(undo);
}

static void test_open_modes(void)
{
    badge_listing *l = calloc(1, sizeof *l);
    badge_flow_result res;
    badge_source none;
    badge_peek peek;
    char a[512], b[1024], was[160], now[160];
    uint8_t byte = 0x5A;
    memset(&none, 0, sizeof none);
    memset(&peek, 0, sizeof peek);

    /* the kinds by the code's fields */
    for (int k = 0; k < 3; k++)
        CHECK_EQ(badge_open_kind((int)KINDS[k].code), KINDS[k].kind);
    CHECK_EQ(badge_open_kind(BADGE_CODE_MISSING), BOPEN_MISSING);
    CHECK_EQ(badge_open_kind((int)0xC8804464), BOPEN_MISSING);         /* summary 4, another description */
    CHECK_EQ(badge_open_kind((int)0xC92048E6), BOPEN_OTHER);           /* description 230 of module 18, not FS */
    CHECK_EQ(badge_open_kind((int)0xD90044E6), BOPEN_LOCKED);          /* FS 230 at another level and summary */
    CHECK_EQ(badge_open_kind(-1), BOPEN_OTHER);

    /* the listing opens to read only, once; the fake refuses a write through such an open */
    world(THREE, 3);
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    badge_listing_free(l);
    CHECK_EQ(store.opens, 1);
    CHECK_EQ(store.opens_read, 1);
    CHECK_EQ(store.opens_write, 0);
    CHECK_EQ(store.refused, 0);
    CHECK_EQ(store.writes, 0);
    CHECK_EQ(fs_open(&store, BADGE_OPEN_READ), 0);
    CHECK_EQ(fs_write(&store, BADGE_FILE_MNG, 0, &byte, 1), (int)CODE_READ_ONLY);
    CHECK_EQ(store.refused, 1);
    CHECK_EQ(fs_close(&store), 0);
    CHECK(store_is(&original));

    /* each kind of failed open: the listing's state, its text (title, body and code), the status line; a write
     * stops at its open with the same text under "Nothing was changed" */
    for (int k = 0; k < 3; k++) {
        world(THREE, 3);
        store.fail_open = (int)KINDS[k].code;
        CHECK_EQ(badge_flow_list(&env, l), KINDS[k].state);
        CHECK_EQ(l->code, (int)KINDS[k].code);
        CHECK_EQ(store.opens_read, 1);
        CHECK_EQ(badge_flow_plan(l, &none, &peek), BPLAN_COLLECTION);
        append("Tiere", BADGE_SRC_FOLDER, &res);
        CHECK_EQ(res.code, (int)KINDS[k].code);
        CHECK_EQ(store.opens_write, 1);
        check_refused(&res, BSTAGE_OPEN, BADGE_OK);
        store.fail_open = 0;
        for (int lang = 0; lang < 2; lang++) {
            CHECK_EQ(badge_text_status(lang, l, a, sizeof a), 0);
            CHECK(!strcmp(a, KINDS[k].status[lang]));
            badge_text_plan(lang, BPLAN_COLLECTION, l, &peek, a, sizeof a);
            CHECK(!strcmp(a, KINDS[k].text[lang]));
            CHECK(text_clean(a));
            badge_text_outcome(lang, &res, a, sizeof a, b, sizeof b);
            CHECK(!strcmp(a, badge_text(lang, BT_NOTHING_CHANGED)));
            CHECK(!strcmp(b, KINDS[k].text[lang]));
        }
        badge_listing_free(l);
    }

    /* a file of the wrong size stays its own case with the not-found text, as before 0.9.4 */
    world(THREE, 3);
    put_file(store.dir, STORE_FILE[0], original.data, original.data_size - 16);
    CHECK_EQ(badge_flow_list(&env, l), BLIST_NO_DATA);
    append("Tiere", BADGE_SRC_FOLDER, &res);
    CHECK_EQ(res.stage, BSTAGE_SIZE);
    badge_text_plan(1, BPLAN_COLLECTION, l, &peek, a, sizeof a);
    CHECK(!strncmp(a, "Keine Badge-Daten auf dieser Konsole. ", 38));
    badge_text_outcome(1, &res, a, sizeof a, b, sizeof b);
    CHECK(!strncmp(b, "Keine Badge-Daten auf dieser Konsole. ", 38));
    badge_listing_free(l);

    /* HOME holds the files against an open to read and write only: the listing reads them, and an append or a
     * removal that meets the lock at its open has written nothing: no backup folder, no manifest, no undo record */
    world(THREE, 3);
    store.fail_open_write = (int)CODE_LOCKED;
    CHECK_EQ(badge_flow_list(&env, l), BLIST_OK);
    CHECK_EQ(l->col.sets, 3);
    badge_listing_free(l);
    append("Tiere", BADGE_SRC_FOLDER, &res);
    CHECK_EQ(res.code, (int)CODE_LOCKED);
    check_refused(&res, BSTAGE_OPEN, BADGE_OK);
    CHECK(!exists(card_dir, "badge-backup"));
    CHECK(!exists(card_dir, "badge-manifest.txt"));
    CHECK(!exists(card_dir, "badge-undo"));
    CHECK(strstr(log_text, "nothing was written to HOME's files") != NULL);
    store.fail_open_write = 0;

    /* ... and after an append of this application's: the manifest, the undo record and the backups stay as they are */
    CHECK_EQ(append("Tiere", BADGE_SRC_FOLDER, &res), BFLOW_DONE);
    tree before;
    store_now(&before);
    card_state(was, sizeof was);
    int writes = store.writes;
    store.fail_open_write = (int)CODE_LOCKED;
    for (int removal = 0; removal < 2; removal++) {
        if (removal)
            badge_flow_remove_last(&env, &res);
        else
            append("Blumen.zip", BADGE_SRC_ZIP, &res);
        CHECK_EQ(res.outcome, BFLOW_REFUSED);
        CHECK_EQ(res.stage, BSTAGE_OPEN);
        CHECK_EQ(res.code, (int)CODE_LOCKED);
        CHECK_EQ(res.wrote, 0);
        CHECK_EQ(res.backup[0], 0);
        CHECK_EQ(store.writes, writes);
        CHECK(store_is(&before));
        card_state(now, sizeof now);
        CHECK(!strcmp(now, was));
        badge_text_outcome(1, &res, a, sizeof a, b, sizeof b);
        CHECK(!strcmp(a, "Nichts wurde geändert") && !strcmp(b, KINDS[0].text[1]));
    }
    store.fail_open_write = 0;
    free_tree(&before);

    /* the writes open to read and write, each time: an append, a removal, a failed write with its restore */
    store.opens = store.opens_read = store.opens_write = 0;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_DONE);
    CHECK_EQ(append("Blumen.zip", BADGE_SRC_ZIP, &res), BFLOW_DONE);
    CHECK_EQ(store.opens_write, 4);
    CHECK_EQ(store.opens_read, 0);
    store.fail_write_at = store.writes + 1;
    CHECK_EQ(badge_flow_remove_last(&env, &res), BFLOW_RESTORED);
    store.fail_write_at = 0;
    CHECK_EQ(store.opens_write, 7);             /* the first open, then the restore's two rounds */
    CHECK_EQ(store.opens_read, 0);
    CHECK_EQ(store.refused, 0);

    /* the folder store: a file that is not there answers not found; an open to read only takes no write */
    badge_store ds;
    badge_dir d;
    char *home = join(root, "pretend-modes");
    size_t size;
    badge_dir_store(&ds, &d, home, 0);
    CHECK_EQ(ds.open(ds.ctx, BADGE_OPEN_READ), BADGE_CODE_MISSING);
    CHECK_EQ(ds.open(ds.ctx, BADGE_OPEN_WRITE), BADGE_CODE_MISSING);
    CHECK(!exists(root, "pretend-modes"));
    badge_dir_store(&ds, &d, home, 1);
    CHECK_EQ(ds.open(ds.ctx, BADGE_OPEN_READ), 0);     /* made as an empty collection, open to read only */
    CHECK(ds.write(ds.ctx, BADGE_FILE_MNG, 0, &byte, 1) != 0);
    CHECK_EQ(ds.close(ds.ctx), 0);
    uint8_t *mng = get_file(home, "BadgeMngFile.dat", &size);
    CHECK(mng && size == BADGE_MNG_SIZE && mng[0] == 0);
    free(mng);
    CHECK_EQ(ds.open(ds.ctx, BADGE_OPEN_WRITE), 0);
    CHECK_EQ(ds.write(ds.ctx, BADGE_FILE_MNG, 0, &byte, 1), 0);
    CHECK_EQ(ds.close(ds.ctx), 0);
    mng = get_file(home, "BadgeMngFile.dat", &size);
    CHECK(mng && size == BADGE_MNG_SIZE && mng[0] == byte);
    free(mng);
    free(home);
    free(l);
}
