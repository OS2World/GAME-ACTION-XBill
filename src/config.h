/* config.h -- hand-written for ArcaOS SDL2 port (replaces autoconf output) */
#ifndef CONFIG_H
#define CONFIG_H

#define STDC_HEADERS 1
#define HAVE_UNISTD_H 1

#define USE_SDL2 1
/* USE_ATHENA, USE_MOTIF, USE_GTK deliberately left undefined */

/* Score file written in the working directory */
#define SCOREFILE "xbill.scores"

/* Image base directory (not used by SDL2 backend -- assets are embedded) */
#define IMAGES "."

#endif /* CONFIG_H */
