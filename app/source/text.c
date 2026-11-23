#include "text.h"

#include "download.h"
#include "wrap.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CARD_DIR "sdmc:/theme-pack"
#define PACK_CIA "sdmc:/theme-pack/theme-pack.cia"
#define LOG_FILE "sdmc:/theme-pack/install.log"

static const char *const FIXED[T_COUNT][2] = {
    [T_DESIGNS] = {"Themes", "Designs"},
    [T_BADGES] = {"Badges", "Badges"},
    [T_FROM_SD] = {"Install from SD", "Von SD installieren"},
    [T_SCAN_QR] = {"Scan QR code", "QR-Code scannen"},
    [T_REMOVE_SET] = {"Remove set", "Set entfernen"},
    [T_EXIT] = {"Exit", "Beenden"},
    [T_BACK] = {"Back", "Zurück"},
    [T_OK] = {"OK", "OK"},
    [T_SELECT] = {"Select", "Auswählen"},
    [T_OPEN] = {"Open", "Öffnen"},
    [T_INSTALL] = {"Install", "Installieren"},
    [T_LOAD] = {"Download", "Laden"},
    [T_CANCEL] = {"Cancel", "Abbrechen"},
    [T_READING_PACK] = {"Reading installed themes ...", "Installierte Designs werden gelesen ..."},
    [T_READING_FOLDER] = {"Reading the folder ...", "Ordner wird gelesen ..."},
    [T_CHECKING_THEME] = {"Checking the theme ...", "Design wird geprüft ..."},
    [T_SD_CARD] = {"SD card", "SD-Karte"},
    [T_EMPTY_FOLDER] = {"No themes and no folders here", "Hier sind keine Designs und keine Ordner"},
    [T_OTHER_FILES] = {"Other files, no themes: %s", "Weitere Dateien, keine Designs: %s"},
    [T_INSTALLED_ALREADY] = {"Already installed", "Schon installiert"},
    [T_INSTALLED_AS] = {"Already installed as theme %u", "Schon installiert als Design %u"},
    [T_UNREADABLE] = {"Cannot be read", "Nicht lesbar"},
    [T_MUSIC] = {"Music", "Musik"},
    [T_WARN_BGM_UNUSED] = {"The zip has music, but the theme does not switch it on, so the HOME Menu plays none.",
                           "Die Zip enthält Musik, aber das Design schaltet sie nicht ein: das HOME-Menü spielt "
                           "keine ab."},
    [T_WARN_BGM_MISSING] = {"The theme switches music on, but the zip has no music file, so the HOME Menu plays "
                            "none.",
                            "Das Design schaltet Musik ein, aber die Zip enthält keine Musikdatei: das HOME-Menü "
                            "spielt keine ab."},
    [T_CANNOT_INSTALL] = {"Cannot be installed", "Kann nicht installiert werden"},
    [T_INSTALL_THEME] = {"Install theme", "Design installieren"},
    [T_INSTALLING] = {"Installing", "Wird installiert"},
    [T_STEP_CHECK] = {"Check", "Prüfen"},
    [T_STEP_CATALOGUE] = {"Catalogue", "Katalog"},
    [T_STEP_THEME] = {"Theme", "Design"},
    [T_STEP_FINISH] = {"Finish", "Abschluss"},
    [T_DONT_SWITCH_OFF] = {"Do not switch off", "Nicht ausschalten"},
    [T_DONT_REMOVE_SD] = {"Do not remove the SD card", "SD-Karte nicht entfernen"},
    [T_INSTALLED] = {"Installed", "Installiert"},
    [T_HOME_MENU] = {"HOME Menu", "HOME-Menü"},
    [T_CHANGE_THEME] = {"Change Theme", "Design ändern"},
    [T_DELETE_ASK] = {"Delete the file?", "Datei löschen?"},
    [T_KEEP] = {"Keep", "Behalten"},
    [T_DELETE] = {"Delete", "Löschen"},
    [T_DELETED] = {"%s was deleted.", "%s wurde gelöscht."},
    [T_DELETE_FAILED] = {"%s could not be deleted.", "%s konnte nicht gelöscht werden."},
    [T_HELP_FILE] = {"The file is on the SD card in the folder %s. The theme is installed in the HOME Menu now and no "
                     "longer needs it.",
                     "Die Datei liegt auf der SD-Karte im Ordner %s. Das Design ist jetzt im HOME-Menü installiert "
                     "und braucht sie nicht mehr."},
    [T_HELP_KEEP] = {"Keep: the file stays, to install the theme again later.",
                     "Behalten: Die Datei bleibt, um das Design später erneut zu installieren."},
    [T_HELP_DELETE] = {"Delete: only the file is removed. The theme stays.",
                       "Löschen: Nur die Datei wird entfernt. Das Design bleibt."},
    [T_OKAY] = {"OK", "Okay"},
    [T_THEMES_INSTALLED] = {"%u themes installed", "%u Designs installiert"},
    [T_THEME_INSTALLED_1] = {"1 theme installed", "1 Design installiert"},
    [T_NO_THEMES_INSTALLED] = {"No themes installed", "Kein Design installiert"},
    [T_SCAN_HINT] = {"Hold the QR code in front of the camera", "QR-Code vor die Kamera halten"},
    [T_CAMERA_FAILED] = {"The camera could not be started.", "Die Kamera konnte nicht gestartet werden."},
    [T_QR_FOUND] = {"QR code found", "QR-Code erkannt"},
    [T_DOWNLOAD] = {"Download", "Herunterladen"},
    [T_DOWNLOADING] = {"Downloading", "Wird heruntergeladen"},
    [T_DOWNLOAD_FAILED] = {"Download failed", "Herunterladen fehlgeschlagen"},
    [T_NO_WIFI] = {"No wireless connection", "Kein WLAN"},
    [T_NO_WIFI_HINT] = {"Switch the wireless on and scan again. Nothing was downloaded.",
                        "WLAN einschalten und erneut scannen. Es wurde nichts geladen."},
    [T_INSTALL_NO_MUSIC] = {"Install without music", "Ohne Musik installieren"},
    [T_LEAVING_OUT_MUSIC] = {"Leaving out the music ...", "Musik wird weggelassen ..."},
    [T_DELETE_THEME] = {"Delete theme", "Design löschen"},
    [T_DELETE_FILE] = {"Delete file", "Datei löschen"},
    [T_ASK_DELETE_THEME] = {"Delete \"%s\" from the HOME Menu?", "\"%s\" aus dem HOME-Menü löschen?"},
    [T_ASK_DELETE_THEME_NOTE] = {"The file stays on the SD card, so the theme can be installed again. A saved layout "
                                 "with this theme loads without it afterwards.",
                                 "Die Datei bleibt auf der SD-Karte, das Design lässt sich also wieder installieren. "
                                 "Ein gespeichertes Layout mit diesem Design lädt danach ohne Design."},
    [T_ASK_DELETE_FILE_WHERE] = {"On the SD card, in the folder %s:", "Auf der SD-Karte im Ordner %s:"},
    [T_ASK_DELETE_FILE] = {"Delete \"%s\"?", "\"%s\" löschen?"},
    [T_ASK_DELETE_FILE_NOTE] = {"The theme stays installed in the HOME Menu.",
                                "Das Design bleibt im HOME-Menü installiert."},
    [T_INSTALLED_DESIGNS] = {"Installed themes", "Installierte Designs"},
    [T_DESIGN_IN_HOME] = {"Theme %u in the HOME Menu", "Design %u im HOME-Menü"},
    [T_ROOM] = {"Room for only %u more", "Nur noch Platz für %u"},
    [T_ASK_DELETE_THEME_NO_FILE] = {"There is no file of it in /Themes: it comes back only from a file elsewhere or a "
                                    "new download. A saved layout with this theme loads without it afterwards.",
                                    "In /Themes liegt keine Datei davon: zurück kommt es nur aus einer Datei an "
                                    "anderer Stelle oder einem neuen Download. Ein gespeichertes Layout mit diesem "
                                    "Design lädt danach ohne Design."},
    [T_DELETING_THEME] = {"Deleting the theme ...", "Design wird gelöscht ..."},
    [T_DELETING_FILE] = {"Deleting the file ...", "Datei wird gelöscht ..."},
    /* ---- flow (0.9.2) */
    [T_ASK_THEMES] = {"Create the folder /Themes on the SD card?", "Den Ordner /Themes auf der SD-Karte erstellen?"},
    [T_ASK_THEMES_NOTE] = {"Theme files are looked for there first, and downloads are saved there.",
                           "Dort werden Design-Dateien zuerst gesucht und Downloads gespeichert."},
    [T_CREATE] = {"Create", "Erstellen"},
    [T_THEMES_NOT_CREATED] = {"The folder /Themes could not be created.",
                              "Der Ordner /Themes konnte nicht erstellt werden."},
    [T_CREATING_FOLDER] = {"Creating the folder ...", "Ordner wird erstellt ..."},
    [T_PREVIEW] = {"Preview", "Vorschau"},
    [T_PREVIEW_LOADING] = {"Loading the preview ...", "Vorschau wird geladen ..."},
    [T_PREVIEW_FAILED] = {"The preview cannot be shown.", "Die Vorschau kann nicht gezeigt werden."},
    [T_PREVIEW_NO_WALL] = {"No wallpaper of its own", "Kein eigenes Hintergrundbild"},
    [T_SETTINGS] = {"Settings", "Einstellungen"},
    [T_MENU_MUSIC] = {"Background music", "Hintergrundmusik"},
    [T_NO_MUSIC] = {"No music", "Keine Musik"},
    [T_SOUNDS] = {"Sounds", "Töne"},
    [T_ON] = {"On", "An"},
    [T_OFF] = {"Off", "Aus"},
    [T_GLASS] = {"Glass", "Glas"},
    [T_GLASS_CLEAR] = {"Clear", "Clear"},       /* the three styles keep their names in both languages */
    [T_GLASS_SHARD] = {"Shard", "Shard"},
    [T_GLASS_PLATIN] = {"Platin", "Platin"},
    [T_PATTERN] = {"Background", "Hintergrund"},
    [T_PATTERN_PLUS] = {"Plus signs and rings", "Plus und Ringe"},
    [T_PATTERN_HOUSES] = {"Little houses", "Häuschen"},
    [T_PATTERN_BUBBLES] = {"Bubbles", "Blasen"},
    [T_SCHEME] = {"Colour", "Farbe"},
    [T_SCHEME_WHITE] = {"White", "Weiss"},
    [T_SCHEME_YELLOW] = {"Yellow", "Gelb"},
    [T_SCHEME_SAND] = {"Sand", "Sand"},
    [T_SCHEME_MINT] = {"Mint", "Minze"},
    [T_SCHEME_SKY] = {"Sky", "Himmel"},
    [T_SCHEME_LILAC] = {"Lilac", "Flieder"},
    [T_SCHEME_ROSE] = {"Rose", "Rose"},
    [T_SCHEME_NIGHT] = {"Night", "Nacht"},
    [T_SCHEME_PEARL] = {"Pearl", "Perle"},
    [T_LOOK] = {"Style", "Stil"},
    [T_LOOK_STOCK] = {"Stock", "Stock"},       /* the four looks keep their names in both languages */
    [T_LOOK_FROST] = {"Frost", "Frost"},
    [T_LOOK_NIGHT] = {"Nachtglas", "Nachtglas"},
    [T_LOOK_PRISM] = {"Prisma", "Prisma"},
    [T_READING_BADGES] = {"Reading badges", "Badges werden gelesen"},
    [T_CHECKING_SET] = {"Checking the set", "Set wird geprüft"},
    [T_INSTALL_SUCCESS] = {"Installed successfully", "Erfolgreich installiert"},
    [T_TRY_AGAIN] = {"Go back and try again. If it happens again, restart the console.",
                     "Gehe zurück und versuche es erneut. Kommt es wieder, starte die Konsole neu."},
    [T_SOUNDS_CREDIT] = {"Interface sounds by Kenney (kenney.nl), CC0", "Bedientöne von Kenney (kenney.nl), CC0"},
};

const char *text(int lang, int id)
{
    return id >= 0 && id < T_COUNT && FIXED[id][lang == LANG_DE] ? FIXED[id][lang == LANG_DE] : "?";
}

void text_installed(int lang, unsigned n, char *out, size_t size)
{
    if (n < 2)
        snprintf(out, size, "%s", text(lang, n ? T_THEME_INSTALLED_1 : T_NO_THEMES_INSTALLED));
    else
        snprintf(out, size, text(lang, T_THEMES_INSTALLED), n);
}

void text_version(char *out, size_t size, int major, int minor, int micro, const char *pre)
{
    snprintf(out, size, "%d.%d.%d%s%s", major, minor, micro, pre[0] ? "-" : "", pre);
}

void text_track_credit(int lang, const char *title, const char *author, const char *licence, char *out, size_t size)
{
    int n = (int)strcspn(licence, ",");
    if (lang == LANG_DE)
        snprintf(out, size, "Musik: \"%s\" von %s, %.*s", title, author, n, licence);
    else
        snprintf(out, size, "Music: \"%s\" by %s, %.*s", title, author, n, licence);
}

static const char *const RESULTS[][2] = {
    [TP_OK] = {"No error.", "Kein Fehler."},
    [TP_E_ARG] = {"Internal error (argument).", "Interner Fehler (Argument)."},
    [TP_E_NOMEM] = {"Not enough memory.", "Nicht genug Speicher."},
    [TP_E_READ] = {"The SD card could not be read.", "Die SD-Karte konnte nicht gelesen werden."},
    [TP_E_WRITE] = {"Writing failed.", "Schreiben fehlgeschlagen."},
    [TP_E_ZIP] = {"Not a zip this application reads, or damaged.", "Keine lesbare Zip, oder beschädigt."},
    [TP_E_MISSING] = {"body_LZ.bin or info.smdh is missing.", "body_LZ.bin oder info.smdh fehlt."},
    [TP_E_TOO_BIG] = {"Too large for the HOME Menu (theme or music).",
                      "Zu gross für das HOME-Menü (Design oder Musik)."},
    [TP_E_LZ11] = {"body_LZ.bin is damaged.", "body_LZ.bin ist beschädigt."},
    [TP_E_SMDH] = {"info.smdh is damaged.", "info.smdh ist beschädigt."},
    [TP_E_INDEX] = {"No theme number is left.", "Keine Design-Nummer ist mehr frei."},
    [TP_E_TMD] = {"The TMD is damaged.", "Die TMD ist beschädigt."},
    [TP_E_GONE] = {"The record lists a theme the new catalogue lacks.",
                   "Die Aufzeichnung führt ein Design, das im neuen Katalog fehlt."},
    [TP_E_NOTHING] = {"Nothing to add.", "Nichts hinzuzufügen."},
    [TP_E_VERSION] = {"The pack's version is at its maximum.", "Die Version des Pakets ist am Maximum."},
    [TP_E_ID] = {"The new catalogue's content ID is taken.", "Die Inhalts-ID des neuen Katalogs ist vergeben."},
    [TP_E_CHANGED] = {"The zip changed while it was read.", "Die Zip hat sich beim Lesen verändert."},
    [TP_E_TITLE] = {"Not the theme pack's title (0004008C00009809).",
                    "Nicht der Titel des Design-Pakets (0004008C00009809)."},
    [TP_E_CATALOGUE] = {"The pack's catalogue is damaged or lacks a theme.",
                        "Der Katalog des Pakets ist beschädigt oder ihm fehlt ein Design."},
    [TP_E_STALE] = {"The record does not match AM.", "Die Aufzeichnung passt nicht zu AM."},
    [TP_E_CIA] = {"Not a theme CIA this application reads, or damaged.",
                  "Keine Design-CIA, die diese Anwendung lesen kann, oder beschädigt."},
    [TP_E_ENCRYPTED] = {"The CIA is encrypted; only unencrypted theme CIAs can be read.",
                        "Die CIA ist verschlüsselt; nur unverschlüsselte Design-CIAs sind lesbar."},
    [TP_E_KIND] = {"Neither a theme zip nor a theme CIA.", "Weder eine Design-Zip noch eine Design-CIA."},
};

const char *text_result(int lang, int result)
{
    if (result < 0 || result > TP_E_KIND)
        return lang == LANG_DE ? "Unbekannter Fehler." : "Unknown error.";
    return RESULTS[result][lang == LANG_DE];
}

/* Appends to a buffer, never past its end. */
static void add(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void add(char *buf, size_t size, const char *fmt, ...)
{
    size_t n = strlen(buf);
    if (n + 1 >= size)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, size - n, fmt, ap);
    va_end(ap);
}

static void set(char *buf, size_t size, const char *s)
{
    snprintf(buf, size, "%s", s);
}

#define L(en, de) (de_ ? (de) : (en))

static void difference(int de_, const tp_difference *d, uint16_t installed, char *buf, size_t size)
{
    switch (d->what) {
    case TP_DIFF_VERSION:
        add(buf, size, L("another title version is installed (%u)", "eine andere Titelversion ist installiert (%u)"),
            installed);
        break;
    case TP_DIFF_MISSING:
        add(buf, size, L("content %u is recorded, AM does not list it", "Inhalt %u ist aufgezeichnet, fehlt aber bei AM"),
            d->index);
        break;
    case TP_DIFF_EXTRA:
        add(buf, size, L("AM lists content %u, the record does not", "AM meldet Inhalt %u, die Aufzeichnung nicht"),
            d->index);
        break;
    case TP_DIFF_ID:
        add(buf, size, L("content %u has another ID", "Inhalt %u hat eine andere ID"), d->index);
        break;
    default:
        add(buf, size, L("content %u has another size", "Inhalt %u hat eine andere Grösse"), d->index);
        break;
    }
}

/* The way back after themes were lost: English after a colon or comma, German as its own clause. */
static const char *recover(int de_)
{
    return L("reinstall the pack CIA with FBI (" PACK_CIA "); themes added after that CIA was built have to be "
             "added again.",
             "Installiere die CIA des Pakets mit FBI neu (" PACK_CIA "); Designs, die nach dem Bau dieser CIA "
             "dazukamen, müssen danach erneut hinzugefügt werden.");
}

static const char *pending_fbi(int de_)
{
    return L("Remove it in FBI: Pending Titles, 0004008C00009809, Delete Pending Title (never Delete All Pending "
             "Titles).",
             "Entferne ihn in FBI: Pending Titles, 0004008C00009809, Delete Pending Title (nie Delete All Pending "
             "Titles).");
}

void text_pack(int lang, const pack_view *pv, char *title, size_t title_size, char *body, size_t body_size)
{
    int de_ = lang == LANG_DE;
    title[0] = body[0] = 0;
    switch (pv->state) {
    case PACK_OK:
        set(title, title_size, text(lang, T_INSTALL_THEME));
        text_installed(lang, (unsigned)pv->usable, body, body_size);
        break;
    case PACK_AM_ERROR:
        set(title, title_size, L("Cannot read the theme pack", "Design-Paket nicht lesbar"));
        add(body, body_size, L("AM answered with error %08X. Restart the console and try again; if it stays, "
                               "report the code. Nothing was changed.",
                               "AM hat mit Fehler %08X geantwortet. Starte die Konsole neu und versuche es erneut; "
                               "bleibt er, melde den Code. Es wurde nichts verändert."),
            (unsigned)pv->error);
        break;
    case PACK_NOT_INSTALLED:
        set(title, title_size, L("No theme pack installed", "Kein Design-Paket installiert"));
        add(body, body_size, L("This version adds themes to an installed theme pack (title 0004008C00009809); it does "
                               "not install a new pack. Install the pack CIA with FBI first.",
                               "Diese Version fügt Designs zu einem installierten Design-Paket (Titel "
                               "0004008C00009809) hinzu; ein neues Paket legt sie nicht an. Installiere zuerst die "
                               "CIA des Pakets mit FBI."));
        break;
    case PACK_PENDING:
        set(title, title_size, L("An import is pending", "Ein Import ist offen"));
        add(body, body_size, L("An earlier import of the theme pack did not finish. %s Then start again.",
                               "Ein früherer Import des Design-Pakets wurde nicht abgeschlossen. %s Dann starte neu."),
            pending_fbi(de_));
        break;
    case PACK_PENDING_UNKNOWN:
        set(title, title_size, L("Cannot tell whether an import is pending", "Unklar, ob ein Import offen ist"));
        add(body, body_size, L("AM did not say whether an import of the theme pack is pending. Restart the console "
                               "and try again.",
                               "AM hat nicht gesagt, ob ein Import des Design-Pakets offen ist. Starte die Konsole "
                               "neu und versuche es erneut."));
        break;
    case PACK_NO_STORE:
        set(title, title_size, L("No record of the installed pack", "Keine Aufzeichnung des installierten Pakets"));
        add(body, body_size, L(CARD_DIR "/installed.tmd is missing. It holds the TMD of the installed pack, which "
                               "AM does not hand out; this application does not guess it. Copy it there from the Mac "
                               "(README, The record). If this application installed themes since the Mac recorded "
                               "the pack, the Mac does not have it: reinstall the pack CIA with FBI, then copy that "
                               "CIA's TMD.",
                               CARD_DIR "/installed.tmd fehlt. Die Datei enthält die TMD des installierten Pakets, die "
                               "AM nicht herausgibt; diese Anwendung rät sie nicht. Kopiere sie vom Mac dorthin "
                               "(README, The record). Hat diese Anwendung seit der Aufzeichnung auf dem Mac Designs "
                               "installiert, hat der Mac sie nicht: installiere die CIA des Pakets mit FBI neu und "
                               "kopiere dann deren TMD."));
        break;
    case PACK_STORE_BAD:
        set(title, title_size, L("The record is damaged", "Die Aufzeichnung ist beschädigt"));
        add(body, body_size, L(CARD_DIR "/installed.tmd is not a TMD of the theme pack: %s Copy it again from the "
                               "Mac.",
                               CARD_DIR "/installed.tmd ist keine TMD des Design-Pakets: %s Kopiere sie erneut vom "
                               "Mac."),
            text_result(lang, pv->detail));
        break;
    case PACK_STALE:
        set(title, title_size, L("The record is out of date", "Die Aufzeichnung ist veraltet"));
        add(body, body_size, L(CARD_DIR "/installed.tmd does not match what AM reports: ",
                               CARD_DIR "/installed.tmd passt nicht zu dem, was AM meldet: "));
        difference(de_, &pv->diff, pv->version, body, body_size);
        add(body, body_size, L(". Something other than this application changed the pack, a reinstall with FBI "
                               "for example: copy the TMD of the CIA that was installed from the Mac (README, The "
                               "record). If this application changed the pack last, its record is lost: reinstall "
                               "the pack CIA with FBI, then copy that CIA's TMD.",
                               ". Etwas anderes als diese Anwendung hat das Paket verändert, zum Beispiel eine "
                               "Neuinstallation mit FBI: kopiere die TMD der installierten CIA vom Mac (README, The "
                               "record). Hat zuletzt diese Anwendung das Paket verändert, ist ihre Aufzeichnung "
                               "verloren: installiere die CIA des Pakets mit FBI neu und kopiere dann deren TMD."));
        break;
    case PACK_NO_CATALOGUE:
        set(title, title_size, L("The pack's catalogue is missing", "Der Katalog des Pakets fehlt"));
        add(body, body_size, L("Content 0 of the theme pack is not installed, so the HOME Menu cannot list its "
                               "themes. Reinstall the pack CIA with FBI (" PACK_CIA ").",
                               "Inhalt 0 des Design-Pakets ist nicht installiert, also kann das HOME-Menü seine "
                               "Designs nicht auflisten. Installiere die CIA des Pakets mit FBI neu (" PACK_CIA ")."));
        break;
    case PACK_CATALOGUE_BAD:
        set(title, title_size, L("Cannot read the pack's catalogue", "Katalog des Pakets nicht lesbar"));
        add(body, body_size, L("%s Reinstall the pack CIA with FBI (" PACK_CIA ").",
                               "%s Installiere die CIA des Pakets mit FBI neu (" PACK_CIA ")."),
            text_result(lang, pv->detail));
        break;
    case PACK_FULL_THEMES:
        set(title, title_size, L("The theme pack is full", "Das Design-Paket ist voll"));
        add(body, body_size, L("The pack lists %u themes, counting those deleted in the HOME Menu, and holds no more "
                               "than %d.",
                               "Das Paket enthält %u Designs, im HOME-Menü gelöschte mitgezählt, und fasst nicht mehr "
                               "als %d."),
            (unsigned)pv->count, TP_THEMES_MAX);
        break;
    case PACK_FULL_INDEX:
        set(title, title_size, L("The theme pack is full", "Das Design-Paket ist voll"));
        add(body, body_size, L("Theme number %d, the last one the HOME Menu can hold, has been handed out.",
                               "Design-Nummer %d, die letzte, die das HOME-Menü fassen kann, ist vergeben."),
            TP_INDEX_MAX);
        break;
    default:
        set(title, title_size, L("Not enough memory", "Nicht genug Speicher"));
        add(body, body_size, L("Restart the application.", "Starte die Anwendung neu."));
        break;
    }
}

void text_plan(int lang, const install_plan *plan, char *body, size_t body_size)
{
    int de_ = lang == LANG_DE;
    body[0] = 0;
    switch (plan->state) {
    case PLAN_OK:
        add(body, body_size, L("It can be installed as theme %u.", "Es kann als Design %u installiert werden."),
            plan->index);
        break;
    case PLAN_FULL_THEMES:
        add(body, body_size, L("The theme pack is full: it holds no more than %d themes, counting those deleted in "
                               "the HOME Menu.",
                               "Das Design-Paket ist voll: es fasst nicht mehr als %d Designs, im HOME-Menü "
                               "gelöschte mitgezählt."),
            TP_THEMES_MAX);
        break;
    case PLAN_FULL_INDEX:
        add(body, body_size, L("The theme pack is full: theme number %d has been handed out.",
                               "Das Design-Paket ist voll: Design-Nummer %d ist vergeben."),
            TP_INDEX_MAX);
        break;
    default:
        add(body, body_size, "%s", text_result(lang, plan->detail));
        break;
    }
}

void text_deletion(int lang, int state, const char *name, char *head, size_t head_size, char *body, size_t body_size)
{
    int de_ = lang == LANG_DE;
    head[0] = body[0] = 0;
    switch (state) {
    case DEL_ACTIVE:
        add(head, head_size, L("\"%s\" is the theme in use.", "\"%s\" ist das Design, das gerade verwendet wird."),
            name);
        add(body, body_size, L("Choose another theme in the HOME Menu first (Change Theme), then delete this one "
                               "here.",
                               "Wähle zuerst im HOME-Menü ein anderes Design (Design ändern) und lösche dieses dann "
                               "hier."));
        break;
    case DEL_SHUFFLE:
        add(head, head_size, L("\"%s\" is in the shuffle selection.", "\"%s\" ist in der Shuffle-Auswahl."), name);
        add(body, body_size, L("Take it out of the shuffle in the HOME Menu first (Change Theme), then delete it "
                               "here. The HOME Menu's own Delete works too, but it empties the whole shuffle "
                               "selection.",
                               "Nimm es zuerst im HOME-Menü aus der Shuffle-Auswahl (Design ändern) und lösche es dann "
                               "hier. Das Löschen im HOME-Menü geht auch, leert aber die ganze Shuffle-Auswahl."));
        break;
    case DEL_HOME_UNKNOWN:
        set(head, head_size, L("Whether the theme is in use cannot be told.",
                               "Unklar, ob das Design gerade verwendet wird."));
        add(body, body_size, L("The HOME Menu's settings (SaveData.dat) could not be read. Delete it in the HOME Menu "
                               "instead: Change Theme, Delete.",
                               "Die Einstellungen des HOME-Menüs (SaveData.dat) waren nicht lesbar. Lösche es "
                               "stattdessen im HOME-Menü: Design ändern, Löschen."));
        break;
    case DEL_NOT_INSTALLED:
        add(head, head_size, L("\"%s\" is deleted already.", "\"%s\" ist schon gelöscht."), name);
        break;
    case DEL_PENDING:
        set(head, head_size, L("An import is pending", "Ein Import ist offen"));
        add(body, body_size, L("An earlier import of the theme pack did not finish. %s",
                               "Ein früherer Import des Design-Pakets wurde nicht abgeschlossen. %s"),
            pending_fbi(de_));
        break;
    case DEL_PACK:
        set(head, head_size, L("The theme pack changed", "Das Design-Paket hat sich verändert"));
        add(body, body_size, L("What AM reports no longer matches what was read at the start. Go back and choose it "
                               "again.",
                               "Was AM meldet, passt nicht mehr zu dem, was beim Start gelesen wurde. Geh zurück und "
                               "wähle es erneut."));
        break;
    default:
        set(head, head_size, L("Not deleted", "Nicht gelöscht"));
        add(body, body_size, L("This application deletes only the themes of the pack 0004008C00009809, never its "
                               "catalogue (content 0) and nothing of another title.",
                               "Diese Anwendung löscht nur Designs des Pakets 0004008C00009809, nie seinen Katalog "
                               "(Inhalt 0) und nichts eines anderen Titels."));
        break;
    }
    add(body, body_size, L("%sNothing was deleted.", "%sEs wurde nichts gelöscht."), body[0] ? " " : "");
}

void text_deleted(int lang, const deletion_judgement *j, int am_failed, uint32_t error, int file_kept,
                  const char *name, char *head, size_t head_size, char *body, size_t body_size)
{
    int de_ = lang == LANG_DE;
    head[0] = body[0] = 0;
    switch (j->outcome) {
    case DEL_DONE:
        add(head, head_size, L("\"%s\" was deleted.", "\"%s\" wurde gelöscht."), name);
        if (file_kept)
            add(body, body_size, L("The HOME Menu no longer lists it. Its file is still on the SD card, so it can be "
                                   "installed again here.",
                                   "Das HOME-Menü zeigt es nicht mehr. Seine Datei liegt weiter auf der SD-Karte, es "
                                   "lässt sich hier wieder installieren."));
        else
            add(body, body_size, L("The HOME Menu no longer lists it.", "Das HOME-Menü zeigt es nicht mehr."));
        if (!j->record_ok)
            add(body, body_size, L(" But the record no longer matches AM: the start screen says what to do.",
                                   " Aber die Aufzeichnung passt nicht mehr zu AM: der Startbildschirm sagt, was zu tun "
                                   "ist."));
        return;
    case DEL_NOT_DONE:
        set(head, head_size, L("Not deleted", "Nicht gelöscht"));
        if (am_failed)
            add(body, body_size, L("AM refused with error %08X; \"%s\" is still installed.",
                                   "AM hat mit Fehler %08X abgelehnt; \"%s\" ist weiter installiert."),
                (unsigned)error, name);
        else
            add(body, body_size, L("AM reported success, but still lists \"%s\" as installed. Restart the console and "
                                   "look in Change Theme.",
                                   "AM hat Erfolg gemeldet, führt \"%s\" aber weiter als installiert. Starte die "
                                   "Konsole neu und sieh unter Design ändern nach."),
                name);
        break;
    case DEL_LOST:
        set(head, head_size, L("Themes lost", "Designs verloren"));
        add(body, body_size, L("After the delete AM lists %u other theme(s) as no longer installed. Way back: %s",
                               "Nach dem Löschen sind laut AM %u weitere(s) Design(s) nicht mehr installiert. Weg "
                               "zurück: %s"),
            (unsigned)j->lost, recover(de_));
        break;
    default:
        set(head, head_size, L("Not known whether it was deleted", "Unklar, ob gelöscht wurde"));
        add(body, body_size, L("AM's list could not be read afterwards. Restart the console and look in Change Theme.",
                               "Die Liste von AM war danach nicht lesbar. Starte die Konsole neu und sieh unter Design "
                               "ändern nach."));
        break;
    }
    add(body, body_size, L(" Every AM result is in " LOG_FILE ".", " Jedes AM-Ergebnis steht in " LOG_FILE "."));
}

static const char *step_name(int de_, int step)
{
    switch (step) {
    case STEP_TITLE1: case STEP_TITLE2: return L("beginning the import", "Beginn des Imports");
    case STEP_TMD: return L("writing the TMD", "Schreiben der TMD");
    case STEP_CONTENT0: return L("writing the catalogue", "Schreiben des Katalogs");
    case STEP_COMMIT1: case STEP_COMMIT2: return L("the commit", "Abschluss");
    case STEP_CONTEXT: return L("the import context", "Import-Kontext");
    case STEP_CONTENT: return L("writing the theme", "Schreiben des Designs");
    default: return L("an AM step", "ein AM-Schritt");
    }
}

void text_outcome(int lang, const judgement *j, const install_result *res, const char *name, char *title,
                  size_t title_size, char *body, size_t body_size)
{
    int de_ = lang == LANG_DE;
    title[0] = body[0] = 0;
    switch (j->outcome) {
    case OUT_DONE:
        set(title, title_size, L("Theme installed", "Design installiert"));
        add(body, body_size, L("\"%s\" is in the theme pack now. Choose it in the HOME Menu: HOME Menu Settings, "
                               "Change Theme. If it is not listed, restart the console.",
                               "\"%s\" ist jetzt im Design-Paket. Wähle es im HOME-Menü: HOME-Menü-Einstellungen, "
                               "Design ändern. Fehlt es in der Liste, starte die Konsole neu."),
            name);
        break;
    case OUT_DONE_UNCONFIRMED:
        set(title, title_size, L("Installed, not confirmed", "Installiert, nicht bestätigt"));
        add(body, body_size, L("AM accepted the import of \"%s\", but ", "AM hat den Import von \"%s\" angenommen, aber "),
            name);
        if (!j->seen)
            add(body, body_size, L("its list could not be read afterwards. Restart the console and look in Change "
                                   "Theme.",
                                   "seine Liste war danach nicht lesbar. Starte die Konsole neu und sieh unter Design "
                                   "ändern nach."));
        else if (j->lost)
            add(body, body_size, L("it lists %u theme(s) that were installed as no longer installed. Way back: %s",
                                   "laut AM sind danach %u vorher installierte(s) Design(s) nicht mehr installiert. "
                                   "Weg zurück: %s"),
                (unsigned)j->lost, recover(de_));
        else if (!j->new_installed)
            add(body, body_size, L("its list does not show the new theme as installed. Restart the console and look "
                                   "in Change Theme.",
                                   "seine Liste zeigt das neue Design nicht als installiert. Starte die Konsole neu "
                                   "und sieh unter Design ändern nach."));
        else
            add(body, body_size, L("an import of the pack is still pending. %s", "ein Import des Pakets ist noch offen. %s"),
                pending_fbi(de_));
        break;
    case OUT_NOTHING_WRITTEN:
        set(title, title_size, L("Nothing was installed", "Nichts installiert"));
        if (res->failed == STEP_MEASURE)
            add(body, body_size, "%s ", text_result(lang, res->detail));
        else if (res->failed == STEP_STAGE)
            add(body, body_size, L("The record could not be written to the SD card. ",
                                   "Die Aufzeichnung konnte nicht auf die SD-Karte geschrieben werden. "));
        else
            add(body, body_size, L("AM refused to begin the import. ", "AM hat den Beginn des Imports abgelehnt. "));
        add(body, body_size, L("Nothing on the console was changed.", "An der Konsole wurde nichts verändert."));
        break;
    case OUT_CANCELLED_KEPT:
        set(title, title_size, L("Not installed", "Nicht installiert"));
        add(body, body_size, L("The import failed (%s) and was cancelled. AM lists every theme that was installed "
                               "as still installed; \"%s\" was not added.",
                               "Der Import ist gescheitert (%s) und wurde abgebrochen. Laut AM sind alle vorher "
                               "installierten Designs weiter installiert; \"%s\" wurde nicht hinzugefügt."),
            step_name(de_, res->failed), name);
        break;
    case OUT_PHASE2_FAILED_KEPT:
        set(title, title_size, L("Not installed", "Nicht installiert"));
        add(body, body_size, L("The catalogue was updated, but the theme itself failed (%s), so the HOME Menu does "
                               "not list it. AM lists every theme that was installed as still installed. Trying again "
                               "adds it under a new number.",
                               "Der Katalog wurde erneuert, aber das Design selbst ist gescheitert (%s), darum zeigt "
                               "das HOME-Menü es nicht. Laut AM sind alle vorher installierten Designs weiter "
                               "installiert. Ein neuer Versuch fügt es unter einer neuen Nummer hinzu."),
            step_name(de_, res->failed));
        break;
    case OUT_LOST:
        set(title, title_size, L("Failed, themes lost", "Fehlgeschlagen, Designs verloren"));
        add(body, body_size, L("The import failed (%s) and was cancelled. AM lists %u theme(s) that were installed "
                               "as no longer installed. Way back: %s",
                               "Der Import ist gescheitert (%s) und wurde abgebrochen. Laut AM sind %u vorher "
                               "installierte(s) Design(s) nicht mehr installiert. Weg zurück: %s"),
            step_name(de_, res->failed), (unsigned)j->lost, recover(de_));
        break;
    default:
        set(title, title_size, L("Failed", "Fehlgeschlagen"));
        add(body, body_size, L("The import failed (%s) and was cancelled. AM's list could not be read afterwards, "
                               "so whether the installed themes are still there is unknown. Restart the console and "
                               "look in Change Theme. If themes are missing, %s",
                               "Der Import ist gescheitert (%s) und wurde abgebrochen. Die Liste von AM war danach "
                               "nicht lesbar; ob die installierten Designs noch da sind, ist unbekannt. Starte die "
                               "Konsole neu und sieh unter Design ändern nach. Wenn Designs fehlen: %s"),
            step_name(de_, res->failed), recover(de_));
        break;
    }
    if (j->outcome != OUT_DONE && j->outcome != OUT_NOTHING_WRITTEN) {
        if (j->pending == PENDING_YES && j->outcome != OUT_DONE_UNCONFIRMED)
            add(body, body_size, L(" An import of the pack is still pending. %s", " Ein Import des Pakets ist noch offen. %s"),
                pending_fbi(de_));
        add(body, body_size, L(" Every AM result is in " LOG_FILE ".", " Jedes AM-Ergebnis steht in " LOG_FILE "."));
    }
}

void text_download(int lang, int status, long http, int detail, const char *error, char *body, size_t body_size)
{
    int de_ = lang == LANG_DE;
    body[0] = 0;
    switch (status) {
    case DL_BAD_URL:
        add(body, body_size, L("The QR code holds no web address (http or https). Nothing was downloaded.",
                               "Der QR-Code enthält keine Webadresse (http oder https). Es wurde nichts geladen."));
        break;
    case DL_NO_CA:
        add(body, body_size, L("The address uses https or leads to it, but the list of trusted certificates is "
                               "not on the card (" CARD_DIR "/cacert.pem), so the site cannot be checked. Copy the "
                               "list there (README, Downloads). Nothing was downloaded.",
                               "Die Adresse nutzt https oder führt dorthin, aber die Liste vertrauenswürdiger "
                               "Zertifikate fehlt auf der Karte (" CARD_DIR "/cacert.pem), also kann die Seite nicht "
                               "geprüft werden. Kopiere die Liste dorthin (README, Downloads). Es wurde nichts "
                               "geladen."));
        break;
    case DL_NETWORK:
        add(body, body_size, L("The download failed (%s). Check the internet connection. Nothing was saved.",
                               "Das Herunterladen ist gescheitert (%s). Prüfe die Internetverbindung. Es wurde nichts "
                               "gespeichert."),
            error && *error ? error : "?");
        break;
    case DL_CERT:
        add(body, body_size, L("The site's certificate does not check out against the list on the card (%s). Nothing "
                               "was downloaded.",
                               "Das Zertifikat der Seite lässt sich mit der Liste auf der Karte nicht bestätigen "
                               "(%s). Es wurde nichts geladen."),
            error && *error ? error : "?");
        break;
    case DL_HTTP:
        add(body, body_size, L("The site answered with HTTP status %ld. Nothing was saved.",
                               "Die Seite hat mit HTTP-Status %ld geantwortet. Es wurde nichts gespeichert."),
            http);
        break;
    case DL_TOO_BIG:
        add(body, body_size, L("The file is larger than 16 MiB, more than a theme can be. Nothing was saved.",
                               "Die Datei ist grösser als 16 MiB, mehr als ein Design sein kann. Es wurde nichts "
                               "gespeichert."));
        break;
    case DL_NOT_THEME:
        add(body, body_size, L("The site sent something that is neither a theme zip nor a theme CIA, a web page that "
                               "asks for a browser check, for example. Nothing was saved. Download the theme on "
                               "another device and copy it to sdmc:/Themes.",
                               "Die Seite hat etwas geschickt, das weder eine Design-Zip noch eine Design-CIA ist, "
                               "zum Beispiel eine Webseite mit einer Browser-Prüfung. Es wurde nichts gespeichert. "
                               "Lade das Design auf einem anderen Gerät und kopiere es nach sdmc:/Themes."));
        break;
    case DL_BAD_THEME:
        add(body, body_size, L("The downloaded theme is damaged: %s Nothing was saved.",
                               "Das geladene Design ist beschädigt: %s Es wurde nichts gespeichert."),
            text_result(lang, detail));
        break;
    case DL_CANCELLED:
        add(body, body_size, L("The download was cancelled. Nothing was saved.",
                               "Das Herunterladen wurde abgebrochen. Es wurde nichts gespeichert."));
        break;
    case DL_NOMEM:
        add(body, body_size, "%s", text_result(lang, TP_E_NOMEM));
        break;
    default:
        add(body, body_size, L("The file could not be written to the SD card. Nothing was saved.",
                               "Die Datei konnte nicht auf die SD-Karte geschrieben werden. Es wurde nichts "
                               "gespeichert."));
        break;
    }
}

/* How the character of unit bytes at p is shown: 1 as it is, 0 as "%XX" for each of its bytes. */
static int shown_as_is(const char *p, size_t unit)
{
    const unsigned char *b = (const unsigned char *)p;
    if (unit == 1)
        return b[0] >= 0x20 && b[0] < 0x7F;
    if (unit == 2)
        return (((b[0] & 0x1Fu) << 6) | (b[1] & 0x3Fu)) >= 0xA0;
    return 1;
}

void text_shown(char *out, size_t size, const char *in)
{
    if (size < 4) {
        if (size)
            out[0] = 0;
        return;
    }
    size_t total = 0;
    for (const char *p = in; *p; p += wrap_unit(p))
        total += shown_as_is(p, wrap_unit(p)) ? wrap_unit(p) : 3 * wrap_unit(p);
    size_t room = total < size ? total : size - 4, n = 0;
    for (const char *p = in; *p; p += wrap_unit(p)) {
        size_t unit = wrap_unit(p);
        int as_is = shown_as_is(p, unit);
        if (n + (as_is ? unit : 3 * unit) > room)
            break;
        for (size_t k = 0; k < unit; k++) {
            if (as_is)
                out[n++] = p[k];
            else
                n += (size_t)snprintf(out + n, 4, "%%%02X", (unsigned char)p[k]);
        }
    }
    if (total >= size)
        memcpy(out + n, "...", 4);
    else
        out[n] = 0;
}
