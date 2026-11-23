/* Built with the application only to fail the build when one of its own macro names is also a name in libctru,
 * citro2d, citro3d, libcurl or zlib. Every own header is included, the generated ui_art.h too (the Makefile builds
 * it before any object). 0.6.1 had "#define KEY_Y 217" for a row of the bar, and in main.c, which
 * includes <3ds.h> first, every later KEY_Y meant 217 instead of the Y button. Here every own header with plain
 * macro names comes first and the SDK's after it, so such a name breaks the SDK's own declarations (an enum
 * constant, function, parameter, field or type then has a number for a name) or redefines one of its macros, which
 * -Werror refuses. Not caught: a name the SDK uses only in headers these do not include, or a macro defined twice
 * with the same body. */
#include "camera.h"
#include "console.h"
#include "deletion.h"
#include "designs.h"
#include "download.h"
#include "files.h"
#include "install.h"
#include "layout.h"
#include "paths.h"
#include "plan.h"
#include "popup.h"
#include "progress.h"
#include "scan.h"
#include "scroll.h"
#include "store.h"
#include "text.h"
#include "touch.h"
#include "turn.h"
#include "ui_art.h"
#include "wrap.h"
#include "ui.h"                 /* last of the own with app_ui.h: it includes citro2d after its own macros */
#include "app_ui.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <curl/curl.h>
#include <zlib.h>

typedef int names_checked;
