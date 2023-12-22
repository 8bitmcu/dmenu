/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

/* -F  option; if 0, dmenu doesn't use fuzzy matching */
static int fuzzy = 0;

/* -M option; if 0, dmenu doesn't allow for multi selection */
static int multiselect = 0;

/* Minimum width of the window */
static int min_width = 480;

/* add an defined amount of pixels between items in list */
static const int user_bh = 5;

/* Window border */
static const int border_width = 1;

/* Padding (inner) between border and content */
static const int border_padding = 10;

/* Margin (outer) between border and content */
static const int border_margin = 25;

/* amount of pixel between the prompt and other list items */
static const unsigned int prompt_offset = 10; 

/* Amount of opacity. 0xff is opaque */
static const unsigned int alpha = 0xcc;

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = { "monospace:size=14" };

/* -p  option; prompt to the left of input field */
static const char *prompt = NULL;

/* dmenu uses vertical list with given number of lines */
static unsigned int lines = 15;

/* character that will be used to hide text when the -P option is specified */
static const char censor_char = '*';
/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";

static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#000000" },
	[SchemeSel] = { "#000000", "#81a2be" },
	[SchemeSelHighlight] = { "#ffffff", "#81a2be" },
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

