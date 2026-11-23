#include "badge_text.h"

#include <stdio.h>
#include <string.h>

/* English, German; as text.c: German for a German console, English for every other. */
static const char *const FIXED[BT_COUNT][2] = {
    [BT_INSTALLED_BADGES] = {"Installed badges", "Installierte Badges"},
    [BT_INSTALL_BADGES] = {"Install badges", "Badges installieren"},
    [BT_REMOVE_BADGES] = {"Remove badges", "Badges entfernen"},
    [BT_EMPTY_FOLDER] = {"No badge sets and no folders here", "Hier sind keine Badge-Sets und keine Ordner"},
    [BT_FOLDER_MISSING] = {"The folder /Badges is not on the SD card", "Der Ordner /Badges ist nicht auf der SD-Karte"},
    [BT_NO_SETS] = {"No badge sets in the HOME Menu", "Keine Badge-Sets im HOME-Menü"},
    [BT_ASK_REMOVE] = {"Remove the set \"%s\"?", "Set \"%s\" entfernen?"},
    [BT_ASK_REMOVE_NOTE] = {"Its badges (%u) leave the HOME Menu. The file on the SD card stays.",
                            "Seine Badges (%u) verschwinden aus dem HOME-Menü. Die Datei auf der SD-Karte bleibt."},
    [BT_REMOVED_HEAD] = {"\"%s\" was removed.", "\"%s\" wurde entfernt."},
    [BT_REMOVED_NOTE] = {"The HOME Menu no longer shows its badges. Its file is still on the SD card, so it can be "
                         "installed again here.",
                         "Das HOME-Menü zeigt seine Badges nicht mehr. Seine Datei liegt weiter auf der SD-Karte, es "
                         "lässt sich hier wieder installieren."},
    [BT_PLACE_BADGES] = {"Place Badges", "Marken anbringen"},
    [BT_REMOVE] = {"Remove", "Entfernen"},
    [BT_WORK_READ] = {"Reading the HOME Menu's badges", "Badges des HOME-Menüs werden gelesen"},
    [BT_WORK_BACKUP] = {"Writing the backup", "Sicherung wird geschrieben"},
    [BT_WORK_BACKUP_CHECK] = {"Checking the backup", "Sicherung wird geprüft"},
    [BT_WORK_CHANGE] = {"Preparing", "Wird vorbereitet"},
    [BT_WORK_WRITE] = {"Writing", "Wird geschrieben"},
    [BT_WORK_VERIFY] = {"Reading back", "Wird nachgelesen"},
    [BT_WORK_RESTORE] = {"Writing the backup back", "Sicherung wird zurückgeschrieben"},
    [BT_STEP_READ] = {"Read badges", "Badges lesen"},
    [BT_STEP_BACKUP] = {"Backup", "Sicherung"},
    [BT_STEP_WRITE] = {"Write", "Schreiben"},
    [BT_STEP_VERIFY] = {"Verify", "Prüfen"},
    [BT_ONLY_LAST] = {"Only the set added last can be removed.",
                      "Nur das zuletzt hinzugefügte Set kann entfernt werden."},
    [BT_NOT_OURS] = {"This set was not installed with this application.",
                     "Dieses Set wurde nicht mit dieser Anwendung installiert."},
    [BT_PLACED] = {"Badges of this set are placed in the HOME Menu. Put them back first.",
                   "Badges dieses Sets liegen im HOME-Menü. Erst zurücklegen."},
    [BT_NO_UNDO] = {"The set's undo record is missing on the SD card.",
                    "Die Rücknahme-Datei des Sets fehlt auf der SD-Karte."},
    [BT_NOT_A_SET] = {"The file is not a badge set: a zip of PNG files or one PNG file.",
                      "Die Datei ist kein Badge-Set: eine ZIP-Datei mit PNG-Dateien oder eine PNG-Datei."},
    [BT_NOTHING_CHANGED] = {"Nothing was changed", "Nichts wurde geändert"},
    [BT_PUT_BACK] = {"Not written, the backup is back", "Nicht geschrieben, Sicherung ist zurück"},
    [BT_WRITE_FAILED] = {"The write failed", "Schreiben fehlgeschlagen"},
};

const char *badge_text(int lang, int id)
{
    return id >= 0 && id < BT_COUNT && FIXED[id][lang == 1] ? FIXED[id][lang == 1] : "?";
}

void badge_text_count(int lang, unsigned badges, char *out, size_t size)
{
    (void)lang;
    snprintf(out, size, badges == 1 ? "%u Badge" : "%u Badges", badges);
}

static const char *const NO_DATA[2] = {"No badge data on this console", "Keine Badge-Daten auf dieser Konsole"};
static const char *const UNREADABLE[2] = {"The badge data cannot be read", "Die Badge-Daten sind nicht lesbar"};
static const char *const LOCKED[2] = {"The badge data is locked", "Die Badge-Daten sind gesperrt"};
static const char *const NOT_OPENED[2] = {"The badge data could not be opened",
                                          "Die Badge-Daten konnten nicht geöffnet werden"};

/* Why the store did not open, by the kind of its code: the not-found text also for a file of the wrong size. */
static void not_open(int de, int kind, unsigned code, char *out, size_t size)
{
    if (kind == BOPEN_LOCKED)
        snprintf(out, size, de ? "%s. Das HOME-Menü hält die Badge-Daten gerade offen. HOME drücken und zur "
                                 "Anwendung zurückkehren, oder 3DStockTheme als installierten Titel aus dem "
                                 "HOME-Menü starten. (Code %08X)"
                               : "%s. The HOME Menu is holding the badge data open. Press HOME and return to the "
                                 "application, or start 3DStockTheme as an installed title from the HOME Menu. "
                                 "(Code %08X)",
                 LOCKED[de], code);
    else if (kind == BOPEN_MISSING)
        snprintf(out, size, de ? "%s. Das HOME-Menü legt sie an, sobald dort Badges benutzt wurden. (Code %08X)"
                               : "%s. The HOME Menu makes it once badges were used there. (Code %08X)",
                 NO_DATA[de], code);
    else
        snprintf(out, size, "%s. (Code %08X)", NOT_OPENED[de], code);
}

int badge_text_status(int lang, const badge_listing *l, char *out, size_t size)
{
    int de = lang == 1;
    if (l->state == BLIST_OK) {
        char count[32];
        badge_text_count(lang, (unsigned)l->col.badges, count, sizeof count);
        snprintf(out, size, l->col.sets == 1 ? "%u Set, %s" : "%u Sets, %s", (unsigned)l->col.sets, count);
        return 1;
    }
    const char *why = l->state == BLIST_NO_DATA ? NO_DATA[de] : l->state == BLIST_LOCKED ? LOCKED[de] : UNREADABLE[de];
    snprintf(out, size, "%s", why);
    return 0;
}

const char *badge_text_result(int lang, int badge)
{
    int de = lang == 1;
    switch (badge) {
    case BADGE_E_NOMEM:
        return de ? "Nicht genug Speicher." : "Not enough memory.";
    case BADGE_E_PNG:
        return de ? "Eine PNG-Datei des Sets ist beschädigt." : "A PNG file of the set is damaged.";
    case BADGE_E_PNG_KIND:
        return de ? "Eine PNG-Datei des Sets hat ein Format, das nicht gelesen wird (16 Bit pro Kanal oder "
                    "Interlacing)."
                  : "A PNG file of the set is of a kind that is not read (16 bits per channel or interlaced).";
    case BADGE_E_IMAGE_SIZE:
        return de ? "Die Seiten eines Bildes sind kein Vielfaches von 64 Pixeln (Set-Symbol: 48 x 48)."
                  : "A picture's sides are not multiples of 64 pixels (the set icon: 48 x 48).";
    case BADGE_E_FOLDER:
        return de ? "Kein Badge-Set: Ein Set besteht nur aus PNG-Dateien, ohne Unterordner."
                  : "Not a badge set: a set is PNG files only, without subfolders.";
    case BADGE_E_NAME:
        return de ? "Ein Name ist leer, länger als 68 Zeichen oder enthält Zeichen, die nicht übernommen werden."
                  : "A name is empty, longer than 68 characters or has characters that are not taken.";
    case BADGE_E_MANIFEST:
        return de ? "Die Liste der mit dieser Anwendung hinzugefügten Sets (badge-manifest.txt) ist beschädigt."
                  : "The list of the sets this application added (badge-manifest.txt) is damaged.";
    case BADGE_E_IDS:
        return de ? "Für neue Sets sind keine IDs mehr frei." : "No IDs are left for new sets.";
    case BADGE_E_PRESENT:
        return de ? "Ein Set mit diesem Namen ist schon im HOME-Menü."
                  : "A set of this name is in the HOME Menu already.";
    case BADGE_E_FULL:
        return de ? "Das HOME-Menü fasst 100 Sets und 1000 Badges. Dieses Set passt nicht mehr hinein."
                  : "The HOME Menu takes 100 sets and 1000 badges. This set does not fit any more.";
    case BADGE_E_REFUSED:
        return de ? "Das Ergebnis hat die Kontrolle vor dem Schreiben nicht bestanden."
                  : "The result did not pass the check before writing.";
    case BADGE_E_EMPTY:
        return de ? "Im HOME-Menü ist kein Set." : "There is no set in the HOME Menu.";
    case BADGE_E_NOT_OURS:
        return badge_text(lang, BT_NOT_OURS);
    case BADGE_E_PLACED:
        return badge_text(lang, BT_PLACED);
    case BADGE_E_NO_UNDO:
        return badge_text(lang, BT_NO_UNDO);
    case BADGE_E_LAST:
        return de ? "Die Badges dieses Sets stehen nicht am Ende der Badge-Daten des HOME-Menüs. Nur das zuletzt "
                    "hinzugefügte Set kann entfernt werden."
                  : "This set's badges are not at the end of the HOME Menu's badge data. Only the set added last can be "
                    "removed.";
    case BADGE_E_SET_CHANGED:   /* a byte the append wrote changed since (HOME's own fields are not compared) */
    case BADGE_E_CHANGED:       /* only without allow_changes, which the application always passes */
        return de ? "Das HOME-Menü hat die Badge-Daten geändert, seit dieses Set installiert wurde, deshalb kann die "
                    "Anwendung das Set so nicht zurücknehmen. Die Badge-Daten des HOME-Menüs sind wie vorher."
                  : "The HOME Menu has changed the badge data since this set was installed, so the application cannot "
                    "take the set back this way. The HOME Menu's badge data is as it was.";
    case BADGE_E_AMBIGUOUS:
        return de ? "Mehrere Rücknahme-Dateien auf der SD-Karte passen zu diesem Set, deshalb kann die Anwendung nicht "
                    "sagen, welche stimmt."
                  : "Several undo records on the SD card fit this set, so the application cannot tell which one is "
                    "right.";
    case BADGE_E_UNDO:
        return de ? "Die Rücknahme-Datei des Sets auf der SD-Karte lässt sich nicht lesen oder ist beschädigt."
                  : "The set's undo record on the SD card cannot be read or is damaged.";
    case BADGE_E_ZIP:
        return de ? "Die ZIP-Datei ist beschädigt oder hat ein Format, das nicht gelesen wird."
                  : "The zip file is damaged or of a kind that is not read.";
    default:        /* BADGE_E_SIZE, _FORMAT, _INDEX, _TAKEN, _SLOTS: the collection itself */
        return de ? "Die Badge-Daten des HOME-Menüs sind nicht so aufgebaut, dass ein Set sicher hinzugefügt oder "
                    "entfernt werden kann."
                  : "The HOME Menu's badge data is not laid out the way a set can be added or removed safely.";
    }
}

void badge_text_plan(int lang, int plan, const badge_listing *l, const badge_peek *peek, char *out, size_t size)
{
    int de = lang == 1;
    switch (plan) {
    case BPLAN_SET:
        snprintf(out, size, "%s", badge_text_result(lang, peek->result));
        break;
    case BPLAN_COLLECTION:
        if (l->state == BLIST_NO_DATA || l->state == BLIST_LOCKED || l->state == BLIST_OPEN)
            not_open(de, l->state == BLIST_LOCKED ? BOPEN_LOCKED : l->state == BLIST_OPEN ? BOPEN_OTHER : BOPEN_MISSING,
                     (unsigned)l->code, out, size);
        else if (l->state == BLIST_READ)
            snprintf(out, size, "%s. (Code %08X)", UNREADABLE[de], (unsigned)l->code);
        else
            snprintf(out, size, "%s", badge_text_result(lang, l->state == BLIST_NOMEM ? BADGE_E_NOMEM : l->badge));
        break;
    case BPLAN_MANIFEST:
        snprintf(out, size, "%s", badge_text_result(lang, BADGE_E_MANIFEST));
        break;
    case BPLAN_PRESENT:
        snprintf(out, size, "%s", badge_text_result(lang, BADGE_E_PRESENT));
        break;
    case BPLAN_FULL:
        snprintf(out, size, "%s", badge_text_result(lang, BADGE_E_FULL));
        break;
    default:
        out[0] = 0;
    }
}

/* A path as the screens show it: without the device ("sdmc:/theme-pack/..." shows as "/theme-pack/..."). */
static const char *shown(const char *path)
{
    const char *colon = strchr(path, ':');
    return colon && colon[1] == '/' ? colon + 1 : path;
}

void badge_text_outcome(int lang, const badge_flow_result *res, char *title, size_t title_size, char *body,
                        size_t body_size)
{
    int de = lang == 1;
    if (res->outcome == BFLOW_RESTORED) {
        snprintf(title, title_size, "%s", badge_text(lang, BT_PUT_BACK));
        snprintf(body, body_size,
                 de ? "Beim Schreiben der Badge-Daten ist ein Fehler aufgetreten (Schritt %d, Code %08X). Die "
                      "Sicherung wurde zurückgeschrieben und nachgelesen: Die Badge-Daten des HOME-Menüs sind wie "
                      "vorher.\n\nSicherung: %s"
                    : "Writing the badge data failed (step %d, code %08X). The backup was written back and read "
                      "back: the HOME Menu's badge data is as it was.\n\nBackup: %s",
                 res->stage, (unsigned)res->code, shown(res->backup));
        return;
    }
    if (res->outcome == BFLOW_UNKNOWN) {
        snprintf(title, title_size, "%s", badge_text(lang, BT_WRITE_FAILED));
        snprintf(body, body_size,
                 de ? "Beim Schreiben der Badge-Daten ist ein Fehler aufgetreten (Schritt %d, Code %08X), und die "
                      "Sicherung konnte nicht bestätigt zurückgeschrieben werden. Die Badges im HOME-Menü vorerst "
                      "nicht verändern.\n\nSicherung: %s\nProtokoll: /theme-pack/install.log"
                    : "Writing the badge data failed (step %d, code %08X), and the backup could not be confirmed "
                      "to be back. Do not change the badges in the HOME Menu for now.\n\nBackup: %s\nLog: "
                      "/theme-pack/install.log",
                 res->stage, (unsigned)res->code, shown(res->backup));
        return;
    }
    snprintf(title, title_size, "%s", badge_text(lang, BT_NOTHING_CHANGED));
    switch (res->stage) {
    case BSTAGE_OPEN:
    case BSTAGE_SIZE:
        not_open(de, res->stage == BSTAGE_OPEN ? badge_open_kind(res->code) : BOPEN_MISSING, (unsigned)res->code, body,
                 body_size);
        break;
    case BSTAGE_READ:
        snprintf(body, body_size, "%s. (Code %08X)", UNREADABLE[de], (unsigned)res->code);
        break;
    case BSTAGE_MEMORY:
        snprintf(body, body_size, "%s", badge_text_result(lang, BADGE_E_NOMEM));
        break;
    case BSTAGE_BACKUP:
        snprintf(body, body_size, de ? "Die Sicherung der Badge-Daten konnte nicht auf die SD-Karte geschrieben "
                                       "werden. Sie braucht 17 MB freien Platz."
                                     : "The backup of the badge data could not be written to the SD card. It needs "
                                       "17 MB of free space.");
        break;
    case BSTAGE_BACKUP_CHECK:
        snprintf(body, body_size, de ? "Die Sicherung auf der SD-Karte liest sich anders zurück, als sie geschrieben "
                                       "wurde. Die SD-Karte prüfen."
                                     : "The backup on the SD card reads back differently from what was written. Check "
                                       "the SD card.");
        break;
    case BSTAGE_RECORD:
        snprintf(body, body_size, de ? "Die Rücknahme-Datei oder die Liste der Sets konnte nicht auf die SD-Karte "
                                       "geschrieben werden."
                                     : "The undo record or the list of sets could not be written to the SD card.");
        break;
    case BSTAGE_BUSY:
        snprintf(body, body_size, de ? "Es wird gerade geschrieben." : "A write is under way.");
        break;
    default:        /* the collection, the set, the manifest, the undo record, the core */
        snprintf(body, body_size, "%s", badge_text_result(lang, res->badge));
    }
}
