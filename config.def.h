/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

/* configuration file location, subdirectory of XDG_CONFIG_HOME */
static const char* dmenu_cfg = "/dmenu/dmenu.toml";

/* -ix option; if 1 dmenu will print the index instead of the item */ 
static int print_index = 0;

/* -i option; if 0, dmenu doesn't show caret & input box */ 
static int input = 1;

/* -F option; if 0, dmenu doesn't use fuzzy matching */
static int fuzzy = 0;

/* -M option; if 0, dmenu doesn't allow for multi selection */
static int multiselect = 0;

/* Minimum width of the window */
static int min_width = 500;

/* up/down padding on line-items */
static int item_height = 5;

/* -n option; if 1 show number of matches over the number of items */
static int show_numbers = 0;

/* Window border */
static int border_width = 1;

/* Padding (inner) between border and content */
static int border_padding = 10;

/* Margin (outer) between border and content */
static int border_margin = 25;

/* amount of pixel between the prompt and other list items */
static unsigned int prompt_offset = 10; 

/* Amount of opacity. 0xff is opaque */
static unsigned int alpha = 0xcc;

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = { "monospace:size=14" };

/* -p option; prompt to the left of input field */
static const char *prompt = NULL;

/* dmenu uses vertical list with given number of lines */
static unsigned int lines = 15;

/* character that will be used to hide text when the -P option is specified */
static char censor_char = '*';
/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char *worddelimiters = " ";

static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#000000" },
	[SchemeSel] = { "#000000", "#81a2be" },
	[SchemeSelHighlight] = { "#ffffff", "#81a2be" },
	[SchemeNormHighlight] = { "#ffffff", "#000000" },
	[SchemeOut] = { "#ffffff", "#000000" },
	[SchemeBorder] = { "#ffffff", "#ffffff" }
};
