/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"
#include "tomlc99/toml.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)

#define OPAQUE                0xffu

#define NUMBERSMAXDIGITS      100
#define NUMBERSBUFSIZE        (NUMBERSMAXDIGITS * 2) + 1

/* enums */
enum { SchemeNorm, SchemeSel, SchemeNormHighlight, SchemeSelHighlight,
       SchemeOut, SchemeBorder, SchemeLast }; /* color schemes */

struct item {
	char *text;
	struct item *left, *right;
	int id; /* for multiselect */
	double distance;
};

static char numbers[NUMBERSBUFSIZE] = "";
static char text[BUFSIZ] = "";
static char *embed;
static int bh, mw, mh;
static int inputw = 0, promptw, passwd = 0;
static int lrpad; /* sum of left and right padding */
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1, screen;
static int *selid = NULL;
static unsigned int selidsize = 0;

static Atom clip, utf8;
static Display *dpy;
static Window root, parentwin, win;
static XIC xic;

static Drw *drw;
static Clr *scheme[SchemeLast];

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

#include "config.h"

static char * cistrstr(const char *s, const char *sub);
static int (*fstrncmp)(const char *, const char *, size_t) = strncasecmp;
static char *(*fstrstr)(const char *, const char *) = cistrstr;
static void xinitvisual();

static int
issel(size_t id)
{
	for (int i = 0;i < selidsize;i++)
		if (selid[i] == id)
			return 1;
	return 0;
}

static unsigned int
textw_clamp(const char *str, unsigned int n)
{
	unsigned int w = drw_fontset_getwidth_clamp(drw, str, n) + lrpad;
	return MIN(w, n);
}

static void
appenditem(struct item *item, struct item **list, struct item **last)
{
	if (*last)
		(*last)->right = item;
	else
		*list = item;

	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void
calcoffsets(void)
{
	int i, n;

	n = lines * bh;

	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : textw_clamp(next->text, n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : textw_clamp(prev->left->text, n)) > n)
			break;
}

static int
max_textw(void)
{

	if(!items)
		return min_width;

	// Max text length is based on the list item with the most characters
	// This is fine for monospaced fonts, but not regular fonts
	// But it is sooo much, much faster (especially on larger files)
	struct item *max = NULL;
	int maxl = 0;

	for (struct item *item = items; item && item->text; item++) {
		if (strlen(item->text) > maxl) {
			max = item;
			maxl = strlen(item->text);
		}
	}
	return TEXTW(max->text);
}

static void
cleanup_cfg(void)
{
	free((void *) fonts[0]);
	free((void *) censor_char);
	free((void *) worddelimiters);
	free((void *) colors[SchemeNorm][ColFg]);
	free((void *) colors[SchemeNorm][ColBg]);
	free((void *) colors[SchemeSel][ColFg]);
	free((void *) colors[SchemeSel][ColBg]);
	free((void *) colors[SchemeSelHighlight][ColFg]);
	free((void *) colors[SchemeSelHighlight][ColBg]);
	free((void *) colors[SchemeOut][ColFg]);
	free((void *) colors[SchemeOut][ColBg]);
	free((void *) colors[SchemeBorder][ColFg]);
	free((void *) colors[SchemeBorder][ColBg]);
}

static void
cleanup(void)
{
	size_t i;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	for (i = 0; items && items[i].text; ++i)
		free(items[i].text);
	free(items);
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
	free(selid);
	cleanup_cfg();
}

static char *
cistrstr(const char *h, const char *n)
{
	size_t i;

	if (!n[0])
		return (char *)h;

	for (; *h; ++h) {
		for (i = 0; n[i] && tolower((unsigned char)n[i]) ==
		            tolower((unsigned char)h[i]); ++i)
			;
		if (n[i] == '\0')
			return (char *)h;
	}
	return NULL;
}

static void
drawhighlights(struct item *item, int x, int y, int maxw)
{
	int i, indent;
	char *highlight;
	char c;

	if (!(strlen(item->text) && strlen(text)))
		return;

	drw_setscheme(drw, scheme[item == sel
	                   ? SchemeSelHighlight
	                   : SchemeNormHighlight]);

	for (i = 0, highlight = item->text; *highlight && text[i];) {
		if (!fstrncmp(&text[i], highlight, 1)) {
			c = highlight[1];
			highlight[1] = '\0';

			/* get indentation */
			indent = TEXTW(item->text) + border_padding - 1;

			/* highlight character */
			drw_text(
				drw,
				x + indent - lrpad,
				y,
				MIN(maxw - indent, TEXTW(highlight) - lrpad),
				bh, 0, highlight, 0
			);
			highlight[1] = c;
			i++;
		}
		highlight++;
	}
}

static int
drawitem(struct item *item, int x, int y, int w)
{
	int r;
	if (item == sel)
		drw_setscheme(drw, scheme[SchemeSel]);
	else if (issel(item->id))
		drw_setscheme(drw, scheme[SchemeOut]);
	else
		drw_setscheme(drw, scheme[SchemeNorm]);

	r = drw_text(drw, x, y, w, bh, lrpad / 2, item->text, 0);
	drawhighlights(item, x, y, w);
	return r;
}

static void
recalculatenumbers()
{
	if (!show_numbers)
		return;

	unsigned int numer = 0, denom = 0;
	struct item *item;
	if (matchend) {
		numer++;
		for (item = matchend; item && item->left; item = item->left)
			numer++;
	}
	for (item = items; item && item->text; item++)
		denom++;
	snprintf(numbers, NUMBERSBUFSIZE, "%d/%d", numer, denom);
}

static void
drawmenu(void)
{
	unsigned int curpos;
	struct item *item;
	int x = border_margin, y = border_margin + border_padding, w;
	char *censort;

	recalculatenumbers();

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	/* draw prompt */
	if (prompt && *prompt) {
		drw_setscheme(drw, scheme[SchemeOut]);
		x = drw_text(drw, x, y, promptw, bh, lrpad / 2, prompt, 0);
	}

	/* draw input field */
	w = ((lines > 0 || !matches) ? mw - x : inputw) - TEXTW(numbers);
	drw_setscheme(drw, scheme[SchemeOut]);

	/* draw censor_char if passwd, otherwise draw user input */
	if (passwd) {
	        censort = ecalloc(1, sizeof(text));
		memset(censort, censor_char[0], strlen(text));
		drw_text(drw, x, y, w, bh, 0, censort, 0);
		free(censort);
	} else if (input)
		drw_text(drw, x, y, w, bh, 0, text, 0);

	/* draw caret */
	if (input) {
		curpos = TEXTW(text) - TEXTW(&text[cursor]);
		if (curpos < w) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x + curpos, y+2, 2, bh - 4, 1, 0);
		}
	}

	/* draw numbers */
	if (show_numbers) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_text(drw, mw - TEXTW(numbers) - border_margin, y, TEXTW(numbers), bh, lrpad / 2, numbers, 0);
	}

	y += prompt_offset;
	/* draw vertical list */
	for (item = curr; item != next; item = item->right)
		drawitem(item, x - promptw, y += bh, mw - (border_margin*2));


	drw_map(drw, win, 0, 0, mw, mh);
}

static void
grabfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}

int
compare_distance(const void *a, const void *b)
{
	struct item *da = *(struct item **) a;
	struct item *db = *(struct item **) b;

	if (!db)
		return 1;
	if (!da)
		return -1;

	return da->distance == db->distance ? 0 : da->distance < db->distance ? -1 : 1;
}

void
fuzzymatch(void)
{
	/* bang - we have so much memory */
	struct item *it;
	struct item **fuzzymatches = NULL;
	char c;
	int number_of_matches = 0, i, pidx, sidx, eidx;
	int text_len = strlen(text), itext_len;

	matches = matchend = NULL;

	/* walk through all items */
	for (it = items; it && it->text; it++) {
		if (text_len) {
			itext_len = strlen(it->text);
			pidx = 0; /* pointer */
			sidx = eidx = -1; /* start of match, end of match */
			/* walk through item text */
			for (i = 0; i < itext_len && (c = it->text[i]); i++) {
				/* fuzzy match pattern */
				if (!fstrncmp(&text[pidx], &c, 1)) {
					if(sidx == -1)
						sidx = i;
					pidx++;
					if (pidx == text_len) {
						eidx = i;
						break;

					}
				}
			}
			/* build list of matches */
			if (eidx != -1) {
				/* compute distance */
				/* add penalty if match starts late (log(sidx+2))
				 * add penalty for long a match without many matching characters */
				it->distance = log(sidx + 2) + (double)(eidx - sidx - text_len);
				/* fprintf(stderr, "distance %s %f\n", it->text, it->distance); */
				appenditem(it, &matches, &matchend);
				number_of_matches++;
			}
		} else {
			appenditem(it, &matches, &matchend);
		}
	}

	if (number_of_matches) {
		/* initialize array with matches */
		if (!(fuzzymatches = realloc(fuzzymatches, number_of_matches * sizeof(struct item*))))
			die("cannot realloc %u bytes:", number_of_matches * sizeof(struct item*));
		for (i = 0, it = matches; it && i < number_of_matches; i++, it = it->right) {
			fuzzymatches[i] = it;
		}
		/* sort matches according to distance */
		qsort(fuzzymatches, number_of_matches, sizeof(struct item*), compare_distance);
		/* rebuild list of matches */
		matches = matchend = NULL;
		for (i = 0, it = fuzzymatches[i];  i < number_of_matches && it && \
				it->text; i++, it = fuzzymatches[i]) {
			appenditem(it, &matches, &matchend);
		}
		free(fuzzymatches);
	}
	curr = sel = matches;
	calcoffsets();
}


static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	if (embed)
		return;
	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard");
}

static void
match(void)
{
	if (fuzzy) {
		fuzzymatch();
		return;
	}
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %zu bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
	textsize = strlen(text) + 1;
	for (item = items; item && item->text; item++) {
		for (i = 0; i < tokc; i++)
			if (!fstrstr(item->text, tokv[i]))
				break;
		if (i != tokc) /* not all tokens match */
			continue;
		/* prefixes go first, then exact matches, ignore substrings */
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &lprefix, &prefixend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		// disable substrings
		//else
			//appenditem(item, &lsubstr, &substrend);
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (lsubstr) {
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

static void
insert(const char *str, ssize_t n)
{
	if (!input)
		return;

	if (strlen(text) + n > sizeof text - 1)
		return;
	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match();
}

static size_t
nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

static void
movewordedge(int dir)
{
	if (dir < 0) { /* move cursor to the start of the word*/
		while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
		while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
	} else { /* move cursor to the end of the word */
		while (text[cursor] && strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
		while (text[cursor] && !strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
	}
}

static void
keypress(XKeyEvent *ev)
{
	char buf[64];
	int len;
	KeySym ksym = NoSymbol;
	Status status;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return;
	case XLookupChars: /* composed string from input method */
		goto insert;
	case XLookupKeySym:
	case XLookupBoth: /* a KeySym and a string are returned: use keysym */
		break;
	}

	if (ev->state & ControlMask) {
		switch(ksym) {
		case XK_a: ksym = XK_Home;      break;
		case XK_b: ksym = XK_Left;      break;
		case XK_c: ksym = XK_Escape;    break;
		case XK_d: ksym = XK_Delete;    break;
		case XK_e: ksym = XK_End;       break;
		case XK_f: ksym = XK_Right;     break;
		case XK_g: ksym = XK_Escape;    break;
		case XK_h: ksym = XK_BackSpace; break;
		case XK_i: ksym = XK_Tab;       break;
		case XK_j: /* fallthrough */
		case XK_J: /* fallthrough */
		case XK_m: /* fallthrough */
		case XK_M: ksym = XK_Return; ev->state &= ~ControlMask; break;
		case XK_n: ksym = XK_Down;      break;
		case XK_p: ksym = XK_Up;        break;

		case XK_k: /* delete right */
			text[cursor] = '\0';
			match();
			break;
		case XK_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w: /* delete word */
			while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_y: /* paste selection */
		case XK_Y:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_Left:
		case XK_KP_Left:
			movewordedge(-1);
			goto draw;
		case XK_Right:
		case XK_KP_Right:
			movewordedge(+1);
			goto draw;
		case XK_Return:
		case XK_KP_Enter:
		if(multiselect > 0) {
			if (sel && issel(sel->id)) {
				for (int i = 0;i < selidsize;i++)
					if (selid[i] == sel->id)
						selid[i] = -1;
			} else {
				for (int i = 0;i < selidsize;i++)
					if (selid[i] == -1) {
						selid[i] = sel->id;
						return;
					}
				selidsize++;
				selid = realloc(selid, (selidsize + 1) * sizeof(int));
				selid[selidsize - 1] = sel->id;
			}
		}
			break;
		case XK_bracketleft:
			cleanup();
			exit(1);
		default:
			return;
		}
	} else if (ev->state & Mod1Mask) {
		switch(ksym) {
		case XK_b:
			movewordedge(-1);
			goto draw;
		case XK_f:
			movewordedge(+1);
			goto draw;
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
		default:
			return;
		}
	}

	switch(ksym) {
	default:
insert:
		if (!iscntrl((unsigned char)*buf))
			insert(buf, len);
		break;
	case XK_Delete:
	case XK_KP_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
	case XK_KP_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Escape:
		cleanup();
		exit(1);
	case XK_Home:
	case XK_KP_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
	case XK_KP_Left:
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
	case XK_KP_Up:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Next:
	case XK_KP_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
	case XK_KP_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		if (!(ev->state & ControlMask)) {
			/* multi-select items */
			for (int i = 0;i < selidsize;i++)
				if (selid[i] != -1 && (!sel || sel->id != selid[i]))
					print_index ? printf("%d\n", items[selid[i]].id) : puts(items[selid[i]].text);
			/* item that is currently under selection */
			if (sel && !(ev->state & ShiftMask))
				print_index ? printf("%d\n", sel->id) : puts(sel->text);
			else
				/* input from the textbox */
				puts(print_index ? "-1" : text);
			cleanup();
			exit(0);
		}
		break;
	case XK_Right:
	case XK_KP_Right:
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Down:
	case XK_KP_Down:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if (!sel)
			return;
		cursor = strnlen(sel->text, sizeof text - 1);
		memcpy(text, sel->text, cursor);
		text[cursor] = '\0';
		match();
		break;
	}

draw:
	drawmenu();
}

static void
buttonpress(XEvent *e)
{
	struct item *item;
	XButtonPressedEvent *ev = &e->xbutton;

	int x = border_padding + border_margin, y = border_margin + border_padding + prompt_offset, h = bh, w;

	if (ev->window != win)
		return;

	/* right-click: exit */
	if (ev->button == Button3)
		exit(1);

	if (prompt && *prompt)
		x += promptw;

	/* input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;

	/* left-click on input: clear input,
	 * NOTE: if there is no left-arrow the space for < is reserved so
	 *       add that to the input width */
	if (ev->button == Button1 &&
	   ((lines <= 0 && ev->x >= 0 && ev->x <= x + w +
	   ((!prev || !curr->left) ? TEXTW("<") : 0)) ||
	   (lines > 0 && ev->y >= y && ev->y <= y + h))) {
		insert(NULL, -cursor);
		drawmenu();
		return;
	}
	/* middle-mouse click: paste selection */
	if (ev->button == Button2) {
		XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
		                  utf8, utf8, win, CurrentTime);
		drawmenu();
		return;
	}
	/* scroll up */
	if (ev->button == Button4 && prev) {
		sel = curr = prev;
		calcoffsets();
		drawmenu();
		return;
	}
	/* scroll down */
	if (ev->button == Button5 && next) {
		sel = curr = next;
		calcoffsets();
		drawmenu();
		return;
	}
	if (ev->button != Button1)
		return;
	if (ev->state & ~ControlMask)
		return;

	/* vertical list: (ctrl)left-click on item */
	w = mw - x;
	for (item = curr; item != next; item = item->right) {
		y += h;
		if (ev->y >= y && ev->y <= (y + h)) {
			if(multiselect || !(ev->state & ControlMask)) {
				sel = item;
				if (sel) {
					if (sel && issel(sel->id)) {
						for (int i = 0;i < selidsize;i++)
							if (selid[i] == sel->id)
								selid[i] = -1;
					} else {
						for (int i = 0;i < selidsize;i++)
							if (selid[i] == -1) {
								selid[i] = sel->id;
								return;
							}
						selidsize++;
						selid = realloc(selid, (selidsize + 1) * sizeof(int));
						selid[selidsize - 1] = sel->id;
					}
					drawmenu();
				}
			}
			if (!(ev->state & ControlMask)) {
				for (int i = 0;i < selidsize;i++)
					if (selid[i] != -1 && (!sel || sel->id != selid[i]))
						puts(items[selid[i]].text);
				if (sel && !(ev->state & ShiftMask))
					puts(sel->text);
				else
					puts(text);
				cleanup();
				exit(0);
			}
			return;
		}
	}
}


static void
paste(void)
{
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	if (XGetWindowProperty(dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		insert(p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
	drawmenu();
}

static void
readstdin(void)
{
	char *line = NULL;
	size_t i, itemsiz = 0, linesiz = 0;
	ssize_t len;

	if (passwd) {
		inputw = lines = 0;
		return;
 	}

	/* read each line from stdin and add it to the item list */
	for (i = 0; (len = getline(&line, &linesiz, stdin)) != -1; i++) {
		if (i + 1 >= itemsiz) {
			itemsiz += 256;
			if (!(items = realloc(items, itemsiz * sizeof(*items))))
				die("cannot realloc %zu bytes:", itemsiz * sizeof(*items));
		}
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (!(items[i].text = strdup(line)))
			die("strdup:");

		items[i].id = i; /* for multiselect */
	}
	free(line);
	if (items)
		items[i].text = NULL;
	lines = MIN(lines, i);
}

static void
run(void)
{
	XEvent ev;

	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case DestroyNotify:
			if (ev.xdestroywindow.window != win)
				break;
			cleanup();
			exit(1);
		case ButtonPress:
			buttonpress(&ev);
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, mw, mh);
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != win)
				grabfocus();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case SelectionNotify:
			if (ev.xselection.property == utf8)
				paste();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void
setup(void)
{
	int x, y, i, j;
	unsigned int du;
	XSetWindowAttributes swa;
	XIM xim;
	Window w, dw, *dws;
	XWindowAttributes wa;
	XClassHint ch = {"dmenu", "dmenu"};
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, di, n, area = 0;
#endif
	/* init appearance */

	unsigned int alphas[2] = { OPAQUE, alpha };
	for (j = 0; j < SchemeLast; j++)
		scheme[j] = drw_scm_create(drw, colors[j], alphas, 2);

	clip = XInternAtom(dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = drw->fonts->h;
	bh = item_height ? bh + item_height : bh + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh + prompt_offset + (border_margin*2) + (border_padding*2);
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
#ifdef XINERAMA
	i = 0;
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]) != 0)
					break;

    mw = MIN(MAX(max_textw() + promptw, min_width), info[i].width);
    x = info[i].x_org + ((info[i].width  - mw) / 2);
    y = info[i].y_org + ((info[i].height - mh) / 2);

		XFree(info);
	} else
#endif
	{
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			die("could not get embedding window attributes: 0x%lx",
			    parentwin);
		mw = MIN(MAX(max_textw() + promptw, min_width), wa.width);
		x = (wa.width  - mw) / 2;
		y = (wa.height - mh) / 2;
	}
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
	inputw = mw / 3; /* input width: ~33% of monitor width */
	match();


	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = 0;
	swa.border_pixel = 0;
	swa.colormap = cmap;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask |
	                 ButtonPressMask;
	win = XCreateWindow(dpy, root, x, y, mw, mh, border_width,
	                    depth, CopyFromParent, visual,
	                    CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &swa);
	if (border_width) {
		XSetWindowBorder(dpy, win, scheme[SchemeBorder][ColBg].pixel);
	}
	XSetClassHint(dpy, win, &ch);


	/* input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		die("XOpenIM failed: could not open input device");

	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	XMapRaised(dpy, win);
	if (embed) {
		XReparentWindow(dpy, win, parentwin, x, y);
		XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
		if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
			for (i = 0; i < du && dws[i] != win; ++i)
				XSelectInput(dpy, dws[i], FocusChangeMask);
			XFree(dws);
		}
		grabfocus();
	}
	drw_resize(drw, mw, mh);
	drawmenu();
}

static void
usage(void)
{
	die("usage: dmenu [-bfvsiP] [-l lines] [-p prompt] [-fn font] [-m monitor]\n"
	    "           [-nhb color] [-nhf color] [-shb color] [-shf color] [-nb color]\n"
      "           [-nf color] [-sb color] [-sf color] [-w windowid] [-it text ]\n"
      "           [-W width] [-F number] [-M number] [-n number] [-ix number]");
}


static void
cfg_read_str(toml_table_t* conf, char* key, const char** dest)
{
	toml_datum_t d = toml_string_in(conf, key);
	if (d.ok)
		*dest = d.u.s;
}

static void
cfg_read_int(toml_table_t* conf, char* key, int* dest)
{
	toml_datum_t d = toml_int_in(conf, key);
	if (d.ok)
		*dest = d.u.i;
}

int
main(int argc, char *argv[])
{
	XWindowAttributes wa;
	int i, fast = 0;

	const char *config_file = strcat(getenv("XDG_CONFIG_HOME"), dmenu_cfg);
	FILE* fp = fopen(config_file, "r");
	if(fp) {
		char errbuf[200];
		toml_table_t* conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
		fclose(fp);

		if (conf) {
			cfg_read_int(conf, "fuzzy", &fuzzy);
			cfg_read_int(conf, "multiselect", &multiselect);
			cfg_read_int(conf, "min_width", &min_width);
			cfg_read_int(conf, "print_index", &print_index);
			cfg_read_int(conf, "show_numbers", &show_numbers);
			cfg_read_int(conf, "item_height", &item_height);
			cfg_read_int(conf, "border_width", &border_width);
			cfg_read_int(conf, "border_padding", &border_padding);
			cfg_read_int(conf, "border_margin", &border_margin);
			cfg_read_int(conf, "prompt_offset", &prompt_offset);
			cfg_read_int(conf, "alpha", &alpha);
			cfg_read_int(conf, "lines", &lines);
			cfg_read_str(conf, "font", &fonts[0]);
			cfg_read_str(conf, "censor_char", &censor_char);
			cfg_read_str(conf, "worddelimiters", &worddelimiters);
			cfg_read_str(conf, "schemenorm_fg", &colors[SchemeNorm][ColFg]);
			cfg_read_str(conf, "schemenorm_bg", &colors[SchemeNorm][ColBg]);
			cfg_read_str(conf, "schemesel_fg", &colors[SchemeSel][ColFg]);
			cfg_read_str(conf, "schemesel_bg", &colors[SchemeSel][ColBg]);
			cfg_read_str(conf, "schemeselhighlight_fg", &colors[SchemeSelHighlight][ColFg]);
			cfg_read_str(conf, "schemeselhighlight_bg", &colors[SchemeSelHighlight][ColBg]);
			cfg_read_str(conf, "schemeout_fg", &colors[SchemeOut][ColFg]);
			cfg_read_str(conf, "schemeout_bg", &colors[SchemeOut][ColBg]);
			cfg_read_str(conf, "schemeborder_fg", &colors[SchemeBorder][ColFg]);
			cfg_read_str(conf, "schemeborder_bg", &colors[SchemeBorder][ColBg]);

			toml_free(conf);
		}
	}

	// overwrite config with cmd line arguments 
	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			cleanup_cfg();
			exit(0);
		} else if (!strcmp(argv[i], "-f"))   /* grabs keyboard before reading stdin */
			fast = 1;
		else if (!strcmp(argv[i], "-P"))   /* is the input a password */
			passwd = 1;
		else if (!strcmp(argv[i], "-s")) { /* case-sensitive item matching */
			fstrncmp = strncmp;
			fstrstr = strstr;
		} else if (!strcmp(argv[i], "-i")) /* input-less */
			input = 0;
		else if (i + 1 == argc) {
			cleanup_cfg();
			usage();
		}
		/* these options take one argument */
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-F"))   /* enable fuzzy matching */
			fuzzy = atoi(argv[++i]); 
		else if (!strcmp(argv[i], "-M"))   /* enables multiple selections */
			multiselect = atoi(argv[++i]); 
		else if (!strcmp(argv[i], "-n"))   /* show number of lines */
			show_numbers = atoi(argv[++i]); 
		else if (!strcmp(argv[i], "-ix"))  /* adds ability to return index in list */
			print_index = atoi(argv[++i]); 
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-nhb")) /* normal hi background color */
			colors[SchemeNormHighlight][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nhf")) /* normal hi foreground color */
			colors[SchemeNormHighlight][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-shb")) /* selected hi background color */
			colors[SchemeSelHighlight][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-shf")) /* selected hi foreground color */
			colors[SchemeSelHighlight][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-w"))   /* embedding window id */
			embed = argv[++i];
		else if (!strcmp(argv[i], "-W"))   /* overwrite minimum width */
			min_width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-it")) {   /* initial text */
			const char * text = argv[++i];
			insert(text, strlen(text));
		}
		else {
			cleanup_cfg();
			usage();
		}

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL))) {
		cleanup_cfg();
		die("cannot open display");
	}
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if (!embed || !(parentwin = strtol(embed, NULL, 0)))
		parentwin = root;
	if (!XGetWindowAttributes(dpy, parentwin, &wa)) {
		cleanup_cfg();
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);
	}
	xinitvisual();
	drw = drw_create(dpy, screen, root, wa.width, wa.height, visual, depth, cmap);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts))) {
		cleanup_cfg();
		die("no fonts could be loaded.");
	}

	lrpad = drw->fonts->h;
	if (border_padding > 0) {
		lrpad += border_padding*2;
	}

#ifdef __OpenBSD__
	if (pledge("stdio rpath", NULL) == -1)
		die("pledge");
#endif

	if (fast && !isatty(0)) {
		grabkeyboard();
		readstdin();
	} else {
		readstdin();
		grabkeyboard();
	}
	setup();
	run();

	return 1; /* unreachable */
}


void
xinitvisual()
{
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			 visual = infos[i].visual;
			 depth = infos[i].depth;
			 cmap = XCreateColormap(dpy, root, visual, AllocNone);
			 useargb = 1;
			 break;
		}
	}

	XFree(infos);

	if (! visual) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
}
