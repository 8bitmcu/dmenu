/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int min_width = 500;

static const int user_bh = 5;               /* add an defined amount of pixels between items in list */

static unsigned int border_width = 20;

static const unsigned int prompt_offset = 10;

static const unsigned int alpha = 0xee;     /* Amount of opacity. 0xff is opaque             */

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = { "monospace:size=14" };
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#000000" },
	[SchemeSel] = { "#eeeeee", "#005577" },
	[SchemeOut] = { "#ffffff", "#000000" },
};

static const unsigned int alphas[SchemeLast][2] = {
	[SchemeNorm] = { OPAQUE, alpha },
	[SchemeSel] = { OPAQUE, alpha },
	[SchemeOut] = { OPAQUE, alpha },
};

/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 15;

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
