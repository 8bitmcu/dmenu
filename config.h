/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int fuzzy = 0;                      /* -F  option; if 0, dmenu doesn't use fuzzy matching     */

static int min_width = 480;

static const int user_bh = 5;               /* add an defined amount of pixels between items in list */

static const int border_width = 1;

static const int border_padding = 20;

static const unsigned int prompt_offset = 10;

static const unsigned int alpha = 0xcc;     /* Amount of opacity. 0xff is opaque             */

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = { "monospace:size=14" };
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#000000" },
	[SchemeSel] = { "#000000", "#81a2be" },
	[SchemeSelHighlight] = { "#000000", "#81a2be" },
	[SchemeNormHighlight] = { "#ffffff", "#000000" },
	[SchemeOut] = { "#ffffff", "#000000" },
	[SchemeBorder] = { "#ffffff", "#ffffff" }
};

static const unsigned int alphas[SchemeLast][2] = {
	[SchemeNorm] = { OPAQUE, alpha },
	[SchemeSel] = { OPAQUE, alpha },
	[SchemeSelHighlight] = { OPAQUE, alpha },
	[SchemeNormHighlight] = { OPAQUE, alpha },
	[SchemeOut] = { OPAQUE, alpha },
	[SchemeBorder] = { OPAQUE, alpha }
};

/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 15;

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
