/* Display module for macOS.
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

#include "macgui.h"
#include "frame.h"
#include "../lwlib/lwlib-widget.h"

#define RGB_TO_ULONG(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define ARGB_TO_ULONG(a, r, g, b) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

#define ALPHA_FROM_ULONG(color) ((color) >> 24)
#define RED_FROM_ULONG(color) (((color) >> 16) & 0xff)
#define GREEN_FROM_ULONG(color) (((color) >> 8) & 0xff)
#define BLUE_FROM_ULONG(color) ((color) & 0xff)

/* Do not change `* 0x101' in the following lines to `<< 8'.  If
   changed, image masks in 1-bit depth will not work. */
#define RED16_FROM_ULONG(color) (RED_FROM_ULONG(color) * 0x101)
#define GREEN16_FROM_ULONG(color) (GREEN_FROM_ULONG(color) * 0x101)
#define BLUE16_FROM_ULONG(color) (BLUE_FROM_ULONG(color) * 0x101)

#define BLACK_PIX_DEFAULT(f) RGB_TO_ULONG(0,0,0)
#define WHITE_PIX_DEFAULT(f) RGB_TO_ULONG(255,255,255)

/* Structure recording bitmaps and reference count.
   If REFCOUNT is 0 then this record is free to be reused.  */

struct mac_bitmap_record
{
  CFArrayRef stipple;
  char *file;
  int refcount;
};


/* For each display (currently only one on mac), we have a structure that
   records information about it.  */

struct mac_display_info
{
  /* Chain of all mac_display_info structures.  */
  struct mac_display_info *next;

  /* The generic display parameters corresponding to this display. */
  struct terminal *terminal;

  /* This is a cons cell of the form (NAME . FONT-LIST-CACHE).  */
  Lisp_Object name_list_element;

  /* Number of frames that are on this display.  */
  int reference_count;

  /* Dots per inch of the screen.  */
  double resx, resy;

  /* Number of planes on this screen.  */
  int n_planes;

  /* Whether the screen supports color */
  int color_p;

  /* Dimensions of this screen.  */
  int height, width;

  /* Mask of things that cause the mouse to be grabbed.  */
  int grabbed;

#if 0
  /* Emacs bitmap-id of the default icon bitmap for this frame.
     Or -1 if none has been allocated yet.  */
  ptrdiff_t icon_bitmap_id;

#endif
  /* The root window of this screen.  */
  Window root_window;

  /* Resource data base */
  XrmDatabase rdb;

  /* Minimum width over all characters in all fonts in font_table.  */
  int smallest_char_width;

  /* Minimum font height over all fonts in font_table.  */
  int smallest_font_height;

  /* Reusable Graphics Context for drawing a cursor in a non-default face. */
  GC scratch_cursor_gc;

  /* Information about the range of text currently shown in
     mouse-face.  */
  Mouse_HLInfo mouse_highlight;

  /* Default name for all frames on this display.  */
  char *mac_id_name;

  /* The number of fonts opened for this display.  */
  int n_fonts;

  /* Pointer to bitmap records.  */
  struct mac_bitmap_record *bitmaps;

  /* Allocated size of bitmaps field.  */
  ptrdiff_t bitmaps_size;

  /* Last used bitmap index.  */
  ptrdiff_t bitmaps_last;

  /* The frame (if any) which has the window that has keyboard focus.
     Zero if none.  This is examined by Ffocus_frame in macfns.c.  Note
     that a mere EnterNotify event can set this; if you need to know the
     last frame specified in a FocusIn or FocusOut event, use
     mac_focus_event_frame.  */
  struct frame *mac_focus_frame;

  /* The last frame mentioned in a FocusIn or FocusOut event.  This is
     separate from mac_focus_frame, because whether or not LeaveNotify
     events cause us to lose focus depends on whether or not we have
     received a FocusIn event for it.  */
  struct frame *mac_focus_event_frame;

  /* The frame which currently has the visual highlight, and should get
     keyboard input (other sorts of input have the frame encoded in the
     event).  It points to the focus frame's selected window's
     frame.  It differs from mac_focus_frame when we're using a global
     minibuffer.  */
  struct frame *highlight_frame;

  /* The frame waiting to be auto-raised in XTread_socket.  */
  struct frame *mac_pending_autoraise_frame;

  /* The frame where the mouse was last time we reported a ButtonPress event.  */
  struct frame *last_mouse_frame;

  /* The frame where the mouse was last time we reported a mouse position.  */
  struct frame *last_mouse_glyph_frame;

  /* Where the mouse was last time we reported a mouse position.
     This is a rectangle on last_mouse_glyph_frame.  */
  NativeRectangle last_mouse_glyph;

  /* Time of last mouse movement on this display.  This is a hack because
     we would really prefer that XTmouse_position would return the time
     associated with the position it returns, but there doesn't seem to be
     any way to wrest the time-stamp from the server along with the position
     query.  So, we just keep track of the time of the last movement we
     received, and return that in hopes that it's somewhat accurate.  */
  Time last_mouse_movement_time;

  /* This is a button event that wants to activate the menubar.
     We save it here until the command loop gets to think about it.  */
  EventRef saved_menu_event;
};

/* This is a chain of structures for all the X displays currently in use.  */
extern struct mac_display_info *x_display_list;

/* This is a chain of structures for all the displays currently in use.  */
extern struct mac_display_info one_mac_display_info;

extern struct mac_display_info *mac_term_init (Lisp_Object, char *, char *);

/* The collection of data describing a window on the Mac.  */
struct mac_output
{
  /* Here are the Graphics Contexts for the default font.  */
  GC normal_gc;				/* Normal video */
  GC cursor_gc;				/* cursor drawing */

  /* The window used for this frame.
     May be zero while the frame object is being created
     and the window has not yet been created.  */
  Window window_desc;

#ifdef HAVE_XWIDGETS
  /* NSView for this window.  Don't use this outside nsxwidget.m.  */
  void *view;
#endif

  /* The window that is the parent of this window.
     Usually this is a window that was made by the window manager,
     but it can be the root window, and it can be explicitly specified
     (see the explicit_parent field, below).  */
  Window parent_desc;

  /* Default ASCII font of this frame. */
  struct font *font;

  /* The baseline offset of the default ASCII font.  */
  int baseline_offset;

  /* If a fontset is specified for this frame instead of font, this
     value contains an ID of the fontset, else -1.  */
  int fontset;

  /* Pixel values used for various purposes.
     border_pixel may be -1 meaning use a gray tile.  */
  unsigned long cursor_pixel;
  unsigned long border_pixel;
  unsigned long mouse_pixel;
  unsigned long cursor_foreground_pixel;

  /* Descriptor for the cursor in use for this window.  */
  Emacs_Cursor text_cursor;
  Emacs_Cursor nontext_cursor;
  Emacs_Cursor modeline_cursor;
  Emacs_Cursor hand_cursor;
  Emacs_Cursor hourglass_cursor;
  Emacs_Cursor horizontal_drag_cursor;
  Emacs_Cursor vertical_drag_cursor;
  Emacs_Cursor current_cursor;	/* unretained */
  Emacs_Cursor left_edge_cursor;
  Emacs_Cursor top_left_corner_cursor;
  Emacs_Cursor top_edge_cursor;
  Emacs_Cursor top_right_corner_cursor;
  Emacs_Cursor right_edge_cursor;
  Emacs_Cursor bottom_right_corner_cursor;
  Emacs_Cursor bottom_edge_cursor;
  Emacs_Cursor bottom_left_corner_cursor;

  /* Menubar "widget" handle.  */
  bool_bf menubar_widget : 1;

  /* True means our parent is another application's window
     and was explicitly specified.  */
  bool_bf explicit_parent : 1;

  /* True means tried already to make this frame visible.  */
  bool_bf asked_for_visible : 1;

  /* True means mac_check_fullscreen is not called yet after
     fullscreen request for this frame.  */
  bool_bf check_fullscreen_needed_p : 1;

  /* True means this frame uses an internal tool bar (as opposed to a
     toolkit one).  */
  bool_bf internal_tool_bar_p : 1;

  /* True means background alpha value is enabled for this frame.  */
  bool_bf background_alpha_enabled_p : 1;

  /* True means synthetic bold workaround is disabled for this
     frame.  */
  bool_bf synthetic_bold_workaround_disabled_p : 1;

  /* Backing scale factor (1 or 2), used for rendering images.  */
  unsigned backing_scale_factor : 2;

  /* State for image vs. backing scaling factor mismatch
     detection.  */
  unsigned scale_mismatch_state : 2;

  /* This variable records the gravity value of the window position if
     the window has an external tool bar when it is created.  The
     position of the window is adjusted using this information when
     the tool bar is first redisplayed.  Once the tool bar is
     redisplayed, it is set to 0 in order to avoid further
     adjustment.  */
  unsigned toolbar_win_gravity : 4;

  /* True means application-side double buffering together with a
     layer-backed view is used.  Otherwise framework-provided double
     buffering with a non-layer-backed view is used.  */
  bool_bf double_buffered_p : 1;

  /* Width of the internal border.  */
  int internal_border_width;

  /* Relief GCs, colors etc.  */
  struct relief
  {
    GC gc;
    unsigned long pixel;
  } black_relief, white_relief;

  /* The background for which the above relief GCs were set up.
     They are changed only when a different background is involved.  */
  unsigned long relief_background;

  /* Hints for the size and the position of a window.  */
  XSizeHints *size_hints;

  /* Quartz 2D graphics context.  */
  CGContextRef cg_context;

  /* Context and layer for atomic drawing (multiple draws with a single invalidation) */
  struct atomic_draw
  {
    CGContextRef cg_context;
    CGLayerRef sandbox;
    int nesting_level; /* 0 = idle, >0 = inside atomic block */
  } atomic_draw;
  
  /* Data representing the array of NativeRectangle's that will be
     inverted on drawRect: invocation.  */
  CFDataRef flash_rectangles_data;
};

/* Return the X output data for frame F.  */
#define FRAME_OUTPUT_DATA(f) ((f)->output_data.mac)

/* Return the Mac window used for displaying data in frame F.  */
#define FRAME_MAC_WINDOW(f) ((f)->output_data.mac->window_desc)
#define FRAME_NATIVE_WINDOW(f) ((f)->output_data.mac->window_desc)

#ifdef HAVE_XWIDGETS
#define FRAME_MAC_VIEW(f)	((f)->output_data.mac->view)
#endif

#define FRAME_FONT(f) ((f)->output_data.mac->font)
#define FRAME_FONTSET(f) ((f)->output_data.mac->fontset)

#define FRAME_BASELINE_OFFSET(f) ((f)->output_data.mac->baseline_offset)

#define FRAME_SIZE_HINTS(f) ((f)->output_data.mac->size_hints)
#define FRAME_CHECK_FULLSCREEN_NEEDED_P(f) \
  ((f)->output_data.mac->check_fullscreen_needed_p)
#define FRAME_INTERNAL_TOOL_BAR_P(f) ((f)->output_data.mac->internal_tool_bar_p)
#define FRAME_BACKGROUND_ALPHA_ENABLED_P(f) \
  ((f)->output_data.mac->background_alpha_enabled_p)
#define FRAME_SYNTHETIC_BOLD_WORKAROUND_DISABLED_P(f) \
  ((f)->output_data.mac->synthetic_bold_workaround_disabled_p)
#define FRAME_BACKING_SCALE_FACTOR(f)		\
  ((f)->output_data.mac->backing_scale_factor)
#define FRAME_SCALE_MISMATCH_STATE(f) \
  ((f)->output_data.mac->scale_mismatch_state)
#define FRAME_FLASH_RECTANGLES_DATA(f) \
  ((f)->output_data.mac->flash_rectangles_data)
#define FRAME_MAC_DOUBLE_BUFFERED_P(f) \
  ((f)->output_data.mac->double_buffered_p)
#define FRAME_ATOMIC_DRAW(f) \
  ((f)->output_data.mac->atomic_draw)
#define FRAME_ATOMIC_DRAW_P(f) \
  ((f)->output_data.mac->atomic_draw.nesting_level > 0)

/* This gives the mac_display_info structure for the display F is on.  */
#define FRAME_DISPLAY_INFO(f) ((void) (f), (&one_mac_display_info))

/* Mac-specific scroll bar stuff.  */

/* We represent scroll bars as lisp vectors.  This allows us to place
   references to them in windows without worrying about whether we'll
   end up with windows referring to dead scroll bars; the garbage
   collector will free it when its time comes.

   We use struct scroll_bar as a template for accessing fields of the
   vector.  */

struct scroll_bar {

  /* These fields are shared by all vectors.  */
  union vectorlike_header header;

  /* The window we're a scroll bar for.  */
  Lisp_Object window;

  /* The next and previous in the chain of scroll bars in this frame.  */
  Lisp_Object next, prev;

  /* Fields after 'prev' are not traced by the GC.  */

  /* The Mac control reference of this scroll bar.  */
  void *mac_control_ref;

  /* The position and size of the scroll bar in pixels, relative to the
     frame.  */
  int top, left, width, height;

  /* True if the scroll bar is horizontal.  */
  bool_bf horizontal : 1;

  /* True if redraw needed in the next XTset_vertical_scroll_bar call.  */
  bool_bf redraw_needed_p : 1;
} GCALIGNED_STRUCT;

/* Turning a lisp vector value into a pointer to a struct scroll_bar.  */
#define XSCROLL_BAR(vec) ((struct scroll_bar *) XVECTOR (vec))


/* Extract the reference to the scroller control from a struct
   scroll_bar.  */
#define SCROLL_BAR_SCROLLER(ptr) ((__bridge EmacsScroller *) (ptr)->mac_control_ref)

/* Store the reference to a scroller control in a struct
   scroll_bar.  */
#define SET_SCROLL_BAR_SCROLLER(ptr, ref) ((ptr)->mac_control_ref = (__bridge void *) (ref))

/* Size of hourglass controls */
#define HOURGLASS_WIDTH (18)
#define HOURGLASS_HEIGHT (18)
#define HOURGLASS_TOP_MARGIN (2)
#define HOURGLASS_RIGHT_MARGIN (32)

/* Some constants that are used locally.  */
/* Creator code for Emacs on macOS.  */
enum {
  MAC_EMACS_CREATOR_CODE	= 'EMAx'
};

/* Apple event descriptor types */
enum {
  TYPE_FILE_NAME		= 'fNam'
};

/* Keywords for Apple event attributes */
enum {
  KEY_EMACS_SUSPENSION_ID_ATTR	= 'esId' /* typeUInt32 */
};

enum {
  THEME_RESIZE_NORTHWEST_SOUTHEAST_CURSOR	= 23,
  THEME_RESIZE_NORTHEAST_SOUTHWEST_CURSOR	= 24,
  THEME_RESIZE_NORTH_SOUTH_CURSOR		= 25,
  THEME_RESIZE_NORTH_CURSOR			= 26,
  THEME_RESIZE_SOUTH_CURSOR			= 27,
  THEME_RESIZE_EAST_WEST_CURSOR			= 28,
  THEME_RESIZE_EAST_CURSOR			= 29,
  THEME_RESIZE_WEST_CURSOR			= 30,
  THEME_RESIZE_NORTHWEST_CURSOR			= 31,
  THEME_RESIZE_NORTHEAST_CURSOR			= 32,
  THEME_RESIZE_SOUTHWEST_CURSOR			= 33,
  THEME_RESIZE_SOUTHEAST_CURSOR			= 34
};

#if HAVE_UNIFORM_TYPE_IDENTIFIERS
#define UTI_PNG		(CFSTR ("public.png"))
#define UTI_JPEG	(CFSTR ("public.jpeg"))
#define UTI_TIFF	(CFSTR ("public.tiff"))
#define UTI_GIF		(CFSTR ("com.compuserve.gif"))
#define UTI_URL		(CFSTR ("public.url"))
#define UTI_PDF		(CFSTR ("com.adobe.pdf"))
#define UTI_SVG		(CFSTR ("public.svg-image"))
#define UTI_IMAGE	(CFSTR ("public.image"))
#else
#define UTI_PNG		kUTTypePNG
#define UTI_JPEG	kUTTypeJPEG
#define UTI_TIFF	kUTTypeTIFF
#define UTI_GIF		kUTTypeGIF
#define UTI_URL		kUTTypeURL
#define UTI_PDF		kUTTypePDF
#define UTI_SVG		kUTTypeScalableVectorGraphics
#define UTI_IMAGE	kUTTypeImage
#endif
#define UTI_HEIC	(CFSTR ("public.heic"))
#define UTI_WEBP	(CFSTR ("org.webmproject.webp"))

enum mac_return_frame_mode {
  RETURN_FRAME_NEVER,
  RETURN_FRAME_EVENTUALLY,
  RETURN_FRAME_NOW
};

/* From macfns.c.  */

extern void mac_free_gcs (struct frame *);
extern const char *mac_get_string_resource (void *, const char *, const char *);

/* Defined in macterm.c.  */

extern bool mac_screen_config_changed;
extern CGColorSpaceRef mac_cg_color_space_rgb;
extern void mac_make_frame_visible (struct frame *f);
extern void mac_make_frame_invisible (struct frame *f);
extern void mac_iconify_frame (struct frame *f);
extern void mac_free_frame_resources (struct frame *);
extern void mac_wm_set_size_hint (struct frame *, long, bool);

extern void mac_delete_terminal (struct terminal *terminal);
extern void mac_query_colors (struct frame *f, Emacs_Color *, int);
#define mac_display_pixel_height(dpyinfo)	((dpyinfo)->height)
#define mac_display_pixel_width(dpyinfo)	((dpyinfo)->width)
extern void mac_set_sticky (struct frame *, Lisp_Object, Lisp_Object);
extern void mac_set_skip_taskbar (struct frame *, Lisp_Object, Lisp_Object);
extern void mac_set_z_group (struct frame *, Lisp_Object, Lisp_Object);
extern void mac_clear_under_internal_border (struct frame *);
extern void mac_begin_scale_mismatch_detection (struct frame *);
extern bool mac_end_scale_mismatch_detection (struct frame *);
extern void mac_clear_area (struct frame *, int, int, int, int);
extern CGImageRef mac_create_image_mask_from_bitmap_data (const char *,
							  int, int);
extern void mac_invert_flash_rectangles (struct frame *);
extern GC mac_create_gc (unsigned long, XGCValues *);
#if DRAWING_USE_GCD
extern GC mac_duplicate_gc (GC);
#endif
extern void mac_free_gc (GC);
extern void mac_set_foreground (GC, unsigned long);
extern void mac_set_background (GC, unsigned long);
extern GC mac_gc_for_face_id (struct frame *f, int, GC);
extern void mac_focus_changed (int, struct mac_display_info *,
			       struct frame *, struct input_event *);
extern Lisp_Object mac_event_frame (void);
extern void mac_set_frame_window_gravity_reference_bounds (struct frame *, int,
							   NativeRectangle);
extern void mac_get_frame_window_gravity_reference_bounds (struct frame *, int,
							   NativeRectangle *);
extern void mac_handle_origin_change (struct frame *);
extern void mac_handle_size_change (struct frame *, int, int);
extern void mac_handle_visibility_change (struct frame *);
extern UInt32 mac_cgevent_flags_to_modifiers (CGEventFlags);
extern CGEventFlags mac_cgevent_to_input_event (CGEventRef,
						struct input_event *);
extern CGRect mac_get_first_rect_for_range (struct window *, const CFRange *,
					    CFRange *);
extern void mac_ax_selected_text_range (struct frame *, CFRange *);
extern EMACS_INT mac_ax_number_of_characters (struct frame *);
extern void mac_ax_visible_character_range (struct frame *, CFRange *);
extern EMACS_INT mac_ax_line_for_index (struct frame *, EMACS_INT);
extern int mac_ax_range_for_line (struct frame *, EMACS_INT, CFRange *);
extern CFStringRef mac_ax_create_string_for_range (struct frame *,
						   const CFRange *, CFRange *);
extern bool mac_keydown_cgevent_quit_p (CGEventRef);
extern void mac_store_apple_event (Lisp_Object, Lisp_Object, const AEDesc *);
extern OSStatus mac_store_event_ref_as_apple_event (AEEventClass, AEEventID,
						    Lisp_Object, Lisp_Object,
						    EventRef, UInt32,
						    const EventParamName *,
						    const EventParamType *);

/* Defined in macselect.c */

extern void mac_clear_frame_selections (struct frame *);
extern Lisp_Object mac_find_apple_event_spec (AEEventClass, AEEventID,
					      Lisp_Object *, Lisp_Object *);
extern pascal OSErr mac_handle_apple_event (const AppleEvent *, AppleEvent *,
					    SInt32);
extern void cleanup_all_suspended_apple_events (void);

/* Defined in macmenu.c */
extern void mac_menu_set_in_use (bool);
extern Lisp_Object mac_popup_dialog (struct frame *, Lisp_Object, Lisp_Object);
extern bool name_is_separator (const char *);
extern void mac_fill_menubar (widget_value *, bool);
extern int create_and_show_popup_menu (struct frame *, widget_value *,
				       int, int, bool);
extern int create_and_show_dialog (struct frame *, widget_value *);

/* Defined in macfns.c */

extern bool mac_defined_color (struct frame *, const char *, Emacs_Color *,
			       bool, bool);
extern void mac_update_title_bar (struct frame *, bool);
extern void mac_real_positions (struct frame *, int *, int *);
extern void mac_change_tab_bar_height (struct frame *, int);
extern void mac_change_tool_bar_height (struct frame *, int);
extern void mac_implicitly_set_name (struct frame *, Lisp_Object, Lisp_Object);
extern void mac_set_scroll_bar_default_width (struct frame *);
extern void mac_set_scroll_bar_default_height (struct frame *);
extern void mac_move_tooltip_to_mouse_location (void);

/* Defined in mac.c.  */

extern struct mac_operating_system_version
{
  CFIndex major, minor, patch;
} mac_operating_system_version;
extern Lisp_Object mac_four_char_code_to_string (FourCharCode);
extern bool mac_string_to_four_char_code (Lisp_Object, FourCharCode *);
extern void mac_foreach_window (struct frame *,
				bool (CF_NOESCAPE ^) (struct window *));
extern void mac_map_keymap (Lisp_Object, bool,
			    void (CF_NOESCAPE ^) (Lisp_Object, Lisp_Object));
extern Lisp_Object mac_aedesc_to_lisp (const AEDesc *);
extern OSErr mac_ae_put_lisp (AEDescList *, UInt32, Lisp_Object);
extern OSErr create_apple_event_from_lisp (Lisp_Object, AppleEvent *);
extern OSErr init_coercion_handler (void);
extern OSErr create_apple_event (AEEventClass, AEEventID, AppleEvent *);
extern Lisp_Object mac_event_parameters_to_lisp (EventRef, UInt32,
						 const EventParamName *,
						 const EventParamType *);
extern CFStringRef cfstring_create_with_utf8_cstring (const char *);
extern CFStringRef cfstring_create_with_string_noencode (Lisp_Object);
extern CFStringRef cfstring_create_with_string (Lisp_Object);
extern Lisp_Object cfdata_to_lisp (CFDataRef);
extern Lisp_Object cfstring_to_lisp_nodecode (CFStringRef);
extern Lisp_Object cfstring_to_lisp (CFStringRef);
extern Lisp_Object cfstring_to_lisp_utf_16 (CFStringRef);
extern Lisp_Object cfnumber_to_lisp (CFNumberRef);
extern Lisp_Object cfdate_to_lisp (CFDateRef);
extern Lisp_Object cfboolean_to_lisp (CFBooleanRef);
extern Lisp_Object cfobject_desc_to_lisp (CFTypeRef);
extern Lisp_Object cfobject_to_lisp (CFTypeRef, int, int);
extern Lisp_Object cfproperty_list_to_lisp (CFPropertyListRef, int, int);
extern Lisp_Object cfproperty_list_to_string (CFPropertyListRef,
					      CFPropertyListFormat);
extern CFPropertyListRef cfproperty_list_create_with_string (Lisp_Object);
extern void xrm_merge_string_database (XrmDatabase, const char *);
extern Lisp_Object xrm_get_resource (XrmDatabase, const char *, const char *);
extern XrmDatabase xrm_get_preference_database (const char *);
extern bool mac_service_provider_registered_p (void);
extern Lisp_Object mac_carbon_version_string (void);
extern const char *mac_relocate (const char *);

/* Defined in macappkit.m.  */

extern struct mac_accessibility_display_options
{
  bool increase_contrast_p;
  bool differentiate_without_color_p;
  bool reduce_transparency_p;
} mac_accessibility_display_options;
extern Lisp_Object mac_nsobject_to_lisp (CFTypeRef);
extern void mac_alert_sound_play (void);
extern double mac_appkit_version (void);
extern double mac_system_uptime (void);
extern bool mac_is_current_process_frontmost (void);
extern void mac_bring_current_process_to_front (bool);
extern bool mac_trash_file (const char *, CFErrorRef *);
extern CFStringRef mac_uti_create_with_mime_type (CFStringRef);
extern CFStringRef mac_uti_create_with_filename_extension (CFStringRef);
extern CFStringRef mac_uti_copy_filename_extension (CFStringRef);
extern CFStringRef mac_uti_copy_mime_type (CFStringRef);
extern void init_activation_policy (void);
extern OSStatus install_application_handler (void);
extern Lisp_Object mac_application_state (void);
extern void mac_set_frame_window_title (struct frame *, CFStringRef);
extern void mac_set_frame_window_modified (struct frame *, bool);
extern void mac_set_frame_window_proxy (struct frame *, CFURLRef);
extern bool mac_is_frame_window_visible (struct frame *);
extern bool mac_is_frame_window_collapsed (struct frame *);
extern bool mac_is_frame_window_drawable (struct frame *);
extern void mac_bring_frame_window_to_front (struct frame *);
extern void mac_send_frame_window_behind (struct frame *);
extern void mac_hide_frame_window (struct frame *);
extern void mac_show_frame_window (struct frame *);
extern OSStatus mac_collapse_frame_window (struct frame *, bool);
extern bool mac_is_frame_window_frontmost (struct frame *);
extern void mac_activate_frame_window (struct frame *);
extern OSStatus mac_move_frame_window_structure (struct frame *, int, int);
extern void mac_size_frame_window (struct frame *, int, int, bool);
extern OSStatus mac_set_frame_window_alpha (struct frame *, CGFloat);
extern OSStatus mac_get_frame_window_alpha (struct frame *, CGFloat *);
extern Lisp_Object mac_set_tab_group_overview_visible_p (struct frame *,
							 Lisp_Object);
extern Lisp_Object mac_set_tab_group_tab_bar_visible_p (struct frame *,
							Lisp_Object);
extern Lisp_Object mac_set_tab_group_selected_frame (struct frame *,
						     Lisp_Object);
extern Lisp_Object mac_set_tab_group_frames (struct frame *, Lisp_Object);
extern Lisp_Object mac_get_tab_group_overview_visible_p (struct frame *);
extern Lisp_Object mac_get_tab_group_tab_bar_visible_p (struct frame *);
extern Lisp_Object mac_get_tab_group_selected_frame (struct frame *);
extern Lisp_Object mac_get_tab_group_frames (struct frame *);
extern CGPoint mac_get_global_mouse (void);
extern bool mac_is_frame_window_toolbar_visible (struct frame *);
extern void mac_set_frame_window_structure_bounds (struct frame *,
						   NativeRectangle);
extern void mac_get_frame_window_structure_bounds (struct frame *,
						   NativeRectangle *);
extern CGFloat mac_get_frame_window_title_bar_height (struct frame *);
extern CGSize mac_get_frame_window_menu_bar_size (struct frame *);
extern CGRect mac_get_frame_window_tool_bar_rect (struct frame *);
extern CGRect mac_get_frame_window_content_rect (struct frame *, bool);
extern CGPoint mac_get_frame_mouse (struct frame *);
extern struct frame *mac_get_frame_at_mouse (bool);
extern void mac_convert_frame_point_to_global (struct frame *, int *, int *);
extern void mac_set_frame_window_background (struct frame *, unsigned long);
extern void mac_update_frame_begin (struct frame *);
extern void mac_update_frame_end (struct frame *);
extern void mac_cursor_to (int, int, int, int);
extern void mac_force_flush (struct frame *);
extern void mac_flush (struct frame *);
extern void mac_create_frame_window (struct frame *);
extern void mac_dispose_frame_window (struct frame *);
extern void mac_change_frame_window_wm_state (struct frame *, WMState, WMState);
#if DRAWING_USE_GCD
extern void mac_draw_to_frame (struct frame *, GC, CGRect,
			       void (^) (CGContextRef, GC));
#else
extern CGContextRef mac_begin_cg_clip (struct frame *, GC, CGRect);
extern void mac_end_cg_clip (struct frame *);
#endif
extern void mac_draw_to_frame_atomic(struct frame *, GC, CGRect,
				     void (^) (CGContextRef, GC));
extern void mac_scroll_area (struct frame *, GC, int, int, int, int, int, int);
extern Lisp_Object mac_color_lookup (const char *);
extern Lisp_Object mac_color_list_alist (void);
extern Lisp_Object mac_display_monitor_attributes_list (struct mac_display_info *);
extern void mac_create_scroll_bar (struct scroll_bar *);
extern void mac_dispose_scroll_bar (struct scroll_bar *);
extern void mac_update_scroll_bar_bounds (struct scroll_bar *);
extern void mac_redraw_scroll_bar (struct scroll_bar *);
extern void mac_set_scroll_bar_thumb (struct scroll_bar *, int, int, int);
extern int mac_get_default_scroll_bar_width (struct frame *);
extern int mac_get_default_scroll_bar_height (struct frame *);
extern bool mac_font_panel_visible_p (void);
extern OSStatus mac_show_hide_font_panel (void);
extern OSStatus mac_set_font_info_for_selection (struct frame *, int, int, int,
						 Lisp_Object);
extern void mac_get_screen_info (struct mac_display_info *);
extern EventTimeout mac_run_loop_run_once (EventTimeout);
extern int mac_get_select_fd (void);
extern int mac_select (int, fd_set *, fd_set *, fd_set *,
		       struct timespec *, sigset_t *);
extern int mac_read_socket (struct terminal *, struct input_event *);
extern void update_frame_tool_bar (struct frame *f);
extern void free_frame_tool_bar (struct frame *f);
extern void mac_show_hourglass (struct frame *);
extern void mac_hide_hourglass (struct frame *);
extern Lisp_Object mac_file_dialog (Lisp_Object, Lisp_Object, Lisp_Object,
				    Lisp_Object, Lisp_Object);
extern Lisp_Object mac_font_dialog (struct frame *f);
extern void mac_page_setup_dialog (void);
extern Lisp_Object mac_get_page_setup (void);
extern Lisp_Object mac_export_frames (Lisp_Object, Lisp_Object);
extern void mac_print_frames_dialog (Lisp_Object);
extern OSStatus mac_get_selection_from_symbol (Lisp_Object, bool, Selection *);
extern bool mac_valid_selection_target_p (Lisp_Object);
extern OSStatus mac_clear_selection (Selection *);
extern Lisp_Object mac_get_selection_ownership_info (Selection);
extern bool mac_valid_selection_value_p (Lisp_Object, Lisp_Object);
extern OSStatus mac_put_selection_value (Selection, Lisp_Object, Lisp_Object);
extern bool mac_selection_has_target_p (Selection, Lisp_Object);
extern Lisp_Object mac_get_selection_value (Selection, Lisp_Object);
extern Lisp_Object mac_get_selection_target_list (Selection);
extern Lisp_Object mac_dnd_default_known_types (void);
extern DragActions mac_dnd_begin_drag_and_drop (struct frame *, DragActions,
						enum mac_return_frame_mode,
						struct frame **, bool,
						Lisp_Object, bool);

extern Emacs_Cursor mac_cursor_create (ThemeCursor, const Emacs_Color *,
				       const Emacs_Color *);
extern void mac_cursor_set (Emacs_Cursor);
extern void mac_cursor_release (Emacs_Cursor);
extern void mac_invalidate_frame_cursor_rects (struct frame *f);
extern void mac_invalidate_rectangles (struct frame *, NativeRectangle *, int);
extern void mac_update_frame_window_style (struct frame *);
extern void mac_update_frame_window_parent (struct frame *);
extern Lisp_Object mac_frame_list_z_order (struct frame *);
extern void mac_frame_restack (struct frame *, struct frame *, bool);
extern bool mac_send_action (Lisp_Object, bool);
extern Lisp_Object mac_osa_language_list (bool);
extern Lisp_Object mac_osa_compile (Lisp_Object, Lisp_Object, bool,
				    Lisp_Object *);
extern Lisp_Object mac_osa_script (Lisp_Object, Lisp_Object, bool, Lisp_Object,
				   Lisp_Object, ptrdiff_t, Lisp_Object *,
				   Lisp_Object *);
extern CFArrayRef mac_document_copy_type_identifiers (void);
extern EmacsDocumentRef mac_document_create_with_url (CFURLRef,
						      CFDictionaryRef);
extern EmacsDocumentRef mac_document_create_with_data (CFDataRef,
						       CFDictionaryRef);
extern size_t mac_document_get_page_count (EmacsDocumentRef);
extern void mac_document_copy_page_info (EmacsDocumentRef, size_t, CGSize *,
					 CGColorRef *, CFDictionaryRef *);
extern void mac_document_draw_page (CGContextRef, CGRect, EmacsDocumentRef,
				    size_t, CFDictionaryRef);
extern void mac_update_accessibility_status (struct frame *);
extern void mac_start_animation (Lisp_Object, Lisp_Object);
extern CFTypeRef mac_sound_create (Lisp_Object, Lisp_Object);
extern void mac_sound_play (CFTypeRef, Lisp_Object, Lisp_Object);
extern void mac_within_gui (void (^block) (void));

#if DRAWING_USE_GCD
#define MAC_BEGIN_DRAW_TO_FRAME(f, gc_draw, rect, context)			\
  mac_draw_to_frame (f, gc_draw, rect, ^(CGContextRef context, GC gc) {
#define MAC_END_DRAW_TO_FRAME(f)		\
  })
#else
#define MAC_BEGIN_DRAW_TO_FRAME(f, gc, rect, context)		\
  do {CGContextRef context = mac_begin_cg_clip (f, gc, rect)
#define MAC_END_DRAW_TO_FRAME(f)		\
  mac_end_cg_clip (f);} while (0)
#endif

#define MAC_BEGIN_DRAW_TO_FRAME_ATOMIC(f, gc_draw, rect, context)			\
  mac_draw_to_frame_atomic (f, gc_draw, rect, ^(CGContextRef context, GC gc) {
#define MAC_END_DRAW_TO_FRAME_ATOMIC(f)		\
  })


#define CG_CONTEXT_FILL_RECT_WITH_GC_BACKGROUND(f, context, rect, gc,	\
						respect_alpha_background) \
  do {									\
    bool clear_p = false, change_alpha_p = false;			\
    CGFloat alpha = 0;							\
    if ((gc)->xgcv.background_transparency == 0				\
	&& (!(respect_alpha_background) || (f)->alpha_background == 1.0)) \
      ;									\
    else if (FRAME_BACKGROUND_ALPHA_ENABLED_P (f)			\
	     && !mac_accessibility_display_options.reduce_transparency_p) \
      {									\
	clear_p = true;							\
	if ((respect_alpha_background) && (f)->alpha_background != 1.0)	\
	  {								\
	    change_alpha_p = true;					\
	    alpha = ((255 - (gc)->xgcv.background_transparency) / 255.0f \
		     * (f)->alpha_background);				\
	  }								\
	}								\
    else								\
      {									\
	change_alpha_p = true;						\
	alpha = 1.0f;							\
      }									\
    if (clear_p) CGContextClearRect (context, rect);			\
    if (!change_alpha_p)						\
      CGContextSetFillColorWithColor (context, (gc)->cg_back_color);	\
    else								\
      {									\
	CGColorRef color =						\
	  CGColorCreateCopyWithAlpha ((gc)->cg_back_color, alpha);	\
	CGContextSetFillColorWithColor (context, color);		\
	CGColorRelease (color);						\
      }									\
    CGContextFillRects (context, &(rect), 1);				\
  } while (0)

/* Defined in macfont.m */
extern void macfont_update_antialias_threshold (void);
extern void *macfont_get_nsctfont (struct font *);
extern Lisp_Object macfont_nsctfont_to_spec (void *);

/* Defined in xdisp.c */
extern void frame_to_window_pixel_xy (struct window *, int *, int *);
extern void rows_from_pos_range (struct window *, ptrdiff_t , ptrdiff_t,
				 Lisp_Object, struct glyph_row **,
				 struct glyph_row **);

/* Defined in font.c */
extern int font_unparse_fcname (Lisp_Object, int, char *, int);

extern frame_parm_handler mac_frame_parm_handlers[];
