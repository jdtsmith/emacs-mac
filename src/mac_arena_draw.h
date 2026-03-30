/* mac_arena_draw.h */
#ifndef MAC_ARENA_DRAW_H
#define MAC_ARENA_DRAW_H

#include "frame.h"

/* Bytes to allocate (at minimum) for each arena data block */
#define MAC_ARENA_DATA_SIZE (1 << 18)
/* Number of commands to allocate for each arena command block */
#define MAC_ARENA_CMDS_PER_BLOCK (1 << 9)

typedef struct mac_arena_block {
  struct mac_arena_block *next; // Next block (if any)
  size_t size;
  size_t used;
  unsigned char stash[]; // Flexible storage
} mac_arena_block;

typedef struct mac_arena {
  mac_arena_block *cmds, *first_cmds; /* current, linked-list of command blocks */
  mac_arena_block *data, *first_data; /* current, linked-list of data blocks */
  CGFloat backing_scale_factor;
} mac_arena;

#define MAC_ARENA_CMD_ALLOC_TYPE (1 << 0)
#define MAC_ARENA_DATA_ALLOC_TYPE (1 << 1)

/* Maximum number of dirty rects to accumulate prior to frame flush */
#define MAC_N_DIRTY_RECTS 16

/* Retain a CFTypeRef into a command's ref slot */
#define MAC_ARENA_RETAIN(slot, ref)                                     \
  do { if (ref) CFRetain (ref); (slot) = (ref); } while (0)

enum mac_session_type
{
  MAC_SESSION_UPDATE,
  MAC_SESSION_OUTOFBAND,
};

/* --- DRAW COMMANDS -- */
enum mac_arena_cmd_type
{
  /* Clipping */
  MAC_ARENA_CMD_SET_CLIP,
  MAC_ARENA_CMD_RESET_CLIP,

  /* Fills */
  MAC_ARENA_CMD_FILL_RECT,
  MAC_ARENA_CMD_ERASE_RECT,
  MAC_ARENA_CMD_INVERT_RECT,
  MAC_ARENA_CMD_DRAW_STIPPLE,
  
  /* Outlines */
  MAC_ARENA_CMD_STROKE_RECT,
  
  /* Relief / 3D */
  MAC_ARENA_CMD_FILL_TRAPEZOID,
  MAC_ARENA_CMD_ERASE_CORNERS,
  
  /* Decoration */
  MAC_ARENA_CMD_DRAW_WAVE,
  
  /* Images */
  MAC_ARENA_CMD_DRAW_IMAGE,
  
  /* Text */
  MAC_ARENA_CMD_DRAW_GLYPHS,
  
  /* Scrolling */
  MAC_ARENA_CMD_SCROLL_RECT,
};

#define MAC_ARENA_CMD_RETAINED_REFS(type1, name1, type2, name2) \
  type1 name1;							\
  type2 name2

#define MAC_ARENA_CMD_RETAINED_REF1(type, name) \
  type name;					\
  CFTypeRef _reserved_ref2

#define MAC_ARENA_CMD_RETAINED_REFS_NONE	\
  CFTypeRef _reserved_ref1;			\
  CFTypeRef _reserved_ref2

typedef struct mac_arena_draw_cmd {
  enum mac_arena_cmd_type type;
  CGRect rect;

  union {
    /* Slots for retained CFObjects that need CFRelease */
    CFTypeRef refs[2];  

    struct {
      MAC_ARENA_CMD_RETAINED_REFS (CGColorRef, fg_color, CGFontRef, font);
      CGGlyph *glyphs;       // data (from Data Arena)
      CGPoint *positions;    // data (from Data Arena)
      int nchars;
    } text;
    
    /* --- Clipping --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REFS_NONE;
      CGRect *rects;  // clip rect data (from Data Arena)
      int nrects;
    } clip;
    
    /* --- Fills --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REF1 (CGColorRef, color);
    } fill;
    
    struct {
      MAC_ARENA_CMD_RETAINED_REF1 (CGColorRef, bg_color);
      bool clear_p;
      CGFloat alpha;
    } erase;

    struct { 
      MAC_ARENA_CMD_RETAINED_REFS_NONE;
    } invert;

    struct {
      MAC_ARENA_CMD_RETAINED_REFS (CGColorRef, color, CFArrayRef, stipple);
    } stipple;
    
    /* --- Outlines --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REF1 (CGColorRef, color);
    } stroke;

    /* --- Relief / 3D --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REF1 (CGColorRef, color);
      CGPoint *points;
    } trapezoid;

    struct {
      MAC_ARENA_CMD_RETAINED_REF1 (CGColorRef, bg_color);
      CGFloat radius;
      CGFloat margin;
      int corners;       /* bitmask */
      bool clear_p;
    } corners;

    /* --- Decoration --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REF1 (CGColorRef, color);
      int wave_length;
      CGFloat line_width; /* from Vmac_underwave_thickness */
    } wave;

    /* --- Images --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REFS (CGImageRef, image, CGColorRef, fill_color);
      CGAffineTransform *transform;  /* from data arena */
      CGRect bounds;
      bool no_interpolation_p;
    } image;

    /* --- Text --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REFS (CFTypeRef, font_ref, CGColorRef, color);
      CGFloat font_size;
      CGPoint text_position;
      CGGlyph *glyphs;          /* from data arena */
      CGPoint *positions;       /* from data arena */
      int len;
      CGFloat bold_factor;
      bool synthetic_bold_p;
      bool synthetic_italic_p;
      bool no_antialias_p;
      bool use_ct_font_p;       /* true = CTFontDrawGlyphs (e.g. emoji),
				   false = CGContextShowGlyphsAtPositions */
    } glyphs;

    /* --- Scrolling --- */
    struct {
      MAC_ARENA_CMD_RETAINED_REFS_NONE;
      CGSize delta;
    } scroll;
  };
} mac_arena_draw_cmd;

/* Allocate a new command from the active arena */
# define MAC_ARENA_CMD(cmd, f, cmd_type, invalid_rect)			\
  mac_arena_draw_cmd *cmd = mac_arena_cmd_alloc ((f)->output_data.mac->active_arena); \
  cmd->type = MAC_ARENA_CMD_##cmd_type;					\
  cmd->rect = (invalid_rect);

#define MAC_ARENA_BLOCK_CMDS(block) (mac_arena_draw_cmd *) (block)->stash;
#define MAC_ARENA_HAS_CMDS_P(arena)				\
  (((arena)->first_cmds) && ((arena)->first_cmds->used>0))

#if DRAWING_USE_GCD
extern void mac_init_arena_system (struct frame *);
#endif
extern void mac_teardown_arena_system (struct frame *);
extern void mac_flush_open_arenas (void);
extern void mac_flush_arena (struct frame *);
extern void * mac_arena_data_alloc (mac_arena *, size_t);
extern mac_arena_draw_cmd * mac_arena_cmd_alloc (mac_arena *);
extern mac_arena* mac_ensure_arena (struct frame *);
extern void mac_arena_reset (mac_arena *);
extern void mac_record_gc_clip (struct frame *, GC);
extern CGColorRef mac_bg_color_for_gc (struct frame *, GC, bool, bool *);
extern void mac_record_erase_bg (struct frame *, GC, CGRect, bool);
extern void mac_accumulate_dirty (struct mac_output *, CGRect);
extern bool mac_begin_draw_session (struct frame *);
extern void mac_end_draw_session (struct frame *, bool);
extern void mac_playback_arena (mac_arena *, struct frame *, CGContextRef);

extern const CGAffineTransform synthetic_italic_atfm;
#endif
