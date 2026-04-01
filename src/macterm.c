/* Implementation of GUI terminal on macOS.
   Copyright (C) 2000-2008  Free Software Foundation, Inc.
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

#include <config.h>

#include "lisp.h"
#include "blockinput.h"
#include "mac_arena_draw.h"
#include "sysstdio.h"

#include "macterm.h"

#include "systime.h"

#include <errno.h>
#include <sys/stat.h>

#include "character.h"
#include "coding.h"
#include "frame.h"
#include "dispextern.h"
#include "xwidget.h"
#include "fontset.h"
#include "termhooks.h"
#include "termopts.h"
#include "termchar.h"
#include "buffer.h"
#include "window.h"
#include "keyboard.h"
#include "atimer.h"
#include "font.h"
#include "menu.h"



/* This is a chain of structures for all the X displays currently in
   use.  */

struct mac_display_info *x_display_list;

/* This is a list of X Resource Database equivalents, each of which is
   implemented with a Lisp object.  They are stored in parallel with
   x_display_list.  */

static Lisp_Object x_display_rdb_list;

/* This is display since Mac does not support multiple ones.  */
struct mac_display_info one_mac_display_info;

/* The keysyms to use for the various modifiers.  */

static struct terminal *mac_create_terminal (struct mac_display_info *dpyinfo);
static void mac_raise_frame (struct frame *);
static void mac_lower_frame (struct frame *);
static void mac_frame_rehighlight_1 (struct mac_display_info *);

static void mac_clip_to_row (struct window *, struct glyph_row *,
			     enum glyph_row_area, GC);
static void mac_check_fullscreen (struct frame *);
static void mac_initialize (void);

static void mac_set_background_and_transparency (GC, unsigned long,
						 unsigned char);
static void mac_set_fill_style (GC, int);
static void mac_set_stipple (GC, CFArrayRef);

/* Fringe bitmaps.  */

static int max_fringe_bmp = 0;
static CGImageRef *fringe_bmp = 0;

CGColorSpaceRef mac_cg_color_space_rgb;
static CGColorRef mac_cg_color_black;

static void
init_cg_color (void)
{
  mac_cg_color_space_rgb = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
  {
    CGFloat rgba[] = {0.0f, 0.0f, 0.0f, 1.0f};

    mac_cg_color_black = CGColorCreate (mac_cg_color_space_rgb, rgba);
  }
}

void
mac_begin_scale_mismatch_detection (struct frame *f)
{
  FRAME_SCALE_MISMATCH_STATE (f) = FRAME_BACKING_SCALE_FACTOR (f);
}

static void
mac_detect_scale_mismatch (struct frame *f, int target_backing_scale)
{
  FRAME_SCALE_MISMATCH_STATE (f) |= target_backing_scale;
}

bool
mac_end_scale_mismatch_detection (struct frame *f)
{
  return FRAME_SCALE_MISMATCH_STATE (f) == (1|2);
}

/* X display function emulation */

static void
mac_erase_rectangle (struct frame *f, GC gc, int x, int y,
		     int width, int height, bool respect_alpha_background)
{
  CGRect rect = CGRectMake (x, y, width, height);

  mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);
  mac_record_erase_bg (f, gc, rect, respect_alpha_background);

  if (gc->xgcv.fill_style == FillOpaqueStippled && gc->xgcv.stipple)
    {
      MAC_ARENA_CMD (cmd, f, DRAW_STIPPLE, rect);
      MAC_ARENA_RETAIN (cmd->stipple.color, gc->cg_fore_color);
      MAC_ARENA_RETAIN (cmd->stipple.stipple, gc->xgcv.stipple);
    }
}

/* Mac version of XClearArea.  */

void
mac_clear_area (struct frame *f, int x, int y, int width, int height)
{
  mac_erase_rectangle (f, f->output_data.mac->normal_gc, x, y, width, height,
		       true);
}

/* Mac version of XClearWindow.  */

static void
mac_clear_window (struct frame *f)
{
  mac_clear_area (f, 0, 0, FRAME_PIXEL_WIDTH (f), FRAME_PIXEL_HEIGHT (f));
}

/* Mac replacement for XCopyArea.  */

#define MAC_DRAW_CG_IMAGE_OVERLAY		(1 << 0)
#define MAC_DRAW_CG_IMAGE_2X			(1 << 1)
#define MAC_DRAW_CG_IMAGE_NO_INTERPOLATION	(1 << 2)

static void
mac_draw_cg_image (struct frame *f, GC gc,
		   CGImageRef image, CGAffineTransform const *transform,
		   int src_x, int src_y, int width, int height,
		   int dest_x, int dest_y, int flags)
{
  CGRect dest_rect = CGRectMake (dest_x, dest_y, width, height);
  CGRect bounds;

  mac_arena *arena = mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);

  if (!(flags & MAC_DRAW_CG_IMAGE_OVERLAY))
    mac_record_erase_bg (f, gc, dest_rect, true);
  
  if (!(flags & MAC_DRAW_CG_IMAGE_2X))
    bounds = CGRectMake (dest_x - src_x, dest_y - src_y,
			 CGImageGetWidth (image),
			 CGImageGetHeight (image));
  else
    bounds = CGRectMake (dest_x - src_x, dest_y - src_y,
			 CGImageGetWidth (image) / 2.,
			 CGImageGetHeight (image) / 2.);

  MAC_ARENA_CMD (cmd, f, DRAW_IMAGE, dest_rect);
  MAC_ARENA_RETAIN (cmd->image.image, image);

  if (CGImageIsMask (image))
    MAC_ARENA_RETAIN (cmd->image.fill_color, gc->cg_fore_color);

  cmd->image.bounds = bounds;
  cmd->image.no_interpolation_p = !!(flags & MAC_DRAW_CG_IMAGE_NO_INTERPOLATION);

  if (transform)
    {
      cmd->image.transform = mac_arena_data_alloc (arena, sizeof (CGAffineTransform));
      *cmd->image.transform = *transform;
    }
  else
    cmd->image.transform = NULL;
}

/* Mac replacement for XCreateBitmapFromBitmapData.  */

CGImageRef
mac_create_image_mask_from_bitmap_data (const char *bits, int width, int height)
{
  static const UInt8 swap_nibble[16]
    = { 0x0, 0x8, 0x4, 0xc,    /* 0000 1000 0100 1100 */
	0x2, 0xa, 0x6, 0xe,    /* 0010 1010 0110 1110 */
	0x1, 0x9, 0x5, 0xd,    /* 0001 1001 0101 1101 */
	0x3, 0xb, 0x7, 0xf };  /* 0011 1011 0111 1111 */
  CGImageRef result = NULL;
  int bytes_per_row = (width + CHAR_BIT - 1) / CHAR_BIT;
  CFIndex length = bytes_per_row * height;
  CFMutableDataRef data = CFDataCreateMutable (NULL, length);
  CGDataProviderRef provider = NULL;

  if (data)
    {
      while (length--)
	{
	  UInt8 c = *bits++;

	  c = ~((swap_nibble[c & 0xf] << 4) | (swap_nibble[(c>>4) & 0xf]));
	  CFDataAppendBytes (data, &c, 1);
	}
      provider = CGDataProviderCreateWithCFData (data);
      CFRelease (data);
    }
  if (provider)
    {
      result = CGImageMaskCreate (width, height, 1, 1, bytes_per_row,
				  provider, NULL, 0);
      CGDataProviderRelease (provider);
    }

  return result;
}

/* Mac ARENA replacement for XFillRectangle.  */

static void
mac_fill_rectangle (struct frame *f, GC gc, int x, int y, int width, int height)
{
  CGRect rect = CGRectMake (x, y, width, height);
  mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);
  MAC_ARENA_CMD (cmd, f, FILL_RECT, rect);
  MAC_ARENA_RETAIN (cmd->fill.color, gc->cg_fore_color);
}

/* Mac ARENA replacement for XDrawRectangle: dest is a window.  */

static void
mac_draw_rectangle (struct frame *f, GC gc, int x, int y, int width, int height)
{
  CGRect rect = CGRectMake (x, y, width + 1, height + 1);
  mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);
  MAC_ARENA_CMD (cmd, f, STROKE_RECT, rect);
  MAC_ARENA_RETAIN (cmd->fill.color, gc->cg_fore_color);
}

static void
mac_fill_trapezoid_for_relief (struct frame *f, GC gc, int x, int y,
			       int width, int height, int top_p)
{
  CGRect rect = CGRectMake (x, y, width, height);
  mac_arena *arena = mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);
  CGPoint *points = mac_arena_data_alloc(arena, 4 * sizeof(CGPoint));
  
  points[0].x = points[1].x = CGRectGetMinX (rect);
  points[2].x = points[3].x = CGRectGetMaxX (rect);
  points[0].y = points[3].y = CGRectGetMinY (rect);
  points[1].y = points[2].y = CGRectGetMaxY (rect);
  
  if (top_p)
    points[2].x -= CGRectGetHeight (rect);
  else
    points[0].x += CGRectGetHeight (rect);

  MAC_ARENA_CMD (cmd, f, FILL_TRAPEZOID, rect);
  MAC_ARENA_RETAIN (cmd->trapezoid.color, gc->cg_fore_color);
  cmd->trapezoid.points = points;
}

static void
mac_erase_corners_for_relief (struct frame *f, GC gc, int x, int y,
			      int width, int height,
			      CGFloat radius, CGFloat margin, int corners)
{
  CGRect rect = CGRectMake (x, y, width, height);

  mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);

  MAC_ARENA_CMD (cmd, f, ERASE_CORNERS, rect);
  bool *clear_p = &cmd->corners.clear_p;
  cmd->corners.bg_color = mac_bg_color_for_gc (f, gc, true, clear_p);
  cmd->corners.clear_p = *clear_p;
  cmd->corners.radius = radius;
  cmd->corners.margin = margin;
  cmd->corners.corners = corners;
}

static void
mac_draw_horizontal_wave (struct frame *f, GC gc, int x, int y,
			  int width, int height, int wave_length)
{
  CGRect wave_clip = CGRectMake (x, y, width, height);

  mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);

  MAC_ARENA_CMD (cmd, f, DRAW_WAVE, wave_clip);
  MAC_ARENA_RETAIN (cmd->wave.color, gc->cg_fore_color);
  cmd->wave.wave_length = wave_length;
  cmd->wave.line_width = (FLOATP (Vmac_underwave_thickness)
                          ? XFLOAT_DATA (Vmac_underwave_thickness)
                          : 0);
}

/* static void */
/* mac_invert_rectangle (struct frame *f, CGRect rect) */
/* { */
/*   GC gc = f->output_data.mac->normal_gc; */
/*   mac_ensure_arena (f); */
/*   mac_record_gc_clip (f, gc); */
/*   MAC_ARENA_CMD (cmd, f, INVERT_RECT, rect); */
/* } */

/* Mac replacement for XChangeGC.  */

static void
mac_change_gc (GC gc, unsigned long mask, XGCValues *xgcv)
{
  if (mask & GCForeground)
    mac_set_foreground (gc, xgcv->foreground);
  if (mask & GCBackground)
    mac_set_background_and_transparency (gc, xgcv->background,
					 ((mask & GCBackgroundTransparency)
					  ? xgcv->background_transparency
					  : gc->xgcv.background_transparency));
  else if (mask & GCBackgroundTransparency)
    /* This case does not happen in the current code.  */
    mac_set_background_and_transparency (gc, gc->xgcv.background,
					 xgcv->background_transparency);
  if (mask & GCFillStyle)
    mac_set_fill_style (gc, xgcv->fill_style);
  if (mask & GCStipple)
    mac_set_stipple (gc, xgcv->stipple);
}

/* Mac replacement for XCreateGC.  */

GC
mac_create_gc (unsigned long mask, XGCValues *xgcv)
{
  GC gc = xzalloc (sizeof (*gc));

  gc->cg_fore_color = gc->cg_back_color = mac_cg_color_black;
  CGColorRetain (gc->cg_fore_color);
  CGColorRetain (gc->cg_back_color);
  mac_change_gc (gc, mask, xgcv);

  return gc;
}

/* Used in xfaces.c.  */

void
mac_free_gc (GC gc)
{
  CGColorRelease (gc->cg_fore_color);
  CGColorRelease (gc->cg_back_color);
  if (gc->xgcv.stipple)
    CFRelease (gc->xgcv.stipple);
#if defined (XMALLOC_BLOCK_INPUT_CHECK) && DRAWING_USE_GCD
  /* Don't use xfree here, because this might be called in a non-main
     thread.  */
  free (gc);
#else
  xfree (gc);
#endif
}

/* Mac replacement for XGetGCValues.  */

static void
mac_get_gc_values (GC gc, unsigned long mask, XGCValues *xgcv)
{
  if (mask & GCForeground)
    xgcv->foreground = gc->xgcv.foreground;
  if (mask & GCBackground)
    xgcv->background = gc->xgcv.background;
  if (mask & GCBackgroundTransparency)
    xgcv->background_transparency = gc->xgcv.background_transparency;
  if (mask & GCFillStyle)
    xgcv->fill_style = gc->xgcv.fill_style;
  if (mask & GCStipple)
    xgcv->stipple = gc->xgcv.stipple;
}

static CGColorRef
mac_cg_color_create (unsigned long color, unsigned char transparency)
{
  if (color == 0 && transparency == 0)
    return CGColorRetain (mac_cg_color_black);
  else
    {
      CGFloat rgba[4];

      rgba[0] = (CGFloat) RED_FROM_ULONG (color) / 255.0f;
      rgba[1] = (CGFloat) GREEN_FROM_ULONG (color) / 255.0f;
      rgba[2] = (CGFloat) BLUE_FROM_ULONG (color) / 255.0f;
      rgba[3] = (CGFloat) (255 - transparency) / 255.0f;

      return CGColorCreate (mac_cg_color_space_rgb, rgba);
    }
}

/* Mac replacement for XSetForeground.  */

void
mac_set_foreground (GC gc, unsigned long color)
{
  if (gc->xgcv.foreground != color)
    {
      gc->xgcv.foreground = color;
      CGColorRelease (gc->cg_fore_color);
      gc->cg_fore_color = mac_cg_color_create (color, 0);
    }
}

static void
mac_set_background_and_transparency (GC gc, unsigned long color,
				     unsigned char transparency)
{
  if (gc->xgcv.background != color
      || gc->xgcv.background_transparency != transparency)
    {
      gc->xgcv.background = color;
      gc->xgcv.background_transparency = transparency;
      CGColorRelease (gc->cg_back_color);
      gc->cg_back_color = mac_cg_color_create (color, transparency);
    }
}

/* Mac replacement for XSetBackground.  */

void
mac_set_background (GC gc, unsigned long color)
{
  mac_set_background_and_transparency (gc, color,
				       gc->xgcv.background_transparency);
}

/* Mac replacement for XSetClipRectangles.  */

static void
mac_set_clip_rectangles (struct frame *f, GC gc,
			 NativeRectangle *rectangles, int n)
{
  if (n > MAC_MAX_CLIP_RECTS)
    n = MAC_MAX_CLIP_RECTS;

  gc->num_clip_rects = n;

  for (int i = 0; i < n; i++)
    {
      NativeRectangle *rect = rectangles + i;
      gc->clip_rects[i] = CGRectMake (rect->x, rect->y, rect->width, rect->height);
    }
}

/* Mac replacement for XSetClipMask.  */

static void
mac_reset_clip_rectangles (struct frame *f, GC gc)
{
  gc->num_clip_rects = 0;
}

/* Mac replacement for XSetFillStyle.  */

static void
mac_set_fill_style (GC gc, int fill_style)
{
  gc->xgcv.fill_style = fill_style;
}

/* Mac replacement for XSetStipple.  */

static void
mac_set_stipple (GC gc, CFArrayRef stipple)
{
  if (gc->xgcv.stipple)
    CFRelease (gc->xgcv.stipple);
  gc->xgcv.stipple = stipple;
  if (gc->xgcv.stipple)
    CFRetain (gc->xgcv.stipple);
}


/* Remove calls to XFlush by defining XFlush to an empty replacement.
   Calls to XFlush should be unnecessary because the X output buffer
   is flushed automatically as needed by calls to XPending,
   XNextEvent, or XWindowEvent according to the XFlush man page.
   mac_read_socket calls XPending.  Removing XFlush improves
   performance.  */

#define XFlush(DISPLAY)	(void) 0


static void
mac_set_frame_alpha (struct frame *f)
{
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  double alpha = 1.0;
  double alpha_min = 1.0;

  if (dpyinfo->highlight_frame == f)
    alpha = f->alpha[0];
  else
    alpha = f->alpha[1];

  if (alpha < 0.0)
    return;

  if (FLOATP (Vframe_alpha_lower_limit))
    alpha_min = XFLOAT_DATA (Vframe_alpha_lower_limit);
  else if (FIXNUMP (Vframe_alpha_lower_limit))
    alpha_min = (XFIXNUM (Vframe_alpha_lower_limit)) / 100.0;

  if (alpha > 1.0)
    alpha = 1.0;
  else if (alpha < alpha_min && alpha_min <= 1.0)
    alpha = alpha_min;

  mac_set_frame_window_alpha (f, alpha);
}


/***********************************************************************
		    Starting and ending an update
 ***********************************************************************/

/* Start an update of frame F.  This function is installed as a hook
   for update_begin, i.e. it is called when update_begin is called.
   This function is called prior to calls to gui_update_window_begin for
   each window being updated.  */

static void
mac_update_begin (struct frame *f)
{
  /* We do not open a draw session, since sometimes update begin/end
     arrive without any draw commands being submitted. Each draw command
     opens an arena if necessary, on demand. */
}

/* Start update of window W.  */

static void
mac_update_window_begin (struct window *w)
{
  w->being_updated_p = true;
}

/* Return GC for the face with FACE_ID on frame F.  If the face is not
   available, return DEFAULT_GC.  */

GC
mac_gc_for_face_id (struct frame *f, int face_id, GC default_gc)
{
  struct face *face = FACE_FROM_ID_OR_NULL (f, face_id);

  if (face)
    {
      prepare_face_for_display (f, face);
      return face->gc;
    }
  else
    return default_gc;
}

/* Draw a vertical window border from (x,y0) to (x,y1)  */

static void
mac_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  GC gc = mac_gc_for_face_id (f, VERTICAL_BORDER_FACE_ID,
			      f->output_data.mac->normal_gc);

  mac_fill_rectangle (f, gc, x, y0, 1, y1 - y0);
}

/* Draw a window divider from (x0,y0) to (x1,y1)  */

static void
mac_draw_window_divider (struct window *w, int x0, int x1, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  GC gc = mac_gc_for_face_id (f, WINDOW_DIVIDER_FACE_ID,
			      f->output_data.mac->normal_gc);

  if (min (x1 - x0, y1 - y0) >= 3)
    {
      GC gc_first = mac_gc_for_face_id (f, WINDOW_DIVIDER_FIRST_PIXEL_FACE_ID,
					f->output_data.mac->normal_gc);
      GC gc_last = mac_gc_for_face_id (f, WINDOW_DIVIDER_LAST_PIXEL_FACE_ID,
				       f->output_data.mac->normal_gc);

      if (y1 - y0 > x1 - x0)
	/* A vertical divider, at least three pixels wide: Draw first
	   and last pixels differently.  */
	{
	  mac_fill_rectangle (f, gc_first, x0, y0, 1, y1 - y0);
	  mac_fill_rectangle (f, gc, x0 + 1, y0, x1 - x0 - 2, y1 - y0);
	  mac_fill_rectangle (f, gc_last, x1 - 1, y0, 1, y1 - y0);
	}
      else
	/* A horizontal divider, at least three pixels high: Draw
	   first and last pixels differently.  */
	{
	  mac_fill_rectangle (f, gc_first, x0, y0, x1 - x0, 1);
	  mac_fill_rectangle (f, gc, x0, y0 + 1, x1 - x0, y1 - y0 - 2);
	  mac_fill_rectangle (f, gc_last, x0, y1 - 1, x1 - x0, 1);
	}
    }
  else
    /* In any other case do not draw the first and last pixels
       differently.  */
    mac_fill_rectangle (f, gc, x0, y0, x1 - x0, y1 - y0);
}

/* End update of window W.

   Draw vertical borders between horizontally adjacent windows, and
   display W's cursor if CURSOR_ON_P is non-zero.

   MOUSE_FACE_OVERWRITTEN_P non-zero means that some row containing
   glyphs in mouse-face were overwritten.  In that case we have to
   make sure that the mouse-highlight is properly redrawn.

   W may be a menu bar pseudo-window in case we don't have X toolkit
   support.  Such windows don't have a cursor, so don't display it
   here.  */

static void
mac_update_window_end (struct window *w, bool cursor_on_p,
		     bool mouse_face_overwritten_p)
{
  if (!w->pseudo_window_p)
    {
      if (w == XWINDOW (selected_window))
	mac_update_accessibility_status (XFRAME (w->frame));
    }

  w->being_updated_p = false;
}

/* End update of frame F.  This function is installed as a hook in
   update_end.  */

static void
mac_update_end (struct frame *f)
{
  /* Mouse highlight may be displayed again.  */
  MOUSE_HL_INFO (f)->mouse_face_defer = false;

  block_input ();
  mac_draw_session_end (f, MAC_SESSION_UPDATE);
  XFlush (FRAME_MAC_DISPLAY (f));
  unblock_input ();
}

/* This function is called from various places in xdisp.c
   whenever a complete update has been performed.  */

static void
mac_frame_up_to_date (struct frame *f)
{
  if (FRAME_MAC_P (f))
    {
      FRAME_MOUSE_UPDATE (f);
    }
}

/* Clear under internal border if any. */

void
mac_clear_under_internal_border (struct frame *f)
{
  if (FRAME_INTERNAL_BORDER_WIDTH (f) > 0)
    {
      int border = FRAME_INTERNAL_BORDER_WIDTH (f);
      int width = FRAME_PIXEL_WIDTH (f);
      int height = FRAME_PIXEL_HEIGHT (f);
      int margin = FRAME_TOP_MARGIN_HEIGHT (f);
      int face_id =
	(FRAME_PARENT_FRAME (f)
	 ? (!NILP (Vface_remapping_alist)
	    ? lookup_basic_face (NULL, f, CHILD_FRAME_BORDER_FACE_ID)
	    : CHILD_FRAME_BORDER_FACE_ID)
	 : (!NILP (Vface_remapping_alist)
	    ? lookup_basic_face (NULL, f, INTERNAL_BORDER_FACE_ID)
	    : INTERNAL_BORDER_FACE_ID));
      GC gc = mac_gc_for_face_id (f, face_id, f->output_data.mac->normal_gc);

      mac_erase_rectangle (f, gc, 0, 0, border, height, true);
      mac_erase_rectangle (f, gc, 0, margin, width, border, true);
      mac_erase_rectangle (f, gc, width - border, 0, border, height, true);
      mac_erase_rectangle (f, gc, 0, height - border, width, border, true);
    }
}

/* Draw truncation mark bitmaps, continuation mark bitmaps, overlay
   arrow bitmaps, or clear the fringes if no bitmaps are required
   before DESIRED_ROW is made current.  This function is called from
   update_window_line only if it is known that there are differences
   between bitmaps to be drawn between current row and DESIRED_ROW.  */

static void
mac_after_update_window_line (struct window *w, struct glyph_row *desired_row)
{
  eassert (w);

  if (!desired_row->mode_line_p && !w->pseudo_window_p)
    desired_row->redraw_fringe_bitmaps_p = true;

  /* When a window has disappeared, make sure that no rest of
     full-width rows stays visible in the internal border.  Could
     check here if updated window is the leftmost/rightmost window,
     but I guess it's not worth doing since vertically split windows
     are almost never used, internal border is rarely set, and the
     overhead is very small.  */
  {
    struct frame *f;
    int width, height;

    if (windows_or_buffers_changed
	&& desired_row->full_width_p
	&& (f = XFRAME (w->frame),
	    width = FRAME_INTERNAL_BORDER_WIDTH (f),
	    width != 0)
	&& (height = desired_row->visible_height,
	    height > 0))
      {
	int y = WINDOW_TO_FRAME_PIXEL_Y (w, max (0, desired_row->y));
	int face_id =
	  (FRAME_PARENT_FRAME (f)
	   ? (!NILP (Vface_remapping_alist)
	      ? lookup_basic_face (NULL, f, CHILD_FRAME_BORDER_FACE_ID)
	      : CHILD_FRAME_BORDER_FACE_ID)
	   : (!NILP (Vface_remapping_alist)
	      ? lookup_basic_face (NULL, f, INTERNAL_BORDER_FACE_ID)
	      : INTERNAL_BORDER_FACE_ID));
	GC gc = mac_gc_for_face_id (f, face_id, f->output_data.mac->normal_gc);

	block_input ();
	mac_erase_rectangle (f, gc, 0, y, width, height, true);
	mac_erase_rectangle (f, gc, FRAME_PIXEL_WIDTH (f) - width, y,
			     width, height, true);
	unblock_input ();
      }
  }
}

static void
mac_draw_fringe_bitmap (struct window *w, struct glyph_row *row, struct draw_fringe_bitmap_params *p)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face = p->face;
  bool overlay_p = p->overlay_p;

  /* Must clip because of partially visible lines.  */
  mac_clip_to_row (w, row, ANY_AREA, face->gc);

  if (p->bx >= 0 && !overlay_p)
    {
      /* In case the same realized face is used for fringes and
	 for something displayed in the text (e.g. face `region' on
	 mono-displays, the fill style may have been changed to
	 FillSolid in x_draw_glyph_string_background.  */
      if (face->stipple)
	{
	  mac_set_fill_style (face->gc, FillOpaqueStippled);
	  row->stipple_p = true;
	}

      mac_erase_rectangle (f, face->gc, p->bx, p->by, p->nx, p->ny, true);
      /* The fringe background has already been filled.  */
      overlay_p = 1;
    }

  if (p->which
      && p->which < max_fringe_bmp
      && p->which < max_used_fringe_bitmap)
    {
      XGCValues gcv;
      int flags;

      mac_get_gc_values (face->gc, GCForeground, &gcv);
      mac_set_foreground (face->gc,
			  (p->cursor_p
			   ? (p->overlay_p ? face->background
			      : f->output_data.mac->cursor_pixel)
			   : face->foreground));
      if (!fringe_bmp[p->which])
	{
	  /* This fringe bitmap is known to fringe.c, but lacks the
	     CGImage data which shadows that bitmap.  This is typical
	     to define-fringe-bitmap being called when the selected
	     frame was not a GUI frame, for example, when packages
	     that define fringe bitmaps are loaded by a daemon Emacs.
	     Create the missing CGImage now.  */
	  gui_define_fringe_bitmap (f, p->which);
	}
      flags = overlay_p ? MAC_DRAW_CG_IMAGE_OVERLAY : 0;
      if (FRAME_BACKING_SCALE_FACTOR (f) != 1)
	flags |= MAC_DRAW_CG_IMAGE_NO_INTERPOLATION;
      mac_draw_cg_image (f, face->gc, fringe_bmp[p->which], NULL, 0, p->dh,
			 p->wd, p->h, p->x, p->y, flags);
      mac_set_foreground (face->gc, gcv.foreground);
    }

  mac_reset_clip_rectangles (f, face->gc);
}

static void
mac_define_fringe_bitmap (int which, unsigned short *bits, int h, int wd)
{
  int i;
  CGDataProviderRef provider;

  if (which >= max_fringe_bmp)
    {
      i = max_fringe_bmp;
      max_fringe_bmp = which + 20;
      fringe_bmp = xrealloc (fringe_bmp, max_fringe_bmp * sizeof (*fringe_bmp));
      while (i < max_fringe_bmp)
	fringe_bmp[i++] = 0;
    }

  for (i = 0; i < h; i++)
    bits[i] = ~bits[i];

  block_input ();

  provider = CGDataProviderCreateWithData (NULL, bits,
					   sizeof (unsigned short) * h, NULL);
  if (provider)
    {
      fringe_bmp[which] = CGImageMaskCreate (wd, h, 1, 1,
					     sizeof (unsigned short),
					     provider, NULL, 0);
      CGDataProviderRelease (provider);
    }

  unblock_input ();
}

static void
mac_destroy_fringe_bitmap (int which)
{
  if (which >= max_fringe_bmp)
    return;

  if (fringe_bmp[which])
    {
      block_input ();
      CGImageRelease (fringe_bmp[which]);
      unblock_input ();
    }
  fringe_bmp[which] = 0;
}

/***********************************************************************
			    Glyph display
 ***********************************************************************/

static void mac_scroll_bar_clear (struct frame *);

#ifdef GLYPH_DEBUG
static void mac_check_font (struct frame *, struct font *);
#endif


/* Set S->gc to a suitable GC for drawing glyph string S in cursor
   face.  */

static void
mac_set_cursor_gc (struct glyph_string *s)
{
  if (s->font == FRAME_FONT (s->f)
      && s->face->background == FRAME_BACKGROUND_PIXEL (s->f)
      && s->face->foreground == FRAME_FOREGROUND_PIXEL (s->f)
      && !s->cmp)
    s->gc = s->f->output_data.mac->cursor_gc;
  else
    {
      /* Cursor on non-default face: must merge.  */
      XGCValues xgcv;
      unsigned long mask;

      xgcv.background = s->f->output_data.mac->cursor_pixel;
      xgcv.foreground = s->face->background;

      /* If the glyph would be invisible, try a different foreground.  */
      if (xgcv.foreground == xgcv.background)
	xgcv.foreground = s->face->foreground;
      if (xgcv.foreground == xgcv.background)
	xgcv.foreground = s->f->output_data.mac->cursor_foreground_pixel;
      if (xgcv.foreground == xgcv.background)
	xgcv.foreground = s->face->foreground;

      /* Make sure the cursor is distinct from text in this face.  */
      if (xgcv.background == s->face->background
	  && xgcv.foreground == s->face->foreground)
	{
	  xgcv.background = s->face->foreground;
	  xgcv.foreground = s->face->background;
	}

      IF_DEBUG (mac_check_font (s->f, s->font));
      mask = GCForeground | GCBackground;

      if (FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc)
	mac_change_gc (FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc,
		       mask, &xgcv);
      else
	FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc
	  = mac_create_gc (mask, &xgcv);

      s->gc = FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc;
    }
}


/* Set up S->gc of glyph string S for drawing text in mouse face.  */

static void
mac_set_mouse_face_gc (struct glyph_string *s)
{
  if (s->font == s->face->font)
    s->gc = s->face->gc;
  else
    {
      /* Otherwise construct scratch_cursor_gc with values from FACE
	 except for FONT.  */
      XGCValues xgcv;
      unsigned long mask;

      xgcv.background = s->face->background;
      xgcv.foreground = s->face->foreground;
      mask = GCForeground | GCBackground;

      if (FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc)
	mac_change_gc (FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc,
		       mask, &xgcv);
      else
	FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc
	  = mac_create_gc (mask, &xgcv);

      s->gc = FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc;
    }

  eassert (s->gc != 0);
}


/* Set S->gc of glyph string S to a GC suitable for drawing a mode line.
   Faces to use in the mode line have already been computed when the
   matrix was built, so there isn't much to do, here.  */

static void
mac_set_mode_line_face_gc (struct glyph_string *s)
{
  s->gc = s->face->gc;
}


/* Set S->gc of glyph string S for drawing that glyph string.  Set
   S->stippled_p to a non-zero value if the face of S has a stipple
   pattern.  */

static void
mac_set_glyph_string_gc (struct glyph_string *s)
{
  prepare_face_for_display (s->f, s->face);

  if (s->hl == DRAW_NORMAL_TEXT)
    {
      s->gc = s->face->gc;
      s->stippled_p = s->face->stipple > 0;
    }
  else if (s->hl == DRAW_INVERSE_VIDEO)
    {
      mac_set_mode_line_face_gc (s);
      s->stippled_p = s->face->stipple > 0;
    }
  else if (s->hl == DRAW_CURSOR)
    {
      mac_set_cursor_gc (s);
      s->stippled_p = false;
    }
  else if (s->hl == DRAW_MOUSE_FACE)
    {
      mac_set_mouse_face_gc (s);
      s->stippled_p = s->face->stipple > 0;
    }
  else if (s->hl == DRAW_IMAGE_RAISED
	   || s->hl == DRAW_IMAGE_SUNKEN)
    {
      s->gc = s->face->gc;
      s->stippled_p = s->face->stipple > 0;
    }
  else
    emacs_abort ();

  /* GC must have been set.  */
  eassert (s->gc != 0);
}


/* Set clipping for output of glyph string S.  S may be part of a mode
   line or menu if we don't have X toolkit support.  */

static void
mac_set_glyph_string_clipping (struct glyph_string *s)
{
  NativeRectangle *r = s->clip;
  int n = get_glyph_string_clip_rects (s, r, 2);

  mac_set_clip_rectangles (s->f, s->gc, r, n);
  s->num_clips = n;
}


/* Set SRC's clipping for output of glyph string DST.  This is called
   when we are drawing DST's left_overhang or right_overhang only in
   the area of SRC.  */

static void
mac_set_glyph_string_clipping_exactly (struct glyph_string *src, struct glyph_string *dst)
{
  NativeRectangle r;

  STORE_NATIVE_RECT (r, src->x, src->y, src->width, src->height);
  dst->clip[0] = r;
  dst->num_clips = 1;
  mac_set_clip_rectangles (dst->f, dst->gc, &r, 1);
}


/* RIF:
   Compute left and right overhang of glyph string S.  */

static void
mac_compute_glyph_string_overhangs (struct glyph_string *s)
{
  if (s->cmp == NULL
      && (s->first_glyph->type == CHAR_GLYPH
	  || s->first_glyph->type == COMPOSITE_GLYPH))
    {
      struct font_metrics metrics;

      if (s->first_glyph->type == CHAR_GLYPH)
	{
	  struct font *font = s->font;

	  font->driver->text_extents (font, s->char2b, s->nchars, &metrics);
	}
      else
	{
	  Lisp_Object gstring = composition_gstring_from_id (s->cmp_id);

	  composition_gstring_width (gstring, s->cmp_from, s->cmp_to, &metrics);
	}
      s->right_overhang = (metrics.rbearing > metrics.width
			   ? metrics.rbearing - metrics.width : 0);
      s->left_overhang = metrics.lbearing < 0 ? - metrics.lbearing : 0;
    }
  else if (s->cmp)
    {
      s->right_overhang = s->cmp->rbearing - s->cmp->pixel_width;
      s->left_overhang = - s->cmp->lbearing;
    }
}


/* Fill rectangle X, Y, W, H with background color of glyph string S.  */

static void
mac_clear_glyph_string_rect (struct glyph_string *s, int x, int y, int w, int h)
{
  mac_erase_rectangle (s->f, s->gc, x, y, w, h, s->hl != DRAW_CURSOR);
}


/* Draw the background of glyph_string S.  If S->background_filled_p
   is non-zero don't draw it.  FORCE_P non-zero means draw the
   background even if it wouldn't be drawn normally.  This is used
   when a string preceding S draws into the background of S, or S
   contains the first component of a composition.  */

static void
mac_draw_glyph_string_background (struct glyph_string *s, bool force_p)
{
  /* Nothing to do if background has already been drawn or if it
     shouldn't be drawn in the first place.  */
  if (!s->background_filled_p)
    {
      int box_line_width = max (s->face->box_horizontal_line_width, 0);

      if (s->stippled_p)
	{
	  /* Fill background with a stipple pattern.  */
	  mac_set_fill_style (s->gc, FillOpaqueStippled);
	  mac_erase_rectangle (s->f, s->gc, s->x, s->y + box_line_width,
			       s->background_width,
			       s->height - 2 * box_line_width,
			       s->hl != DRAW_CURSOR);
	  mac_set_fill_style (s->gc, FillSolid);
	  s->background_filled_p = true;
	}
      else
        if (FONT_HEIGHT (s->font) < s->height - 2 * box_line_width
	       /* When xdisp.c ignores FONT_HEIGHT, we cannot trust
		  font dimensions, since the actual glyphs might be
		  much smaller.  So in that case we always clear the
		  rectangle with background color.  */
	       || FONT_TOO_HIGH (s->font)
	       || s->font_not_found_p
	       || s->extends_to_end_of_line_p
	       || force_p)
	{
	  mac_clear_glyph_string_rect (s, s->x, s->y + box_line_width,
				       s->background_width,
				       s->height - 2 * box_line_width);
	  s->background_filled_p = true;
	}
    }
}


/* Draw the foreground of glyph string S.  */

static void
mac_draw_glyph_string_foreground (struct glyph_string *s)
{
  int i, x;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + max (s->face->box_vertical_line_width, 0);
  else
    x = s->x;

  /* Draw characters of S as rectangles if S's font could not be
     loaded.  */
  if (s->font_not_found_p)
    {
      for (i = 0; i < s->nchars; ++i)
	{
	  struct glyph *g = s->first_glyph + i;
	  mac_draw_rectangle (s->f, s->gc, x, s->y,
			      g->pixel_width - 1, s->height - 1);
	  x += g->pixel_width;
	}
    }
  else
    {
      struct font *font = s->font;
      int boff = font->baseline_offset;
      int y;

      if (font->vertical_centering)
	boff = VCENTER_BASELINE_OFFSET (font, s->f) - boff;

      y = s->ybase - boff;
      if (s->for_overlaps
	  || (s->background_filled_p && s->hl != DRAW_CURSOR))
	font->driver->draw (s, 0, s->nchars, x, y, false);
      else
	font->driver->draw (s, 0, s->nchars, x, y, true);
      if (s->face->overstrike)
	font->driver->draw (s, 0, s->nchars, x + 1, y, false);
    }
}

/* Draw the foreground of composite glyph string S.  */

static void
mac_draw_composite_glyph_string_foreground (struct glyph_string *s)
{
  int i, j, x;
  struct font *font = s->font;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face && s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + max (s->face->box_vertical_line_width, 0);
  else
    x = s->x;

  /* S is a glyph string for a composition.  S->cmp_from is the index
     of the first character drawn for glyphs of this composition.
     S->cmp_from == 0 means we are drawing the very first character of
     this composition.  */

  /* Draw a rectangle for the composition if the font for the very
     first character of the composition could not be loaded.  */
  if (s->font_not_found_p)
    {
      if (s->cmp_from == 0)
	mac_draw_rectangle (s->f, s->gc, x, s->y,
			    s->width - 1, s->height - 1);
    }
  else if (! s->first_glyph->u.cmp.automatic)
    {
      int y = s->ybase;

      for (i = 0, j = s->cmp_from; i < s->nchars; i++, j++)
	/* TAB in a composition means display glyphs with padding
	   space on the left or right.  */
	if (COMPOSITION_GLYPH (s->cmp, j) != '\t')
	  {
	    int xx = x + s->cmp->offsets[j * 2];
	    int yy = y - s->cmp->offsets[j * 2 + 1];

	    font->driver->draw (s, j, j + 1, xx, yy, false);
	    if (s->face->overstrike)
	      font->driver->draw (s, j, j + 1, xx + 1, yy, false);
	  }
    }
  else
    {
      Lisp_Object gstring = composition_gstring_from_id (s->cmp_id);
      Lisp_Object glyph;
      int y = s->ybase;
      int width = 0;

      for (i = j = s->cmp_from; i < s->cmp_to; i++)
	{
	  glyph = LGSTRING_GLYPH (gstring, i);
	  if (NILP (LGLYPH_ADJUSTMENT (glyph)))
	    width += LGLYPH_WIDTH (glyph);
	  else
	    {
	      int xoff, yoff, wadjust;

	      if (j < i)
		{
		  font->driver->draw (s, j, i, x, y, false);
		  if (s->face->overstrike)
		    font->driver->draw (s, j, i, x + 1, y, false);
		  x += width;
		}
	      xoff = LGLYPH_XOFF (glyph);
	      yoff = LGLYPH_YOFF (glyph);
	      wadjust = LGLYPH_WADJUST (glyph);
	      font->driver->draw (s, i, i + 1, x + xoff, y + yoff, false);
	      if (s->face->overstrike)
		font->driver->draw (s, i, i + 1, x + xoff + 1, y + yoff,
				    false);
	      x += wadjust;
	      j = i + 1;
	      width = 0;
	    }
	}
      if (j < i)
	{
	  font->driver->draw (s, j, i, x, y, false);
	  if (s->face->overstrike)
	    font->driver->draw (s, j, i, x + 1, y, false);
	}
    }
}


/* Draw the foreground of glyph string S for glyphless characters.  */

static void
mac_draw_glyphless_glyph_string_foreground (struct glyph_string *s)
{
  struct glyph *glyph = s->first_glyph;
  unsigned char2b[8];
  int x, i, j;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face && s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + max (s->face->box_vertical_line_width, 0);
  else
    x = s->x;

  s->char2b = char2b;

  for (i = 0; i < s->nchars; i++, glyph++)
    {
#ifdef GCC_LINT
      enum { PACIFY_GCC_BUG_81401 = 1 };
#else
      enum { PACIFY_GCC_BUG_81401 = 0 };
#endif
      char buf[7 + PACIFY_GCC_BUG_81401];
      char *str = NULL;
      int len = glyph->u.glyphless.len;

      if (glyph->u.glyphless.method == GLYPHLESS_DISPLAY_ACRONYM)
	{
	  if (len > 0
	      && CHAR_TABLE_P (Vglyphless_char_display)
	      && (CHAR_TABLE_EXTRA_SLOTS (XCHAR_TABLE (Vglyphless_char_display))
		  >= 1))
	    {
	      Lisp_Object acronym
		= (! glyph->u.glyphless.for_no_font
		   ? CHAR_TABLE_REF (Vglyphless_char_display,
				     glyph->u.glyphless.ch)
		   : XCHAR_TABLE (Vglyphless_char_display)->extras[0]);
	      if (CONSP (acronym))
		acronym = XCAR (acronym);
	      if (STRINGP (acronym))
		str = SSDATA (acronym);
	    }
	}
      else if (glyph->u.glyphless.method == GLYPHLESS_DISPLAY_HEX_CODE)
	{
	  unsigned int ch = glyph->u.glyphless.ch;
	  eassume (ch <= MAX_CHAR);
	  sprintf (buf, "%0*X", ch < 0x10000 ? 4 : 6, ch);
	  str = buf;
	}

      if (str)
	{
	  int upper_len = (len + 1) / 2;

	  /* It is assured that all LEN characters in STR is ASCII.  */
	  for (j = 0; j < len; j++)
            char2b[j] = s->font->driver->encode_char (s->font, str[j]) & 0xFFFF;
	  s->font->driver->draw (s, 0, upper_len,
				 x + glyph->slice.glyphless.upper_xoff,
				 s->ybase + glyph->slice.glyphless.upper_yoff,
				 false);
	  s->font->driver->draw (s, upper_len, len,
				 x + glyph->slice.glyphless.lower_xoff,
				 s->ybase + glyph->slice.glyphless.lower_yoff,
				 false);
	}
      if (glyph->u.glyphless.method != GLYPHLESS_DISPLAY_THIN_SPACE)
	mac_draw_rectangle (s->f, s->gc, x, s->ybase - glyph->ascent,
			    glyph->pixel_width - 1,
			    glyph->ascent + glyph->descent - 1);
      x += glyph->pixel_width;
   }

  /* Defend against hypothetical bad code elsewhere that uses
     s->char2b after this function returns.  */
  s->char2b = NULL;
}


/* On frame F, translate pixel colors to RGB values for the NCOLORS
   colors in COLORS.  */

void
mac_query_colors (struct frame *f, Emacs_Color *colors, int ncolors)
{
  int i;

  for (i = 0; i < ncolors; ++i)
    {
      unsigned long pixel = colors[i].pixel;

      colors[i].red = RED16_FROM_ULONG (pixel);
      colors[i].green = GREEN16_FROM_ULONG (pixel);
      colors[i].blue = BLUE16_FROM_ULONG (pixel);
    }
}

/* Store F's background color into *BGCOLOR.  */

static void
mac_query_frame_background_color (struct frame *f, Emacs_Color *bgcolor)
{
  bgcolor->pixel = FRAME_BACKGROUND_PIXEL (f);
  mac_query_colors (f, bgcolor, 1);
}

/* Brightness beyond which a color won't have its highlight brightness
   boosted.

   Nominally, highlight colors for `3d' faces are calculated by
   brightening an object's color by a constant scale factor, but this
   doesn't yield good results for dark colors, so for colors who's
   brightness is less than this value (on a scale of 0-255) have to
   use an additional additive factor.

   The value here is set so that the default menu-bar/mode-line color
   (grey75) will not have its highlights changed at all.  */
#define HIGHLIGHT_COLOR_DARK_BOOST_LIMIT 187


/* Allocate a color which is lighter or darker than *COLOR by FACTOR
   or DELTA.  Try a color with RGB values multiplied by FACTOR first.
   If this produces the same color as COLOR, try a color where all RGB
   values have DELTA added.  Return the allocated color in *COLOR.
   DISPLAY is the X display, CMAP is the colormap to operate on.
   Value is non-zero if successful.  */

static bool
mac_alloc_lighter_color (struct frame *f, unsigned long *color, double factor,
			 int delta)
{
  unsigned long new;
  long bright;

  /* On Mac, RGB values are 0-255, not 0-65535, so scale delta. */
  delta /= 256;

  /* Change RGB values by specified FACTOR.  Avoid overflow!  */
  eassert (factor >= 0);
  new = RGB_TO_ULONG (min (0xff, (int) (factor * RED_FROM_ULONG (*color))),
                    min (0xff, (int) (factor * GREEN_FROM_ULONG (*color))),
                    min (0xff, (int) (factor * BLUE_FROM_ULONG (*color))));

  /* Calculate brightness of COLOR.  */
  bright = (2 * RED_FROM_ULONG (*color) + 3 * GREEN_FROM_ULONG (*color)
            + BLUE_FROM_ULONG (*color)) / 6;

  /* We only boost colors that are darker than
     HIGHLIGHT_COLOR_DARK_BOOST_LIMIT.  */
  if (bright < HIGHLIGHT_COLOR_DARK_BOOST_LIMIT)
    /* Make an additive adjustment to NEW, because it's dark enough so
       that scaling by FACTOR alone isn't enough.  */
    {
      /* How far below the limit this color is (0 - 1, 1 being darker).  */
      double dimness = 1 - (double) bright / HIGHLIGHT_COLOR_DARK_BOOST_LIMIT;
      /* The additive adjustment.  */
      int min_delta = delta * dimness * factor / 2;

      if (factor < 1)
        new = RGB_TO_ULONG (max (0, min (0xff, (int) (RED_FROM_ULONG (new)) - min_delta)),
			    max (0, min (0xff, (int) (GREEN_FROM_ULONG (new)) - min_delta)),
			    max (0, min (0xff, (int) (BLUE_FROM_ULONG (new)) - min_delta)));
      else
        new = RGB_TO_ULONG (max (0, min (0xff, (int) (min_delta + RED_FROM_ULONG (new)))),
			    max (0, min (0xff, (int) (min_delta + GREEN_FROM_ULONG (new)))),
			    max (0, min (0xff, (int) (min_delta + BLUE_FROM_ULONG (new)))));
    }

  if (new == *color)
    new = RGB_TO_ULONG (max (0, min (0xff, (int) (delta + RED_FROM_ULONG (*color)))),
                      max (0, min (0xff, (int) (delta + GREEN_FROM_ULONG (*color)))),
                      max (0, min (0xff, (int) (delta + BLUE_FROM_ULONG (*color)))));

  /* MAC_TODO: Map to palette and retry with delta if same? */
  /* MAC_TODO: Free colors (if using palette)? */

  if (new == *color)
    return 0;

  *color = new;

  return 1;
}


/* Set up the foreground color for drawing relief lines of glyph
   string S.  RELIEF is a pointer to a struct relief containing the GC
   with which lines will be drawn.  Use a color that is FACTOR or
   DELTA lighter or darker than the relief's background which is found
   in S->f->output_data.mac->relief_background.  If such a color cannot
   be allocated, use DEFAULT_PIXEL, instead.  */

static void
mac_setup_relief_color (struct frame *f, struct relief *relief, double factor,
			int delta, unsigned long default_pixel)
{
  XGCValues xgcv;
  struct mac_output *di = f->output_data.mac;
  unsigned long mask = GCForeground;
  unsigned long pixel;
  unsigned long background = di->relief_background;
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);

  /* MAC_TODO: Free colors (if using palette)? */

  /* Allocate new color.  */
  xgcv.foreground = default_pixel;
  pixel = background;
  if (dpyinfo->n_planes != 1
      && mac_alloc_lighter_color (f, &pixel, factor, delta))
    xgcv.foreground = relief->pixel = pixel;

  if (relief->gc == 0)
    {
#if 0 /* MAC_TODO: stipple */
      xgcv.stipple = dpyinfo->gray;
      mask |= GCStipple;
#endif
      relief->gc = mac_create_gc (mask, &xgcv);
    }
  else
    mac_change_gc (relief->gc, mask, &xgcv);
}


/* Set up colors for the relief lines around glyph string S.  */

static void
mac_setup_relief_colors (struct glyph_string *s)
{
  struct mac_output *di = s->f->output_data.mac;
  unsigned long color;

  if (s->face->use_box_color_for_shadows_p)
    color = s->face->box_color;
  else if (s->first_glyph->type == IMAGE_GLYPH
	   && s->img->pixmap
	   && !IMAGE_BACKGROUND_TRANSPARENT (s->img, s->f, 0))
    color = IMAGE_BACKGROUND (s->img, s->f, 0);
  else
    {
      XGCValues xgcv;

      /* Get the background color of the face.  */
      mac_get_gc_values (s->gc, GCBackground, &xgcv);
      color = xgcv.background;
    }

  if (di->white_relief.gc == 0
      || color != di->relief_background)
    {
      di->relief_background = color;
      mac_setup_relief_color (s->f, &di->white_relief, 1.2, 0x8000,
			    WHITE_PIX_DEFAULT (s->f));
      mac_setup_relief_color (s->f, &di->black_relief, 0.6, 0x4000,
			    BLACK_PIX_DEFAULT (s->f));
    }
}


/* Draw a relief on frame F inside the rectangle given by LEFT_X,
   TOP_Y, RIGHT_X, and BOTTOM_Y.  WIDTH is the thickness of the relief
   to draw, it must be >= 0.  RAISED_P means draw a raised
   relief.  LEFT_P means draw a relief on the left side of
   the rectangle.  RIGHT_P means draw a relief on the right
   side of the rectangle.  CLIP_RECT is the clipping rectangle to use
   when drawing.  */

static void
mac_draw_relief_rect (struct frame *f, int left_x, int top_y, int right_x,
		      int bottom_y, int hwidth, int vwidth, bool raised_p,
		      bool top_p, bool bot_p, bool left_p, bool right_p,
		      NativeRectangle *clip_rect)
{
  GC top_left_gc, bottom_right_gc;
  int corners = 0;

  if (raised_p)
    {
      top_left_gc = f->output_data.mac->white_relief.gc;
      bottom_right_gc = f->output_data.mac->black_relief.gc;
    }
  else
    {
      top_left_gc = f->output_data.mac->black_relief.gc;
      bottom_right_gc = f->output_data.mac->white_relief.gc;
    }

  mac_set_clip_rectangles (f, top_left_gc, clip_rect, 1);
  mac_set_clip_rectangles (f, bottom_right_gc, clip_rect, 1);

  if (left_p)
    {
      mac_fill_rectangle (f, top_left_gc, left_x, top_y,
			  vwidth, bottom_y + 1 - top_y);
      if (top_p)
	corners |= 1 << CORNER_TOP_LEFT;
      if (bot_p)
	corners |= 1 << CORNER_BOTTOM_LEFT;
    }
  if (right_p)
    {
      mac_fill_rectangle (f, bottom_right_gc, right_x + 1 - vwidth, top_y,
			  vwidth, bottom_y + 1 - top_y);
      if (top_p)
	corners |= 1 << CORNER_TOP_RIGHT;
      if (bot_p)
	corners |= 1 << CORNER_BOTTOM_RIGHT;
    }
  if (top_p)
    {
      if (!right_p)
	mac_fill_rectangle (f, top_left_gc, left_x, top_y,
			    right_x + 1 - left_x, hwidth);
      else
	mac_fill_trapezoid_for_relief (f, top_left_gc, left_x, top_y,
				       right_x + 1 - left_x, hwidth, 1);
    }
  if (bot_p)
    {
      if (!left_p)
	mac_fill_rectangle (f, bottom_right_gc, left_x, bottom_y + 1 - hwidth,
			    right_x + 1 - left_x, hwidth);
      else
	mac_fill_trapezoid_for_relief (f, bottom_right_gc,
				       left_x, bottom_y + 1 - hwidth,
				       right_x + 1 - left_x, hwidth, 0);
    }
  if (left_p && vwidth > 1)
    mac_fill_rectangle (f, bottom_right_gc, left_x, top_y,
			1, bottom_y + 1 - top_y);
  if (top_p && hwidth > 1)
    mac_fill_rectangle (f, bottom_right_gc, left_x, top_y,
			right_x + 1 - left_x, 1);
  if (corners)
    {
      mac_set_background (top_left_gc, FRAME_BACKGROUND_PIXEL (f));
      mac_erase_corners_for_relief (f, top_left_gc, left_x, top_y,
				    right_x - left_x + 1, bottom_y - top_y + 1,
				    6, 1, corners);
    }

  mac_reset_clip_rectangles (f, top_left_gc);
  mac_reset_clip_rectangles (f, bottom_right_gc);
}


/* Draw a box on frame F inside the rectangle given by LEFT_X, TOP_Y,
   RIGHT_X, and BOTTOM_Y.  WIDTH is the thickness of the lines to
   draw, it must be >= 0.  LEFT_P means draw a line on the
   left side of the rectangle.  RIGHT_P means draw a line
   on the right side of the rectangle.  CLIP_RECT is the clipping
   rectangle to use when drawing.  */

static void
mac_draw_box_rect (struct glyph_string *s,
		   int left_x, int top_y, int right_x, int bottom_y, int hwidth,
		   int vwidth, bool left_p, bool right_p,
		   NativeRectangle *clip_rect)
{
  XGCValues xgcv;

  mac_get_gc_values (s->gc, GCForeground, &xgcv);
  mac_set_foreground (s->gc, s->face->box_color);
  mac_set_clip_rectangles (s->f, s->gc, clip_rect, 1);

  /* Top.  */
  mac_fill_rectangle (s->f, s->gc, left_x, top_y,
		      right_x - left_x + 1, hwidth);

  /* Left.  */
  if (left_p)
    mac_fill_rectangle (s->f, s->gc, left_x, top_y,
			vwidth, bottom_y - top_y + 1);

  /* Bottom.  */
  mac_fill_rectangle (s->f, s->gc, left_x, bottom_y - hwidth + 1,
		      right_x - left_x + 1, hwidth);

  /* Right.  */
  if (right_p)
    mac_fill_rectangle (s->f, s->gc, right_x - vwidth + 1,
			top_y, vwidth, bottom_y - top_y + 1);

  mac_set_foreground (s->gc, xgcv.foreground);
  mac_reset_clip_rectangles (s->f, s->gc);
}


/* Draw a box around glyph string S.  */

static void
mac_draw_glyph_string_box (struct glyph_string *s)
{
  int hwidth, vwidth, left_x, right_x, top_y, bottom_y, last_x;
  bool raised_p, left_p, right_p;
  struct glyph *last_glyph;
  NativeRectangle clip_rect;

  last_x = ((s->row->full_width_p && !s->w->pseudo_window_p)
	    ? WINDOW_RIGHT_EDGE_X (s->w)
	    : window_box_right (s->w, s->area));

  /* The glyph that may have a right box line.  For static
     compositions and images, the right-box flag is on the first glyph
     of the glyph string; for other types it's on the last glyph.  */
  if (s->cmp || s->img)
    last_glyph = s->first_glyph;
  else if (s->first_glyph->type == COMPOSITE_GLYPH
	   && s->first_glyph->u.cmp.automatic)
    {
      /* For automatic compositions, we need to look up the last glyph
	 in the composition.  */
        struct glyph *end = s->row->glyphs[s->area] + s->row->used[s->area];
	struct glyph *g = s->first_glyph;
	for (last_glyph = g++;
	     g < end && g->u.cmp.automatic && g->u.cmp.id == s->cmp_id
	       && g->slice.cmp.to < s->cmp_to;
	     last_glyph = g++)
	  ;
    }
  else
    last_glyph = s->first_glyph + s->nchars - 1;

  vwidth = eabs (s->face->box_vertical_line_width);
  hwidth = eabs (s->face->box_horizontal_line_width);
  raised_p = s->face->box == FACE_RAISED_BOX;
  left_x = s->x;
  right_x = (s->row->full_width_p && s->extends_to_end_of_line_p
	     ? last_x - 1
	     : min (last_x, s->x + s->background_width) - 1);
  top_y = s->y;
  bottom_y = top_y + s->height - 1;

  left_p = (s->first_glyph->left_box_line_p
	    || (s->hl == DRAW_MOUSE_FACE
		&& (s->prev == NULL
		    || s->prev->hl != s->hl)));
  right_p = (last_glyph->right_box_line_p
	     || (s->hl == DRAW_MOUSE_FACE
		 && (s->next == NULL
		     || s->next->hl != s->hl)));

  get_glyph_string_clip_rect (s, &clip_rect);

  if (s->face->box == FACE_SIMPLE_BOX)
    mac_draw_box_rect (s, left_x, top_y, right_x, bottom_y, hwidth,
		       vwidth, left_p, right_p, &clip_rect);
  else
    {
      mac_setup_relief_colors (s);
      mac_draw_relief_rect (s->f, left_x, top_y, right_x, bottom_y, hwidth,
			    vwidth, raised_p, true, true, left_p, right_p,
			    &clip_rect);
    }
}

/* Draw foreground of image glyph string S.  */

static void
mac_draw_image_foreground (struct glyph_string *s)
{
  int x = s->x;
  int y = s->ybase - image_ascent (s->img, s->face, &s->slice);

  /* If first glyph of S has a left box line, start drawing it to the
     right of that line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p
      && s->slice.x == 0)
    x += max (s->face->box_vertical_line_width, 0);

  /* If there is a margin around the image, adjust x- and y-position
     by that margin.  */
  if (s->slice.x == 0)
    x += s->img->hmargin;
  if (s->slice.y == 0)
    y += s->img->vmargin;

  if (s->img->pixmap)
    {
      int flags = MAC_DRAW_CG_IMAGE_OVERLAY;

      mac_set_glyph_string_clipping (s);

      mac_detect_scale_mismatch (s->f, s->img->target_backing_scale);
      if (s->img->target_backing_scale == 2)
	flags |= MAC_DRAW_CG_IMAGE_2X;
      if (!s->img->smoothing)
	flags |= MAC_DRAW_CG_IMAGE_NO_INTERPOLATION;
      mac_draw_cg_image (s->f, s->gc, s->img->cg_image, s->img->cg_transform,
			 s->slice.x, s->slice.y,
			 s->slice.width, s->slice.height, x, y, flags);
      if (!s->img->mask)
	{
	  /* When the image has a mask, we can expect that at
	     least part of a mouse highlight or a block cursor will
	     be visible.  If the image doesn't have a mask, make
	     a block cursor visible by drawing a rectangle around
	     the image.  I believe it's looking better if we do
	     nothing here for mouse-face.  */
	  if (s->hl == DRAW_CURSOR)
	    {
	      int relief = eabs (s->img->relief);
	      mac_draw_rectangle (s->f, s->gc, x - relief, y - relief,
				  s->slice.width + relief*2 - 1,
				  s->slice.height + relief*2 - 1);
	    }
	}
    }
  else
    /* Draw a rectangle if image could not be loaded.  */
    mac_draw_rectangle (s->f, s->gc, x, y,
			s->slice.width - 1, s->slice.height - 1);
}


/* Draw a relief around the image glyph string S.  */

static void
mac_draw_image_relief (struct glyph_string *s)
{
  int x1, y1, thick;
  bool raised_p, top_p, bot_p, left_p, right_p;
  int extra_x, extra_y;
  NativeRectangle r;
  int x = s->x;
  int y = s->ybase - image_ascent (s->img, s->face, &s->slice);

  /* If first glyph of S has a left box line, start drawing it to the
     right of that line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p
      && s->slice.x == 0)
    x += max (s->face->box_vertical_line_width, 0);

  /* If there is a margin around the image, adjust x- and y-position
     by that margin.  */
  if (s->slice.x == 0)
    x += s->img->hmargin;
  if (s->slice.y == 0)
    y += s->img->vmargin;

  if (s->hl == DRAW_IMAGE_SUNKEN
      || s->hl == DRAW_IMAGE_RAISED)
    {
      if (s->face->id == TAB_BAR_FACE_ID)
	thick = (tab_bar_button_relief < 0
		 ? DEFAULT_TAB_BAR_BUTTON_RELIEF
		 : min (tab_bar_button_relief, 1000000));
      else
	thick = (tool_bar_button_relief < 0
		 ? DEFAULT_TOOL_BAR_BUTTON_RELIEF
		 : min (tool_bar_button_relief, 1000000));
      raised_p = s->hl == DRAW_IMAGE_RAISED;
    }
  else
    {
      thick = eabs (s->img->relief);
      raised_p = s->img->relief > 0;
    }

  x1 = x + s->slice.width - 1;
  y1 = y + s->slice.height - 1;

  extra_x = extra_y = 0;
  if (s->face->id == TAB_BAR_FACE_ID)
    {
      if (CONSP (Vtab_bar_button_margin)
	  && FIXNUMP (XCAR (Vtab_bar_button_margin))
	  && FIXNUMP (XCDR (Vtab_bar_button_margin)))
	{
	  extra_x = XFIXNUM (XCAR (Vtab_bar_button_margin)) - thick;
	  extra_y = XFIXNUM (XCDR (Vtab_bar_button_margin)) - thick;
	}
      else if (FIXNUMP (Vtab_bar_button_margin))
	extra_x = extra_y = XFIXNUM (Vtab_bar_button_margin) - thick;
    }

  if (s->face->id == TOOL_BAR_FACE_ID)
    {
      if (CONSP (Vtool_bar_button_margin)
	  && FIXNUMP (XCAR (Vtool_bar_button_margin))
	  && FIXNUMP (XCDR (Vtool_bar_button_margin)))
	{
	  extra_x = XFIXNUM (XCAR (Vtool_bar_button_margin));
	  extra_y = XFIXNUM (XCDR (Vtool_bar_button_margin));
	}
      else if (FIXNUMP (Vtool_bar_button_margin))
	extra_x = extra_y = XFIXNUM (Vtool_bar_button_margin);
    }

  top_p = bot_p = left_p = right_p = false;

  if (s->slice.x == 0)
    x -= thick + extra_x, left_p = true;
  if (s->slice.y == 0)
    y -= thick + extra_y, top_p = true;
  if (s->slice.x + s->slice.width == s->img->width)
    x1 += thick + extra_x, right_p = true;
  if (s->slice.y + s->slice.height == s->img->height)
    y1 += thick + extra_y, bot_p = true;

  mac_setup_relief_colors (s);
  get_glyph_string_clip_rect (s, &r);
  mac_draw_relief_rect (s->f, x, y, x1, y1, thick, thick, raised_p,
			top_p, bot_p, left_p, right_p, &r);
}


/* Draw part of the background of glyph string S.  X, Y, W, and H
   give the rectangle to draw.  */

static void
mac_draw_glyph_string_bg_rect (struct glyph_string *s, int x, int y, int w, int h)
{
  if (s->stippled_p)
    {
      /* Fill background with a stipple pattern.  */
      mac_set_fill_style (s->gc, FillOpaqueStippled);
      mac_erase_rectangle (s->f, s->gc, x, y, w, h, true);
      mac_set_fill_style (s->gc, FillSolid);
    }
  else
    mac_clear_glyph_string_rect (s, x, y, w, h);
}


/* Draw image glyph string S.

            s->y
   s->x      +-------------------------
	     |   s->face->box
	     |
	     |     +-------------------------
	     |     |  s->img->margin
	     |     |
	     |     |       +-------------------
	     |     |       |  the image

 */
static void
mac_draw_image_glyph_string (struct glyph_string *s)
{
  int box_line_hwidth = max (s->face->box_vertical_line_width, 0);
  int box_line_vwidth = max (s->face->box_horizontal_line_width, 0);
  int height;
      
  height = s->height;
  if (s->slice.y == 0)
    height -= box_line_vwidth;
  if (s->slice.y + s->slice.height >= s->img->height)
    height -= box_line_vwidth;

  /* Fill background with face under the image, only if row is
     taller than image or if image has a clip mask */
  s->stippled_p = s->face->stipple > 0;
  if (height > s->slice.height
      || s->img->hmargin
      || s->img->vmargin
      || s->img->mask
      || s->img->pixmap == 0
      || s->width != s->background_width)
    {
      if (s->stippled_p)
	s->row->stipple_p = true;

      int x = s->x;
      int y = s->y;
      int width = s->background_width;

      if (s->first_glyph->left_box_line_p
	  && s->slice.x == 0)
	{
	  x += box_line_hwidth;
	  width -= box_line_hwidth;
	}

      if (s->slice.y == 0)
	y += box_line_vwidth;

      mac_draw_glyph_string_bg_rect (s, x, y, width, height);

      s->background_filled_p = true;
    }

  /* Draw the foreground.  */
  mac_draw_image_foreground (s);

  /* If we need a relief around the image, draw it. */
  if (s->img->relief
      || s->hl == DRAW_IMAGE_RAISED
      || s->hl == DRAW_IMAGE_SUNKEN)
    mac_draw_image_relief (s);
}



/* Draw stretch glyph string S.  */

static void
mac_draw_stretch_glyph_string (struct glyph_string *s)
{
  eassert (s->first_glyph->type == STRETCH_GLYPH);

  if (s->hl == DRAW_CURSOR
      && !x_stretch_cursor_p)
    {
      /* If `x-stretch-cursor' is nil, don't draw a block cursor as
	 wide as the stretch glyph.  */
      int width, background_width = s->background_width;
      int x = s->x;

      if (!s->row->reversed_p)
	{
	  int left_x = window_box_left_offset (s->w, TEXT_AREA);

	  if (x < left_x)
	    {
	      background_width -= left_x - x;
	      x = left_x;
	    }
	}
      else
	{
	  /* In R2L rows, draw the cursor on the right edge of the
	     stretch glyph.  */
	  int right_x = window_box_right (s->w, TEXT_AREA);

	  if (x + background_width > right_x)
	    background_width -= x - right_x;
	  x += background_width;
	}
      width = min (FRAME_COLUMN_WIDTH (s->f), background_width);
      if (s->row->reversed_p)
	x -= width;

      /* Draw cursor.  */
      mac_draw_glyph_string_bg_rect (s, x, s->y, width, s->height);

      /* Clear rest using the GC of the original non-cursor face.  */
      if (width < background_width)
	{
	  int y = s->y;
	  int w = background_width - width, h = s->height;
	  NativeRectangle r;
	  GC gc;

	  if (!s->row->reversed_p)
	    x += width;
	  else
	    x = s->x;
	  if (s->row->mouse_face_p
	      && cursor_in_mouse_face_p (s->w))
	    {
	      mac_set_mouse_face_gc (s);
	      gc = s->gc;
	    }
	  else
	    gc = s->face->gc;

	  get_glyph_string_clip_rect (s, &r);
	  mac_set_clip_rectangles (s->f, gc, &r, 1);

	  if (s->face->stipple > 0)
	    {
	      /* Fill background with a stipple pattern.  */
	      mac_set_fill_style (gc, FillOpaqueStippled);
	      mac_erase_rectangle (s->f, gc, x, y, w, h, true);
	      mac_set_fill_style (gc, FillSolid);

	      s->row->stipple_p = true;
	    }
	  else
	    mac_erase_rectangle (s->f, gc, x, y, w, h, true);

	  mac_reset_clip_rectangles (s->f, gc);
	}
    }
  else if (!s->background_filled_p)
    {
      int background_width = s->background_width;
      int x = s->x, text_left_x = window_box_left (s->w, TEXT_AREA);

      /* Don't draw into left fringe or scrollbar area except for
         header line and mode line.  */
      if (s->area == TEXT_AREA
	  && x < text_left_x && !s->row->mode_line_p)
	{
	  background_width -= text_left_x - x;
	  x = text_left_x;
	}

      if (!s->row->stipple_p)
	s->row->stipple_p = s->stippled_p;

      if (background_width > 0)
	mac_draw_glyph_string_bg_rect (s, x, s->y,
				       background_width, s->height);
    }

  s->background_filled_p = true;
}

/*
   Draw a wavy line under S. The wave fills wave_height pixels from y0.

                    x0         wave_length = 2
                                 --
                y0   *   *   *   *   *
                     |* * * * * * * * *
    wave_height = 3  | *   *   *   *

*/

static void
mac_draw_underwave (struct glyph_string *s, int decoration_width)
{
  int wave_height = 3, wave_length = 2;

  mac_draw_horizontal_wave (s->f, s->gc, s->x, s->ybase - wave_height + 3,
			    decoration_width, wave_height, wave_length);
}

/* Draw glyph string S.  */

static void
mac_draw_glyph_string (struct glyph_string *s)
{
  bool relief_drawn_p = false;

  /* If S draws into the background of its successors, draw the
     background of the successors first so that S can draw into it.
     This makes S->next use XDrawString instead of XDrawImageString.  */
  if (s->next && s->right_overhang && !s->for_overlaps)
    {
      int width;
      struct glyph_string *next;

      for (width = 0, next = s->next;
	   next && width < s->right_overhang;
	   width += next->width, next = next->next)
	if (next->first_glyph->type != IMAGE_GLYPH)
	  {
	    mac_set_glyph_string_gc (next);
	    mac_set_glyph_string_clipping (next);
	    if (next->first_glyph->type == STRETCH_GLYPH)
	      mac_draw_stretch_glyph_string (next);
	    else
	      mac_draw_glyph_string_background (next, true);
	    next->num_clips = 0;
	  }
    }

  /* Set up S->gc, set clipping and draw S.  */
  mac_set_glyph_string_gc (s);

  /* Draw relief (if any) in advance for char/composition so that the
     glyph string can be drawn over it.  */
  if (!s->for_overlaps
      && s->face->box != FACE_NO_BOX
      && (s->first_glyph->type == CHAR_GLYPH
	  || s->first_glyph->type == COMPOSITE_GLYPH))

    {
      mac_set_glyph_string_clipping (s);
      mac_draw_glyph_string_background (s, true);
      mac_draw_glyph_string_box (s);
      mac_set_glyph_string_clipping (s);
      relief_drawn_p = true;
    }
  else if (!s->clip_head /* draw_glyphs didn't specify a clip mask. */
	   && !s->clip_tail
	   && ((s->prev && s->prev->hl != s->hl && s->left_overhang)
	       || (s->next && s->next->hl != s->hl && s->right_overhang)))
    /* We must clip just this glyph.  left_overhang part has already
       drawn when s->prev was drawn, and right_overhang part will be
       drawn later when s->next is drawn. */
    mac_set_glyph_string_clipping_exactly (s, s);
  else
    mac_set_glyph_string_clipping (s);

  switch (s->first_glyph->type)
    {
    case IMAGE_GLYPH:
      mac_draw_image_glyph_string (s);
      break;

    case XWIDGET_GLYPH:
      x_draw_xwidget_glyph_string (s);
      break;

    case STRETCH_GLYPH:
      mac_draw_stretch_glyph_string (s);
      break;

    case CHAR_GLYPH:
      if (s->for_overlaps)
	s->background_filled_p = true;
      else
	mac_draw_glyph_string_background (s, false);
      mac_draw_glyph_string_foreground (s);
      break;

    case COMPOSITE_GLYPH:
      if (s->for_overlaps || (s->cmp_from > 0
			      && ! s->first_glyph->u.cmp.automatic))
	s->background_filled_p = true;
      else
	mac_draw_glyph_string_background (s, true);
      mac_draw_composite_glyph_string_foreground (s);
      break;

    case GLYPHLESS_GLYPH:
      if (s->for_overlaps)
	s->background_filled_p = true;
      else
	mac_draw_glyph_string_background (s, true);
      mac_draw_glyphless_glyph_string_foreground (s);
      break;

    default:
      emacs_abort ();
    }

  if (!s->for_overlaps)
    {
      int area_x, area_y, area_width, area_height;
      int area_max_x, decoration_width;

      /* Prevent the underline from overwriting surrounding areas
	 and the fringe.  */
      window_box (s->w, s->area, &area_x, &area_y,
		  &area_width, &area_height);
      area_max_x = area_x + area_width - 1;

      decoration_width = s->width;
      if (!s->row->mode_line_p
	  && !s->row->tab_line_p
	  && area_max_x < (s->x + decoration_width - 1))
	decoration_width -= (s->x + decoration_width - 1) - area_max_x;

      /* Draw relief if not yet drawn.  */
      if (!relief_drawn_p && s->face->box != FACE_NO_BOX)
	mac_draw_glyph_string_box (s);

      /* Draw underline.  */
      if (s->face->underline)
	{
	  if (s->face->underline == FACE_UNDERLINE_WAVE)
	    {
	      if (s->face->underline_defaulted_p)
		mac_draw_underwave (s, decoration_width);
	      else
		{
		  XGCValues xgcv;
		  mac_get_gc_values (s->gc, GCForeground, &xgcv);
		  mac_set_foreground (s->gc, s->face->underline_color);
		  mac_draw_underwave (s, decoration_width);
		  mac_set_foreground (s->gc, xgcv.foreground);
		}
	    }
	  else if (s->face->underline == FACE_UNDERLINE_SINGLE)
	    {
	      unsigned long thickness, position;
	      int y;

	      if (s->prev
		  && s->prev->face->underline == FACE_UNDERLINE_SINGLE
		  && (s->prev->face->underline_at_descent_line_p
		      == s->face->underline_at_descent_line_p)
		  && (s->prev->face->underline_pixels_above_descent_line
		      == s->face->underline_pixels_above_descent_line))
		{
		  /* We use the same underline style as the previous one.  */
		  thickness = s->prev->underline_thickness;
		  position = s->prev->underline_position;
		}
	      else
		{
		  struct font *font = font_for_underline_metrics (s);

		  unsigned long minimum_offset;
		  bool underline_at_descent_line;
		  bool use_underline_position_properties;
		  Lisp_Object val = (WINDOW_BUFFER_LOCAL_VALUE
				     (Qunderline_minimum_offset, s->w));

		  if (FIXNUMP (val))
		    minimum_offset = max (0, XFIXNUM (val));
		  else
		    minimum_offset = 1;

		  val = (WINDOW_BUFFER_LOCAL_VALUE
			 (Qx_underline_at_descent_line, s->w));
		  underline_at_descent_line
		    = (!(NILP (val) || BASE_EQ (val, Qunbound))
		       || s->face->underline_at_descent_line_p);

		  val = (WINDOW_BUFFER_LOCAL_VALUE
			 (Qx_use_underline_position_properties, s->w));
		  use_underline_position_properties
		    = !(NILP (val) || BASE_EQ (val, Qunbound));

		  /* Get the underline thickness.  Default is 1 pixel.  */
		  if (font && font->underline_thickness > 0)
                    thickness = font->underline_thickness;
		  else
		    thickness = 1;
		  if (underline_at_descent_line)
		    position = ((s->height - thickness)
				- (s->ybase - s->y)
				- s->face->underline_pixels_above_descent_line);
		  else
		    {
                      /* Get the underline position.  This is the
                         recommended vertical offset in pixels from
                         the baseline to the top of the underline.
                         This is a signed value according to the
                         specs, and its default is

			 ROUND ((maximum descent) / 2), with
			 ROUND(x) = floor (x + 0.5)  */

		      if (use_underline_position_properties
                          && font && font->underline_position >= 0)
                        position = font->underline_position;
		      else if (font)
                        position = (font->descent + 1) / 2;
		      else
			position = minimum_offset;
		    }

		  /* Ignore minimum_offset if the amount of pixels was
		     explicitly specified.  */
		  if (!s->face->underline_pixels_above_descent_line)
		    position = max (position, minimum_offset);
		}
	      /* Check the sanity of thickness and position.  We should
		 avoid drawing underline out of the current line area.  */
	      if (s->y + s->height <= s->ybase + position)
		position = (s->height - 1) - (s->ybase - s->y);
	      if (s->y + s->height < s->ybase + position + thickness)
		thickness = (s->y + s->height) - (s->ybase + position);
	      s->underline_thickness = thickness;
	      s->underline_position = position;
	      y = s->ybase + position;
	      if (s->face->underline_defaulted_p)
		mac_fill_rectangle (s->f, s->gc, s->x, y, s->width, thickness);
	      else
		{
		  XGCValues xgcv;
		  mac_get_gc_values (s->gc, GCForeground, &xgcv);
		  mac_set_foreground (s->gc, s->face->underline_color);
		  mac_fill_rectangle (s->f, s->gc, s->x, y, s->width, thickness);
		  mac_set_foreground (s->gc, xgcv.foreground);
		}
	    }
	}

      /* Draw overline.  */
      if (s->face->overline_p)
	{
	  unsigned long dy = 0, h = 1;

	  if (s->face->overline_color_defaulted_p)
	    mac_fill_rectangle (s->f, s->gc, s->x, s->y + dy,
				decoration_width, h);
	  else
	    {
	      XGCValues xgcv;
	      mac_get_gc_values (s->gc, GCForeground, &xgcv);
	      mac_set_foreground (s->gc, s->face->overline_color);
	      mac_fill_rectangle (s->f, s->gc, s->x, s->y + dy,
				  decoration_width, h);
	      mac_set_foreground (s->gc, xgcv.foreground);
	    }
	}

      /* Draw strike-through.  */
      if (s->face->strike_through_p)
	{
	  /* Y-coordinate and height of the glyph string's first
	     glyph.  We cannot use s->y and s->height because those
	     could be larger if there are taller display elements
	     (e.g., characters displayed with a larger font) in the
	     same glyph row.  */
	  int glyph_y = s->ybase - s->first_glyph->ascent;
	  int glyph_height = s->first_glyph->ascent + s->first_glyph->descent;
	  /* Strike-through width and offset from the glyph string's
	     top edge.  */
          unsigned long h = 1;
          unsigned long dy = (glyph_height - h) / 2;

	  if (s->face->strike_through_color_defaulted_p)
	    mac_fill_rectangle (s->f, s->gc, s->x, glyph_y + dy, s->width, h);
	  else
	    {
	      XGCValues xgcv;
	      mac_get_gc_values (s->gc, GCForeground, &xgcv);
	      mac_set_foreground (s->gc, s->face->strike_through_color);
	      mac_fill_rectangle (s->f, s->gc, s->x, glyph_y + dy,
				  decoration_width, h);
	      mac_set_foreground (s->gc, xgcv.foreground);
	    }
	}

      if (s->prev)
	{
	  struct glyph_string *prev;

	  for (prev = s->prev; prev; prev = prev->prev)
	    if (prev->hl != s->hl
		&& prev->x + prev->width + prev->right_overhang > s->x)
	      {
		/* As prev was drawn while clipped to its own area, we
		   must draw the right_overhang part using s->hl now.  */
		enum draw_glyphs_face save = prev->hl;

		prev->hl = s->hl;
		mac_set_glyph_string_gc (prev);
		mac_set_glyph_string_clipping_exactly (s, prev);
		if (prev->first_glyph->type == CHAR_GLYPH)
		  mac_draw_glyph_string_foreground (prev);
		else
		  mac_draw_composite_glyph_string_foreground (prev);
		mac_reset_clip_rectangles (prev->f, prev->gc);
		prev->hl = save;
		prev->num_clips = 0;
	      }
	}

      if (s->next)
	{
	  struct glyph_string *next;

	  for (next = s->next; next; next = next->next)
	    if (next->hl != s->hl
		&& next->x - next->left_overhang < s->x + s->width)
	      {
		/* As next will be drawn while clipped to its own area,
		   we must draw the left_overhang part using s->hl now.  */
		enum draw_glyphs_face save = next->hl;

		next->hl = s->hl;
		mac_set_glyph_string_gc (next);
		mac_set_glyph_string_clipping_exactly (s, next);
		if (next->first_glyph->type == CHAR_GLYPH)
		  mac_draw_glyph_string_foreground (next);
		else
		  mac_draw_composite_glyph_string_foreground (next);
		mac_reset_clip_rectangles (next->f, next->gc);
		next->hl = save;
		next->num_clips = 0;
		next->clip_head = s->next;
	      }
	}
    }

  /* Reset clipping.  */
  mac_reset_clip_rectangles (s->f, s->gc);
  s->num_clips = 0;

  /* Set the stippled flag that tells redisplay whether or not a
     stipple was actually draw.  */

  if (s->first_glyph->type != STRETCH_GLYPH
      && s->first_glyph->type != IMAGE_GLYPH
      && !s->row->stipple_p)
    s->row->stipple_p = s->stippled_p;
}

/* Mac replacement for XCopyArea: used only for scrolling.  */

static void
mac_scroll_area (struct frame *f, GC gc, int src_x, int src_y,
		 int width, int height, int dest_x, int dest_y)
{
  CGRect dst_rect = CGRectMake (dest_x, dest_y, width, height); 
  mac_ensure_arena (f);
  mac_record_gc_clip (f, gc);
  
  MAC_ARENA_CMD (cmd, f, SCROLL_RECT, dst_rect); /* dst is dirty, not src */
  cmd->scroll.delta = CGSizeMake (dest_x - src_x, dest_y - src_y);
}

/* Shift display to make room for inserted glyphs.   */

static void
mac_shift_glyphs_for_insert (struct frame *f, int x, int y, int width, int height, int shift_by)
{
  mac_scroll_area (f, f->output_data.mac->normal_gc,
		   x, y, width, height,
		   x + shift_by, y);
}

/* Delete N glyphs at the nominal cursor position.  Not implemented
   for X frames.  */

static void
mac_delete_glyphs (struct frame *f, int n)
{
  emacs_abort ();
}


/* Clear an entire frame.  */

static void
mac_clear_frame (struct frame *f)
{
  /* Clearing the frame will erase any cursor, so mark them all as no
     longer visible.  */
  mark_window_cursors_off (XWINDOW (FRAME_ROOT_WINDOW (f)));

  block_input ();

  mac_clear_window (f);

  /* We have to clear the scroll bars.  If we have changed colors or
     something like that, then they should be notified.  */
  mac_scroll_bar_clear (f);

  XFlush (FRAME_MAC_DISPLAY (f));
  unblock_input ();
}


/* Invert the bottom/top lines of the frame for .15 sec.  */

static void
mac_flash (struct frame *f)
{
  /* Get the height not including a menu bar widget.  */
  int height = FRAME_PIXEL_HEIGHT (f);
  /* Height of each line to flash.  */
  int flash_height = FRAME_LINE_HEIGHT (f);
  /* These will be the left and right margins of the rectangles.  */
  int flash_left = FRAME_INTERNAL_BORDER_WIDTH (f);
  int flash_right = (FRAME_TEXT_COLS_TO_PIXEL_WIDTH (f, FRAME_COLS (f))
		     - FRAME_INTERNAL_BORDER_WIDTH (f));
  int width = flash_right - flash_left;
  CGRect rects[2];
  int nrects;

  if (height > 3 * FRAME_LINE_HEIGHT (f))
    {
      /* If window is tall enough, flash top and bottom line.  */
      rects[0] = CGRectMake(flash_left,
			    FRAME_INTERNAL_BORDER_WIDTH (f)
			    + FRAME_TOP_MARGIN_HEIGHT(f),
			    width, flash_height);
      rects[1] = CGRectMake(flash_left,
			    height - flash_height
			    - FRAME_INTERNAL_BORDER_WIDTH(f),
			    width, flash_height);
      nrects = 2;
    }
  else
    {
      /* If it is short, flash it all.  */
      rects[0] = CGRectMake(flash_left,
			    FRAME_INTERNAL_BORDER_WIDTH(f),
			    width,
			    height - 2 * FRAME_INTERNAL_BORDER_WIDTH(f));
      nrects = 1;
    }

  mac_flash_rects(f, rects, nrects);  
}


/* Make audible bell.  */

static void
mac_ring_bell (struct frame *f)
{
  if (visible_bell)
    mac_flash (f);
  else
    {
      block_input ();
      mac_alert_sound_play ();
      XFlush (FRAME_MAC_DISPLAY (f));
      unblock_input ();
    }
}

/***********************************************************************
			      Line Dance
 ***********************************************************************/

/* Perform an insert-lines or delete-lines operation, inserting N
   lines or deleting -N lines at vertical position VPOS.  */

static void
mac_ins_del_lines (struct frame *f, int vpos, int n)
{
  emacs_abort ();
}


/* Scroll part of the display as described by RUN.  */

static void
mac_scroll_run (struct window *w, struct run *run)
{
  struct frame *f = XFRAME (w->frame);
  int x, y, width, height, from_y, to_y, bottom_y;

  if (FRAME_OBSCURED_P (f))
    return;

  /* Get frame-relative bounding box of the text display area of W,
     without mode lines.  Include in this box the left and right
     fringe of W.  */
  window_box (w, ANY_AREA, &x, &y, &width, &height);

  from_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->current_y);
  to_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->desired_y);
  bottom_y = y + height;

  if (to_y < from_y)
    {
      /* Scrolling up.  Make sure we don't copy part of the mode
	 line at the bottom.  */
      if (from_y + run->height > bottom_y)
	height = bottom_y - from_y;
      else
	height = run->height;
    }
  else
    {
      /* Scrolling down.  Make sure we don't copy over the mode line.
	 at the bottom.  */
      if (to_y + run->height > bottom_y)
	height = bottom_y - to_y;
      else
	height = run->height;
    }

  block_input ();

  /* Cursor off.  Will be switched on again in gui_update_window_end.  */
  gui_clear_cursor (w);

  mac_scroll_area (f, f->output_data.mac->normal_gc,
		   x, from_y,
		   width, height,
		   x, to_y);

  unblock_input ();
}



/***********************************************************************
			   Exposure Events
 ***********************************************************************/


static void
mac_frame_highlight (struct frame *f)
{
  gui_update_cursor (f, true);
  block_input ();
  mac_set_frame_alpha (f);
  unblock_input ();
}

static void
mac_frame_unhighlight (struct frame *f)
{
  gui_update_cursor (f, true);
  block_input ();
  mac_set_frame_alpha (f);
  unblock_input ();
}

/* The focus has changed.  Update the frames as necessary to reflect
   the new situation.  Note that we can't change the selected frame
   here, because the Lisp code we are interrupting might become confused.
   Each event gets marked with the frame in which it occurred, so the
   Lisp code can tell when the switch took place by examining the events.  */

static void
mac_new_focus_frame (struct mac_display_info *dpyinfo, struct frame *frame)
{
  struct frame *old_focus = dpyinfo->mac_focus_frame;

  if (frame != dpyinfo->mac_focus_frame)
    {
      /* Set this before calling other routines, so that they see
	 the correct value of mac_focus_frame.  */
      dpyinfo->mac_focus_frame = frame;

      if (old_focus && old_focus->auto_lower)
	mac_lower_frame (old_focus);

      if (dpyinfo->mac_focus_frame && dpyinfo->mac_focus_frame->auto_raise)
	dpyinfo->mac_pending_autoraise_frame = dpyinfo->mac_focus_frame;
      else
	dpyinfo->mac_pending_autoraise_frame = NULL;

      if (frame)
	mac_set_font_info_for_selection (frame, DEFAULT_FACE_ID, 0, -1, Qnil);
    }

  mac_frame_rehighlight_1 (dpyinfo);
}

/* Handle FocusIn and FocusOut state changes for FRAME.
   If FRAME has focus and there exists more than one frame, puts
   a FOCUS_IN_EVENT into *BUFP.  */

void
mac_focus_changed (int type, struct mac_display_info *dpyinfo, struct frame *frame, struct input_event *bufp)
{
  if (type == activeFlag)
    {
      if (dpyinfo->mac_focus_event_frame != frame)
        {
          mac_new_focus_frame (dpyinfo, frame);
          dpyinfo->mac_focus_event_frame = frame;
          bufp->kind = FOCUS_IN_EVENT;
          XSETFRAME (bufp->frame_or_window, frame);
        }
    }
  else
    {
      if (dpyinfo->mac_focus_event_frame == frame)
        {
          dpyinfo->mac_focus_event_frame = 0;
          mac_new_focus_frame (dpyinfo, 0);

          bufp->kind = FOCUS_OUT_EVENT;
          XSETFRAME (bufp->frame_or_window, frame);
        }
    }
}

/* The focus has changed, or we have redirected a frame's focus to
   another frame (this happens when a frame uses a surrogate
   mini-buffer frame).  Shift the highlight as appropriate.

   The FRAME argument doesn't necessarily have anything to do with which
   frame is being highlighted or un-highlighted; we only use it to find
   the appropriate X display info.  */

static void
mac_frame_rehighlight (struct frame *frame)
{
  mac_frame_rehighlight_1 (FRAME_DISPLAY_INFO (frame));
}

static void
mac_frame_rehighlight_1 (struct mac_display_info *dpyinfo)
{
  struct frame *old_highlight = dpyinfo->highlight_frame;

  if (dpyinfo->mac_focus_frame)
    {
      dpyinfo->highlight_frame
	= ((FRAMEP (FRAME_FOCUS_FRAME (dpyinfo->mac_focus_frame)))
	   ? XFRAME (FRAME_FOCUS_FRAME (dpyinfo->mac_focus_frame))
	   : dpyinfo->mac_focus_frame);
      if (! FRAME_LIVE_P (dpyinfo->highlight_frame))
	{
	  fset_focus_frame (dpyinfo->mac_focus_frame, Qnil);
	  dpyinfo->highlight_frame = dpyinfo->mac_focus_frame;
	}
    }
  else
    dpyinfo->highlight_frame = 0;

  if (dpyinfo->highlight_frame != old_highlight)
    {
      if (old_highlight)
	mac_frame_unhighlight (old_highlight);
      if (dpyinfo->highlight_frame)
	mac_frame_highlight (dpyinfo->highlight_frame);
    }
}



/* Convert a keysym to its name.  */

char *
get_keysym_name (int keysym)
{
  char *value;

  block_input ();
#if 0
  value = XKeysymToString (keysym);
#else
  value = 0;
#endif
  unblock_input ();

  return value;
}



/************************************************************************
			      Mouse Face
 ************************************************************************/

Lisp_Object
mac_event_frame (void)
{
  struct mac_display_info *dpyinfo = &one_mac_display_info;

  if (dpyinfo->mac_focus_frame)
    {
      Lisp_Object frame;
      XSETFRAME (frame, dpyinfo->mac_focus_frame);
      return frame;
    }
  else
    /* Mac version may get events, such as a menu bar click, even when
       all the frames are invisible.  In this case, we regard the
       event came to the selected frame.  */
    return selected_frame;
}

/* Return the current position of the mouse.
   *FP should be a frame which indicates which display to ask about.

   If the mouse movement started in a scroll bar, set *FP, *BAR_WINDOW,
   and *PART to the frame, window, and scroll bar part that the mouse
   is over.  Set *X and *Y to the portion and whole of the mouse's
   position on the scroll bar.

   If the mouse movement started elsewhere, set *FP to the frame the
   mouse is on, *BAR_WINDOW to nil, and *X and *Y to the character cell
   the mouse is over.

   Set *TIMESTAMP to the server time-stamp for the time at which the mouse
   was at this position.

   Don't store anything if we don't have a valid set of values to report.

   This clears the mouse_moved flag, so we can wait for the next mouse
   movement.  */

static void
mac_mouse_position (struct frame **fp, int insist, Lisp_Object *bar_window,
		    enum scroll_bar_part *part, Lisp_Object *x, Lisp_Object *y,
		    Time *timestamp)
{
  struct frame *f1;
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (*fp);
  bool return_no_frame_flag = false;

  block_input ();

  Lisp_Object frame, tail;

  /* Clear the mouse-moved flag for every frame on this display.  */
  FOR_EACH_FRAME (tail, frame)
    if (FRAME_MAC_P (XFRAME (frame)))
      XFRAME (frame)->mouse_moved = false;

  if (gui_mouse_grabbed (dpyinfo) && !EQ (track_mouse, Qdropping)
      && !EQ (track_mouse, Qdrag_source))
    /* If mouse was grabbed on a frame, give coords for that frame
       even if the mouse is now outside it.  */
    f1 = dpyinfo->last_mouse_frame;
  else
    {
      f1 = mac_get_frame_at_mouse (EQ (track_mouse, Qdrag_source)
				   || EQ (track_mouse, Qdropping));
      if ((!f1 || FRAME_TOOLTIP_P (f1))
	  && (EQ (track_mouse, Qdropping)
	      || EQ (track_mouse, Qdrag_source))
	  && gui_mouse_grabbed (dpyinfo))
	{
	  /* When dropping then if we didn't get a frame or only a
	     tooltip frame and the mouse was grabbed on a frame,
	     give coords for that frame even if the mouse is now
	     outside it.  */
	  f1 = dpyinfo->last_mouse_frame;
	  return_no_frame_flag = EQ (track_mouse, Qdrag_source);
	}
      else if (f1 && FRAME_TOOLTIP_P (f1))
	f1 = NULL;

      if (!f1)
	f1 = XFRAME (mac_event_frame ());
    }

  if (f1)
    {
      /* Ok, we found a frame.  Store all the values.
	 last_mouse_glyph is a rectangle used to reduce the generation
	 of mouse events.  To not miss any motion events, we must
	 divide the frame into rectangles of the size of the smallest
	 character that could be displayed on it, i.e. into the same
	 rectangles that matrices on the frame are divided into.  */
      CGPoint mouse_pos = mac_get_frame_mouse (f1);

      dpyinfo = FRAME_DISPLAY_INFO (f1);
      remember_mouse_glyph (f1, mouse_pos.x, mouse_pos.y,
			    &dpyinfo->last_mouse_glyph);
      dpyinfo->last_mouse_glyph_frame = f1;

      *bar_window = Qnil;
      *part = 0;
      *fp = return_no_frame_flag ? NULL : f1;
      XSETINT (*x, mouse_pos.x);
      XSETINT (*y, mouse_pos.y);
      *timestamp = dpyinfo->last_mouse_movement_time;
    }

  unblock_input ();
}


/************************************************************************
			 Scroll bars, general
 ************************************************************************/

/* Create a scroll bar and return the scroll bar vector for it.  W is
   the Emacs window on which to create the scroll bar. TOP, LEFT,
   WIDTH and HEIGHT are the pixel coordinates and dimensions of the
   scroll bar. */

static struct scroll_bar *
mac_scroll_bar_create (struct window *w, int top, int left,
		     int width, int height, bool horizontal)
{
  struct frame *f = XFRAME (w->frame);
  struct scroll_bar *bar = ALLOCATE_PSEUDOVECTOR (struct scroll_bar, prev,
						  PVEC_OTHER);
  Lisp_Object barobj;

  block_input ();

  XSETWINDOW (bar->window, w);
  bar->top = top;
  bar->left = left;
  bar->width = width;
  bar->height = height;
  bar->redraw_needed_p = 0;
  bar->horizontal = horizontal;

  mac_create_scroll_bar (bar);

  /* Add bar to its frame's list of scroll bars.  */
  bar->next = FRAME_SCROLL_BARS (f);
  bar->prev = Qnil;
  XSETVECTOR (barobj, bar);
  fset_scroll_bars (f, barobj);
  if (!NILP (bar->next))
    XSETVECTOR (XSCROLL_BAR (bar->next)->prev, bar);

  unblock_input ();
  return bar;
}


/* Destroy scroll bar BAR, and set its Emacs window's scroll bar to
   nil.  */

static void
mac_scroll_bar_remove (struct scroll_bar *bar)
{
  block_input ();

  /* Destroy the Mac scroll bar control  */
  mac_dispose_scroll_bar (bar);

  /* Dissociate this scroll bar from its window.  */
  if (bar->horizontal)
    wset_horizontal_scroll_bar (XWINDOW (bar->window), Qnil);
  else
    wset_vertical_scroll_bar (XWINDOW (bar->window), Qnil);

  unblock_input ();
}


/* Set the handle of the vertical scroll bar for WINDOW to indicate
   that we are displaying PORTION characters out of a total of WHOLE
   characters, starting at POSITION.  If WINDOW has no scroll bar,
   create one.  */

static void
mac_set_vertical_scroll_bar (struct window *w, int portion, int whole, int position)
{
  struct scroll_bar *bar;
  int top, height, left, width;
  int window_y, window_height;

  /* Get window dimensions.  */
  window_box (w, ANY_AREA, 0, &window_y, 0, &window_height);
  top = window_y;
  height = window_height;
  left = WINDOW_SCROLL_BAR_AREA_X (w);
  width = WINDOW_SCROLL_BAR_AREA_WIDTH (w);

  /* Does the scroll bar exist yet?  */
  if (NILP (w->vertical_scroll_bar))
    {
      Lisp_Object barobj;

      bar = mac_scroll_bar_create (w, top, left, width, height, false);
      XSETVECTOR (barobj, bar);
      wset_vertical_scroll_bar (w, barobj);
    }
  else
    {
      /* It may just need to be moved and resized.  */
      bar = XSCROLL_BAR (w->vertical_scroll_bar);

      block_input ();

      /* If already correctly positioned, do nothing.  */
      if (bar->left == left && bar->top == top
	  && bar->width == width && bar->height == height)
	{
	  if (bar->redraw_needed_p)
	    mac_redraw_scroll_bar (bar);
	}
      else
	{
          /* Remember new settings.  */
          bar->left = left;
          bar->top = top;
          bar->width = width;
          bar->height = height;

	  mac_update_scroll_bar_bounds (bar);
        }

      unblock_input ();
    }

  bar->redraw_needed_p = 0;

  mac_set_scroll_bar_thumb (bar, portion, position, whole);
}


static void
mac_set_horizontal_scroll_bar (struct window *w, int portion, int whole, int position)
{
  struct frame *f = XFRAME (w->frame);
  struct scroll_bar *bar;
  int top, height, left, width;
  int window_x, window_width;
  int pixel_width = WINDOW_PIXEL_WIDTH (w);

  /* Get window dimensions.  */
  window_box (w, ANY_AREA, &window_x, 0, &window_width, 0);
  left = window_x;
  width = window_width;
  top = WINDOW_SCROLL_BAR_AREA_Y (w);
  height = WINDOW_SCROLL_BAR_AREA_HEIGHT (w);

  /* Does the scroll bar exist yet?  */
  if (NILP (w->horizontal_scroll_bar))
    {
      Lisp_Object barobj;
      int sp_width = pixel_width - WINDOW_RIGHT_DIVIDER_WIDTH (w) - width;

      if (sp_width > 0)
	{
	  int left_edge = WINDOW_LEFT_EDGE_X (w);

	  /* Clear part between window_width and WINDOW_PIXEL_WIDTH.  */
	  block_input ();
	  mac_clear_area (f, left_edge == left ? left + width : left_edge,
			  top, sp_width, height);
	  unblock_input ();
	}
      bar = mac_scroll_bar_create (w, top, left, width, height, true);
      XSETVECTOR (barobj, bar);
      wset_horizontal_scroll_bar (w, barobj);
    }
  else
    {
      /* It may just need to be moved and resized.  */
      bar = XSCROLL_BAR (w->horizontal_scroll_bar);

      block_input ();

      /* If already correctly positioned, do nothing.  */
      if (bar->left == left && bar->top == top
	  && bar->width == width && bar->height == height)
	{
	  if (bar->redraw_needed_p)
	    mac_redraw_scroll_bar (bar);
	}
      else
	{
	  int sp_width = pixel_width - WINDOW_RIGHT_DIVIDER_WIDTH (w) - width;

	  if (sp_width > 0)
	    {
	      int left_edge = WINDOW_LEFT_EDGE_X (w);

	      /* Clear part between window_width and WINDOW_PIXEL_WIDTH.  */
	      mac_clear_area (f, left_edge == left ? left + width : left_edge,
			      top, sp_width, height);
	    }

          /* Remember new settings.  */
          bar->left = left;
          bar->top = top;
          bar->width = width;
          bar->height = height;

	  mac_update_scroll_bar_bounds (bar);
        }

      unblock_input ();
    }

  bar->redraw_needed_p = 0;

  mac_set_scroll_bar_thumb (bar, portion, position, whole);
}


/* The following three hooks are used when we're doing a thorough
   redisplay of the frame.  We don't explicitly know which scroll bars
   are going to be deleted, because keeping track of when windows go
   away is a real pain - "Can you say set-window-configuration, boys
   and girls?"  Instead, we just assert at the beginning of redisplay
   that *all* scroll bars are to be removed, and then save a scroll bar
   from the fiery pit when we actually redisplay its window.  */

/* Arrange for all scroll bars on FRAME to be removed at the next call
   to `*judge_scroll_bars_hook'.  A scroll bar may be spared if
   `*redeem_scroll_bar_hook' is applied to its window before the judgment.  */

static void
mac_condemn_scroll_bars (struct frame *frame)
{
  if (!NILP (FRAME_SCROLL_BARS (frame)))
    {
      if (!NILP (FRAME_CONDEMNED_SCROLL_BARS (frame)))
	{
	  /* Prepend scrollbars to already condemned ones.  */
	  Lisp_Object last = FRAME_SCROLL_BARS (frame);

	  while (!NILP (XSCROLL_BAR (last)->next))
	    last = XSCROLL_BAR (last)->next;

	  XSCROLL_BAR (last)->next = FRAME_CONDEMNED_SCROLL_BARS (frame);
	  XSCROLL_BAR (FRAME_CONDEMNED_SCROLL_BARS (frame))->prev = last;
	}

      fset_condemned_scroll_bars (frame, FRAME_SCROLL_BARS (frame));
      fset_scroll_bars (frame, Qnil);
    }
}


/* Un-mark WINDOW's scroll bar for deletion in this judgment cycle.
   Note that WINDOW isn't necessarily condemned at all.  */

static void
mac_redeem_scroll_bar (struct window *w)
{
  struct scroll_bar *bar;
  Lisp_Object barobj;
  struct frame *f;

  /* We can't redeem this window's scroll bar if it doesn't have one.  */
  if (NILP (w->vertical_scroll_bar) && NILP (w->horizontal_scroll_bar))
    emacs_abort ();

  if (!NILP (w->vertical_scroll_bar) && WINDOW_HAS_VERTICAL_SCROLL_BAR (w))
    {
      bar = XSCROLL_BAR (w->vertical_scroll_bar);
      /* Unlink it from the condemned list.  */
      f = XFRAME (WINDOW_FRAME (w));
      if (NILP (bar->prev))
	{
	  /* If the prev pointer is nil, it must be the first in one of
	     the lists.  */
	  if (EQ (FRAME_SCROLL_BARS (f), w->vertical_scroll_bar))
	    /* It's not condemned.  Everything's fine.  */
	    goto horizontal;
	  else if (EQ (FRAME_CONDEMNED_SCROLL_BARS (f),
		       w->vertical_scroll_bar))
	    fset_condemned_scroll_bars (f, bar->next);
	  else
	    /* If its prev pointer is nil, it must be at the front of
	       one or the other!  */
	    emacs_abort ();
	}
      else
	XSCROLL_BAR (bar->prev)->next = bar->next;

      if (! NILP (bar->next))
	XSCROLL_BAR (bar->next)->prev = bar->prev;

      bar->next = FRAME_SCROLL_BARS (f);
      bar->prev = Qnil;
      XSETVECTOR (barobj, bar);
      fset_scroll_bars (f, barobj);
      if (! NILP (bar->next))
	XSETVECTOR (XSCROLL_BAR (bar->next)->prev, bar);
    }

 horizontal:
  if (!NILP (w->horizontal_scroll_bar) && WINDOW_HAS_HORIZONTAL_SCROLL_BAR (w))
    {
      bar = XSCROLL_BAR (w->horizontal_scroll_bar);
      /* Unlink it from the condemned list.  */
      f = XFRAME (WINDOW_FRAME (w));
      if (NILP (bar->prev))
	{
	  /* If the prev pointer is nil, it must be the first in one of
	     the lists.  */
	  if (EQ (FRAME_SCROLL_BARS (f), w->horizontal_scroll_bar))
	    /* It's not condemned.  Everything's fine.  */
	    return;
	  else if (EQ (FRAME_CONDEMNED_SCROLL_BARS (f),
		       w->horizontal_scroll_bar))
	    fset_condemned_scroll_bars (f, bar->next);
	  else
	    /* If its prev pointer is nil, it must be at the front of
	       one or the other!  */
	    emacs_abort ();
	}
      else
	XSCROLL_BAR (bar->prev)->next = bar->next;

      if (! NILP (bar->next))
	XSCROLL_BAR (bar->next)->prev = bar->prev;

      bar->next = FRAME_SCROLL_BARS (f);
      bar->prev = Qnil;
      XSETVECTOR (barobj, bar);
      fset_scroll_bars (f, barobj);
      if (! NILP (bar->next))
	XSETVECTOR (XSCROLL_BAR (bar->next)->prev, bar);
    }
}

/* Remove all scroll bars on FRAME that haven't been saved since the
   last call to `*condemn_scroll_bars_hook'.  */

static void
mac_judge_scroll_bars (struct frame *f)
{
  Lisp_Object bar, next;

  bar = FRAME_CONDEMNED_SCROLL_BARS (f);

  /* Clear out the condemned list now so we won't try to process any
     more events on the hapless scroll bars.  */
  fset_condemned_scroll_bars (f, Qnil);

  for (; ! NILP (bar); bar = next)
    {
      struct scroll_bar *b = XSCROLL_BAR (bar);

      mac_scroll_bar_remove (b);

      next = b->next;
      b->next = b->prev = Qnil;
    }

  /* Now there should be no references to the condemned scroll bars,
     and they should get garbage-collected.  */
}

/* The screen has been cleared so we may have changed foreground or
   background colors, and the scroll bars may need to be redrawn.
   Clear out the scroll bars, and ask for expose events, so we can
   redraw them.  */

static void
mac_scroll_bar_clear (struct frame *f)
{
  Lisp_Object bar;

  /* We can have scroll bars even if this is 0,
     if we just turned off scroll bar mode.
     But in that case we should not clear them.  */
  if (FRAME_HAS_VERTICAL_SCROLL_BARS (f))
    for (bar = FRAME_SCROLL_BARS (f); !NILP (bar);
	 bar = XSCROLL_BAR (bar)->next)
      XSCROLL_BAR (bar)->redraw_needed_p = 1;
}


/***********************************************************************
			       Tool-bars
 ***********************************************************************/

void
mac_set_frame_window_gravity_reference_bounds (struct frame *f, int win_gravity,
					       NativeRectangle r)
{
  NativeRectangle bounds;

  mac_get_frame_window_structure_bounds (f, &bounds);

  if (r.width <= 0)
    r.width = bounds.width;
  if (r.height <= 0)
    r.height = bounds.height;

  switch (win_gravity)
    {
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
      break;

    case NorthGravity:
    case CenterGravity:
    case SouthGravity:
      r.x -= r.width / 2;
      break;

    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
      r.x -= r.width;
      break;

    default:
      r.x = bounds.x;
    }

  switch (win_gravity)
    {
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
      break;

    case WestGravity:
    case CenterGravity:
    case EastGravity:
      r.y -= r.height / 2;
      break;

    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
      r.y -= r.height;
      break;

    default:
      r.y = bounds.y;
    }

  if (r.x != bounds.x || r.y != bounds.y
      || r.width != bounds.width || r.height != bounds.height)
    mac_set_frame_window_structure_bounds (f, r);
}

void
mac_get_frame_window_gravity_reference_bounds (struct frame *f, int win_gravity,
					       NativeRectangle *r)
{
  mac_get_frame_window_structure_bounds (f, r);

  switch (win_gravity)
    {
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
      break;

    case NorthGravity:
    case CenterGravity:
    case SouthGravity:
      r->x += r->width / 2;
      break;

    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
      r->x += r->width;
      break;
    }

  switch (win_gravity)
    {
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
      break;

    case WestGravity:
    case CenterGravity:
    case EastGravity:
      r->y += r->height / 2;
      break;

    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
      r->y += r->height;
      break;
    }
}


/***********************************************************************
			     Text Cursor
 ***********************************************************************/

/* Set clipping for output in glyph row ROW.  W is the window in which
   we operate.  GC is the graphics context to set clipping in.

   ROW may be a text row or, e.g., a mode line.  Text rows must be
   clipped to the interior of the window dedicated to text display,
   mode lines must be clipped to the whole window.  */

static void
mac_clip_to_row (struct window *w, struct glyph_row *row,
		 enum glyph_row_area area, GC gc)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  NativeRectangle clip_rect;
  int window_x, window_y, window_width;

  window_box (w, area, &window_x, &window_y, &window_width, 0);

  clip_rect.x = window_x;
  clip_rect.y = WINDOW_TO_FRAME_PIXEL_Y (w, max (0, row->y));
  clip_rect.y = max (clip_rect.y, window_y);
  clip_rect.width = window_width;
  clip_rect.height = row->visible_height;

  mac_set_clip_rectangles (f, gc, &clip_rect, 1);
}


/* Draw a hollow box cursor on window W in glyph row ROW.  */

static void
mac_draw_hollow_cursor (struct window *w, struct glyph_row *row)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  int x, y, wd, h;
  XGCValues xgcv;
  struct glyph *cursor_glyph;
  GC gc;

  /* Get the glyph the cursor is on.  If we can't tell because
     the current matrix is invalid or such, give up.  */
  cursor_glyph = get_phys_cursor_glyph (w);
  if (cursor_glyph == NULL)
    return;

  /* Compute frame-relative coordinates for phys cursor.  */
  get_phys_cursor_geometry (w, row, cursor_glyph, &x, &y, &h);
  wd = w->phys_cursor_width - 1;

  /* The foreground of cursor_gc is typically the same as the normal
     background color, which can cause the cursor box to be invisible.  */
  xgcv.foreground = f->output_data.mac->cursor_pixel;
  if (dpyinfo->scratch_cursor_gc)
    mac_change_gc (dpyinfo->scratch_cursor_gc, GCForeground, &xgcv);
  else
    dpyinfo->scratch_cursor_gc = mac_create_gc (GCForeground, &xgcv);
  gc = dpyinfo->scratch_cursor_gc;

  /* When on R2L character, show cursor at the right edge of the
     glyph, unless the cursor box is as wide as the glyph or wider
     (the latter happens when x-stretch-cursor is non-nil).  */
  if ((cursor_glyph->resolved_level & 1) != 0
      && cursor_glyph->pixel_width > wd)
    {
      x += cursor_glyph->pixel_width - wd;
      if (wd > 0)
	wd -= 1;
    }
  /* Set clipping, draw the rectangle, and reset clipping again.  */
  mac_clip_to_row (w, row, TEXT_AREA, gc);
  mac_draw_rectangle (f, gc, x, y, wd, h - 1);
  mac_reset_clip_rectangles (f, gc);
}


/* Draw a bar cursor on window W in glyph row ROW.

   Implementation note: One would like to draw a bar cursor with an
   angle equal to the one given by the font property XA_ITALIC_ANGLE.
   Unfortunately, I didn't find a font yet that has this property set.
   --gerd.  */

static void
mac_draw_bar_cursor (struct window *w, struct glyph_row *row, int width, enum text_cursor_kinds kind)
{
  struct frame *f = XFRAME (w->frame);
  struct glyph *cursor_glyph;

  /* If cursor is out of bounds, don't draw garbage.  This can happen
     in mini-buffer windows when switching between echo area glyphs
     and mini-buffer.  */
  cursor_glyph = get_phys_cursor_glyph (w);
  if (cursor_glyph == NULL)
    return;

  /* Experimental avoidance of cursor on xwidget.  */
  if (cursor_glyph->type == XWIDGET_GLYPH)
    return;

  /* If on an image, draw like a normal cursor.  That's usually better
     visible than drawing a bar, esp. if the image is large so that
     the bar might not be in the window.  */
  if (cursor_glyph->type == IMAGE_GLYPH)
    {
      struct glyph_row *r;
      r = MATRIX_ROW (w->current_matrix, w->phys_cursor.vpos);
      draw_phys_cursor_glyph (w, r, DRAW_CURSOR);
    }
  else
    {
      GC gc = FRAME_DISPLAY_INFO (f)->scratch_cursor_gc;
      unsigned long mask = GCForeground | GCBackground;
      struct face *face = FACE_FROM_ID (f, cursor_glyph->face_id);
      XGCValues xgcv;

      /* If the glyph's background equals the color we normally draw
	 the bars cursor in, the bar cursor in its normal color is
	 invisible.  Use the glyph's foreground color instead in this
	 case, on the assumption that the glyph's colors are chosen so
	 that the glyph is legible.  */
      if (face->background == f->output_data.mac->cursor_pixel)
	xgcv.background = xgcv.foreground = face->foreground;
      else
	xgcv.background = xgcv.foreground = f->output_data.mac->cursor_pixel;

      if (gc)
	mac_change_gc (gc, mask, &xgcv);
      else
	{
	  gc = mac_create_gc (mask, &xgcv);
	  FRAME_DISPLAY_INFO (f)->scratch_cursor_gc = gc;
	}

      mac_clip_to_row (w, row, TEXT_AREA, gc);

      if (kind == BAR_CURSOR)
	{
	  int x = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x);

	  if (width < 0)
	    width = FRAME_CURSOR_WIDTH (f);
	  width = min (cursor_glyph->pixel_width, width);

	  w->phys_cursor_width = width;

	  /* If the character under cursor is R2L, draw the bar cursor
	     on the right of its glyph, rather than on the left.  */
	  if ((cursor_glyph->resolved_level & 1) != 0)
	    x += cursor_glyph->pixel_width - width;

	  mac_fill_rectangle (f, gc, x,
			      WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y),
			      width, row->height);
	}
      else /* HBAR_CURSOR */
	{
	  int dummy_x, dummy_y, dummy_h;
	  int x = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x);

	  if (width < 0)
	    width = row->height;

	  width = min (row->height, width);

	  get_phys_cursor_geometry (w, row, cursor_glyph, &dummy_x,
				    &dummy_y, &dummy_h);

	  if ((cursor_glyph->resolved_level & 1) != 0
	      && cursor_glyph->pixel_width > w->phys_cursor_width)
	    x += cursor_glyph->pixel_width - w->phys_cursor_width;
	  mac_fill_rectangle (f, gc, x,
			      WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y +
						       row->height - width),
			      w->phys_cursor_width, width);
	}

      mac_reset_clip_rectangles (f, gc);
    }
}


/* RIF: Define cursor CURSOR on frame F.  */

static void
mac_define_frame_cursor (struct frame *f, Emacs_Cursor cursor)
{
  if (f->output_data.mac->current_cursor != cursor)
    {
      f->output_data.mac->current_cursor = cursor;
      mac_invalidate_frame_cursor_rects (f);
    }
}


/* RIF: Clear area on frame F.  */

static void
mac_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  mac_clear_area (f, x, y, width, height);
}


/* RIF: Draw cursor on window W.  */

static void
mac_draw_window_cursor (struct window *w, struct glyph_row *glyph_row, int x,
		      int y, enum text_cursor_kinds cursor_type,
		      int cursor_width, bool on_p, bool active_p)
{
  if (on_p)
    {
      w->phys_cursor_type = cursor_type;
      w->phys_cursor_on_p = true;

      if (glyph_row->exact_window_width_line_p
	  && (glyph_row->reversed_p
	      ? (w->phys_cursor.hpos < 0)
	      : (w->phys_cursor.hpos >= glyph_row->used[TEXT_AREA])))
	{
	  glyph_row->cursor_in_fringe_p = true;
	  draw_fringe_bitmap (w, glyph_row, glyph_row->reversed_p);
	}
      else
	{
	  switch (cursor_type)
	    {
	    case HOLLOW_BOX_CURSOR:
	      mac_draw_hollow_cursor (w, glyph_row);
	      break;

	    case FILLED_BOX_CURSOR:
	      draw_phys_cursor_glyph (w, glyph_row, DRAW_CURSOR);
	      break;

	    case BAR_CURSOR:
	      mac_draw_bar_cursor (w, glyph_row, cursor_width, BAR_CURSOR);
	      break;

	    case HBAR_CURSOR:
	      mac_draw_bar_cursor (w, glyph_row, cursor_width, HBAR_CURSOR);
	      break;

	    case NO_CURSOR:
	      w->phys_cursor_width = 0;
	      break;

	    default:
	      emacs_abort ();
	    }
	}

      if (w == XWINDOW (selected_window))
	mac_update_accessibility_status (XFRAME (w->frame));
    }
}


/* Changing the font of the frame.  */

/* Give frame F the font FONT-OBJECT as its default font.  The return
   value is FONT-OBJECT.  FONTSET is an ID of the fontset for the
   frame.  If it is negative, generate a new fontset from
   FONT-OBJECT.  */

static Lisp_Object
mac_new_font (struct frame *f, Lisp_Object font_object, int fontset)
{
  struct font *font = XFONT_OBJECT (font_object);
  int unit, font_ascent, font_descent;

  if (fontset < 0)
    fontset = fontset_from_font (font_object);
  FRAME_FONTSET (f) = fontset;
  if (FRAME_FONT (f) == font)
    /* This font is already set in frame F.  There's nothing more to
       do.  */
    return font_object;

  FRAME_FONT (f) = font;
  FRAME_BASELINE_OFFSET (f) = font->baseline_offset;
  FRAME_COLUMN_WIDTH (f) = font->average_width;
  get_font_ascent_descent (font, &font_ascent, &font_descent);
  FRAME_LINE_HEIGHT (f) = font_ascent + font_descent;

  FRAME_TOOL_BAR_HEIGHT (f) = FRAME_TOOL_BAR_LINES (f) * FRAME_LINE_HEIGHT (f);
  FRAME_MENU_BAR_HEIGHT (f) = FRAME_MENU_BAR_LINES (f) * FRAME_LINE_HEIGHT (f);
  FRAME_TAB_BAR_HEIGHT (f) = FRAME_TAB_BAR_LINES (f) * FRAME_LINE_HEIGHT (f);

  unit = FRAME_COLUMN_WIDTH (f);
  /* The width of a toolkit scrollbar does not change with the new
     font but we have to calculate the number of columns it occupies
     anew.  */
  FRAME_CONFIG_SCROLL_BAR_COLS (f)
    = (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) + unit - 1) / unit;


  /* Don't change the size of a tip frame; there's no point in doing it
     because it's done in Fx_show_tip, and it leads to problems because
     the tip frame has no widget.  */
  if (FRAME_MAC_WINDOW (f) != 0 && !FRAME_TOOLTIP_P (f))
    adjust_frame_size
      (f, FRAME_COLS (f) * FRAME_COLUMN_WIDTH (f),
       FRAME_LINES (f) * FRAME_LINE_HEIGHT (f), 3, false, Qfont);

  return font_object;
}


void
mac_handle_origin_change (struct frame *f)
{
  int old_left = f->left_pos;
  int old_top = f->top_pos;

  mac_real_positions (f, &f->left_pos, &f->top_pos);

  if (!FRAME_TOOLTIP_P (f)
      && (old_left != f->left_pos || old_top != f->top_pos))
    {
      struct input_event buf;

      EVENT_INIT (buf);
      buf.kind = MOVE_FRAME_EVENT;
      XSETFRAME (buf.frame_or_window, f);
      buf.arg = Qnil;
      kbd_buffer_store_event (&buf);
    }
}

void
mac_handle_size_change (struct frame *f, int width, int height)
{
  /* This might be called when a full screen window is closed on OS X
     10.10.  */
  if (!WINDOWP (FRAME_ROOT_WINDOW (f)))
    return;

  /* Pass true for DELAY since we can't run Lisp code inside of a
     block_input.  */
  change_frame_size (f, width, height, false, true, false);

  /* Clear out any recollection of where the mouse highlighting was,
     since it might be in a place that's outside the new frame size.
     Actually checking whether it is outside is a pain in the neck, so
     don't try--just let the highlighting be done afresh with new
     size.  */
  cancel_mouse_face (f);
}


/* Calculate the absolute position in frame F
   from its current recorded position values and gravity.  */

static void
mac_calc_absolute_position (struct frame *f)
{
  int flags = f->size_hint_flags;
  struct frame *p = FRAME_PARENT_FRAME (f);
  NativeRectangle bounds;

  /* We have nothing to do if the current position
     is already for the top-left corner.  */
  if (! ((flags & XNegative) || (flags & YNegative)))
    return;

  block_input ();
  mac_get_frame_window_structure_bounds (f, &bounds);
  unblock_input ();

  /* Treat negative positions as relative to the leftmost bottommost
     position that fits on the screen.  */
  if (flags & XNegative)
    f->left_pos += ((p == NULL
		     ? mac_display_pixel_width (FRAME_DISPLAY_INFO (f))
		     : FRAME_PIXEL_WIDTH (p))
		    - bounds.width);

  if (flags & YNegative)
    f->top_pos += ((p == NULL
		    ? mac_display_pixel_height (FRAME_DISPLAY_INFO (f))
		    : FRAME_PIXEL_HEIGHT (p))
		   - bounds.height);

  /* The left_pos and top_pos
     are now relative to the top and left screen edges,
     so the flags should correspond.  */
  f->size_hint_flags &= ~ (XNegative | YNegative);
}

/* CHANGE_GRAVITY is 1 when calling from Fset_frame_position,
   to really change the position, and 0 when calling from
   mac_make_frame_visible (in that case, XOFF and YOFF are the current
   position values).  It is -1 when calling from x_set_frame_parameters,
   which means, do adjust for borders but don't change the gravity.  */

static void
mac_set_offset (struct frame *f, register int xoff, register int yoff, int change_gravity)
{
  if (change_gravity > 0)
    {
      f->top_pos = yoff;
      f->left_pos = xoff;
      f->size_hint_flags &= ~ (XNegative | YNegative);
      if (xoff < 0)
	f->size_hint_flags |= XNegative;
      if (yoff < 0)
	f->size_hint_flags |= YNegative;
      f->win_gravity = NorthWestGravity;
    }
  mac_calc_absolute_position (f);

  block_input ();
  mac_wm_set_size_hint (f, 0, false);

  mac_move_frame_window_structure (f, f->left_pos, f->top_pos);
  /* When the frame is maximized/fullscreen, the actual window will
     not be moved and mac_handle_origin_change will not be called via
     window system events.  */
  {
    NativeRectangle bounds;

    mac_get_frame_window_structure_bounds (f, &bounds);
    f->left_pos = bounds.x;
    f->top_pos = bounds.y;
  }

  unblock_input ();
}

void
mac_set_sticky (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  block_input ();
  mac_change_frame_window_wm_state (f, !NILP (new_value) ? WM_STATE_STICKY : 0,
				    NILP (new_value) ? WM_STATE_STICKY : 0);
  unblock_input ();
}

void
mac_set_skip_taskbar (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    {
      block_input ();
      mac_change_frame_window_wm_state (f, (!NILP (new_value)
					    ? WM_STATE_SKIP_TASKBAR : 0),
					(NILP (new_value)
					 ? WM_STATE_SKIP_TASKBAR : 0));
      unblock_input ();

      FRAME_SKIP_TASKBAR (f) = !NILP (new_value);
    }
}

void
mac_set_z_group (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  WMState flags_to_set, flags_to_clear;

  if (NILP (new_value))
    {
      flags_to_set = 0;
      flags_to_clear = WM_STATE_ABOVE | WM_STATE_BELOW;
      FRAME_Z_GROUP (f) = z_group_none;
    }
  else if (EQ (new_value, Qabove))
    {
      flags_to_set = WM_STATE_ABOVE;
      flags_to_clear = WM_STATE_BELOW;
      FRAME_Z_GROUP (f) = z_group_above;
    }
  else if (EQ (new_value, Qbelow))
    {
      flags_to_set = WM_STATE_BELOW;
      flags_to_clear = WM_STATE_ABOVE;
      FRAME_Z_GROUP (f) = z_group_below;
    }
  else if (EQ (new_value, Qabove_suspended))
    {
      flags_to_set = 0;
      flags_to_clear = WM_STATE_ABOVE;
      FRAME_Z_GROUP (f) = z_group_above_suspended;
    }
  else
    error ("Invalid z-group specification");

  block_input ();
  mac_change_frame_window_wm_state (f, flags_to_set, flags_to_clear);
  unblock_input ();
}

static void
mac_fullscreen_hook (struct frame *f)
{
  FRAME_CHECK_FULLSCREEN_NEEDED_P (f) = 1;
  if (FRAME_VISIBLE_P (f))
    {
      block_input ();
      mac_check_fullscreen (f);
      unblock_input ();
    }
}

/* Check if we need to resize the frame due to a fullscreen request.
   If so needed, resize the frame. */
static void
mac_check_fullscreen (struct frame *f)
{
  WMState flags_to_set, flags_to_clear;

  switch (f->want_fullscreen)
    {
    case FULLSCREEN_NONE:
      flags_to_set = 0;
      break;
    case FULLSCREEN_BOTH:
      flags_to_set = WM_STATE_FULLSCREEN;
      break;
    case FULLSCREEN_DEDICATED_DESKTOP:
      flags_to_set = (WM_STATE_FULLSCREEN | WM_STATE_DEDICATED_DESKTOP);
      break;
    case FULLSCREEN_WIDTH:
      flags_to_set = WM_STATE_MAXIMIZED_HORZ;
      break;
    case FULLSCREEN_HEIGHT:
      flags_to_set = WM_STATE_MAXIMIZED_VERT;
      break;
    case FULLSCREEN_MAXIMIZED:
      flags_to_set = (WM_STATE_MAXIMIZED_HORZ | WM_STATE_MAXIMIZED_VERT);
      break;
    }

  flags_to_clear = (flags_to_set ^ (WM_STATE_MAXIMIZED_HORZ
				    | WM_STATE_MAXIMIZED_VERT
				    | WM_STATE_FULLSCREEN
				    | WM_STATE_DEDICATED_DESKTOP));

  f->want_fullscreen = FULLSCREEN_NONE;
  FRAME_CHECK_FULLSCREEN_NEEDED_P (f) = 0;

  mac_change_frame_window_wm_state (f, flags_to_set, flags_to_clear);
}

/* Change the size of frame F's X window to WIDTH and HEIGHT pixels.  If
   CHANGE_GRAVITY, change to top-left-corner window gravity for this
   size change and subsequent size changes.  Otherwise we leave the
   window gravity unchanged.  */

static void
mac_set_window_size (struct frame *f, bool change_gravity,
		     int width, int height)
{
  block_input ();

  if (!FRAME_TOOLTIP_P (f)
      /* Don't override pending size change.  */
      && f->new_height == 0 && f->new_width == 0)
    {
      /* When the frame is maximized/fullscreen or running under for
         example Xmonad, x_set_window_size_1 will be a no-op.
         In that case, the right thing to do is extend rows/width to
         the current frame size.  We do that first if x_set_window_size_1
         turns out to not be a no-op (there is no way to know).
         The size will be adjusted again if the frame gets a
         ConfigureNotify event as a result of x_set_window_size.  */
      change_frame_size (f, FRAME_PIXEL_WIDTH (f), FRAME_PIXEL_HEIGHT (f),
			 false, true, false);
    }

  f->win_gravity = NorthWestGravity;
  mac_wm_set_size_hint (f, 0, false);

  mac_size_frame_window (f, width, height, true);

  SET_FRAME_GARBAGED (f);

  /* If cursor was outside the new size, mark it as off.  */
  mark_window_cursors_off (XWINDOW (f->root_window));

  unblock_input ();

  do_pending_window_change (false);
}

/* Move the mouse to position pixel PIX_X, PIX_Y relative to frame F.  */

void
frame_set_mouse_pixel_position (struct frame *f, int pix_x, int pix_y)
{
  block_input ();
  mac_convert_frame_point_to_global (f, &pix_x, &pix_y);
  CGWarpMouseCursorPosition (CGPointMake (pix_x, pix_y));
  unblock_input ();
}

/* Raise frame F.  */

static void
mac_raise_frame (struct frame *f)
{
  if (FRAME_VISIBLE_P (f))
    {
      block_input ();
      mac_bring_frame_window_to_front (f);
      unblock_input ();
    }
}

/* Lower frame F.  */

static void
mac_lower_frame (struct frame *f)
{
  if (FRAME_VISIBLE_P (f))
    {
      block_input ();
      mac_send_frame_window_behind (f);
      unblock_input ();
    }
}

static void
mac_frame_raise_lower (struct frame *f, bool raise_flag)
{
  if (raise_flag)
    mac_raise_frame (f);
  else
    mac_lower_frame (f);
}

static Lisp_Object
mac_get_focus_frame (struct frame *f)
{
  Lisp_Object lisp_focus;

  struct frame *focus =  FRAME_DISPLAY_INFO (f)->mac_focus_frame;

  if (!focus)
    return Qnil;

  XSETFRAME (lisp_focus, focus);
  return lisp_focus;
}

static void
mac_focus_frame (struct frame *f, bool noactivate)
{
  /* The arg NOACTIVATE is unused: the Mac port has been giving F the
     focus without raising it unnecessarily.  */
  block_input ();
  if (!mac_is_current_process_frontmost ())
    mac_bring_current_process_to_front (mac_is_frame_window_frontmost (f));
  mac_activate_frame_window (f);
  unblock_input ();
}


/* Change of visibility.  */

void
mac_handle_visibility_change (struct frame *f)
{
  int visible = 0;
  bool iconified = false;
  struct input_event buf;

  if (mac_is_frame_window_visible (f))
    {
      if (mac_is_frame_window_collapsed (f))
	iconified = true;
      else if (mac_is_frame_window_drawable (f))
	visible = 1;
      else
	visible = 2;
    }

  if (!FRAME_VISIBLE_P (f) && visible)
    {
      if (FRAME_CHECK_FULLSCREEN_NEEDED_P (f))
	mac_check_fullscreen (f);

      if (FRAME_ICONIFIED_P (f))
	{
	  EVENT_INIT (buf);
	  buf.kind = DEICONIFY_EVENT;
	  XSETFRAME (buf.frame_or_window, f);
	  buf.arg = Qnil;
	  kbd_buffer_store_event (&buf);
	}
    }
  else if (FRAME_OBSCURED_P (f) && visible == 1)
    SET_FRAME_GARBAGED (f);
  else if (FRAME_VISIBLE_P (f) && !visible)
    if (iconified)
      {
	EVENT_INIT (buf);
	buf.kind = ICONIFY_EVENT;
	XSETFRAME (buf.frame_or_window, f);
	buf.arg = Qnil;
	kbd_buffer_store_event (&buf);
      }

  SET_FRAME_VISIBLE (f, visible);
  /* Avoid unnecessary gui_consider_frame_title in
     SET_FRAME_ICONIFIED.  */
  if (f->iconified != iconified)
    SET_FRAME_ICONIFIED (f, iconified);
}

/* This tries to wait until the frame is really visible.
   However, if the window manager asks the user where to position
   the frame, this will return before the user finishes doing that.
   The frame will not actually be visible at that time,
   but it will become visible later when the window manager
   finishes with it.  */

void
mac_make_frame_visible (struct frame *f)
{
  if (FRAME_PARENT_FRAME (f))
    {
      if (!FRAME_VISIBLE_P (f))
	{
	  block_input ();
	  mac_show_frame_window (f);
	  unblock_input ();

	  SET_FRAME_VISIBLE (f, true);
	  SET_FRAME_ICONIFIED (f, false);
	}
      return;
    }

  block_input ();

  if (! FRAME_VISIBLE_P (f))
    {
      /* We test FRAME_GARBAGED_P here to make sure we don't
	 call mac_set_offset a second time
	 if we get to mac_make_frame_visible a second time
	 before the window gets really visible.  */
      if (! FRAME_ICONIFIED_P (f)
	  && ! f->output_data.mac->asked_for_visible)
	mac_set_offset (f, f->left_pos, f->top_pos, 0);

      f->output_data.mac->asked_for_visible = true;

      mac_collapse_frame_window (f, false);
      mac_show_frame_window (f);
    }

  XFlush (FRAME_MAC_DISPLAY (f));

  unblock_input ();

  mac_handle_visibility_change (f);
}

/* Change from mapped state to withdrawn state.  */

/* Make the frame visible (mapped and not iconified).  */

void
mac_make_frame_invisible (struct frame *f)
{
  /* A deactivate event does not occur when the last visible frame is
     made invisible.  So if we clear the highlight here, it will not
     be rehighlighted when it is made visible.  */
#if 0
  /* Don't keep the highlight on an invisible frame.  */
  if (FRAME_DISPLAY_INFO (f)->highlight_frame == f)
    FRAME_DISPLAY_INFO (f)->highlight_frame = 0;
#endif

  block_input ();

  /* Before unmapping the window, update the WM_SIZE_HINTS property to claim
     that the current position of the window is user-specified, rather than
     program-specified, so that when the window is mapped again, it will be
     placed at the same location, without forcing the user to position it
     by hand again (they have already done that once for this window.)  */
  mac_wm_set_size_hint (f, 0, true);

  mac_hide_frame_window (f);

  unblock_input ();

  mac_handle_visibility_change (f);
}

static void
mac_make_frame_visible_invisible (struct frame *f, bool visible)
{
  if (visible)
    mac_make_frame_visible (f);
  else
    mac_make_frame_invisible (f);
}

/* Change window state from mapped to iconified.  */

void
mac_iconify_frame (struct frame *f)
{
  OSStatus err;

  /* A deactivate event does not occur when the last visible frame is
     iconified.  So if we clear the highlight here, it will not be
     rehighlighted when it is deiconified.  */
#if 0
  /* Don't keep the highlight on an invisible frame.  */
  if (FRAME_DISPLAY_INFO (f)->highlight_frame == f)
    FRAME_DISPLAY_INFO (f)->highlight_frame = 0;
#endif

  if (FRAME_ICONIFIED_P (f))
    return;

  block_input ();

  if (! FRAME_VISIBLE_P (f))
    mac_show_frame_window (f);

  err = mac_collapse_frame_window (f, true);

  unblock_input ();

  if (err != noErr)
    error ("Can't notify window manager of iconification");

  mac_handle_visibility_change (f);
}


/* Free X resources of frame F.  */

void
mac_free_frame_resources (struct frame *f)
{
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  Mouse_HLInfo *hlinfo = &dpyinfo->mouse_highlight;

  block_input ();

  /* AppKit version of mac_dispose_frame_window, which is implemented
     as -[NSWindow close], will change the focus to the next window
     during its call.  So, unlike other platforms, we clean up the
     focus-related variables before calling mac_dispose_frame_window.  */
  if (f == dpyinfo->mac_focus_frame)
    {
      dpyinfo->mac_focus_frame = 0;
      mac_set_font_info_for_selection (NULL, DEFAULT_FACE_ID, 0, -1, Qnil);
    }
  if (f == dpyinfo->mac_focus_event_frame)
    dpyinfo->mac_focus_event_frame = 0;
  if (f == dpyinfo->highlight_frame)
    dpyinfo->highlight_frame = 0;
  if (f == hlinfo->mouse_face_mouse_frame)
    reset_mouse_highlight (hlinfo);

  mac_flush_arena (f);
  mac_get_backing_bitmap (f);
  mac_teardown_arena_system (f);

  if (FRAME_MAC_WINDOW (f))
    mac_dispose_frame_window (f);

  free_frame_menubar (f);

  free_frame_faces (f);

  mac_free_gcs (f);

  xfree (FRAME_SIZE_HINTS (f));

  /* Free cursors.  */
  mac_cursor_release (f->output_data.mac->text_cursor);
  mac_cursor_release (f->output_data.mac->nontext_cursor);
  mac_cursor_release (f->output_data.mac->modeline_cursor);
  mac_cursor_release (f->output_data.mac->hand_cursor);
  mac_cursor_release (f->output_data.mac->hourglass_cursor);
  mac_cursor_release (f->output_data.mac->horizontal_drag_cursor);
  mac_cursor_release (f->output_data.mac->vertical_drag_cursor);
  mac_cursor_release (f->output_data.mac->left_edge_cursor);
  mac_cursor_release (f->output_data.mac->top_left_corner_cursor);
  mac_cursor_release (f->output_data.mac->top_edge_cursor);
  mac_cursor_release (f->output_data.mac->top_right_corner_cursor);
  mac_cursor_release (f->output_data.mac->right_edge_cursor);
  mac_cursor_release (f->output_data.mac->bottom_right_corner_cursor);
  mac_cursor_release (f->output_data.mac->bottom_edge_cursor);
  mac_cursor_release (f->output_data.mac->bottom_left_corner_cursor);
  
  xfree (f->output_data.mac);
  f->output_data.mac = NULL;

  unblock_input ();
}


/* Destroy the X window of frame F.  */

static void
mac_destroy_window (struct frame *f)
{
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);

  mac_free_frame_resources (f);

  dpyinfo->reference_count--;
}


/* Setting window manager hints.  */

/* Set the normal size hints for the window manager, for frame F.
   FLAGS is the flags word to use--or 0 meaning preserve the flags
   that the window now has.
   If USER_POSITION, set the USPosition
   flag (this is useful when FLAGS is 0).  */
void
mac_wm_set_size_hint (struct frame *f, long flags, bool user_position)
{
  int base_width, base_height;
  XSizeHints *size_hints;

  base_width = FRAME_TEXT_COLS_TO_PIXEL_WIDTH (f, 0);
  base_height = FRAME_TEXT_LINES_TO_PIXEL_HEIGHT (f, 0);

  size_hints = FRAME_SIZE_HINTS (f);
  if (size_hints == NULL)
    size_hints = FRAME_SIZE_HINTS (f) = xzalloc (sizeof (XSizeHints));

  size_hints->flags |= PResizeInc | PMinSize | PBaseSize ;
  size_hints->width_inc = frame_resize_pixelwise ? 1 : FRAME_COLUMN_WIDTH (f);
  size_hints->height_inc = frame_resize_pixelwise ? 1 : FRAME_LINE_HEIGHT (f);
  size_hints->min_width  = base_width;
  size_hints->min_height = base_height;
  size_hints->base_width  = base_width;
  size_hints->base_height = base_height;

  if (flags)
    size_hints->flags = flags;
  else if (user_position)
    {
      size_hints->flags &= ~ PPosition;
      size_hints->flags |= USPosition;
    }
}


/***********************************************************************
				Fonts
 ***********************************************************************/

#ifdef GLYPH_DEBUG

/* Check that FONT is valid on frame F.  It is if it can be found in F's
   font table.  */

static void
mac_check_font (struct frame *f, struct font *font)
{
  eassert (font != NULL && ! NILP (font->props[FONT_TYPE_INDEX]));
  if (font->driver->check)
    eassert (font->driver->check (f, font) == 0);
}

#endif /* GLYPH_DEBUG */


/* The Mac Event loop code */

/* Whether or not the screen configuration has changed.  */
bool mac_screen_config_changed = 0;

/* Table for translating Mac keycode to X keysym values.  Contributed
   by Sudhir Shenoy.
   Mapping for special keys is now identical to that in Apple X11
   except `clear' (-> <clear>) on the KeyPad, `enter' (-> <kp-enter>)
   on the right of the Cmd key on laptops, and fn + `enter' (->
   <linefeed>). */
static const unsigned char keycode_to_xkeysym_table[] = {
  /*0x00*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*0x10*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*0x20*/ 0, 0, 0, 0, 0x0d /*return*/, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

  /*0x30*/ 0x09 /*tab*/, 0 /*0x0020 space*/, 0, 0x08 /*backspace*/,
  /*0x34*/ 0x8d /*enter on laptops*/, 0x1b /*escape*/, 0, 0,
  /*0x38*/ 0, 0, 0, 0,
  /*0x3C*/ 0, 0, 0, 0,

  /*0x40*/ 0xce /*f17*/, 0xae /*kp-decimal*/, 0, 0xaa /*kp-multiply*/,
  /*0x44*/ 0, 0xab /*kp-add*/, 0, 0x0b /*clear*/,
  /*0x48*/ 0, 0, 0, 0xaf /*kp-divide*/,
  /*0x4C*/ 0x8d /*kp-enter*/, 0, 0xad /*kp-subtract*/, 0xcf /*f18*/,

  /*0x50*/ 0xd0 /*f19*/, 0xbd /*kp-equal*/, 0xb0 /*kp-0*/, 0xb1 /*kp-1*/,
  /*0x54*/ 0xb2 /*kp-2*/, 0xb3 /*kp-3*/, 0xb4 /*kp-4*/, 0xb5 /*kp-5*/,
  /*0x58*/ 0xb6 /*kp-6*/, 0xb7 /*kp-7*/, 0xd1 /*f20*/, 0xb8 /*kp-8*/,
  /*0x5C*/ 0xb9 /*kp-9*/, 0, 0, 0xac /*kp-separator*/,

  /*0x60*/ 0xc2 /*f5*/, 0xc3 /*f6*/, 0xc4 /*f7*/, 0xc0 /*f3*/,
  /*0x64*/ 0xc5 /*f8*/, 0xc6 /*f9*/, 0, 0xc8 /*f11*/,
  /*0x68*/ 0, 0xca /*f13*/, 0xcd /*f16*/, 0xcb /*f14*/,
  /*0x6C*/ 0, 0xc7 /*f10*/, 0x0a /*fn+enter on laptops*/, 0xc9 /*f12*/,

  /*0x70*/ 0, 0xcc /*f15*/, 0x6a /*help*/, 0x50 /*home*/,
  /*0x74*/ 0x55 /*pgup*/, 0xff /*delete*/, 0xc1 /*f4*/, 0x57 /*end*/,
  /*0x78*/ 0xbf /*f2*/, 0x56 /*pgdown*/, 0xbe /*f1*/, 0x51 /*left*/,
  /*0x7C*/ 0x53 /*right*/, 0x54 /*down*/, 0x52 /*up*/, 0
};

/* Table for translating Mac keycode with the `fn' key to that without
   it.  Destination symbols in comments are keys on US keyboard, and
   they may not be the same on other types of keyboards.  If the
   destination is identical to the source, it doesn't map `fn' key to
   a modifier.  */
static const unsigned char fn_keycode_to_keycode_table[] = {
  /*0x00*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*0x10*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*0x20*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

  /*0x30*/ 0, 0, 0, 0,
  /*0x34*/ 0, 0, 0, 0,
  /*0x38*/ 0, 0, 0, 0,
  /*0x3C*/ 0, 0, 0, 0,

  /*0x40*/ 0x40 /*f17 = f17*/, 0x2f /*kp-decimal -> '.'*/, 0, 0x23 /*kp-multiply -> 'p'*/,
  /*0x44*/ 0, 0x2c /*kp-add -> '/'*/, 0, 0x16 /*clear -> '6'*/,
  /*0x48*/ 0, 0, 0, 0x1d /*kp-/ -> '0'*/,
  /*0x4C*/ 0x24 /*kp-enter -> return*/, 0, 0x29 /*kp-subtract -> ';'*/, 0x4f /*f18 = f18*/,

  /*0x50*/ 0x50 /*f19 = f19*/, 0x1b /*kp-equal -> '-'*/, 0x2e /*kp-0 -> 'm'*/, 0x26 /*kp-1 -> 'j'*/,
  /*0x54*/ 0x28 /*kp-2 -> 'k'*/, 0x25 /*kp-3 -> 'l'*/, 0x20 /*kp-4 -> 'u'*/, 0x22 /*kp-5 ->'i'*/,
  /*0x58*/ 0x1f /*kp-6 -> 'o'*/, 0x1a /*kp-7 -> '7'*/, 0x5a /*f20 = f20*/, 0x1c /*kp-8 -> '8'*/,
  /*0x5C*/ 0x19 /*kp-9 -> '9'*/, 0, 0, 0,

  /*0x60*/ 0x60 /*f5 = f5*/, 0x61 /*f6 = f6*/, 0x62 /*f7 = f7*/, 0x63 /*f3 = f3*/,
  /*0x64*/ 0x64 /*f8 = f8*/, 0x65 /*f9 = f9*/, 0, 0x67 /*f11 = f11*/,
  /*0x68*/ 0, 0x69 /*f13 = f13*/, 0x6a /*f16 = f16*/, 0x6b /*f14 = f14*/,
  /*0x6C*/ 0, 0x6d /*f10 = f10*/, 0, 0x6f /*f12 = f12*/,

  /*0x70*/ 0, 0x71 /*f15 = f15*/, 0x72 /*help = help*/, 0x7b /*home -> left*/,
  /*0x74*/ 0x7e /*pgup -> up*/, 0x33 /*delete -> backspace*/, 0x76 /*f4 = f4*/, 0x7c /*end -> right*/,
  /*0x78*/ 0x78 /*f2 = f2*/, 0x7d /*pgdown -> down*/, 0x7a /*f1 = f1*/, 0x7b /*left = left*/,
  /*0x7C*/ 0x7c /*right = right*/, 0x7d /*down = down*/, 0x7e /*up = up*/, 0
};

/* Convert CGEvent flags to Carbon key modifiers.  */

UInt32
mac_cgevent_flags_to_modifiers (CGEventFlags flags)
{
  UInt32 modifiers = 0;

  if (flags & kCGEventFlagMaskAlphaShift)
    modifiers |= alphaLock;
  if (flags & kCGEventFlagMaskShift)
    modifiers |= shiftKey;
  if (flags & kCGEventFlagMaskControl)
    modifiers |= controlKey;
  if (flags & kCGEventFlagMaskAlternate)
    modifiers |= optionKey;
  if (flags & kCGEventFlagMaskCommand)
    modifiers |= cmdKey;
  /* if (flags & kCGEventFlagMaskHelp); */
  if (flags & kCGEventFlagMaskSecondaryFn)
    modifiers |= kEventKeyModifierFnMask;
  if (flags & kCGEventFlagMaskNumericPad)
    modifiers |= kEventKeyModifierNumLockMask;

  return modifiers;
}

/* Update the Unicode string in a Quartz event CGEVENT according to
   its keycode, flags, and keyboard type values.  Return true if and
   only if the Unicode string is successfully updated.  */

static bool
mac_cgevent_update_unicode_string (CGEventRef cgevent)
{
  bool result = false;
  UInt32 modifiers = mac_cgevent_flags_to_modifiers (CGEventGetFlags (cgevent));
  UInt32 keycode = CGEventGetIntegerValueField (cgevent,
						kCGKeyboardEventKeycode);
  UCKeyboardLayout *uchr_ptr = NULL;
  TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource ();
  CFDataRef uchr_data = NULL;

  if (source)
    uchr_data =
      TISGetInputSourceProperty (source, kTISPropertyUnicodeKeyLayoutData);
  if (uchr_data)
    uchr_ptr = (UCKeyboardLayout *) CFDataGetBytePtr (uchr_data);

  if (uchr_ptr)
    {
      OSStatus status;
      UInt16 key_action =
	(CGEventGetIntegerValueField (cgevent, kCGKeyboardEventAutorepeat)
	 ? kUCKeyActionAutoKey : kUCKeyActionDown);
      UInt32 modifier_key_state = modifiers >> 8;
      UInt32 keyboard_type =
	CGEventGetIntegerValueField (cgevent, kCGKeyboardEventKeyboardType);
      UInt32 dead_key_state = 0;
      UniCharCount actual_length;
      UniChar string[255];

      status = UCKeyTranslate (uchr_ptr, keycode, key_action,
			       modifier_key_state, keyboard_type,
			       kUCKeyTranslateNoDeadKeysMask,
			       &dead_key_state, 255, &actual_length, string);
      if (status == noErr)
	{
	  CGEventKeyboardSetUnicodeString (cgevent, actual_length, string);
	  result = true;
	}
    }
  if (source)
    CFRelease (source);

  return result;
}

static Lisp_Object
mac_modifier_map_lookup (Lisp_Object modifier_map, Lisp_Object kind)
{
  if (SYMBOLP (modifier_map))
    return modifier_map;
  else
    {
      Lisp_Object value = plist_get (modifier_map, kind);

      return SYMBOLP (value) ? value : Qnil;
    }
}

static bool
mac_modifier_map_button (Lisp_Object modifier_map, ptrdiff_t *code)
{
  Lisp_Object value = plist_get (modifier_map, QCbutton);
  ptrdiff_t c = *code;

  while (CONSP (value) && c > 0)
    value = XCDR (value), c--;
  if (c == 0)
    {
      if (CONSP (value))
	value = XCAR (value);
      if (INTEGERP (value) && XFIXNUM (value) > 0 && *code != XFIXNUM (value) - 1)
	{
	  *code = XFIXNUM (value) - 1;

	  return true;
	}
    }

  return false;
}

/* Convert a Quartz event CGEVENT to an input event, and update
   `modifier', `code', `timestamp' (and also `kind' for key-down
   events) members of *BUF if BUF is not NULL.  Return bitwise-or of
   CGEvent flag masks that have been mapped to emacs modifiers.  */

CGEventFlags
mac_cgevent_to_input_event (CGEventRef cgevent, struct input_event *buf)
{
  CGEventMask key_event_mask =
    (CGEventMaskBit (kCGEventKeyDown) | CGEventMaskBit (kCGEventKeyUp));
  CGEventMask mouse_button_event_mask =
    (CGEventMaskBit (kCGEventLeftMouseDown)
     | CGEventMaskBit (kCGEventLeftMouseUp)
     | CGEventMaskBit (kCGEventRightMouseDown)
     | CGEventMaskBit (kCGEventRightMouseUp)
     | CGEventMaskBit (kCGEventLeftMouseDragged)
     | CGEventMaskBit (kCGEventRightMouseDragged)
     | CGEventMaskBit (kCGEventOtherMouseDown)
     | CGEventMaskBit (kCGEventOtherMouseUp)
     | CGEventMaskBit (kCGEventOtherMouseDragged));
  CGEventMask other_mouse_event_mask =
    (CGEventMaskBit (kCGEventMouseMoved)
     | CGEventMaskBit (kCGEventScrollWheel)
     | CGEventMaskBit (29)	/* NSEventTypeGesture */
     /* Probably NSEventTypeGesture above covers all the cases below.
	But just in case...  */
     | CGEventMaskBit (30)	/* NSEventTypeMagnify */
     | CGEventMaskBit (31)	/* NSEventTypeSwipe */
     | CGEventMaskBit (18)	/* NSEventTypeRotate */
     | CGEventMaskBit (19)	/* NSEventTypeBeginGesture */
     | CGEventMaskBit (20)	/* NSEventTypeEndGesture */
     | CGEventMaskBit (32)	/* NSEventTypeSmartMagnify */
     | CGEventMaskBit (33)	/* NSEventTypeQuickLook */
     | CGEventMaskBit (34)	/* NSEventTypePressure */
     | CGEventMaskBit (37)	/* NSEventTypeDirectTouch */
     | CGEventMaskBit (38));	/* NSEventTypeChangeMode */
  CGEventFlags possibly_mapped_flags =
    (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate
     | kCGEventFlagMaskCommand | kCGEventFlagMaskSecondaryFn);
  enum side {LEFT, RIGHT, NSIDES};
  static const struct {
    CGEventFlags device_indep;
    CGEventFlags device_deps[NSIDES];
  } mask_table[] = {{kCGEventFlagMaskControl,
		     {NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK}},
		    {kCGEventFlagMaskAlternate,
		     {NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK}},
		    {kCGEventFlagMaskCommand,
		     {NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK}},
		    {kCGEventFlagMaskSecondaryFn,
		     {0, 0}}};
  static Lisp_Object *const modifier_maps[][NSIDES] =
    {{&Vmac_control_modifier, &Vmac_right_control_modifier},
     {&Vmac_option_modifier, &Vmac_right_option_modifier},
     {&Vmac_command_modifier, &Vmac_right_command_modifier},
     {&Vmac_function_modifier, NULL}};
  Lisp_Object kind;
  int i, keycode, emacs_modifiers = 0;
  ptrdiff_t code = -1;
  bool map_button_p = false;
  CGEventFlags mapped_flags = 0;
  CGEventMask type_mask;
  CGEventFlags flags = CGEventGetFlags (cgevent);

  if (buf == NULL && (flags & possibly_mapped_flags) == 0)
    return 0;

  type_mask = CGEventMaskBit (CGEventGetType (cgevent));
  if (type_mask & key_event_mask)
    {
      keycode = CGEventGetIntegerValueField (cgevent, kCGKeyboardEventKeycode);

      if ((flags & kCGEventFlagMaskSecondaryFn)
	  /* We exclude the case `keycode == 0' (kVK_ANSI_A) because
	     the condition `fn_keycode_to_keycode_table[keycode] ==
	     keycode' holds bogusly.  */
	  && keycode > 0 && keycode <= 0x7f)
	{
	  ptrdiff_t stripped = fn_keycode_to_keycode_table[keycode];

	  /* The meaning of kCGEventFlagMaskSecondaryFn has changed in
	     Mac OS X 10.5, and it now behaves much like Cocoa's
	     NSFunctionKeyMask.  It no longer means `fn' key is down
	     for the following keys: F1, F2, and so on, Help, Forward
	     Delete, Home, End, Page Up, Page Down, the arrow keys,
	     and Clear.  We ignore the corresponding bit if that key
	     can be entered without the `fn' key.  */
	  if (stripped == keycode)
	    flags &= ~kCGEventFlagMaskSecondaryFn;
	  else if (stripped)
	    {
	      Lisp_Object k = (keycode_to_xkeysym_table[stripped]
			       ? QCfunction : QCordinary);

	      if (!NILP (mac_modifier_map_lookup (Vmac_function_modifier, k)))
		keycode = stripped;
	    }
	}

      if (keycode >= 0 && keycode <= 0x7f && keycode_to_xkeysym_table[keycode])
	kind = QCfunction;
      else
	kind = QCordinary;
    }
  else if (type_mask & mouse_button_event_mask)
    {
      code = CGEventGetIntegerValueField (cgevent, kCGMouseEventButtonNumber);

      if (mac_wheel_button_is_mouse_2)
	{
	  if (code == kCGMouseButtonRight)
	    code = kCGMouseButtonCenter;
	  else if (code == kCGMouseButtonCenter)
	    code = kCGMouseButtonRight;
	}

      if (NILP (Vmac_emulate_three_button_mouse))
	map_button_p = true;
      else
	{
	  if (code == kCGMouseButtonLeft
	      && (flags & (kCGEventFlagMaskAlternate
			   | kCGEventFlagMaskCommand)))
	    {
	      CGEventFlags mask_for_center =
		(!EQ (Vmac_emulate_three_button_mouse, Qreverse)
		 ? kCGEventFlagMaskCommand : kCGEventFlagMaskAlternate);

	      if (flags & mask_for_center)
		{
		  code = kCGMouseButtonCenter;
		  flags &= ~mask_for_center;
		}
	      else
		{
		  code = kCGMouseButtonRight;
		  flags &= ~(kCGEventFlagMaskAlternate
			     | kCGEventFlagMaskCommand);
		}
	    }
	}

      kind = QCmouse;
    }
  else if (type_mask & other_mouse_event_mask)
    kind = QCmouse;
  else
    kind = QCordinary;

  if (flags & kCGEventFlagMaskShift)
    emacs_modifiers |= shift_modifier;
  for (i = 0; i < ARRAYELTS (mask_table); i++)
    if (flags & mask_table[i].device_indep)
      {
	Lisp_Object modifier_symbols[NSIDES];
	bool lookup_left_p = true;
	int n = 0;

	if (flags & mask_table[i].device_deps[RIGHT])
	  {
	    if (map_button_p
		&& mac_modifier_map_button (*modifier_maps[i][RIGHT], &code))
	      map_button_p = lookup_left_p = false;
	    else
	      {
		Lisp_Object right_modifier_symbol =
		  mac_modifier_map_lookup (*modifier_maps[i][RIGHT], kind);

		if (!EQ (right_modifier_symbol, Qleft))
		  {
		    modifier_symbols[n++] = right_modifier_symbol;
		    if (!(flags & mask_table[i].device_deps[LEFT]))
		      lookup_left_p = false;
		  }
	      }
	  }
	if (lookup_left_p)
	  {
	    if (map_button_p
		&& mac_modifier_map_button (*modifier_maps[i][LEFT], &code))
	      map_button_p = false;
	    else
	      modifier_symbols[n++] =
		mac_modifier_map_lookup (*modifier_maps[i][LEFT], kind);
	  }

	flags &= ~mask_table[i].device_indep;
	while (n > 0)
	  {
	    Lisp_Object modifier_symbol = modifier_symbols[--n];

	    if (NILP (modifier_symbol))
	      flags |= mask_table[i].device_indep;
	    else
	      {
		Lisp_Object value = Fget (modifier_symbol, Qmodifier_value);

		if (FIXNATP (value))
		  {
		    emacs_modifiers |= XFIXNAT (value);
		    mapped_flags |= mask_table[i].device_indep;
		  }
	      }
	  }
      }

  if (buf == NULL)
    return mapped_flags;

  if (type_mask == CGEventMaskBit (kCGEventKeyDown))
    {
      if (keycode >= 0 && keycode <= 0x7f && keycode_to_xkeysym_table[keycode])
	{
	  buf->kind = NON_ASCII_KEYSTROKE_EVENT;
	  code = 0xff00 | keycode_to_xkeysym_table[keycode];
	}
      else
	{
	  UniCharCount length;
	  UniChar text[2];

	  if ((emacs_modifiers & ~shift_modifier) == 0)
	    CGEventKeyboardGetUnicodeString (cgevent, 2, &length, text);
	  else
	    {
	      CGEventRef tmp = CGEventCreateCopy (cgevent);

	      CGEventSetIntegerValueField (tmp, kCGKeyboardEventKeycode,
					   keycode);
	      CGEventSetFlags (tmp, flags);
	      mac_cgevent_update_unicode_string (tmp);
	      CGEventKeyboardGetUnicodeString (tmp, 2, &length, text);
	      CFRelease (tmp);
	    }

	  if (length == 1)
	    {
	      if (text[0] < 0x80)
		buf->kind = ASCII_KEYSTROKE_EVENT;
	      else
		buf->kind = MULTIBYTE_CHAR_KEYSTROKE_EVENT;
	      code = text[0];
	    }
	  else if (length == 2
		   && UCIsSurrogateHighCharacter (text[0])
		   && UCIsSurrogateLowCharacter (text[1]))
	    {
	      buf->kind = MULTIBYTE_CHAR_KEYSTROKE_EVENT;
	      code = UCGetUnicodeScalarValueForSurrogatePair (text[0], text[1]);
	    }
	  else
	    {
	      buf->kind = NO_EVENT;
	      code = 0;
	    }
	}
      emacs_modifiers |= (extra_keyboard_modifiers
			  & (meta_modifier | alt_modifier
			     | hyper_modifier | super_modifier));
    }

  buf->modifiers = emacs_modifiers;
  buf->code = code;
  buf->timestamp = CGEventGetTimestamp (cgevent) / kMillisecondScale;

  return mapped_flags;
}

static void
mac_get_selected_range (struct window *w, CFRange *range)
{
  struct buffer *b = XBUFFER (w->contents);
  EMACS_INT begv = BUF_BEGV (b), zv = BUF_ZV (b);
  EMACS_INT start, end;

  if (w == XWINDOW (selected_window) && b == current_buffer)
    start = PT;
  else
    start = marker_position (w->pointm);

  if (NILP (Vtransient_mark_mode) || NILP (BVAR (b, mark_active))
      || XMARKER (BVAR (b, mark))->buffer == NULL)
    end = start;
  else
    {
      EMACS_INT mark_pos = marker_position (BVAR (b, mark));

      if (start <= mark_pos)
	end = mark_pos;
      else
	{
	  end = start;
	  start = mark_pos;
	}
    }

  if (start != end)
    {
      if (start < begv)
	start = begv;
      else if (start > zv)
	start = zv;

      if (end < begv)
	end = begv;
      else if (end > zv)
	end = zv;
    }

  range->location = start - begv;
  range->length = end - start;
}

/* Like fetch_char_advance, but fetch character from the buffer BUF.  */

static inline int
buf_fetch_char_advance (struct buffer *buf, ptrdiff_t *charidx,
			ptrdiff_t *byteidx)
{
  int output;
  ptrdiff_t c = *charidx, b = *byteidx;
  c++;
  unsigned char *chp = BUF_BYTE_ADDRESS (buf, b);
  if (!NILP (BVAR (buf, enable_multibyte_characters)))
    {
      int chlen;
      output = string_char_and_length (chp, &chlen);
      b += chlen;
    }
  else
    {
      output = *chp;
      b++;
    }
  *charidx = c;
  *byteidx = b;
  return output;
}

/* Store the text of the buffer BUF from START to END as Unicode
   characters in CHARACTERS.  Return true if successful.  */

static bool
mac_store_buffer_text_to_unicode_chars (struct buffer *buf, EMACS_INT start,
					EMACS_INT end, UniChar *characters)
{
  EMACS_INT start_byte = buf_charpos_to_bytepos (buf, start);

  while (start < end)
    {
      int c;

      c = buf_fetch_char_advance (buf, &start, &start_byte);
      *characters++ = (c < 0xD800 || (c > 0xDFFF && c < 0x10000)) ? c : 0xfffd;
    }

  return true;
}

CGRect
mac_get_first_rect_for_range (struct window *w, const CFRange *range,
			      CFRange *actual_range)
{
  struct buffer *b = XBUFFER (w->contents);
  EMACS_INT start_charpos, end_charpos, min_charpos, max_charpos;
  struct glyph_row *row;
  struct glyph *glyph, *end, *left_glyph, *right_glyph;
  int x, left_x, right_x, text_area_width;

  start_charpos = BUF_BEGV (b) + range->location;
  end_charpos = start_charpos + range->length;
  if (range->length == 0)
    {
      end_charpos++;
      row = row_containing_pos (w, start_charpos,
				MATRIX_FIRST_TEXT_ROW (w->current_matrix),
				NULL, 0);
      if (row == NULL)
	row = MATRIX_ROW (w->current_matrix, w->window_end_vpos);
    }
  else
    {
      struct glyph_row *r2;

      /* Find the rows corresponding to START_CHARPOS and END_CHARPOS.  */
      rows_from_pos_range (w, start_charpos, end_charpos, Qnil, &row, &r2);
      if (row == NULL)
	row = MATRIX_ROW (w->current_matrix, w->window_end_vpos);
      if (r2 == NULL)
	r2 = MATRIX_ROW (w->current_matrix, w->window_end_vpos);
      if (row->y > r2->y)
	row = r2;
    }

  if (!row->reversed_p)
    {
      /* This row is in a left to right paragraph.  Scan it left to
	 right.  */
      glyph = row->glyphs[TEXT_AREA];
      end = glyph + row->used[TEXT_AREA];
      x = row->x;

      /* Skip truncation glyphs at the start of the glyph row.  */
      if (row->displays_text_p)
	for (; glyph < end
	       && INTEGERP (glyph->object)
	       && glyph->charpos < 0;
	     ++glyph)
	  x += glyph->pixel_width;

      /* Scan the glyph row, looking for the first glyph from buffer
	 whose position is between START_CHARPOS and END_CHARPOS.  */
      for (; glyph < end
	     && !INTEGERP (glyph->object)
	     && !(BUFFERP (glyph->object)
		  && (glyph->charpos >= start_charpos
		      && glyph->charpos < end_charpos));
	   ++glyph)
	x += glyph->pixel_width;

      left_x = x;
      left_glyph = glyph;
    }
  else
    {
      /* This row is in a right to left paragraph.  Scan it right to
	 left.  */
      struct glyph *g;

      end = row->glyphs[TEXT_AREA] - 1;
      glyph = end + row->used[TEXT_AREA];

      /* Skip truncation glyphs at the start of the glyph row.  */
      if (row->displays_text_p)
	for (; glyph > end
	       && INTEGERP (glyph->object)
	       && glyph->charpos < 0;
	     --glyph)
	  ;

      /* Scan the glyph row, looking for the first glyph from buffer
	 whose position is between START_CHARPOS and END_CHARPOS.  */
      for (; glyph > end
	     && !INTEGERP (glyph->object)
	     && !(BUFFERP (glyph->object)
		  && (glyph->charpos >= start_charpos
		      && glyph->charpos < end_charpos));
	   --glyph)
	;

      glyph++; /* first glyph to the right of the first rect */
      for (g = row->glyphs[TEXT_AREA], x = row->x; g < glyph; g++)
	x += g->pixel_width;

      right_x = x;
      right_glyph = glyph;
    }

  if (range->length == 0)
    {
      min_charpos = max_charpos = start_charpos;
      if (!row->reversed_p)
	right_x = left_x;
      else
	left_x = right_x;
    }
  else
    {
      if (!row->reversed_p)
	{
	  if (MATRIX_ROW_END_CHARPOS (row) <= end_charpos)
	    {
	      min_charpos = max_charpos = MATRIX_ROW_END_CHARPOS (row);
	      right_x = INT_MAX;
	      right_glyph = end;
	    }
	  else
	    {
	      /* Skip truncation and continuation glyphs near the end
		 of the row, and also blanks and stretch glyphs
		 inserted by extend_face_to_end_of_line.  */
	      while (end > glyph
		     && INTEGERP ((end - 1)->object))
		--end;
	      /* Scan the rest of the glyph row from the end, looking
		 for the first glyph whose position is between
		 START_CHARPOS and END_CHARPOS */
	      for (--end;
		   end > glyph
		     && !INTEGERP (end->object)
		     && !(BUFFERP (end->object)
			  && (end->charpos >= start_charpos
			      && end->charpos < end_charpos));
		   --end)
		;
	      /* Find the X coordinate of the last glyph of the first
		 rect.  */
	      for (; glyph <= end; ++glyph)
		x += glyph->pixel_width;

	      min_charpos = end_charpos;
	      max_charpos = start_charpos;
	      right_x = x;
	      right_glyph = glyph;
	    }
	}
      else
	{
	  if (MATRIX_ROW_END_CHARPOS (row) <= end_charpos)
	    {
	      min_charpos = max_charpos = MATRIX_ROW_END_CHARPOS (row);
	      left_x = 0;
	      left_glyph = end + 1;
	    }
	  else
	    {
	      /* Skip truncation and continuation glyphs near the end
		 of the row, and also blanks and stretch glyphs
		 inserted by extend_face_to_end_of_line.  */
	      x = row->x;
	      end++;
	      while (end < glyph
		     && INTEGERP (end->object))
		{
		  x += end->pixel_width;
		  ++end;
		}
	      /* Scan the rest of the glyph row from the end, looking
		 for the first glyph whose position is between
		 START_CHARPOS and END_CHARPOS */
	      for ( ;
		    end < glyph
		      && !INTEGERP (end->object)
		      && !(BUFFERP (end->object)
			   && (end->charpos >= start_charpos
			       && end->charpos < end_charpos));
		    ++end)
		x += end->pixel_width;

	      min_charpos = end_charpos;
	      max_charpos = start_charpos;
	      left_x = x;
	      left_glyph = end;
	    }
	}

      for (glyph = left_glyph; glyph < right_glyph; ++glyph)
	if (!STRINGP (glyph->object) && glyph->charpos > 0)
	  {
	    if (glyph->charpos < min_charpos)
	      min_charpos = glyph->charpos;
	    if (glyph->charpos + 1 > max_charpos)
	      max_charpos = glyph->charpos + 1;
	  }
      if (min_charpos > max_charpos)
	min_charpos = max_charpos;
    }

  if (actual_range)
    {
      actual_range->location = min_charpos - BUF_BEGV (b);
      actual_range->length = max_charpos - min_charpos;
    }

  text_area_width = window_box_width (w, TEXT_AREA);
  if (left_x < 0)
    left_x = 0;
  else if (left_x > text_area_width)
    left_x = text_area_width;
  if (right_x < 0)
    right_x = 0;
  else if (right_x > text_area_width)
    right_x = text_area_width;

  return CGRectMake (WINDOW_TEXT_TO_FRAME_PIXEL_X (w, left_x),
		     WINDOW_TO_FRAME_PIXEL_Y (w, row->y),
		     right_x - left_x, row->height);
}

void
mac_ax_selected_text_range (struct frame *f, CFRange *range)
{
  mac_get_selected_range (XWINDOW (f->selected_window), range);
}

EMACS_INT
mac_ax_number_of_characters (struct frame *f)
{
  struct buffer *b = XBUFFER (XWINDOW (f->selected_window)->contents);

  return BUF_ZV (b) - BUF_BEGV (b);
}

void
mac_ax_visible_character_range (struct frame *f, CFRange *range)
{
  struct window *w = XWINDOW (f->selected_window);
  struct buffer *b = XBUFFER (w->contents);
  EMACS_INT start, end;

  /* XXX: Check validity of window_end_pos?  */
  start = marker_position (w->start);
  end = BUF_Z (b) - w->window_end_pos;

  range->location = start - BUF_BEGV (b);
  range->length = end - start;
}

EMACS_INT
mac_ax_line_for_index (struct frame *f, EMACS_INT index)
{
  struct buffer *b = XBUFFER (XWINDOW (f->selected_window)->contents);
  EMACS_INT line;
  const unsigned char *limit, *begv, *zv, *gap_end, *p;

  if (index >= 0)
    limit = BUF_CHAR_ADDRESS (b, BUF_BEGV (b) + index);
  else
    limit = BUF_BYTE_ADDRESS (b, BUF_PT_BYTE (b));
  begv = BUF_BYTE_ADDRESS (b, BUF_BEGV_BYTE (b));
  zv = BUF_BYTE_ADDRESS (b, BUF_ZV_BYTE (b));

  if (limit < begv || limit > zv)
    return -1;

  line = 0;
  gap_end = BUF_GAP_END_ADDR (b);
  if (begv < gap_end && gap_end <= limit)
    {
      for (p = gap_end; (p = memchr (p, '\n', limit - p)) != NULL; p++)
	line++;
      limit = BUF_GPT_ADDR (b);
    }

  for (p = begv; (p = memchr (p, '\n', limit - p)) != NULL; p++)
    line++;

  return line;
}

static ptrdiff_t
mac_ax_buffer_skip_lines_offset (struct buffer *buf, EMACS_INT n,
                                ptrdiff_t start_byte, ptrdiff_t end_byte)
{
  ptrdiff_t pos = start_byte;

  while (n > 0 && pos < end_byte)
    {
      /* We calculate addresses INSIDE the loop. 
         This way, if the gap moved in the last iteration, 
         we get the fresh, correct address for this iteration. */
      unsigned char *p = BUF_BYTE_ADDRESS (buf, pos);
      unsigned char *gpt = BUF_GPT_ADDR (buf);
      unsigned char *end_ptr = BUF_BYTE_ADDRESS (buf, end_byte);
      unsigned char *limit;

      /* Determine the end of the current contiguous memory block */
      if (p < gpt)
        limit = gpt;
      else
        limit = end_ptr;

      unsigned char *next_nl = memchr (p, '\n', limit - p);

      if (next_nl)
        {
          pos += (next_nl - p) + 1;
          n--;
        }
      else if (limit == end_ptr)
        {
          /* Reached the absolute end of the searchable range */
          return end_byte;
        }
      else
        {
          /* Reached the gap; jump to the start of the next block */
          pos = BUF_GPT_BYTE (buf);
        }
    }

  return pos;
}

int
mac_ax_range_for_line (struct frame *f, EMACS_INT line, CFRange *range)
{
  ptrdiff_t begv_byte, zv_byte, p_byte;
  EMACS_INT start_char, end_char;

  if (line < 0)
    return 0;

  if (!BUFFERP (XWINDOW (f->selected_window)->contents))
    return 0;
  struct buffer *b = XBUFFER (XWINDOW (f->selected_window)->contents);
  
  begv_byte = BUF_BEGV_BYTE (b);
  zv_byte = BUF_ZV_BYTE (b);

  /* Find the start of the target line */
  p_byte = mac_ax_buffer_skip_lines_offset (b, line, begv_byte, zv_byte);
  
  /* Validate p_byte isn't past the end */
  if (p_byte >= zv_byte && line > 0) 
    return 0;

  start_char = buf_bytepos_to_charpos (b, p_byte);
  
  /* Find the end of that line (skip 1 more line) */
  p_byte = mac_ax_buffer_skip_lines_offset (b, 1, p_byte, zv_byte);
  end_char = buf_bytepos_to_charpos (b, p_byte);

  range->location = start_char - BUF_BEGV (b);
  range->length = end_char - start_char;

  return 1;
}

CFStringRef
mac_ax_create_string_for_range (struct frame *f, const CFRange *range,
				CFRange *actual_range)
{
  struct buffer *b = XBUFFER (XWINDOW (f->selected_window)->contents);
  CFStringRef result = NULL;
  EMACS_INT start, end, begv = BUF_BEGV (b), zv = BUF_ZV (b);

  start = begv + range->location;
  end = start + range->length;
  if (start < begv)
    start = begv;
  else if (start > zv)
    start = zv;
  if (end < begv)
    end = begv;
  else if (end > zv)
    end = zv;
  if (start <= end)
    {
      EMACS_INT length = end - start;
      UniChar *characters = xmalloc (length * sizeof (UniChar));

      if (mac_store_buffer_text_to_unicode_chars (b, start, end, characters))
	{
	  result = CFStringCreateWithCharacters (NULL, characters, length);
	  if (actual_range)
	    {
	      actual_range->location = start - begv;
	      actual_range->length = length;
	    }
	}
      xfree (characters);
    }

  return result;
}

/* Return true if and only if a key-down Quartz event CGEVENT is
   regarded as a quit char event.  */

bool
mac_keydown_cgevent_quit_p (CGEventRef cgevent)
{
  struct input_event buf;

  mac_cgevent_to_input_event (cgevent, &buf);
  if (buf.kind == ASCII_KEYSTROKE_EVENT)
    {
      int c = buf.code & 0377;

      if (buf.modifiers & ctrl_modifier)
	c = make_ctrl_char (c);

      c |= (buf.modifiers
	    & (meta_modifier | alt_modifier
	       | hyper_modifier | super_modifier));

      return c == quit_char;
    }
  else
    return false;
}

void
mac_store_apple_event (Lisp_Object class, Lisp_Object id, const AEDesc *desc)
{
  struct input_event buf;

  EVENT_INIT (buf);

  buf.kind = MAC_APPLE_EVENT;
  buf.x = class;
  buf.y = id;
  buf.frame_or_window = mac_event_frame ();
  /* Now that Lisp object allocations are protected by BLOCK_INPUT, it
     is safe to use them during read_socket_hook.  */
  buf.arg = mac_aedesc_to_lisp (desc);
  kbd_buffer_store_event (&buf);
}

OSStatus
mac_store_event_ref_as_apple_event (AEEventClass class, AEEventID id,
				    Lisp_Object class_key, Lisp_Object id_key,
				    EventRef event, UInt32 num_params,
				    const EventParamName *names,
				    const EventParamType *types)
{
  OSStatus err = eventNotHandledErr;
  Lisp_Object binding;

  binding = mac_find_apple_event_spec (class, id, &class_key, &id_key);
  if (!NILP (binding) && !EQ (binding, Qundefined))
    {
      if (INTEGERP (binding))
	err = XFIXNUM (binding);
      else
	{
	  struct input_event buf;

	  EVENT_INIT (buf);

	  buf.kind = MAC_APPLE_EVENT;
	  buf.x = class_key;
	  buf.y = id_key;
	  buf.frame_or_window = mac_event_frame ();
	  /* Now that Lisp object allocations are protected by
	     BLOCK_INPUT, it is safe to use them during
	     read_socket_hook.  */
	  buf.arg = Fcons (build_string ("aevt"),
			   mac_event_parameters_to_lisp (event, num_params,
							 names, types));
	  kbd_buffer_store_event (&buf);
	  err = noErr;
	}
    }

  return err;
}

static void
mac_handle_cg_display_reconfig (CGDirectDisplayID display,
				CGDisplayChangeSummaryFlags flags,
				void *user_info)
{
  mac_screen_config_changed = 1;
}

static OSErr
init_dm_notification_handler (void)
{
  CGDisplayRegisterReconfigurationCallback (mac_handle_cg_display_reconfig,
					    NULL);

  return noErr;
}

static void
mac_handle_tis_notification (CFNotificationCenterRef center, void *observer,
			     CFStringRef name, const void *object,
			     CFDictionaryRef userInfo)
{
  struct input_event buf;
  Lisp_Object arg;

  EVENT_INIT (buf);

  buf.kind = MAC_APPLE_EVENT;
  buf.x = Qtext_input;
  buf.y = Qnotification;
  buf.frame_or_window = mac_event_frame ();
  arg = list1 (Fcons (Qname, (Fcons (build_string ("Lisp"),
				     cfstring_to_lisp_nodecode (name)))));
  buf.arg = Fcons (build_string ("aevt"), arg);
  kbd_buffer_store_event (&buf);
}

static void
init_tis_notification_handler (void)
{
  CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter ();

  CFNotificationCenterAddObserver (center, NULL, mac_handle_tis_notification,
				   kTISNotifySelectedKeyboardInputSourceChanged,
				   NULL, CFNotificationSuspensionBehaviorDrop);
  CFNotificationCenterAddObserver (center, NULL, mac_handle_tis_notification,
				   kTISNotifyEnabledKeyboardInputSourcesChanged,
				   NULL,
				   CFNotificationSuspensionBehaviorCoalesce);
}


/***********************************************************************
			    Initialization
 ***********************************************************************/

static int mac_initialized = 0;

static XrmDatabase
mac_make_rdb (const char *xrm_option)
{
  XrmDatabase database;

  database = xrm_get_preference_database (NULL);
  if (xrm_option)
    xrm_merge_string_database (database, xrm_option);

  return database;
}

struct mac_display_info *
mac_term_init (Lisp_Object display_name, char *xrm_option, char *resource_name)
{
  struct terminal *terminal;
  struct mac_display_info *dpyinfo;

  block_input ();

  if (!mac_initialized)
    {
      mac_initialize ();
      mac_initialized = 1;
    }

  if (x_display_list)
    error ("Sorry, this version can only handle one display");

  dpyinfo = &one_mac_display_info;
  memset (dpyinfo, 0, sizeof (*dpyinfo));
  terminal = mac_create_terminal (dpyinfo);

  terminal->kboard = allocate_kboard (Qmac);
  /* Don't let the initial kboard remain current longer than necessary.
     That would cause problems if a file loaded on startup tries to
     prompt in the mini-buffer.  */
  if (current_kboard == initial_kboard)
    current_kboard = terminal->kboard;
  terminal->kboard->reference_count++;

  /* Put this display on the chain.  */
  dpyinfo->next = x_display_list;
  x_display_list = dpyinfo;

  dpyinfo->name_list_element = Fcons (display_name, Qnil);

  /* https://lists.gnu.org/r/emacs-devel/2015-11/msg00194.html  */
  dpyinfo->smallest_font_height = 1;
  dpyinfo->smallest_char_width = 1;

  /* Set the name of the terminal. */
  terminal->name = xlispstrdup (display_name);

  Lisp_Object system_name = Fsystem_name ();
  static char const title[] = "GNU Emacs";
  if (STRINGP (system_name))
    {
      static char const at[] = " at ";
      ptrdiff_t nbytes = sizeof (title) + sizeof (at);
      if (INT_ADD_WRAPV (nbytes, SBYTES (system_name), &nbytes))
	memory_full (SIZE_MAX);
      dpyinfo->mac_id_name = xmalloc (nbytes);
      sprintf (dpyinfo->mac_id_name, "%s%s%s", title, at, SDATA (system_name));
    }
  else
    {
      dpyinfo->mac_id_name = xmalloc (sizeof (title));
      strcpy (dpyinfo->mac_id_name, title);
    }

  /* Put the rdb where we can find it in a way that works on
     all versions.  */
  dpyinfo->rdb = mac_make_rdb (xrm_option);
  x_display_rdb_list = Fcons (dpyinfo->rdb, x_display_rdb_list);

  mac_get_screen_info (dpyinfo);

  reset_mouse_highlight (&dpyinfo->mouse_highlight);

  dpyinfo->resx = 72.0;
  dpyinfo->resy = 72.0;

  add_keyboard_wait_descriptor (mac_get_select_fd ());

  /* In Mac GUI, asynchronous I/O (using SIGIO) can't be used for
     window events because they don't come from sockets, even though
     it works fine on tty's.  */
  Fset_input_interrupt_mode (Qnil);

  gui_init_fringe (terminal->rif);

  unblock_input ();

  return dpyinfo;
}

/* Get rid of display DPYINFO, assuming all frames are already gone.  */

static void
mac_delete_display (struct mac_display_info *dpyinfo)
{
  struct terminal *t;

  /* Close all frames and delete the generic struct terminal for this
     display.  */
  for (t = terminal_list; t; t = t->next_terminal)
    if (t->type == output_mac && t->display_info.mac == dpyinfo)
      {
        delete_terminal (t);
        break;
      }

  if (x_display_list == dpyinfo)
    {
      x_display_list = dpyinfo->next;
      x_display_rdb_list = XCDR (x_display_rdb_list);
    }
  else
    {
      struct mac_display_info *tail;
      Lisp_Object tail_rdb;

      for (tail = x_display_list, tail_rdb = x_display_rdb_list; tail;
	   tail = tail->next, tail_rdb = XCDR (tail_rdb))
	if (tail->next == dpyinfo)
	  {
	    tail->next = tail->next->next;
	    XSETCDR (tail_rdb, XCDR (XCDR (tail_rdb)));
	  }
    }

  xfree (dpyinfo->mac_id_name);
}


static void
record_startup_key_modifiers (void)
{
  Vmac_startup_options = Fcons (Fcons (Qkeyboard_modifiers,
				       make_fixnum (GetCurrentKeyModifiers ())),
				Vmac_startup_options);
}

/* Set up use of X before we make the first connection.  */

static struct redisplay_interface mac_redisplay_interface =
  {
    mac_frame_parm_handlers,
    gui_produce_glyphs,
    gui_write_glyphs,
    gui_insert_glyphs,
    gui_clear_end_of_line,
    mac_scroll_run,
    mac_after_update_window_line,
    mac_update_window_begin,
    mac_update_window_end,
    NULL, /* was: mac_flush, but that's a no-op w/ >=v10.14 layer-based drawing */
    gui_clear_window_mouse_face,
    gui_get_glyph_overhangs,
    gui_fix_overlapping_area,
    mac_draw_fringe_bitmap,
    mac_define_fringe_bitmap,
    mac_destroy_fringe_bitmap,
    mac_compute_glyph_string_overhangs,
    mac_draw_glyph_string,
    mac_define_frame_cursor,
    mac_clear_frame_area,
    mac_clear_under_internal_border,
    mac_draw_window_cursor,
    mac_draw_vertical_window_border,
    mac_draw_window_divider,
    mac_shift_glyphs_for_insert, /* Never called; see comment in function.  */
    mac_show_hourglass,
    mac_hide_hourglass
  };


/* This function is called when the last frame on a display is deleted. */
void
mac_delete_terminal (struct terminal *terminal)
{
  struct mac_display_info *dpyinfo = terminal->display_info.mac;

  /* Protect against recursive calls.  delete_frame in
     delete_terminal calls us back when it deletes our last frame.  */
  if (!terminal->name)
    return;

  block_input ();
  image_destroy_all_bitmaps (dpyinfo);

  /* No more input on this descriptor.  */
  delete_keyboard_wait_descriptor (mac_get_select_fd ());

  mac_delete_display (dpyinfo);
  unblock_input ();
}

static struct terminal *
mac_create_terminal (struct mac_display_info *dpyinfo)
{
  struct terminal *terminal;

  terminal = create_terminal (output_mac, &mac_redisplay_interface);

  terminal->display_info.mac = dpyinfo;
  dpyinfo->terminal = terminal;

  /* kboard is initialized in mac_term_init. */

  terminal->clear_frame_hook = mac_clear_frame;
  terminal->ins_del_lines_hook = mac_ins_del_lines;
  terminal->delete_glyphs_hook = mac_delete_glyphs;
  terminal->ring_bell_hook = mac_ring_bell;
  terminal->toggle_invisible_pointer_hook = NULL;
  terminal->update_begin_hook = mac_update_begin;
  terminal->update_end_hook = mac_update_end;
  terminal->read_socket_hook = mac_read_socket;
  terminal->frame_up_to_date_hook = mac_frame_up_to_date;
  terminal->defined_color_hook = mac_defined_color;
  terminal->query_frame_background_color = mac_query_frame_background_color;
  terminal->query_colors = mac_query_colors;
  terminal->mouse_position_hook = mac_mouse_position;
  terminal->get_focus_frame = mac_get_focus_frame;
  terminal->focus_frame_hook = mac_focus_frame;
  terminal->frame_rehighlight_hook = mac_frame_rehighlight;
  terminal->frame_raise_lower_hook = mac_frame_raise_lower;
  terminal->frame_visible_invisible_hook = mac_make_frame_visible_invisible;
  terminal->fullscreen_hook = mac_fullscreen_hook;
  terminal->iconify_frame_hook = mac_iconify_frame;
  terminal->set_window_size_hook = mac_set_window_size;
  terminal->set_frame_offset_hook = mac_set_offset;
  terminal->set_frame_alpha_hook = mac_set_frame_alpha;
  terminal->set_new_font_hook = mac_new_font;
  terminal->implicit_set_name_hook = mac_implicitly_set_name;
  terminal->menu_show_hook = mac_menu_show;
  terminal->activate_menubar_hook = mac_activate_menubar;
  terminal->popup_dialog_hook = mac_popup_dialog;
  terminal->change_tab_bar_height_hook = mac_change_tab_bar_height;
  terminal->change_tool_bar_height_hook = mac_change_tool_bar_height;
  terminal->set_vertical_scroll_bar_hook = mac_set_vertical_scroll_bar;
  terminal->set_horizontal_scroll_bar_hook = mac_set_horizontal_scroll_bar;
  terminal->set_scroll_bar_default_width_hook = mac_set_scroll_bar_default_width;
  terminal->set_scroll_bar_default_height_hook = mac_set_scroll_bar_default_height;
  terminal->condemn_scroll_bars_hook = mac_condemn_scroll_bars;
  terminal->redeem_scroll_bar_hook = mac_redeem_scroll_bar;
  terminal->judge_scroll_bars_hook = mac_judge_scroll_bars;
  terminal->get_string_resource_hook = mac_get_string_resource;
  terminal->free_pixmap = image_free_pix_container;
  terminal->delete_frame_hook = mac_destroy_window;
  terminal->delete_terminal_hook = mac_delete_terminal;
  /* Other hooks are NULL by default.  */

  return terminal;
}

static void
mac_initialize (void)
{
  baud_rate = 19200;

  block_input ();

  init_coercion_handler ();

  init_dm_notification_handler ();

  init_tis_notification_handler ();

  install_application_handler ();

  init_cg_color ();

  record_startup_key_modifiers ();

  init_activation_policy ();

  unblock_input ();
}


void
syms_of_macterm (void)
{
  DEFSYM (Qcontrol, "control");
  DEFSYM (Qmeta, "meta");
  DEFSYM (Qalt, "alt");
  DEFSYM (Qhyper, "hyper");
  DEFSYM (Qsuper, "super");
  DEFSYM (Qmodifier_value, "modifier-value");
  DEFSYM (QCordinary, ":ordinary");
  DEFSYM (QCfunction, ":function");
  DEFSYM (QCmouse, ":mouse");
  DEFSYM (QCbutton, ":button");

  Fput (Qcontrol, Qmodifier_value, make_fixnum (ctrl_modifier));
  Fput (Qmeta,    Qmodifier_value, make_fixnum (meta_modifier));
  Fput (Qalt,     Qmodifier_value, make_fixnum (alt_modifier));
  Fput (Qhyper,   Qmodifier_value, make_fixnum (hyper_modifier));
  Fput (Qsuper,   Qmodifier_value, make_fixnum (super_modifier));

  DEFSYM (Qpanel_closed, "panel-closed");
  DEFSYM (Qselection, "selection");

  DEFSYM (Qservice, "service");
  DEFSYM (Qpaste, "paste");
  DEFSYM (Qperform, "perform");

  DEFSYM (Qtext_input, "text-input");
  DEFSYM (Qinsert_text, "insert-text");
  DEFSYM (Qset_marked_text, "set-marked-text");
  DEFSYM (Qnotification, "notification");
  DEFSYM (Qmac_ts_active_input_string, "mac-ts-active-input-string");

  DEFSYM (Qaction, "action");
  DEFSYM (Qmac_action_key_paths, "mac-action-key-paths");

  DEFSYM (Qaccessibility, "accessibility");

  DEFSYM (Qreverse, "reverse");

  DEFSYM (Qkeyboard_modifiers, "keyboard-modifiers");

  DEFSYM (Qautomatic, "automatic");
  DEFSYM (Qinverted, "inverted");

  DEFSYM (QCdirection_inverted_from_device_p,
	  ":direction-inverted-from-device-p");
  DEFSYM (QCdelta_x, ":delta-x");
  DEFSYM (QCdelta_y, ":delta-y");
  DEFSYM (QCdelta_z, ":delta-z");
  DEFSYM (QCscrolling_delta_x, ":scrolling-delta-x");
  DEFSYM (QCscrolling_delta_y, ":scrolling-delta-y");
  DEFSYM (QCphase, ":phase");
  DEFSYM (QCmomentum_phase, ":momentum-phase");
  DEFSYM (QCswipe_tracking_from_scroll_events_enabled_p,
	  ":swipe-tracking-from-scroll-events-enabled-p");
  DEFSYM (Qbegan, "began");
  DEFSYM (Qstationary, "stationary");
  DEFSYM (Qchanged, "changed");
  DEFSYM (Qended, "ended");
  DEFSYM (Qcancelled, "cancelled");
  DEFSYM (Qmay_begin, "may-begin");
  DEFSYM (QCmagnification, ":magnification");

  DEFSYM (Qapplication_kvo, "application-kvo");
  DEFSYM (Qchange, "change");

  DEFSYM (Qmac_map_system_symbol, "mac-map-system-symbol");

  staticpro (&x_display_rdb_list);
  x_display_rdb_list = Qnil;

  /* We don't yet support this, but defining this here avoids whining
     from cus-start.el and other places, like "M-x set-variable".  */
  DEFVAR_BOOL ("x-use-underline-position-properties",
	       x_use_underline_position_properties,
	       doc: /* SKIP: real doc in xterm.c.  */);
  x_use_underline_position_properties = true;
  DEFSYM (Qx_use_underline_position_properties,
	  "x-use-underline-position-properties");

  DEFVAR_BOOL ("x-underline-at-descent-line",
	       x_underline_at_descent_line,
	       doc: /* SKIP: real doc in xterm.c.  */);
  x_underline_at_descent_line = false;
  DEFSYM (Qx_underline_at_descent_line, "x-underline-at-descent-line");

  DEFVAR_LISP ("x-toolkit-scroll-bars", Vx_toolkit_scroll_bars,
	       doc: /* SKIP: real doc in xterm.c.  */);
  Vx_toolkit_scroll_bars = Qt;

  DEFVAR_LISP("mac-underwave-thickness", Vmac_underwave_thickness,
	      doc: /* The thickness of wavy underlines.  If nil, the
default line thickness is used. Otherwise, a floating point number
should be specified, where 1.0 is the default, with smaller values
thinner, and larger values thicker. */);
  Vmac_underwave_thickness = Qnil;
  
  DEFVAR_BOOL ("mac-redisplay-dont-reset-vscroll", mac_redisplay_dont_reset_vscroll,
	       doc: /* Non-nil means update doesn't reset vscroll.  */);
  mac_redisplay_dont_reset_vscroll = false;

  DEFVAR_BOOL ("mac-ignore-momentum-wheel-events", mac_ignore_momentum_wheel_events,
	       doc: /* Non-nil means momentum wheel events are ignored.  */);
  mac_ignore_momentum_wheel_events = false;

/* Variables to configure modifier key assignment.  */

  DEFVAR_LISP ("mac-control-modifier", Vmac_control_modifier,
    doc: /* Modifier key assumed when the Mac control key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  The default is `control'.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.  */);
  Vmac_control_modifier = Qcontrol;

  DEFVAR_LISP ("mac-right-control-modifier", Vmac_right_control_modifier,
    doc: /* Modifier key assumed when the Mac right control key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  The value `left' means the same setting as
`mac-control-modifier'.  The default is `left'.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.

Note: the left and right versions cannot be distinguished on some
environments such as Screen Sharing.  Also, certain combinations of a
key with both versions of the same modifier do not emit events at the
system level.  */);
  Vmac_right_control_modifier = Qleft;

  DEFVAR_LISP ("mac-option-modifier", Vmac_option_modifier,
    doc: /* Modifier key assumed when the Mac alt/option key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  If the value is nil then the key will act as
the normal Mac option modifier, and the option key can be used to
compose characters depending on the chosen Mac keyboard setting.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.  */);
  Vmac_option_modifier = list4 (QCfunction, Qalt, QCmouse, Qalt);

  DEFVAR_LISP ("mac-right-option-modifier", Vmac_right_option_modifier,
    doc: /* Modifier key assumed when the Mac right option key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  If the value is nil then the key will act as
the normal Mac option modifier, and the option key can be used to
compose characters depending on the chosen Mac keyboard setting.  The
value `left' means the same setting as `mac-option-modifier'.  The
default is `left'.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.

Note: the left and right versions cannot be distinguished on some
environments such as Screen Sharing.  Also, certain combinations of a
key with both versions of the same modifier do not emit events at the
system level.  */);
  Vmac_right_option_modifier = Qleft;

  DEFVAR_LISP ("mac-command-modifier", Vmac_command_modifier,
    doc: /* Modifier key assumed when the Mac command key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  The default is `meta'.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.  */);
  Vmac_command_modifier = Qmeta;

  DEFVAR_LISP ("mac-right-command-modifier", Vmac_right_command_modifier,
    doc: /* Modifier key assumed when the Mac right command key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  The value `left' means the same setting as
`mac-command-modifier'.  The default is `left'.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.

Note: the left and right versions cannot be distinguished on some
environments such as Screen Sharing.  Also, certain combinations of a
key with both versions of the same modifier do not emit events at the
system level.  */);
  Vmac_right_command_modifier = Qleft;

  DEFVAR_LISP ("mac-function-modifier", Vmac_function_modifier,
    doc: /* Modifier key assumed when the Mac function (fn) key is pressed.
The value is of the form either SYMBOL or `(:ordinary SYMBOL :function
SYMBOL :mouse SYMBOL)'.  The latter allows us to specify different
behaviors among ordinary keys, function keys, and mouse operations.

Each SYMBOL can be `control', `meta', `alt', `hyper', or `super' for
the respective modifier.  If the value is nil, then the key will act
as the normal Mac function (fn) key.

The property list form can include the `:button' property for button
number mapping, which becomes active when the value of
`mac-emulate-three-button-mouse' is nil.  The `:button' property can
be either a positive integer specifying the destination of the primary
button only, or a list (VALUE-FOR-PRIMARY-BUTTON VALUE-FOR-MOUSE-2
VALUE-FOR-MOUSE-3 ...) of positive integers specifying the
destinations of multiple buttons in order.  Note that the secondary
button and the button 3 (usually the wheel button) correspond to
mouse-3 and mouse-2 respectively if the value of
`mac-wheel-button-is-mouse-2' is non-nil (default), and mouse-2 and
mouse-3 respectively otherwise.  If a button is mapped to the same
number as its source, then it behaves as if the button were not mapped
so the `:mouse' property becomes in effect instead.

The default value is `(:button 2)' meaning that the primary button is
recognized as mouse-2 if the Mac function (fn) key is pressed.  */);
  Vmac_function_modifier = list2 (QCbutton, make_fixnum (2));

  DEFVAR_LISP ("mac-emulate-three-button-mouse",
	       Vmac_emulate_three_button_mouse,
    doc: /* Specify a way of three button mouse emulation.
The value can be nil, t, or the symbol `reverse'.  The latter two
provide handy settings especially for one-button mice.

A value of nil means that the modifier variable settings, if any, are
used for mapping button numbers.  If you need more flexible settings
than what t or the symbol `reverse' below provides, then set this
variable to nil and customize the modifier variables
`mac-control-modifier', `mac-right-control-modifier',
`mac-option-modifier', `mac-right-option-modifier',
`mac-command-modifier', `mac-right-command-modifier', and/or
`mac-function-modifier' so their values have the `:button' property.

t means that when the option-key is held down while pressing the mouse
button, the click will register as mouse-2 and while the command-key
is held down, the click will register as mouse-3.

The symbol `reverse' means that the option-key will register for
mouse-3 and the command-key will register for mouse-2.  */);
  Vmac_emulate_three_button_mouse = Qnil;

  DEFVAR_BOOL ("mac-wheel-button-is-mouse-2", mac_wheel_button_is_mouse_2,
    doc: /* Non-nil if the wheel button is mouse-2 and the right click mouse-3.
Otherwise, the right click will be treated as mouse-2 and the wheel
button will be mouse-3.  */);
  mac_wheel_button_is_mouse_2 = true;

  DEFVAR_BOOL ("mac-pass-command-to-system", mac_pass_command_to_system,
    doc: /* Non-nil if command key presses are passed on to the Mac Toolbox.  */);
  mac_pass_command_to_system = true;

  DEFVAR_BOOL ("mac-pass-control-to-system", mac_pass_control_to_system,
    doc: /* Non-nil if control key presses are passed on to the Mac Toolbox.  */);
  mac_pass_control_to_system = true;

  DEFVAR_LISP ("mac-startup-options", Vmac_startup_options,
    doc: /* Alist of Mac-specific startup options.
Each element looks like (OPTION-TYPE . OPTIONS).
OPTION-TYPE is a symbol specifying the type of startup options:

  command-line -- List of Mac-specific command line options.
  apple-event -- Apple event that came with the \"Open Application\" event.
  keyboard-modifiers -- Number representing keyboard modifiers on startup.
    See also `mac-keyboard-modifier-mask-alist'.  */);
  Vmac_startup_options = Qnil;

  DEFVAR_LISP ("mac-ts-active-input-overlay", Vmac_ts_active_input_overlay,
    doc: /* Overlay used to display Mac TSM active input area.  */);
  Vmac_ts_active_input_overlay = Qnil;

  DEFVAR_BOOL ("mac-drawing-use-gcd", mac_drawing_use_gcd,
    doc: /* Non-nil means graphical drawing uses GCD (Grand Central Dispatch).
Performs graphical drawing operations via dispatch to dedicated
threads.*/);
  mac_drawing_use_gcd = true;

  DEFVAR_LISP ("mac-frame-tabbing", Vmac_frame_tabbing,
    doc: /* Specify tabbing behavior of a frame that is becoming visible.
The symbol `automatic' (default) means it follows the system setting,
and `inverted' means the system setting is inverted.  Nil and t means
tabbing is disallowed and preferred, respectively.

This variable has no effect on OS X 10.11 and earlier.  */);
  Vmac_frame_tabbing = Qautomatic;

  DEFVAR_BOOL ("mac-ignore-accessibility", mac_ignore_accessibility,
    doc: /* Non-nil means to ignore the system accessibility feature.
Emacspeak users may want to set this variable so it may not interfere
with the system accessibility services such as VoiceOver.  */);
  mac_ignore_accessibility = false;
}
