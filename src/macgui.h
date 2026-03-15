/* Definitions and headers for communication on macOS.
   Copyright (C) 2000-2008 Free Software Foundation, Inc.
   Copyright (C) 2009-2025  YAMAMOTO Mitsuharu

This file is part of GNU Emacs Mac port.

GNU Emacs Mac port is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs Mac port is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs Mac port.  If not, see <https://www.gnu.org/licenses/>.  */

/* Originally contributed by Andrew Choi (akochoi@mac.com) for Emacs 21.  */

#ifndef EMACS_MACGUI_H
#define EMACS_MACGUI_H

typedef Lisp_Object XrmDatabase;

#ifdef Z
#define Z_defined 1
#undef Z
#endif
#undef DEBUG
#ifdef HAVE_UNEXEC
#undef free
#undef malloc
#undef realloc
#endif	/* HAVE_UNEXEC */
/* Macros max and min defined in lisp.h conflict with those in
   precompiled header Carbon.h.  */
#undef max
#undef min
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#include <Carbon/Carbon.h>
#ifdef HAVE_UNEXEC
#undef free
#define free unexec_free
#undef malloc
#define malloc unexec_malloc
#undef realloc
#define realloc unexec_realloc
#endif	/* HAVE_UNEXEC */
#undef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#undef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#ifdef Z_defined
#undef Z
#define Z (current_buffer->text->z)
#undef Z_defined
#endif
#undef ALIGN

#include <Accelerate/Accelerate.h>

#ifndef CF_NOESCAPE
#if __has_attribute (noescape)
#define CF_NOESCAPE __attribute__ ((noescape))
#else
#define CF_NOESCAPE
#endif
#endif

typedef void *Window;
typedef void *Selection;

typedef const struct _EmacsDocument *EmacsDocumentRef; /* opaque */

#define Emacs_Cursor CFTypeRef

#ifndef DRAWING_USE_GCD
#define DRAWING_USE_GCD 1
#endif

/* Emulate X GC's by keeping color info in a structure.  */
typedef struct _XGCValues
{
  unsigned foreground : 32;
  unsigned background : 24;

  /* Background transparency: 0 = opaque, 255 = transparent.  */
  unsigned background_transparency : 8;

  /* FillSolid or FillOpaqueStippled.  */
  int fill_style;

  /* Array of 1 or 2 Core Graphics image mask(s).  The element at
     index 1 is used as a 2x high-resolution mask if it exists.  */
  CFArrayRef stipple;
} XGCValues;

#define MAC_MAX_CLIP_RECTS 4

typedef struct _XGC
{
  /* Original value.  */
  XGCValues xgcv;

  /* Cached data members follow.  */

  /* Quartz 2D foreground color.  */
  CGColorRef cg_fore_color;

  /* Quartz 2D background color.  */
  CGColorRef cg_back_color;

  /* Data consisting of clipping rectangles used in Quartz 2D drawing.
     The y-coordinate is in the flipped coordinates.  */
  CGRect clip_rects[MAC_MAX_CLIP_RECTS];
  int num_clip_rects;
} *GC;

#define GCForeground            (1L<<2)
#define GCBackground            (1L<<3)
#define GCFillStyle		(1L<<8)
#define GCStipple		(1L<<11)
#define GCGraphicsExposures	0
#define GCBackgroundTransparency (1L<<16)

#define FillSolid		0
#define FillTiled		1
#define FillStippled		2
#define FillOpaqueStippled	3

/* Bit Gravity */

#define ForgetGravity		0
#define NorthWestGravity	1
#define NorthGravity		2
#define NorthEastGravity	3
#define WestGravity		4
#define CenterGravity		5
#define EastGravity		6
#define SouthWestGravity	7
#define SouthGravity		8
#define SouthEastGravity	9
#define StaticGravity		10

#define NoValue		0x0000
#define XValue  	0x0001
#define YValue		0x0002
#define WidthValue  	0x0004
#define HeightValue  	0x0008
#define AllValues 	0x000F
#define XNegative 	0x0010
#define YNegative 	0x0020

typedef struct {
	long flags;	/* marks which fields in this structure are defined */
#if 0
	int x, y;		/* obsolete for new window mgrs, but clients */
	int width, height;	/* should set so old wm's don't mess up */
#endif
	int min_width, min_height;
#if 0
	int max_width, max_height;
#endif
	int width_inc, height_inc;
#if 0
	struct {
		int x;	/* numerator */
		int y;	/* denominator */
	} min_aspect, max_aspect;
#endif
	int base_width, base_height;		/* added by ICCCM version 1 */
#if 0
	int win_gravity;			/* added by ICCCM version 1 */
#endif
} XSizeHints;

#define USPosition	(1L << 0) /* user specified x, y */
#define USSize		(1L << 1) /* user specified width, height */

#define PPosition	(1L << 2) /* program specified position */
#define PSize		(1L << 3) /* program specified size */
#define PMinSize	(1L << 4) /* program specified minimum size */
#define PMaxSize	(1L << 5) /* program specified maximum size */
#define PResizeInc	(1L << 6) /* program specified resize increments */
#define PAspect		(1L << 7) /* program specified min and max aspect ratios */
#define PBaseSize	(1L << 8) /* program specified base for incrementing */
#define PWinGravity	(1L << 9) /* program specified window gravity */

/* Constants corresponding to window state hint atoms in X11 Extended
   Window Manager Hints (without "_NET_" prefix).  Partly implemented.  */

enum
{
  WM_STATE_MODAL		= 1 << 0,
  WM_STATE_STICKY		= 1 << 1,
  WM_STATE_MAXIMIZED_VERT	= 1 << 2,
  WM_STATE_MAXIMIZED_HORZ	= 1 << 3,
  WM_STATE_SHADED		= 1 << 4,
  WM_STATE_SKIP_TASKBAR		= 1 << 5,
  WM_STATE_SKIP_PAGER		= 1 << 6,
  WM_STATE_HIDDEN		= 1 << 7,
  WM_STATE_FULLSCREEN		= 1 << 8,
  WM_STATE_ABOVE		= 1 << 9,
  WM_STATE_BELOW		= 1 << 10,
  WM_STATE_DEMANDS_ATTENTION	= 1 << 11
};

/* These are not derived from X11 EWMH window state hints, but used
   like them.  */
enum
{
  WM_STATE_NO_MENUBAR		= 1 << 12,
  WM_STATE_DEDICATED_DESKTOP	= 1 << 13,
  WM_STATE_OVERRIDE_REDIRECT	= 1 << 14
};

typedef uint32_t WMState;

typedef struct {
  int x, y;
  int width, height;   /* signed so as to avoid unexpected promotion
			  to unsigned (e.g., r.x + r.width).  */
} SignedRectangle;

#define NativeRectangle SignedRectangle

#define STORE_NATIVE_RECT(nr,rx,ry,rwidth,rheight)	\
  ((nr).x = (rx),					\
   (nr).y = (ry),					\
   (nr).width = (rwidth),				\
   (nr).height = (rheight))

enum {
  CFOBJECT_TO_LISP_WITH_TAG			= 1 << 0,
  CFOBJECT_TO_LISP_DONT_DECODE_STRING		= 1 << 1,
  CFOBJECT_TO_LISP_DONT_DECODE_DICTIONARY_KEY	= 1 << 2
};

#define DIALOG_LEFT_MARGIN (112)
#define DIALOG_TOP_MARGIN (24)
#define DIALOG_RIGHT_MARGIN (24)
#define DIALOG_BOTTOM_MARGIN (20)
#define DIALOG_MIN_INNER_WIDTH (338)
#define DIALOG_MAX_INNER_WIDTH (564)
#define DIALOG_BUTTON_BUTTON_HORIZONTAL_SPACE (12)
#define DIALOG_BUTTON_BUTTON_VERTICAL_SPACE (12)
#define DIALOG_BUTTON_MIN_WIDTH (68)
#define DIALOG_TEXT_MIN_HEIGHT (50)
#define DIALOG_TEXT_BUTTONS_VERTICAL_SPACE (10)
#define DIALOG_ICON_WIDTH (64)
#define DIALOG_ICON_HEIGHT (64)
#define DIALOG_ICON_LEFT_MARGIN (24)
#define DIALOG_ICON_TOP_MARGIN (15)

#endif /* EMACS_MACGUI_H */
