/* The other half of names.c: the SDK's headers first, then every own header, so an own macro that redefines one
 * of the SDK's macros is reported in our header, where -Werror sees it (the other order reports it inside the SDK's
 * headers, which the compiler keeps quiet about). */
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <curl/curl.h>
#include <zlib.h>

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

typedef int names_sdk_first_checked;
