/* mac_arena_draw.c */
/* Arena-based GCD background drawing for Cocoa on macOS
   Copyright (C) 2026  J.D. Smith

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

/* arena-draw

Arena-draw is an allocating, multi-arena drawing system for emacs-mac.
Its primary function is to permit simplification of the `mac' terminal
(macterm.[ch]), which have become pure command producers, and to
substantially reduce the time the LISP thread needs to spend drawing.
All drawing now occurs here.

Arenas are filled with commands (see `mac_arena_draw_cmd') and
associated allocated or retained data (e.g. glyphs, rectangles, colors,
etc.) necessary for drawing operation.  Arenas full of commands can then
be played back as a single block in quick succession on the GCD queue,
significantly reducing GCD scheduling overhead (see
`mac_playback_arena').

The multi-arena design also avoids blocking the LISP thread, which no
longer must wait for drawing to complete.  Indeed, the LISP thread can
"work ahead" by several frames until it runs out of available arenas.
Arena drawing makes it possible to move *all* drawing (LISP or
GUI-driven) to the background GCD thread.  All draw session playback is
atomic and cannot be interrupted, eliminating flashing and other visual
artifacts. */

#include <config.h>
#include "lisp.h"
#include "frame.h"
#include "macterm.h"
#include "mac_arena_draw.h"
#include <stddef.h>

/* ** Arena lifecycle */

#ifdef MAC_DEBUG_SIGNPOST
static size_t arena_highwater_mark = 0;
#endif

#if DRAWING_USE_GCD
void
mac_init_arena_system (struct frame *f)
{
  struct mac_output *mo = FRAME_OUTPUT_DATA (f);
  struct frame *p = FRAME_PARENT_FRAME (f);
  if (p)
    { /* Re-use parent's drawing queue */
      struct mac_output *pmo = FRAME_OUTPUT_DATA (p);
      if (pmo && pmo->drawing_queue)
	mo->drawing_queue = pmo->drawing_queue;
    }
  if (!mo->drawing_queue)
    mo->drawing_queue = dispatch_queue_create ("org.gnu.Emacs.drawing", NULL);
  mo->arena_sem = dispatch_semaphore_create (MAC_ARENA_COUNT);
}
#endif

/* Cycle to the frame's next arena */
inline void
mac_arena_cycle (struct frame *f)
{
  struct mac_output *mo = FRAME_OUTPUT_DATA (f);
  mo->next_arena = (mo->next_arena + 1) % MAC_ARENA_COUNT;
}

void
mac_flush_arena (struct frame *f)
{
  struct mac_output *mo = FRAME_OUTPUT_DATA (f);
  if (mo->active_arena)
    mac_draw_session_end (f);
}

void
mac_flush_open_arenas (void)
{
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    {
      struct frame *f = XFRAME (frame);
      mac_flush_arena (f);
    }
}

static void
mac_arena_release_draw_cmds (mac_arena_block *block)
{
  mac_arena_draw_cmd *cmds = MAC_ARENA_BLOCK_CMDS (block);
  size_t cmd_count = block->used / sizeof(mac_arena_draw_cmd);

  for (size_t i = 0; i < cmd_count; i++)
    for (size_t j = 0; j <= 1; j++)
      if (cmds[i].refs[j])
	CFRelease(cmds[i].refs[j]);
}

void
mac_teardown_arena_system (struct frame *f)
{
  struct mac_output *mo = FRAME_OUTPUT_DATA (f);
  struct frame *p = FRAME_PARENT_FRAME (f);

  if (mo->drawing_queue)
    {
      /* Wait for any in-flight drawing to complete */
      dispatch_sync (mo->drawing_queue, ^{});

      /* Free arena blocks */
      for (int i = 0; i < MAC_ARENA_COUNT; i++)
        {
          mac_arena *arena = &mo->arenas[i];
          mac_arena_block *block = arena->first_cmds;

          while (block)
	    {
	      mac_arena_block *next = block->next;
              mac_arena_release_draw_cmds (block);
	      xfree (block);
	      block = next;
	    }
	  block = arena->first_data;
          while (block)
            {
	      mac_arena_block *next = block->next;
	      xfree (block);
	      block = next;
            }
        }

#if DRAWING_USE_GCD
      if (!p)
	mo->drawing_queue = NULL;
      mo->arena_sem = NULL;
#endif
    }
}

/* Allocate arena memory for data or commands.  If TYPE is DATA, SIZE is
   in bytes.  Otherwise it is ignored, and an allocation for a single
   command struct is returned. */
static void *
mac_arena_alloc (mac_arena *arena, int type, size_t size) {
  if (!arena)
    return NULL;

  mac_arena_block **cur, **first;
    
  if (type == MAC_ARENA_DATA_ALLOC_TYPE)
    {
      cur = &arena->data;
      first = &arena->first_data;
      size = (size + 15) & ~15;  // Align to 16 bytes for SIMD/performance
    }
  else
    {
      cur = &arena->cmds;
      first = &arena->first_cmds;
      size = sizeof (mac_arena_draw_cmd);
    }

  if (!*cur || ((*cur)->used + size > (*cur)->size))
    {
      if (*cur && (*cur)->next)  // Block exists
	{
	  *cur = (*cur)->next;
	  (*cur)->used = 0;
	}
      else
	{
	  size_t new_size = (type == MAC_ARENA_DATA_ALLOC_TYPE) 
	    ? max(MAC_ARENA_DATA_SIZE, size)
	    : MAC_ARENA_CMDS_PER_BLOCK * size;
	  mac_arena_block *new_block = xmalloc (sizeof (mac_arena_block) + new_size);
	  new_block->next = NULL;
	  new_block->size = new_size;
	  new_block->used = 0;

	  if (!*first) *first = new_block;
	  if (*cur) (*cur)->next = new_block;
	  *cur = new_block;
	}
    }
  
  void *ptr = (*cur)->stash + (*cur)->used;
  (*cur)->used += size;
  
  if (type == MAC_ARENA_CMD_ALLOC_TYPE)
    {
      mac_arena_draw_cmd *cmd = (mac_arena_draw_cmd *)ptr;
      for (size_t i = 0; i <= 1; i++)
	cmd->refs[i] = NULL;
    }
  
  return ptr;
}

void *
mac_arena_data_alloc  (mac_arena *arena, size_t size)
{
  void *ptr = mac_arena_alloc (arena, MAC_ARENA_DATA_ALLOC_TYPE, size);
  return ptr;
}

mac_arena_draw_cmd *
mac_arena_cmd_alloc (mac_arena *arena)
{
  mac_arena_draw_cmd *cmd;
  cmd = mac_arena_alloc (arena, MAC_ARENA_CMD_ALLOC_TYPE, 1);
  return cmd;
}

/* Ensure an arena is open, and return it.  We assume that any drawing
   that occurs on the frame without an active arena belongs to an "out
   of band" drawing session.  Such sessions are closed during
   mac_frame_up_to_date. */
mac_arena*
mac_ensure_arena (struct frame *f)
{
  if (!FRAME_OUTPUT_DATA (f)->active_arena)
    mac_draw_session_begin (f, MAC_SESSION_OUTOFBAND);
  return FRAME_OUTPUT_DATA (f)->active_arena;
}

/* Reset arena for re-use */
void
mac_arena_reset (mac_arena *arena) {
  mac_arena_block *this;

  for (this = arena->first_cmds; this; this = this->next)
    {
      mac_arena_release_draw_cmds (this);
      this->used = 0;
      if (this == arena->cmds) break;
    }
  arena->cmds = arena->first_cmds;
  
  for (this = arena->first_data; this; this = this->next)
    {
      this->used = 0;
      if (this == arena->data) break;
    }
  arena->data = arena->first_data;
}


/* ** Arena drawing playback */

/* Record a new CLIP_RECT command (if needed).  If GC is NULL, or there
   are 0 clip_rects, record a RESET_CLIP command. */
void
mac_record_gc_clip (struct frame *f, GC gc) {
  {
    struct mac_output *mo = FRAME_OUTPUT_DATA (f);

    if (gc && gc->num_clip_rects == mo->current_clip_nrects)
      {
	bool equal_p = true;
	for (size_t i = 0; i < gc->num_clip_rects; i++)
	  {
	    if (!CGRectEqualToRect (gc->clip_rects[i],
				    mo->current_clip_rects[i]))
	      {
		equal_p = false;
		break;
	      }
	  }
	if (equal_p)  /* Same clip rects, no command needed */
	  return;
      }

    mac_arena *arena = mac_ensure_arena (f);

    if (!gc || gc->num_clip_rects == 0)
      {
	MAC_ARENA_CMD (cmd, f, RESET_CLIP, CGRectNull);
	mo->current_clip_nrects = 0;
      }
    else
      {
	size_t bytes = gc->num_clip_rects * sizeof (CGRect);
	CGRect *arena_rects = mac_arena_data_alloc (arena, bytes);
	memcpy (arena_rects, gc->clip_rects, bytes);
	/* N.B.: final rect arg is ignored */
	MAC_ARENA_CMD (cmd, f, SET_CLIP, CGRectNull);
	cmd->clip.rects = mo->current_clip_rects = arena_rects;
	cmd->clip.nrects = mo->current_clip_nrects = gc->num_clip_rects;
      }
  }
}

/* Compute the background fill color, respecting alpha and transparency.
   Returns the retained CGColorRef (caller must release). Based on prior
   CG_CONTEXT_FILL_RECT_WITH_GC_BACKGROUND macro. */
CGColorRef
mac_bg_color_for_gc (struct frame *f, GC gc, bool respect_alpha_background,
		     bool *clear_p)
{
  *clear_p = false;

  if (gc->xgcv.background_transparency == 0
      && (!respect_alpha_background || f->alpha_background == 1.0))
    {
      CGColorRetain (gc->cg_back_color);
      return gc->cg_back_color;
    }

  if (FRAME_BACKGROUND_ALPHA_ENABLED_P (f)
      && !mac_accessibility_display_options.reduce_transparency_p)
    {
      *clear_p = true;
      if (respect_alpha_background && f->alpha_background != 1.0)
        {
          CGFloat alpha = ((255 - gc->xgcv.background_transparency)
                           / 255.0f * f->alpha_background);
          return CGColorCreateCopyWithAlpha (gc->cg_back_color, alpha);
        }
      CGColorRetain (gc->cg_back_color); /* So caller doesn't have to */
      return gc->cg_back_color;
    }

  return CGColorCreateCopyWithAlpha (gc->cg_back_color, 1.0f);
}

void
mac_record_erase_bg (struct frame *f, GC gc, CGRect rect,
                    bool respect_alpha_background)
{
  MAC_ARENA_CMD (cmd, f, ERASE_RECT, rect);
  cmd->erase.bg_color = mac_bg_color_for_gc (f, gc, respect_alpha_background,
					     &cmd->erase.clear_p);
}

/* Accumulate a new dirty rect into up to MAC_N_DIRTY_RECTS slots,
   merging into the best option if it wastes no more than 35% area (or
   if we are out of free slots).  Dirty rects can be accumulated across
   multiple arena playbacks, and are reset during presentation. */
void
mac_accumulate_dirty (struct mac_output *mo, CGRect rect)
{
  if (CGRectIsEmpty(rect))
    return;

  int best_idx = -1;
  CGFloat best_waste_ratio = CGFLOAT_MAX;

  for (int i = 0; i < mo->dirty_rect_count; i++)
    {
      CGRect merged = CGRectUnion (mo->dirty_rects[i], rect);
      CGFloat merged_area = merged.size.width * merged.size.height;
      CGFloat sum_areas = (mo->dirty_rects[i].size.width *
			   mo->dirty_rects[i].size.height
			   + rect.size.width * rect.size.height);
      CGFloat ratio = merged_area / sum_areas;
      if (ratio < best_waste_ratio)
        {
          best_waste_ratio = ratio;
          best_idx = i;
        }
    }

  if (best_idx >= 0 &&
      (best_waste_ratio < 1.35 || mo->dirty_rect_count == MAC_N_DIRTY_RECTS))
    /* Merge if we have a good candidate, or are out of room */
    mo->dirty_rects[best_idx] = CGRectUnion(mo->dirty_rects[best_idx], rect);
  else
    mo->dirty_rects[mo->dirty_rect_count++] = rect;
}

typedef struct mac_arena_state
{
  CGColorRef current_fill;
  CGFontRef current_font;
  CGFloat current_font_size;
} mac_arena_state;


/* Playback an individual drawing command from arena into context,
   tracking and update draw state */

static inline void
mac_playback_cmd (mac_arena_draw_cmd *cmd, mac_arena *arena,
		  CGContextRef context, mac_arena_state *state)
{
  switch (cmd->type)
    {
    case MAC_ARENA_CMD_DRAW_GLYPHS:
      {
	if (!cmd->glyphs.use_ct_font_p)
	  {
	    CGFontRef font = (CGFontRef) cmd->glyphs.font_ref;
	    if (state->current_font != font)
	      {
		CGContextSetFont (context, font);
		state->current_font = font;
	      }
	    if (state->current_font_size != cmd->glyphs.font_size)
	      {
		CGContextSetFontSize (context, cmd->glyphs.font_size);
		state->current_font_size = cmd->glyphs.font_size;
	      }
	  }
	if (cmd->glyphs.color != state->current_fill)
	  {
	    CGContextSetFillColorWithColor (context, cmd->glyphs.color);
	    state->current_fill = cmd->glyphs.color;
	  }
		
	CGContextSaveGState (context);
	CGContextScaleCTM (context, 1, -1);
		
	if (cmd->glyphs.synthetic_bold_p)
	  {
	    CGContextSetTextDrawingMode (context, kCGTextFillStroke);
	    CGContextSetLineWidth (context, cmd->glyphs.bold_factor
				   * cmd->glyphs.font_size);
	    CGContextSetStrokeColorWithColor (context, cmd->glyphs.color);
	  }

	if (cmd->glyphs.no_antialias_p)
	  CGContextSetShouldAntialias (context, false);
	CGContextSetTextMatrix (context, cmd->glyphs.synthetic_italic_p
				? synthetic_italic_atfm
				: CGAffineTransformIdentity);
	CGContextSetTextPosition (context, cmd->glyphs.text_position.x,
				  cmd->glyphs.text_position.y);
	      
	if (cmd->glyphs.use_ct_font_p)
	  {
	    if (cmd->glyphs.len > 0)
	      CTFontDrawGlyphs ((CTFontRef) cmd->glyphs.font_ref,
				cmd->glyphs.glyphs,
				cmd->glyphs.positions,
				cmd->glyphs.len, context);
	  }
	else
	  {
	    CGContextShowGlyphsAtPositions (context, cmd->glyphs.glyphs,
					    cmd->glyphs.positions,
					    cmd->glyphs.len);
	  }
	CGContextRestoreGState(context);
      }
      break;

    case MAC_ARENA_CMD_SCROLL_RECT:
      mac_scroll_bitmap (context, cmd->rect,
			 cmd->scroll.delta,
			 arena->backing_scale_factor);
      break;

    case MAC_ARENA_CMD_FILL_RECT:
      if (cmd->fill.color != state->current_fill)
	{
	  CGContextSetFillColorWithColor (context, cmd->fill.color);
	  state->current_fill = cmd->fill.color;
	}
      CGContextFillRect(context, cmd->rect);
      break;

    case MAC_ARENA_CMD_ERASE_RECT:
      if (cmd->erase.clear_p)
	CGContextClearRect (context, cmd->rect);
      if (cmd->erase.bg_color != state->current_fill)
	{
	  CGContextSetFillColorWithColor (context, cmd->erase.bg_color);
	  state->current_fill = cmd->erase.bg_color;
	}
      CGContextFillRect (context, cmd->rect);
      break;

    case MAC_ARENA_CMD_INVERT_RECT:
      CGContextSaveGState(context);
      CGContextSetGrayFillColor(context, 1.0f, 1.0f);
      CGContextSetBlendMode(context, kCGBlendModeDifference);
      CGContextFillRect(context, cmd->rect);
      CGContextRestoreGState(context);
      break;

    case MAC_ARENA_CMD_STROKE_RECT:
      CGContextSetStrokeColorWithColor (context, cmd->fill.color);
      CGContextStrokeRect (context,
			   CGRectInset (cmd->rect, 0.5f, 0.5f));
      break;

    case MAC_ARENA_CMD_FILL_TRAPEZOID:
      if (cmd->trapezoid.color != state->current_fill)
	{
	  CGContextSetFillColorWithColor (context, cmd->trapezoid.color);
	  state->current_fill = cmd->trapezoid.color;
	}
      CGContextAddLines (context, cmd->trapezoid.points, 4);
      CGContextFillPath (context);
      break;

    case MAC_ARENA_CMD_DRAW_IMAGE:
      {
	CGRect dest = cmd->rect;
	CGRect bounds = cmd->image.bounds;

	CGContextSaveGState (context);
	CGContextClipToRect (context, dest);

	/* Mask fills only */
	if (cmd->image.fill_color)
	  CGContextSetFillColorWithColor (context, cmd->image.fill_color);

	if (cmd->image.transform)
	  {
	    CGContextTranslateCTM (context,
				   CGRectGetMinX (bounds),
				   CGRectGetMinY (bounds));
	    CGContextConcatCTM (context, *cmd->image.transform);
	    CGContextTranslateCTM (context, 0, CGRectGetHeight (bounds));
	  }
	else
	  CGContextTranslateCTM (context,
				 CGRectGetMinX (bounds),
				 CGRectGetMaxY (bounds));
		
	CGContextScaleCTM (context, 1, -1);

	if (cmd->image.no_interpolation_p)
	  CGContextSetInterpolationQuality (context, kCGInterpolationNone);

	bounds.origin = CGPointZero;
	CGContextDrawImage (context, bounds, cmd->image.image);
		
	CGContextRestoreGState (context);
      }
      break;

    case MAC_ARENA_CMD_DRAW_STIPPLE:
      {
	CGContextSaveGState (context);
	CGContextSetFillColorWithColor (context, cmd->stipple.color);
	CGContextScaleCTM (context, 1, -1);
	CGContextSetInterpolationQuality (context, kCGInterpolationNone);
		
	int scale = CFArrayGetCount (cmd->stipple.stipple);
	CGFloat backing = arena->backing_scale_factor;
	if (backing < scale)
	  scale = backing;
		
	CGImageRef image_mask =
	  (CGImageRef) CFArrayGetValueAtIndex (cmd->stipple.stipple, scale - 1);
	CGRect dest = CGRectMake (0, 0,
			       CGImageGetWidth (image_mask) / (CGFloat) scale,
			       CGImageGetHeight (image_mask) / (CGFloat) scale);
	CGContextDrawTiledImage (context, dest, image_mask);
	CGContextRestoreGState (context);
      }
      break;

    case MAC_ARENA_CMD_DRAW_WAVE:
      {
	CGRect wave_clip = cmd->rect;
	CGFloat gperiod = cmd->wave.wave_length * 2;
	CGFloat gx1 = (floor ((CGRectGetMinX (wave_clip) - 1.0f) / gperiod)
		       * gperiod + 0.5f);
	CGFloat gxmax = CGRectGetMaxX (wave_clip);
	CGFloat gy1 = CGRectGetMinY (wave_clip) + 0.5f;
	CGFloat gy2 = CGRectGetMaxY (wave_clip) - 0.5f;

	CGContextSaveGState (context);
	CGContextClipToRect (context, wave_clip);
	CGContextSetStrokeColorWithColor (context, cmd->wave.color);

	if (cmd->wave.line_width > 0)
	  CGContextSetLineWidth (context, cmd->wave.line_width);

	CGContextMoveToPoint (context, gx1, gy1);
	while (gx1 <= gxmax)
	  {
	    CGContextAddLineToPoint (context, gx1 + gperiod * 0.5f, gy2);
	    gx1 += gperiod;
	    CGContextAddLineToPoint (context, gx1, gy1);
	  }
	CGContextStrokePath (context);
	CGContextRestoreGState (context);
      }
      break;
	      
    case MAC_ARENA_CMD_ERASE_CORNERS:
      {
	CGRect rect = cmd->rect;

	if (cmd->corners.bg_color != state->current_fill)
	  {
	    CGContextSetFillColorWithColor (context, cmd->corners.bg_color);
	    state->current_fill = cmd->corners.bg_color;
	  }

	CGContextSaveGState (context);
	for (int i = 0; i < CORNER_LAST; i++)
	  if (cmd->corners.corners & (1 << i))
	    {
	      CGFloat xm, ym, xc, yc;

	      if (i == CORNER_TOP_LEFT || i == CORNER_BOTTOM_LEFT)
		xm = CGRectGetMinX (rect) - cmd->corners.margin,
		  xc = xm + cmd->corners.radius;
	      else
		xm = CGRectGetMaxX (rect) + cmd->corners.margin,
		  xc = xm - cmd->corners.radius;
	      if (i == CORNER_TOP_LEFT || i == CORNER_TOP_RIGHT)
		ym = CGRectGetMinY (rect) - cmd->corners.margin,
		  yc = ym + cmd->corners.radius;
	      else
		ym = CGRectGetMaxY (rect) + cmd->corners.margin,
		  yc = ym - cmd->corners.radius;

	      CGContextMoveToPoint (context, xm, ym);
	      CGContextAddArc (context, xc, yc, cmd->corners.radius,
			       i * M_PI_2, (i + 1) * M_PI_2, 0);
	    }
	CGContextClip (context);

	if (cmd->corners.clear_p)
	  CGContextClearRect (context, rect);
	CGContextFillRect (context, rect);
	CGContextRestoreGState (context);
      }
      break;

    default:
      break;
    }
}

/* Playback all the given ARENA's drawing commands into CONTEXT of frame
   F.  To be called from the GCD Queue only. */
void
mac_playback_arena(mac_arena *arena, struct frame *f, CGContextRef context)
{
  struct mac_output *mo = FRAME_OUTPUT_DATA (f);
  mac_arena_state state = {0};
#ifdef MAC_DEBUG_SIGNPOST
  size_t total_cmds = 0;
  MAC_SIGNPOST_PTR_BEGIN (arena, draw, Playback,
			  "Arena: %u Frame: %{public}s",
			  (unsigned) (arena - mo->arenas),
			  SSDATA((f)->name));
#endif
  mac_setup_drawing_context (context);
  for (mac_arena_block *block = arena->first_cmds;
       block; block = block->next)
    {
      mac_arena_draw_cmd *cmds = MAC_ARENA_BLOCK_CMDS (block);
      size_t cmd_count = block->used / sizeof(mac_arena_draw_cmd);
	
      for (size_t i = 0; i < cmd_count; i++)
	{
	  mac_arena_draw_cmd *cmd = &cmds[i];
	  MAC_SIGNPOST_DRAW_CMD_BEGIN (cmd, "");
	  
	  if (cmd->type == MAC_ARENA_CMD_SET_CLIP)
	    {
	      CGContextResetClip (context); /* GC clips never nest */
	      CGContextClipToRects (context, cmd->clip.rects, cmd->clip.nrects);
	    }
	  else if (cmd->type == MAC_ARENA_CMD_RESET_CLIP)
	    {
              CGContextResetClip (context);
	    }
	  else
	    { /* Clip drawing commands to bounds and playback */
	      CGRect clipBox = CGContextGetClipBoundingBox (context);
	      cmd->rect = CGRectIntersection (cmd->rect, clipBox);
	      if (!CGRectIsNull (cmd->rect))
		{
		  mac_accumulate_dirty (mo, cmd->rect);
		  mac_playback_cmd(cmd, arena, context, &state);
		}
	    }
#ifdef MAC_DEBUG_SIGNPOST
	  MAC_SIGNPOST_DRAW_CMD_END ();
	  total_cmds++;
#endif
	}
	
      if (block == arena->cmds)
	break;  /* Don't walk past the last active block */
    }
  CGContextFlush (context);
  mac_teardown_drawing_context ();
#ifdef MAC_DEBUG_SIGNPOST
  size_t data_allocated = 0;
  mac_arena_block *this;
  for (this = arena->first_data; this; this = this->next)
    {
      data_allocated += this->used;
      if (this == arena->data) break;
    }
  if (data_allocated > arena_highwater_mark)
    arena_highwater_mark = data_allocated;

  MAC_SIGNPOST_PTR_END (arena, draw, Playback,
			"NCMDS: %u (%.2f blocks) NDIRTY: %d DATA: %.1f blocks (%.1f max)",
			(unsigned) total_cmds,
			(float) (total_cmds) / MAC_ARENA_CMDS_PER_BLOCK,
			mo->dirty_rect_count,
			(float) data_allocated / MAC_ARENA_DATA_SIZE,
			(float) arena_highwater_mark / MAC_ARENA_DATA_SIZE);
#endif
}
