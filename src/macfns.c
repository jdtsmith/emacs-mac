/* Graphical user interface functions for macOS.
   Copyright (C) 2000-2008  Free Software Foundation, Inc.
   Copyright (C) 2009-2024  YAMAMOTO Mitsuharu

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
#include <stdio.h>
#include <math.h>

#include "lisp.h"
#include "macterm.h"
#include "frame.h"
#include "window.h"
#include "buffer.h"
#include "dispextern.h"
#include "keyboard.h"
#include "blockinput.h"
#include "charset.h"
#include "coding.h"
#include "termhooks.h"
#include "font.h"
#include "process.h"

#include <sys/types.h>
#include <sys/stat.h>

static ptrdiff_t image_cache_refcount;
#ifdef GLYPH_DEBUG
static int dpyinfo_refcount;
#endif

static struct mac_display_info *mac_display_info_for_name (Lisp_Object);

/* Let the user specify an display with a Lisp object.
   OBJECT may be nil, a frame or a terminal object.
   nil stands for the selected frame--or, if that is not a Mac frame,
   the first Mac display on the list.  */

struct mac_display_info *
check_x_display_info (Lisp_Object object)
{
  struct mac_display_info *dpyinfo = NULL;

  if (NILP (object))
    {
      struct frame *sf = XFRAME (selected_frame);

      if (FRAME_MAC_P (sf) && FRAME_LIVE_P (sf))
	dpyinfo = FRAME_DISPLAY_INFO (sf);
      else if (x_display_list != 0)
	dpyinfo = x_display_list;
      else
	error ("Mac native windows are not in use or not initialized");
    }
  else if (TERMINALP (object))
    {
      struct terminal *t = decode_live_terminal (object);

      if (t->type != output_mac)
        error ("Terminal %d is not a Mac display", t->id);

      dpyinfo = t->display_info.mac;
    }
  else if (STRINGP (object))
    dpyinfo = mac_display_info_for_name (object);
  else
    {
      struct frame *f = decode_window_system_frame (object);
      dpyinfo = FRAME_DISPLAY_INFO (f);
    }

  return dpyinfo;
}


/* The default colors for the Mac color map */
typedef struct colormap_t
{
  unsigned long color;
  const char *name;
} colormap_t;

static const colormap_t mac_color_map[] =
{
  { RGB_TO_ULONG(255, 250, 250), "snow" },
  { RGB_TO_ULONG(248, 248, 255), "ghost white" },
  { RGB_TO_ULONG(248, 248, 255), "GhostWhite" },
  { RGB_TO_ULONG(245, 245, 245), "white smoke" },
  { RGB_TO_ULONG(245, 245, 245), "WhiteSmoke" },
  { RGB_TO_ULONG(220, 220, 220), "gainsboro" },
  { RGB_TO_ULONG(255, 250, 240), "floral white" },
  { RGB_TO_ULONG(255, 250, 240), "FloralWhite" },
  { RGB_TO_ULONG(253, 245, 230), "old lace" },
  { RGB_TO_ULONG(253, 245, 230), "OldLace" },
  { RGB_TO_ULONG(250, 240, 230), "linen" },
  { RGB_TO_ULONG(250, 235, 215), "antique white" },
  { RGB_TO_ULONG(250, 235, 215), "AntiqueWhite" },
  { RGB_TO_ULONG(255, 239, 213), "papaya whip" },
  { RGB_TO_ULONG(255, 239, 213), "PapayaWhip" },
  { RGB_TO_ULONG(255, 235, 205), "blanched almond" },
  { RGB_TO_ULONG(255, 235, 205), "BlanchedAlmond" },
  { RGB_TO_ULONG(255, 228, 196), "bisque" },
  { RGB_TO_ULONG(255, 218, 185), "peach puff" },
  { RGB_TO_ULONG(255, 218, 185), "PeachPuff" },
  { RGB_TO_ULONG(255, 222, 173), "navajo white" },
  { RGB_TO_ULONG(255, 222, 173), "NavajoWhite" },
  { RGB_TO_ULONG(255, 228, 181), "moccasin" },
  { RGB_TO_ULONG(255, 248, 220), "cornsilk" },
  { RGB_TO_ULONG(255, 255, 240), "ivory" },
  { RGB_TO_ULONG(255, 250, 205), "lemon chiffon" },
  { RGB_TO_ULONG(255, 250, 205), "LemonChiffon" },
  { RGB_TO_ULONG(255, 245, 238), "seashell" },
  { RGB_TO_ULONG(240, 255, 240), "honeydew" },
  { RGB_TO_ULONG(245, 255, 250), "mint cream" },
  { RGB_TO_ULONG(245, 255, 250), "MintCream" },
  { RGB_TO_ULONG(240, 255, 255), "azure" },
  { RGB_TO_ULONG(240, 248, 255), "alice blue" },
  { RGB_TO_ULONG(240, 248, 255), "AliceBlue" },
  { RGB_TO_ULONG(230, 230, 250), "lavender" },
  { RGB_TO_ULONG(255, 240, 245), "lavender blush" },
  { RGB_TO_ULONG(255, 240, 245), "LavenderBlush" },
  { RGB_TO_ULONG(255, 228, 225), "misty rose" },
  { RGB_TO_ULONG(255, 228, 225), "MistyRose" },
  { RGB_TO_ULONG(255, 255, 255), "white" },
  { RGB_TO_ULONG(0  , 0  , 0  ), "black" },
  { RGB_TO_ULONG(47 , 79 , 79 ), "dark slate gray" },
  { RGB_TO_ULONG(47 , 79 , 79 ), "DarkSlateGray" },
  { RGB_TO_ULONG(47 , 79 , 79 ), "dark slate grey" },
  { RGB_TO_ULONG(47 , 79 , 79 ), "DarkSlateGrey" },
  { RGB_TO_ULONG(105, 105, 105), "dim gray" },
  { RGB_TO_ULONG(105, 105, 105), "DimGray" },
  { RGB_TO_ULONG(105, 105, 105), "dim grey" },
  { RGB_TO_ULONG(105, 105, 105), "DimGrey" },
  { RGB_TO_ULONG(112, 128, 144), "slate gray" },
  { RGB_TO_ULONG(112, 128, 144), "SlateGray" },
  { RGB_TO_ULONG(112, 128, 144), "slate grey" },
  { RGB_TO_ULONG(112, 128, 144), "SlateGrey" },
  { RGB_TO_ULONG(119, 136, 153), "light slate gray" },
  { RGB_TO_ULONG(119, 136, 153), "LightSlateGray" },
  { RGB_TO_ULONG(119, 136, 153), "light slate grey" },
  { RGB_TO_ULONG(119, 136, 153), "LightSlateGrey" },
  { RGB_TO_ULONG(190, 190, 190), "gray" },
  { RGB_TO_ULONG(190, 190, 190), "grey" },
  { RGB_TO_ULONG(211, 211, 211), "light grey" },
  { RGB_TO_ULONG(211, 211, 211), "LightGrey" },
  { RGB_TO_ULONG(211, 211, 211), "light gray" },
  { RGB_TO_ULONG(211, 211, 211), "LightGray" },
  { RGB_TO_ULONG(25 , 25 , 112), "midnight blue" },
  { RGB_TO_ULONG(25 , 25 , 112), "MidnightBlue" },
  { RGB_TO_ULONG(0  , 0  , 128), "navy" },
  { RGB_TO_ULONG(0  , 0  , 128), "navy blue" },
  { RGB_TO_ULONG(0  , 0  , 128), "NavyBlue" },
  { RGB_TO_ULONG(100, 149, 237), "cornflower blue" },
  { RGB_TO_ULONG(100, 149, 237), "CornflowerBlue" },
  { RGB_TO_ULONG(72 , 61 , 139), "dark slate blue" },
  { RGB_TO_ULONG(72 , 61 , 139), "DarkSlateBlue" },
  { RGB_TO_ULONG(106, 90 , 205), "slate blue" },
  { RGB_TO_ULONG(106, 90 , 205), "SlateBlue" },
  { RGB_TO_ULONG(123, 104, 238), "medium slate blue" },
  { RGB_TO_ULONG(123, 104, 238), "MediumSlateBlue" },
  { RGB_TO_ULONG(132, 112, 255), "light slate blue" },
  { RGB_TO_ULONG(132, 112, 255), "LightSlateBlue" },
  { RGB_TO_ULONG(0  , 0  , 205), "medium blue" },
  { RGB_TO_ULONG(0  , 0  , 205), "MediumBlue" },
  { RGB_TO_ULONG(65 , 105, 225), "royal blue" },
  { RGB_TO_ULONG(65 , 105, 225), "RoyalBlue" },
  { RGB_TO_ULONG(0  , 0  , 255), "blue" },
  { RGB_TO_ULONG(30 , 144, 255), "dodger blue" },
  { RGB_TO_ULONG(30 , 144, 255), "DodgerBlue" },
  { RGB_TO_ULONG(0  , 191, 255), "deep sky blue" },
  { RGB_TO_ULONG(0  , 191, 255), "DeepSkyBlue" },
  { RGB_TO_ULONG(135, 206, 235), "sky blue" },
  { RGB_TO_ULONG(135, 206, 235), "SkyBlue" },
  { RGB_TO_ULONG(135, 206, 250), "light sky blue" },
  { RGB_TO_ULONG(135, 206, 250), "LightSkyBlue" },
  { RGB_TO_ULONG(70 , 130, 180), "steel blue" },
  { RGB_TO_ULONG(70 , 130, 180), "SteelBlue" },
  { RGB_TO_ULONG(176, 196, 222), "light steel blue" },
  { RGB_TO_ULONG(176, 196, 222), "LightSteelBlue" },
  { RGB_TO_ULONG(173, 216, 230), "light blue" },
  { RGB_TO_ULONG(173, 216, 230), "LightBlue" },
  { RGB_TO_ULONG(176, 224, 230), "powder blue" },
  { RGB_TO_ULONG(176, 224, 230), "PowderBlue" },
  { RGB_TO_ULONG(175, 238, 238), "pale turquoise" },
  { RGB_TO_ULONG(175, 238, 238), "PaleTurquoise" },
  { RGB_TO_ULONG(0  , 206, 209), "dark turquoise" },
  { RGB_TO_ULONG(0  , 206, 209), "DarkTurquoise" },
  { RGB_TO_ULONG(72 , 209, 204), "medium turquoise" },
  { RGB_TO_ULONG(72 , 209, 204), "MediumTurquoise" },
  { RGB_TO_ULONG(64 , 224, 208), "turquoise" },
  { RGB_TO_ULONG(0  , 255, 255), "cyan" },
  { RGB_TO_ULONG(224, 255, 255), "light cyan" },
  { RGB_TO_ULONG(224, 255, 255), "LightCyan" },
  { RGB_TO_ULONG(95 , 158, 160), "cadet blue" },
  { RGB_TO_ULONG(95 , 158, 160), "CadetBlue" },
  { RGB_TO_ULONG(102, 205, 170), "medium aquamarine" },
  { RGB_TO_ULONG(102, 205, 170), "MediumAquamarine" },
  { RGB_TO_ULONG(127, 255, 212), "aquamarine" },
  { RGB_TO_ULONG(0  , 100, 0  ), "dark green" },
  { RGB_TO_ULONG(0  , 100, 0  ), "DarkGreen" },
  { RGB_TO_ULONG(85 , 107, 47 ), "dark olive green" },
  { RGB_TO_ULONG(85 , 107, 47 ), "DarkOliveGreen" },
  { RGB_TO_ULONG(143, 188, 143), "dark sea green" },
  { RGB_TO_ULONG(143, 188, 143), "DarkSeaGreen" },
  { RGB_TO_ULONG(46 , 139, 87 ), "sea green" },
  { RGB_TO_ULONG(46 , 139, 87 ), "SeaGreen" },
  { RGB_TO_ULONG(60 , 179, 113), "medium sea green" },
  { RGB_TO_ULONG(60 , 179, 113), "MediumSeaGreen" },
  { RGB_TO_ULONG(32 , 178, 170), "light sea green" },
  { RGB_TO_ULONG(32 , 178, 170), "LightSeaGreen" },
  { RGB_TO_ULONG(152, 251, 152), "pale green" },
  { RGB_TO_ULONG(152, 251, 152), "PaleGreen" },
  { RGB_TO_ULONG(0  , 255, 127), "spring green" },
  { RGB_TO_ULONG(0  , 255, 127), "SpringGreen" },
  { RGB_TO_ULONG(124, 252, 0  ), "lawn green" },
  { RGB_TO_ULONG(124, 252, 0  ), "LawnGreen" },
  { RGB_TO_ULONG(0  , 255, 0  ), "green" },
  { RGB_TO_ULONG(127, 255, 0  ), "chartreuse" },
  { RGB_TO_ULONG(0  , 250, 154), "medium spring green" },
  { RGB_TO_ULONG(0  , 250, 154), "MediumSpringGreen" },
  { RGB_TO_ULONG(173, 255, 47 ), "green yellow" },
  { RGB_TO_ULONG(173, 255, 47 ), "GreenYellow" },
  { RGB_TO_ULONG(50 , 205, 50 ), "lime green" },
  { RGB_TO_ULONG(50 , 205, 50 ), "LimeGreen" },
  { RGB_TO_ULONG(154, 205, 50 ), "yellow green" },
  { RGB_TO_ULONG(154, 205, 50 ), "YellowGreen" },
  { RGB_TO_ULONG(34 , 139, 34 ), "forest green" },
  { RGB_TO_ULONG(34 , 139, 34 ), "ForestGreen" },
  { RGB_TO_ULONG(107, 142, 35 ), "olive drab" },
  { RGB_TO_ULONG(107, 142, 35 ), "OliveDrab" },
  { RGB_TO_ULONG(189, 183, 107), "dark khaki" },
  { RGB_TO_ULONG(189, 183, 107), "DarkKhaki" },
  { RGB_TO_ULONG(240, 230, 140), "khaki" },
  { RGB_TO_ULONG(238, 232, 170), "pale goldenrod" },
  { RGB_TO_ULONG(238, 232, 170), "PaleGoldenrod" },
  { RGB_TO_ULONG(250, 250, 210), "light goldenrod yellow" },
  { RGB_TO_ULONG(250, 250, 210), "LightGoldenrodYellow" },
  { RGB_TO_ULONG(255, 255, 224), "light yellow" },
  { RGB_TO_ULONG(255, 255, 224), "LightYellow" },
  { RGB_TO_ULONG(255, 255, 0  ), "yellow" },
  { RGB_TO_ULONG(255, 215, 0  ), "gold" },
  { RGB_TO_ULONG(238, 221, 130), "light goldenrod" },
  { RGB_TO_ULONG(238, 221, 130), "LightGoldenrod" },
  { RGB_TO_ULONG(218, 165, 32 ), "goldenrod" },
  { RGB_TO_ULONG(184, 134, 11 ), "dark goldenrod" },
  { RGB_TO_ULONG(184, 134, 11 ), "DarkGoldenrod" },
  { RGB_TO_ULONG(188, 143, 143), "rosy brown" },
  { RGB_TO_ULONG(188, 143, 143), "RosyBrown" },
  { RGB_TO_ULONG(205, 92 , 92 ), "indian red" },
  { RGB_TO_ULONG(205, 92 , 92 ), "IndianRed" },
  { RGB_TO_ULONG(139, 69 , 19 ), "saddle brown" },
  { RGB_TO_ULONG(139, 69 , 19 ), "SaddleBrown" },
  { RGB_TO_ULONG(160, 82 , 45 ), "sienna" },
  { RGB_TO_ULONG(205, 133, 63 ), "peru" },
  { RGB_TO_ULONG(222, 184, 135), "burlywood" },
  { RGB_TO_ULONG(245, 245, 220), "beige" },
  { RGB_TO_ULONG(245, 222, 179), "wheat" },
  { RGB_TO_ULONG(244, 164, 96 ), "sandy brown" },
  { RGB_TO_ULONG(244, 164, 96 ), "SandyBrown" },
  { RGB_TO_ULONG(210, 180, 140), "tan" },
  { RGB_TO_ULONG(210, 105, 30 ), "chocolate" },
  { RGB_TO_ULONG(178, 34 , 34 ), "firebrick" },
  { RGB_TO_ULONG(165, 42 , 42 ), "brown" },
  { RGB_TO_ULONG(233, 150, 122), "dark salmon" },
  { RGB_TO_ULONG(233, 150, 122), "DarkSalmon" },
  { RGB_TO_ULONG(250, 128, 114), "salmon" },
  { RGB_TO_ULONG(255, 160, 122), "light salmon" },
  { RGB_TO_ULONG(255, 160, 122), "LightSalmon" },
  { RGB_TO_ULONG(255, 165, 0  ), "orange" },
  { RGB_TO_ULONG(255, 140, 0  ), "dark orange" },
  { RGB_TO_ULONG(255, 140, 0  ), "DarkOrange" },
  { RGB_TO_ULONG(255, 127, 80 ), "coral" },
  { RGB_TO_ULONG(240, 128, 128), "light coral" },
  { RGB_TO_ULONG(240, 128, 128), "LightCoral" },
  { RGB_TO_ULONG(255, 99 , 71 ), "tomato" },
  { RGB_TO_ULONG(255, 69 , 0  ), "orange red" },
  { RGB_TO_ULONG(255, 69 , 0  ), "OrangeRed" },
  { RGB_TO_ULONG(255, 0  , 0  ), "red" },
  { RGB_TO_ULONG(255, 105, 180), "hot pink" },
  { RGB_TO_ULONG(255, 105, 180), "HotPink" },
  { RGB_TO_ULONG(255, 20 , 147), "deep pink" },
  { RGB_TO_ULONG(255, 20 , 147), "DeepPink" },
  { RGB_TO_ULONG(255, 192, 203), "pink" },
  { RGB_TO_ULONG(255, 182, 193), "light pink" },
  { RGB_TO_ULONG(255, 182, 193), "LightPink" },
  { RGB_TO_ULONG(219, 112, 147), "pale violet red" },
  { RGB_TO_ULONG(219, 112, 147), "PaleVioletRed" },
  { RGB_TO_ULONG(176, 48 , 96 ), "maroon" },
  { RGB_TO_ULONG(199, 21 , 133), "medium violet red" },
  { RGB_TO_ULONG(199, 21 , 133), "MediumVioletRed" },
  { RGB_TO_ULONG(208, 32 , 144), "violet red" },
  { RGB_TO_ULONG(208, 32 , 144), "VioletRed" },
  { RGB_TO_ULONG(255, 0  , 255), "magenta" },
  { RGB_TO_ULONG(238, 130, 238), "violet" },
  { RGB_TO_ULONG(221, 160, 221), "plum" },
  { RGB_TO_ULONG(218, 112, 214), "orchid" },
  { RGB_TO_ULONG(186, 85 , 211), "medium orchid" },
  { RGB_TO_ULONG(186, 85 , 211), "MediumOrchid" },
  { RGB_TO_ULONG(153, 50 , 204), "dark orchid" },
  { RGB_TO_ULONG(153, 50 , 204), "DarkOrchid" },
  { RGB_TO_ULONG(148, 0  , 211), "dark violet" },
  { RGB_TO_ULONG(148, 0  , 211), "DarkViolet" },
  { RGB_TO_ULONG(138, 43 , 226), "blue violet" },
  { RGB_TO_ULONG(138, 43 , 226), "BlueViolet" },
  { RGB_TO_ULONG(160, 32 , 240), "purple" },
  { RGB_TO_ULONG(147, 112, 219), "medium purple" },
  { RGB_TO_ULONG(147, 112, 219), "MediumPurple" },
  { RGB_TO_ULONG(216, 191, 216), "thistle" },
  { RGB_TO_ULONG(255, 250, 250), "snow1" },
  { RGB_TO_ULONG(238, 233, 233), "snow2" },
  { RGB_TO_ULONG(205, 201, 201), "snow3" },
  { RGB_TO_ULONG(139, 137, 137), "snow4" },
  { RGB_TO_ULONG(255, 245, 238), "seashell1" },
  { RGB_TO_ULONG(238, 229, 222), "seashell2" },
  { RGB_TO_ULONG(205, 197, 191), "seashell3" },
  { RGB_TO_ULONG(139, 134, 130), "seashell4" },
  { RGB_TO_ULONG(255, 239, 219), "AntiqueWhite1" },
  { RGB_TO_ULONG(238, 223, 204), "AntiqueWhite2" },
  { RGB_TO_ULONG(205, 192, 176), "AntiqueWhite3" },
  { RGB_TO_ULONG(139, 131, 120), "AntiqueWhite4" },
  { RGB_TO_ULONG(255, 228, 196), "bisque1" },
  { RGB_TO_ULONG(238, 213, 183), "bisque2" },
  { RGB_TO_ULONG(205, 183, 158), "bisque3" },
  { RGB_TO_ULONG(139, 125, 107), "bisque4" },
  { RGB_TO_ULONG(255, 218, 185), "PeachPuff1" },
  { RGB_TO_ULONG(238, 203, 173), "PeachPuff2" },
  { RGB_TO_ULONG(205, 175, 149), "PeachPuff3" },
  { RGB_TO_ULONG(139, 119, 101), "PeachPuff4" },
  { RGB_TO_ULONG(255, 222, 173), "NavajoWhite1" },
  { RGB_TO_ULONG(238, 207, 161), "NavajoWhite2" },
  { RGB_TO_ULONG(205, 179, 139), "NavajoWhite3" },
  { RGB_TO_ULONG(139, 121, 94), "NavajoWhite4" },
  { RGB_TO_ULONG(255, 250, 205), "LemonChiffon1" },
  { RGB_TO_ULONG(238, 233, 191), "LemonChiffon2" },
  { RGB_TO_ULONG(205, 201, 165), "LemonChiffon3" },
  { RGB_TO_ULONG(139, 137, 112), "LemonChiffon4" },
  { RGB_TO_ULONG(255, 248, 220), "cornsilk1" },
  { RGB_TO_ULONG(238, 232, 205), "cornsilk2" },
  { RGB_TO_ULONG(205, 200, 177), "cornsilk3" },
  { RGB_TO_ULONG(139, 136, 120), "cornsilk4" },
  { RGB_TO_ULONG(255, 255, 240), "ivory1" },
  { RGB_TO_ULONG(238, 238, 224), "ivory2" },
  { RGB_TO_ULONG(205, 205, 193), "ivory3" },
  { RGB_TO_ULONG(139, 139, 131), "ivory4" },
  { RGB_TO_ULONG(240, 255, 240), "honeydew1" },
  { RGB_TO_ULONG(224, 238, 224), "honeydew2" },
  { RGB_TO_ULONG(193, 205, 193), "honeydew3" },
  { RGB_TO_ULONG(131, 139, 131), "honeydew4" },
  { RGB_TO_ULONG(255, 240, 245), "LavenderBlush1" },
  { RGB_TO_ULONG(238, 224, 229), "LavenderBlush2" },
  { RGB_TO_ULONG(205, 193, 197), "LavenderBlush3" },
  { RGB_TO_ULONG(139, 131, 134), "LavenderBlush4" },
  { RGB_TO_ULONG(255, 228, 225), "MistyRose1" },
  { RGB_TO_ULONG(238, 213, 210), "MistyRose2" },
  { RGB_TO_ULONG(205, 183, 181), "MistyRose3" },
  { RGB_TO_ULONG(139, 125, 123), "MistyRose4" },
  { RGB_TO_ULONG(240, 255, 255), "azure1" },
  { RGB_TO_ULONG(224, 238, 238), "azure2" },
  { RGB_TO_ULONG(193, 205, 205), "azure3" },
  { RGB_TO_ULONG(131, 139, 139), "azure4" },
  { RGB_TO_ULONG(131, 111, 255), "SlateBlue1" },
  { RGB_TO_ULONG(122, 103, 238), "SlateBlue2" },
  { RGB_TO_ULONG(105, 89 , 205), "SlateBlue3" },
  { RGB_TO_ULONG(71 , 60 , 139), "SlateBlue4" },
  { RGB_TO_ULONG(72 , 118, 255), "RoyalBlue1" },
  { RGB_TO_ULONG(67 , 110, 238), "RoyalBlue2" },
  { RGB_TO_ULONG(58 , 95 , 205), "RoyalBlue3" },
  { RGB_TO_ULONG(39 , 64 , 139), "RoyalBlue4" },
  { RGB_TO_ULONG(0  , 0  , 255), "blue1" },
  { RGB_TO_ULONG(0  , 0  , 238), "blue2" },
  { RGB_TO_ULONG(0  , 0  , 205), "blue3" },
  { RGB_TO_ULONG(0  , 0  , 139), "blue4" },
  { RGB_TO_ULONG(30 , 144, 255), "DodgerBlue1" },
  { RGB_TO_ULONG(28 , 134, 238), "DodgerBlue2" },
  { RGB_TO_ULONG(24 , 116, 205), "DodgerBlue3" },
  { RGB_TO_ULONG(16 , 78 , 139), "DodgerBlue4" },
  { RGB_TO_ULONG(99 , 184, 255), "SteelBlue1" },
  { RGB_TO_ULONG(92 , 172, 238), "SteelBlue2" },
  { RGB_TO_ULONG(79 , 148, 205), "SteelBlue3" },
  { RGB_TO_ULONG(54 , 100, 139), "SteelBlue4" },
  { RGB_TO_ULONG(0  , 191, 255), "DeepSkyBlue1" },
  { RGB_TO_ULONG(0  , 178, 238), "DeepSkyBlue2" },
  { RGB_TO_ULONG(0  , 154, 205), "DeepSkyBlue3" },
  { RGB_TO_ULONG(0  , 104, 139), "DeepSkyBlue4" },
  { RGB_TO_ULONG(135, 206, 255), "SkyBlue1" },
  { RGB_TO_ULONG(126, 192, 238), "SkyBlue2" },
  { RGB_TO_ULONG(108, 166, 205), "SkyBlue3" },
  { RGB_TO_ULONG(74 , 112, 139), "SkyBlue4" },
  { RGB_TO_ULONG(176, 226, 255), "LightSkyBlue1" },
  { RGB_TO_ULONG(164, 211, 238), "LightSkyBlue2" },
  { RGB_TO_ULONG(141, 182, 205), "LightSkyBlue3" },
  { RGB_TO_ULONG(96 , 123, 139), "LightSkyBlue4" },
  { RGB_TO_ULONG(198, 226, 255), "SlateGray1" },
  { RGB_TO_ULONG(185, 211, 238), "SlateGray2" },
  { RGB_TO_ULONG(159, 182, 205), "SlateGray3" },
  { RGB_TO_ULONG(108, 123, 139), "SlateGray4" },
  { RGB_TO_ULONG(202, 225, 255), "LightSteelBlue1" },
  { RGB_TO_ULONG(188, 210, 238), "LightSteelBlue2" },
  { RGB_TO_ULONG(162, 181, 205), "LightSteelBlue3" },
  { RGB_TO_ULONG(110, 123, 139), "LightSteelBlue4" },
  { RGB_TO_ULONG(191, 239, 255), "LightBlue1" },
  { RGB_TO_ULONG(178, 223, 238), "LightBlue2" },
  { RGB_TO_ULONG(154, 192, 205), "LightBlue3" },
  { RGB_TO_ULONG(104, 131, 139), "LightBlue4" },
  { RGB_TO_ULONG(224, 255, 255), "LightCyan1" },
  { RGB_TO_ULONG(209, 238, 238), "LightCyan2" },
  { RGB_TO_ULONG(180, 205, 205), "LightCyan3" },
  { RGB_TO_ULONG(122, 139, 139), "LightCyan4" },
  { RGB_TO_ULONG(187, 255, 255), "PaleTurquoise1" },
  { RGB_TO_ULONG(174, 238, 238), "PaleTurquoise2" },
  { RGB_TO_ULONG(150, 205, 205), "PaleTurquoise3" },
  { RGB_TO_ULONG(102, 139, 139), "PaleTurquoise4" },
  { RGB_TO_ULONG(152, 245, 255), "CadetBlue1" },
  { RGB_TO_ULONG(142, 229, 238), "CadetBlue2" },
  { RGB_TO_ULONG(122, 197, 205), "CadetBlue3" },
  { RGB_TO_ULONG(83 , 134, 139), "CadetBlue4" },
  { RGB_TO_ULONG(0  , 245, 255), "turquoise1" },
  { RGB_TO_ULONG(0  , 229, 238), "turquoise2" },
  { RGB_TO_ULONG(0  , 197, 205), "turquoise3" },
  { RGB_TO_ULONG(0  , 134, 139), "turquoise4" },
  { RGB_TO_ULONG(0  , 255, 255), "cyan1" },
  { RGB_TO_ULONG(0  , 238, 238), "cyan2" },
  { RGB_TO_ULONG(0  , 205, 205), "cyan3" },
  { RGB_TO_ULONG(0  , 139, 139), "cyan4" },
  { RGB_TO_ULONG(151, 255, 255), "DarkSlateGray1" },
  { RGB_TO_ULONG(141, 238, 238), "DarkSlateGray2" },
  { RGB_TO_ULONG(121, 205, 205), "DarkSlateGray3" },
  { RGB_TO_ULONG(82 , 139, 139), "DarkSlateGray4" },
  { RGB_TO_ULONG(127, 255, 212), "aquamarine1" },
  { RGB_TO_ULONG(118, 238, 198), "aquamarine2" },
  { RGB_TO_ULONG(102, 205, 170), "aquamarine3" },
  { RGB_TO_ULONG(69 , 139, 116), "aquamarine4" },
  { RGB_TO_ULONG(193, 255, 193), "DarkSeaGreen1" },
  { RGB_TO_ULONG(180, 238, 180), "DarkSeaGreen2" },
  { RGB_TO_ULONG(155, 205, 155), "DarkSeaGreen3" },
  { RGB_TO_ULONG(105, 139, 105), "DarkSeaGreen4" },
  { RGB_TO_ULONG(84 , 255, 159), "SeaGreen1" },
  { RGB_TO_ULONG(78 , 238, 148), "SeaGreen2" },
  { RGB_TO_ULONG(67 , 205, 128), "SeaGreen3" },
  { RGB_TO_ULONG(46 , 139, 87 ), "SeaGreen4" },
  { RGB_TO_ULONG(154, 255, 154), "PaleGreen1" },
  { RGB_TO_ULONG(144, 238, 144), "PaleGreen2" },
  { RGB_TO_ULONG(124, 205, 124), "PaleGreen3" },
  { RGB_TO_ULONG(84 , 139, 84 ), "PaleGreen4" },
  { RGB_TO_ULONG(0  , 255, 127), "SpringGreen1" },
  { RGB_TO_ULONG(0  , 238, 118), "SpringGreen2" },
  { RGB_TO_ULONG(0  , 205, 102), "SpringGreen3" },
  { RGB_TO_ULONG(0  , 139, 69 ), "SpringGreen4" },
  { RGB_TO_ULONG(0  , 255, 0  ), "green1" },
  { RGB_TO_ULONG(0  , 238, 0  ), "green2" },
  { RGB_TO_ULONG(0  , 205, 0  ), "green3" },
  { RGB_TO_ULONG(0  , 139, 0  ), "green4" },
  { RGB_TO_ULONG(127, 255, 0  ), "chartreuse1" },
  { RGB_TO_ULONG(118, 238, 0  ), "chartreuse2" },
  { RGB_TO_ULONG(102, 205, 0  ), "chartreuse3" },
  { RGB_TO_ULONG(69 , 139, 0  ), "chartreuse4" },
  { RGB_TO_ULONG(192, 255, 62 ), "OliveDrab1" },
  { RGB_TO_ULONG(179, 238, 58 ), "OliveDrab2" },
  { RGB_TO_ULONG(154, 205, 50 ), "OliveDrab3" },
  { RGB_TO_ULONG(105, 139, 34 ), "OliveDrab4" },
  { RGB_TO_ULONG(202, 255, 112), "DarkOliveGreen1" },
  { RGB_TO_ULONG(188, 238, 104), "DarkOliveGreen2" },
  { RGB_TO_ULONG(162, 205, 90 ), "DarkOliveGreen3" },
  { RGB_TO_ULONG(110, 139, 61 ), "DarkOliveGreen4" },
  { RGB_TO_ULONG(255, 246, 143), "khaki1" },
  { RGB_TO_ULONG(238, 230, 133), "khaki2" },
  { RGB_TO_ULONG(205, 198, 115), "khaki3" },
  { RGB_TO_ULONG(139, 134, 78 ), "khaki4" },
  { RGB_TO_ULONG(255, 236, 139), "LightGoldenrod1" },
  { RGB_TO_ULONG(238, 220, 130), "LightGoldenrod2" },
  { RGB_TO_ULONG(205, 190, 112), "LightGoldenrod3" },
  { RGB_TO_ULONG(139, 129, 76 ), "LightGoldenrod4" },
  { RGB_TO_ULONG(255, 255, 224), "LightYellow1" },
  { RGB_TO_ULONG(238, 238, 209), "LightYellow2" },
  { RGB_TO_ULONG(205, 205, 180), "LightYellow3" },
  { RGB_TO_ULONG(139, 139, 122), "LightYellow4" },
  { RGB_TO_ULONG(255, 255, 0  ), "yellow1" },
  { RGB_TO_ULONG(238, 238, 0  ), "yellow2" },
  { RGB_TO_ULONG(205, 205, 0  ), "yellow3" },
  { RGB_TO_ULONG(139, 139, 0  ), "yellow4" },
  { RGB_TO_ULONG(255, 215, 0  ), "gold1" },
  { RGB_TO_ULONG(238, 201, 0  ), "gold2" },
  { RGB_TO_ULONG(205, 173, 0  ), "gold3" },
  { RGB_TO_ULONG(139, 117, 0  ), "gold4" },
  { RGB_TO_ULONG(255, 193, 37 ), "goldenrod1" },
  { RGB_TO_ULONG(238, 180, 34 ), "goldenrod2" },
  { RGB_TO_ULONG(205, 155, 29 ), "goldenrod3" },
  { RGB_TO_ULONG(139, 105, 20 ), "goldenrod4" },
  { RGB_TO_ULONG(255, 185, 15 ), "DarkGoldenrod1" },
  { RGB_TO_ULONG(238, 173, 14 ), "DarkGoldenrod2" },
  { RGB_TO_ULONG(205, 149, 12 ), "DarkGoldenrod3" },
  { RGB_TO_ULONG(139, 101, 8  ), "DarkGoldenrod4" },
  { RGB_TO_ULONG(255, 193, 193), "RosyBrown1" },
  { RGB_TO_ULONG(238, 180, 180), "RosyBrown2" },
  { RGB_TO_ULONG(205, 155, 155), "RosyBrown3" },
  { RGB_TO_ULONG(139, 105, 105), "RosyBrown4" },
  { RGB_TO_ULONG(255, 106, 106), "IndianRed1" },
  { RGB_TO_ULONG(238, 99 , 99 ), "IndianRed2" },
  { RGB_TO_ULONG(205, 85 , 85 ), "IndianRed3" },
  { RGB_TO_ULONG(139, 58 , 58 ), "IndianRed4" },
  { RGB_TO_ULONG(255, 130, 71 ), "sienna1" },
  { RGB_TO_ULONG(238, 121, 66 ), "sienna2" },
  { RGB_TO_ULONG(205, 104, 57 ), "sienna3" },
  { RGB_TO_ULONG(139, 71 , 38 ), "sienna4" },
  { RGB_TO_ULONG(255, 211, 155), "burlywood1" },
  { RGB_TO_ULONG(238, 197, 145), "burlywood2" },
  { RGB_TO_ULONG(205, 170, 125), "burlywood3" },
  { RGB_TO_ULONG(139, 115, 85 ), "burlywood4" },
  { RGB_TO_ULONG(255, 231, 186), "wheat1" },
  { RGB_TO_ULONG(238, 216, 174), "wheat2" },
  { RGB_TO_ULONG(205, 186, 150), "wheat3" },
  { RGB_TO_ULONG(139, 126, 102), "wheat4" },
  { RGB_TO_ULONG(255, 165, 79 ), "tan1" },
  { RGB_TO_ULONG(238, 154, 73 ), "tan2" },
  { RGB_TO_ULONG(205, 133, 63 ), "tan3" },
  { RGB_TO_ULONG(139, 90 , 43 ), "tan4" },
  { RGB_TO_ULONG(255, 127, 36 ), "chocolate1" },
  { RGB_TO_ULONG(238, 118, 33 ), "chocolate2" },
  { RGB_TO_ULONG(205, 102, 29 ), "chocolate3" },
  { RGB_TO_ULONG(139, 69 , 19 ), "chocolate4" },
  { RGB_TO_ULONG(255, 48 , 48 ), "firebrick1" },
  { RGB_TO_ULONG(238, 44 , 44 ), "firebrick2" },
  { RGB_TO_ULONG(205, 38 , 38 ), "firebrick3" },
  { RGB_TO_ULONG(139, 26 , 26 ), "firebrick4" },
  { RGB_TO_ULONG(255, 64 , 64 ), "brown1" },
  { RGB_TO_ULONG(238, 59 , 59 ), "brown2" },
  { RGB_TO_ULONG(205, 51 , 51 ), "brown3" },
  { RGB_TO_ULONG(139, 35 , 35 ), "brown4" },
  { RGB_TO_ULONG(255, 140, 105), "salmon1" },
  { RGB_TO_ULONG(238, 130, 98 ), "salmon2" },
  { RGB_TO_ULONG(205, 112, 84 ), "salmon3" },
  { RGB_TO_ULONG(139, 76 , 57 ), "salmon4" },
  { RGB_TO_ULONG(255, 160, 122), "LightSalmon1" },
  { RGB_TO_ULONG(238, 149, 114), "LightSalmon2" },
  { RGB_TO_ULONG(205, 129, 98 ), "LightSalmon3" },
  { RGB_TO_ULONG(139, 87 , 66 ), "LightSalmon4" },
  { RGB_TO_ULONG(255, 165, 0  ), "orange1" },
  { RGB_TO_ULONG(238, 154, 0  ), "orange2" },
  { RGB_TO_ULONG(205, 133, 0  ), "orange3" },
  { RGB_TO_ULONG(139, 90 , 0  ), "orange4" },
  { RGB_TO_ULONG(255, 127, 0  ), "DarkOrange1" },
  { RGB_TO_ULONG(238, 118, 0  ), "DarkOrange2" },
  { RGB_TO_ULONG(205, 102, 0  ), "DarkOrange3" },
  { RGB_TO_ULONG(139, 69 , 0  ), "DarkOrange4" },
  { RGB_TO_ULONG(255, 114, 86 ), "coral1" },
  { RGB_TO_ULONG(238, 106, 80 ), "coral2" },
  { RGB_TO_ULONG(205, 91 , 69 ), "coral3" },
  { RGB_TO_ULONG(139, 62 , 47 ), "coral4" },
  { RGB_TO_ULONG(255, 99 , 71 ), "tomato1" },
  { RGB_TO_ULONG(238, 92 , 66 ), "tomato2" },
  { RGB_TO_ULONG(205, 79 , 57 ), "tomato3" },
  { RGB_TO_ULONG(139, 54 , 38 ), "tomato4" },
  { RGB_TO_ULONG(255, 69 , 0  ), "OrangeRed1" },
  { RGB_TO_ULONG(238, 64 , 0  ), "OrangeRed2" },
  { RGB_TO_ULONG(205, 55 , 0  ), "OrangeRed3" },
  { RGB_TO_ULONG(139, 37 , 0  ), "OrangeRed4" },
  { RGB_TO_ULONG(255, 0  , 0  ), "red1" },
  { RGB_TO_ULONG(238, 0  , 0  ), "red2" },
  { RGB_TO_ULONG(205, 0  , 0  ), "red3" },
  { RGB_TO_ULONG(139, 0  , 0  ), "red4" },
  { RGB_TO_ULONG(255, 20 , 147), "DeepPink1" },
  { RGB_TO_ULONG(238, 18 , 137), "DeepPink2" },
  { RGB_TO_ULONG(205, 16 , 118), "DeepPink3" },
  { RGB_TO_ULONG(139, 10 , 80 ), "DeepPink4" },
  { RGB_TO_ULONG(255, 110, 180), "HotPink1" },
  { RGB_TO_ULONG(238, 106, 167), "HotPink2" },
  { RGB_TO_ULONG(205, 96 , 144), "HotPink3" },
  { RGB_TO_ULONG(139, 58 , 98 ), "HotPink4" },
  { RGB_TO_ULONG(255, 181, 197), "pink1" },
  { RGB_TO_ULONG(238, 169, 184), "pink2" },
  { RGB_TO_ULONG(205, 145, 158), "pink3" },
  { RGB_TO_ULONG(139, 99 , 108), "pink4" },
  { RGB_TO_ULONG(255, 174, 185), "LightPink1" },
  { RGB_TO_ULONG(238, 162, 173), "LightPink2" },
  { RGB_TO_ULONG(205, 140, 149), "LightPink3" },
  { RGB_TO_ULONG(139, 95 , 101), "LightPink4" },
  { RGB_TO_ULONG(255, 130, 171), "PaleVioletRed1" },
  { RGB_TO_ULONG(238, 121, 159), "PaleVioletRed2" },
  { RGB_TO_ULONG(205, 104, 137), "PaleVioletRed3" },
  { RGB_TO_ULONG(139, 71 , 93 ), "PaleVioletRed4" },
  { RGB_TO_ULONG(255, 52 , 179), "maroon1" },
  { RGB_TO_ULONG(238, 48 , 167), "maroon2" },
  { RGB_TO_ULONG(205, 41 , 144), "maroon3" },
  { RGB_TO_ULONG(139, 28 , 98 ), "maroon4" },
  { RGB_TO_ULONG(255, 62 , 150), "VioletRed1" },
  { RGB_TO_ULONG(238, 58 , 140), "VioletRed2" },
  { RGB_TO_ULONG(205, 50 , 120), "VioletRed3" },
  { RGB_TO_ULONG(139, 34 , 82 ), "VioletRed4" },
  { RGB_TO_ULONG(255, 0  , 255), "magenta1" },
  { RGB_TO_ULONG(238, 0  , 238), "magenta2" },
  { RGB_TO_ULONG(205, 0  , 205), "magenta3" },
  { RGB_TO_ULONG(139, 0  , 139), "magenta4" },
  { RGB_TO_ULONG(255, 131, 250), "orchid1" },
  { RGB_TO_ULONG(238, 122, 233), "orchid2" },
  { RGB_TO_ULONG(205, 105, 201), "orchid3" },
  { RGB_TO_ULONG(139, 71 , 137), "orchid4" },
  { RGB_TO_ULONG(255, 187, 255), "plum1" },
  { RGB_TO_ULONG(238, 174, 238), "plum2" },
  { RGB_TO_ULONG(205, 150, 205), "plum3" },
  { RGB_TO_ULONG(139, 102, 139), "plum4" },
  { RGB_TO_ULONG(224, 102, 255), "MediumOrchid1" },
  { RGB_TO_ULONG(209, 95 , 238), "MediumOrchid2" },
  { RGB_TO_ULONG(180, 82 , 205), "MediumOrchid3" },
  { RGB_TO_ULONG(122, 55 , 139), "MediumOrchid4" },
  { RGB_TO_ULONG(191, 62 , 255), "DarkOrchid1" },
  { RGB_TO_ULONG(178, 58 , 238), "DarkOrchid2" },
  { RGB_TO_ULONG(154, 50 , 205), "DarkOrchid3" },
  { RGB_TO_ULONG(104, 34 , 139), "DarkOrchid4" },
  { RGB_TO_ULONG(155, 48 , 255), "purple1" },
  { RGB_TO_ULONG(145, 44 , 238), "purple2" },
  { RGB_TO_ULONG(125, 38 , 205), "purple3" },
  { RGB_TO_ULONG(85 , 26 , 139), "purple4" },
  { RGB_TO_ULONG(171, 130, 255), "MediumPurple1" },
  { RGB_TO_ULONG(159, 121, 238), "MediumPurple2" },
  { RGB_TO_ULONG(137, 104, 205), "MediumPurple3" },
  { RGB_TO_ULONG(93 , 71 , 139), "MediumPurple4" },
  { RGB_TO_ULONG(255, 225, 255), "thistle1" },
  { RGB_TO_ULONG(238, 210, 238), "thistle2" },
  { RGB_TO_ULONG(205, 181, 205), "thistle3" },
  { RGB_TO_ULONG(139, 123, 139), "thistle4" },
  { RGB_TO_ULONG(0  , 0  , 0  ), "gray0" },
  { RGB_TO_ULONG(0  , 0  , 0  ), "grey0" },
  { RGB_TO_ULONG(3  , 3  , 3  ), "gray1" },
  { RGB_TO_ULONG(3  , 3  , 3  ), "grey1" },
  { RGB_TO_ULONG(5  , 5  , 5  ), "gray2" },
  { RGB_TO_ULONG(5  , 5  , 5  ), "grey2" },
  { RGB_TO_ULONG(8  , 8  , 8  ), "gray3" },
  { RGB_TO_ULONG(8  , 8  , 8  ), "grey3" },
  { RGB_TO_ULONG(10 , 10 , 10 ), "gray4" },
  { RGB_TO_ULONG(10 , 10 , 10 ), "grey4" },
  { RGB_TO_ULONG(13 , 13 , 13 ), "gray5" },
  { RGB_TO_ULONG(13 , 13 , 13 ), "grey5" },
  { RGB_TO_ULONG(15 , 15 , 15 ), "gray6" },
  { RGB_TO_ULONG(15 , 15 , 15 ), "grey6" },
  { RGB_TO_ULONG(18 , 18 , 18 ), "gray7" },
  { RGB_TO_ULONG(18 , 18 , 18 ), "grey7" },
  { RGB_TO_ULONG(20 , 20 , 20 ), "gray8" },
  { RGB_TO_ULONG(20 , 20 , 20 ), "grey8" },
  { RGB_TO_ULONG(23 , 23 , 23 ), "gray9" },
  { RGB_TO_ULONG(23 , 23 , 23 ), "grey9" },
  { RGB_TO_ULONG(26 , 26 , 26 ), "gray10" },
  { RGB_TO_ULONG(26 , 26 , 26 ), "grey10" },
  { RGB_TO_ULONG(28 , 28 , 28 ), "gray11" },
  { RGB_TO_ULONG(28 , 28 , 28 ), "grey11" },
  { RGB_TO_ULONG(31 , 31 , 31 ), "gray12" },
  { RGB_TO_ULONG(31 , 31 , 31 ), "grey12" },
  { RGB_TO_ULONG(33 , 33 , 33 ), "gray13" },
  { RGB_TO_ULONG(33 , 33 , 33 ), "grey13" },
  { RGB_TO_ULONG(36 , 36 , 36 ), "gray14" },
  { RGB_TO_ULONG(36 , 36 , 36 ), "grey14" },
  { RGB_TO_ULONG(38 , 38 , 38 ), "gray15" },
  { RGB_TO_ULONG(38 , 38 , 38 ), "grey15" },
  { RGB_TO_ULONG(41 , 41 , 41 ), "gray16" },
  { RGB_TO_ULONG(41 , 41 , 41 ), "grey16" },
  { RGB_TO_ULONG(43 , 43 , 43 ), "gray17" },
  { RGB_TO_ULONG(43 , 43 , 43 ), "grey17" },
  { RGB_TO_ULONG(46 , 46 , 46 ), "gray18" },
  { RGB_TO_ULONG(46 , 46 , 46 ), "grey18" },
  { RGB_TO_ULONG(48 , 48 , 48 ), "gray19" },
  { RGB_TO_ULONG(48 , 48 , 48 ), "grey19" },
  { RGB_TO_ULONG(51 , 51 , 51 ), "gray20" },
  { RGB_TO_ULONG(51 , 51 , 51 ), "grey20" },
  { RGB_TO_ULONG(54 , 54 , 54 ), "gray21" },
  { RGB_TO_ULONG(54 , 54 , 54 ), "grey21" },
  { RGB_TO_ULONG(56 , 56 , 56 ), "gray22" },
  { RGB_TO_ULONG(56 , 56 , 56 ), "grey22" },
  { RGB_TO_ULONG(59 , 59 , 59 ), "gray23" },
  { RGB_TO_ULONG(59 , 59 , 59 ), "grey23" },
  { RGB_TO_ULONG(61 , 61 , 61 ), "gray24" },
  { RGB_TO_ULONG(61 , 61 , 61 ), "grey24" },
  { RGB_TO_ULONG(64 , 64 , 64 ), "gray25" },
  { RGB_TO_ULONG(64 , 64 , 64 ), "grey25" },
  { RGB_TO_ULONG(66 , 66 , 66 ), "gray26" },
  { RGB_TO_ULONG(66 , 66 , 66 ), "grey26" },
  { RGB_TO_ULONG(69 , 69 , 69 ), "gray27" },
  { RGB_TO_ULONG(69 , 69 , 69 ), "grey27" },
  { RGB_TO_ULONG(71 , 71 , 71 ), "gray28" },
  { RGB_TO_ULONG(71 , 71 , 71 ), "grey28" },
  { RGB_TO_ULONG(74 , 74 , 74 ), "gray29" },
  { RGB_TO_ULONG(74 , 74 , 74 ), "grey29" },
  { RGB_TO_ULONG(77 , 77 , 77 ), "gray30" },
  { RGB_TO_ULONG(77 , 77 , 77 ), "grey30" },
  { RGB_TO_ULONG(79 , 79 , 79 ), "gray31" },
  { RGB_TO_ULONG(79 , 79 , 79 ), "grey31" },
  { RGB_TO_ULONG(82 , 82 , 82 ), "gray32" },
  { RGB_TO_ULONG(82 , 82 , 82 ), "grey32" },
  { RGB_TO_ULONG(84 , 84 , 84 ), "gray33" },
  { RGB_TO_ULONG(84 , 84 , 84 ), "grey33" },
  { RGB_TO_ULONG(87 , 87 , 87 ), "gray34" },
  { RGB_TO_ULONG(87 , 87 , 87 ), "grey34" },
  { RGB_TO_ULONG(89 , 89 , 89 ), "gray35" },
  { RGB_TO_ULONG(89 , 89 , 89 ), "grey35" },
  { RGB_TO_ULONG(92 , 92 , 92 ), "gray36" },
  { RGB_TO_ULONG(92 , 92 , 92 ), "grey36" },
  { RGB_TO_ULONG(94 , 94 , 94 ), "gray37" },
  { RGB_TO_ULONG(94 , 94 , 94 ), "grey37" },
  { RGB_TO_ULONG(97 , 97 , 97 ), "gray38" },
  { RGB_TO_ULONG(97 , 97 , 97 ), "grey38" },
  { RGB_TO_ULONG(99 , 99 , 99 ), "gray39" },
  { RGB_TO_ULONG(99 , 99 , 99 ), "grey39" },
  { RGB_TO_ULONG(102, 102, 102), "gray40" },
  { RGB_TO_ULONG(102, 102, 102), "grey40" },
  { RGB_TO_ULONG(105, 105, 105), "gray41" },
  { RGB_TO_ULONG(105, 105, 105), "grey41" },
  { RGB_TO_ULONG(107, 107, 107), "gray42" },
  { RGB_TO_ULONG(107, 107, 107), "grey42" },
  { RGB_TO_ULONG(110, 110, 110), "gray43" },
  { RGB_TO_ULONG(110, 110, 110), "grey43" },
  { RGB_TO_ULONG(112, 112, 112), "gray44" },
  { RGB_TO_ULONG(112, 112, 112), "grey44" },
  { RGB_TO_ULONG(115, 115, 115), "gray45" },
  { RGB_TO_ULONG(115, 115, 115), "grey45" },
  { RGB_TO_ULONG(117, 117, 117), "gray46" },
  { RGB_TO_ULONG(117, 117, 117), "grey46" },
  { RGB_TO_ULONG(120, 120, 120), "gray47" },
  { RGB_TO_ULONG(120, 120, 120), "grey47" },
  { RGB_TO_ULONG(122, 122, 122), "gray48" },
  { RGB_TO_ULONG(122, 122, 122), "grey48" },
  { RGB_TO_ULONG(125, 125, 125), "gray49" },
  { RGB_TO_ULONG(125, 125, 125), "grey49" },
  { RGB_TO_ULONG(127, 127, 127), "gray50" },
  { RGB_TO_ULONG(127, 127, 127), "grey50" },
  { RGB_TO_ULONG(130, 130, 130), "gray51" },
  { RGB_TO_ULONG(130, 130, 130), "grey51" },
  { RGB_TO_ULONG(133, 133, 133), "gray52" },
  { RGB_TO_ULONG(133, 133, 133), "grey52" },
  { RGB_TO_ULONG(135, 135, 135), "gray53" },
  { RGB_TO_ULONG(135, 135, 135), "grey53" },
  { RGB_TO_ULONG(138, 138, 138), "gray54" },
  { RGB_TO_ULONG(138, 138, 138), "grey54" },
  { RGB_TO_ULONG(140, 140, 140), "gray55" },
  { RGB_TO_ULONG(140, 140, 140), "grey55" },
  { RGB_TO_ULONG(143, 143, 143), "gray56" },
  { RGB_TO_ULONG(143, 143, 143), "grey56" },
  { RGB_TO_ULONG(145, 145, 145), "gray57" },
  { RGB_TO_ULONG(145, 145, 145), "grey57" },
  { RGB_TO_ULONG(148, 148, 148), "gray58" },
  { RGB_TO_ULONG(148, 148, 148), "grey58" },
  { RGB_TO_ULONG(150, 150, 150), "gray59" },
  { RGB_TO_ULONG(150, 150, 150), "grey59" },
  { RGB_TO_ULONG(153, 153, 153), "gray60" },
  { RGB_TO_ULONG(153, 153, 153), "grey60" },
  { RGB_TO_ULONG(156, 156, 156), "gray61" },
  { RGB_TO_ULONG(156, 156, 156), "grey61" },
  { RGB_TO_ULONG(158, 158, 158), "gray62" },
  { RGB_TO_ULONG(158, 158, 158), "grey62" },
  { RGB_TO_ULONG(161, 161, 161), "gray63" },
  { RGB_TO_ULONG(161, 161, 161), "grey63" },
  { RGB_TO_ULONG(163, 163, 163), "gray64" },
  { RGB_TO_ULONG(163, 163, 163), "grey64" },
  { RGB_TO_ULONG(166, 166, 166), "gray65" },
  { RGB_TO_ULONG(166, 166, 166), "grey65" },
  { RGB_TO_ULONG(168, 168, 168), "gray66" },
  { RGB_TO_ULONG(168, 168, 168), "grey66" },
  { RGB_TO_ULONG(171, 171, 171), "gray67" },
  { RGB_TO_ULONG(171, 171, 171), "grey67" },
  { RGB_TO_ULONG(173, 173, 173), "gray68" },
  { RGB_TO_ULONG(173, 173, 173), "grey68" },
  { RGB_TO_ULONG(176, 176, 176), "gray69" },
  { RGB_TO_ULONG(176, 176, 176), "grey69" },
  { RGB_TO_ULONG(179, 179, 179), "gray70" },
  { RGB_TO_ULONG(179, 179, 179), "grey70" },
  { RGB_TO_ULONG(181, 181, 181), "gray71" },
  { RGB_TO_ULONG(181, 181, 181), "grey71" },
  { RGB_TO_ULONG(184, 184, 184), "gray72" },
  { RGB_TO_ULONG(184, 184, 184), "grey72" },
  { RGB_TO_ULONG(186, 186, 186), "gray73" },
  { RGB_TO_ULONG(186, 186, 186), "grey73" },
  { RGB_TO_ULONG(189, 189, 189), "gray74" },
  { RGB_TO_ULONG(189, 189, 189), "grey74" },
  { RGB_TO_ULONG(191, 191, 191), "gray75" },
  { RGB_TO_ULONG(191, 191, 191), "grey75" },
  { RGB_TO_ULONG(194, 194, 194), "gray76" },
  { RGB_TO_ULONG(194, 194, 194), "grey76" },
  { RGB_TO_ULONG(196, 196, 196), "gray77" },
  { RGB_TO_ULONG(196, 196, 196), "grey77" },
  { RGB_TO_ULONG(199, 199, 199), "gray78" },
  { RGB_TO_ULONG(199, 199, 199), "grey78" },
  { RGB_TO_ULONG(201, 201, 201), "gray79" },
  { RGB_TO_ULONG(201, 201, 201), "grey79" },
  { RGB_TO_ULONG(204, 204, 204), "gray80" },
  { RGB_TO_ULONG(204, 204, 204), "grey80" },
  { RGB_TO_ULONG(207, 207, 207), "gray81" },
  { RGB_TO_ULONG(207, 207, 207), "grey81" },
  { RGB_TO_ULONG(209, 209, 209), "gray82" },
  { RGB_TO_ULONG(209, 209, 209), "grey82" },
  { RGB_TO_ULONG(212, 212, 212), "gray83" },
  { RGB_TO_ULONG(212, 212, 212), "grey83" },
  { RGB_TO_ULONG(214, 214, 214), "gray84" },
  { RGB_TO_ULONG(214, 214, 214), "grey84" },
  { RGB_TO_ULONG(217, 217, 217), "gray85" },
  { RGB_TO_ULONG(217, 217, 217), "grey85" },
  { RGB_TO_ULONG(219, 219, 219), "gray86" },
  { RGB_TO_ULONG(219, 219, 219), "grey86" },
  { RGB_TO_ULONG(222, 222, 222), "gray87" },
  { RGB_TO_ULONG(222, 222, 222), "grey87" },
  { RGB_TO_ULONG(224, 224, 224), "gray88" },
  { RGB_TO_ULONG(224, 224, 224), "grey88" },
  { RGB_TO_ULONG(227, 227, 227), "gray89" },
  { RGB_TO_ULONG(227, 227, 227), "grey89" },
  { RGB_TO_ULONG(229, 229, 229), "gray90" },
  { RGB_TO_ULONG(229, 229, 229), "grey90" },
  { RGB_TO_ULONG(232, 232, 232), "gray91" },
  { RGB_TO_ULONG(232, 232, 232), "grey91" },
  { RGB_TO_ULONG(235, 235, 235), "gray92" },
  { RGB_TO_ULONG(235, 235, 235), "grey92" },
  { RGB_TO_ULONG(237, 237, 237), "gray93" },
  { RGB_TO_ULONG(237, 237, 237), "grey93" },
  { RGB_TO_ULONG(240, 240, 240), "gray94" },
  { RGB_TO_ULONG(240, 240, 240), "grey94" },
  { RGB_TO_ULONG(242, 242, 242), "gray95" },
  { RGB_TO_ULONG(242, 242, 242), "grey95" },
  { RGB_TO_ULONG(245, 245, 245), "gray96" },
  { RGB_TO_ULONG(245, 245, 245), "grey96" },
  { RGB_TO_ULONG(247, 247, 247), "gray97" },
  { RGB_TO_ULONG(247, 247, 247), "grey97" },
  { RGB_TO_ULONG(250, 250, 250), "gray98" },
  { RGB_TO_ULONG(250, 250, 250), "grey98" },
  { RGB_TO_ULONG(252, 252, 252), "gray99" },
  { RGB_TO_ULONG(252, 252, 252), "grey99" },
  { RGB_TO_ULONG(255, 255, 255), "gray100" },
  { RGB_TO_ULONG(255, 255, 255), "grey100" },
  { RGB_TO_ULONG(169, 169, 169), "dark grey" },
  { RGB_TO_ULONG(169, 169, 169), "DarkGrey" },
  { RGB_TO_ULONG(169, 169, 169), "dark gray" },
  { RGB_TO_ULONG(169, 169, 169), "DarkGray" },
  { RGB_TO_ULONG(0  , 0  , 139), "dark blue" },
  { RGB_TO_ULONG(0  , 0  , 139), "DarkBlue" },
  { RGB_TO_ULONG(0  , 139, 139), "dark cyan" },
  { RGB_TO_ULONG(0  , 139, 139), "DarkCyan" },
  { RGB_TO_ULONG(139, 0  , 139), "dark magenta" },
  { RGB_TO_ULONG(139, 0  , 139), "DarkMagenta" },
  { RGB_TO_ULONG(139, 0  , 0  ), "dark red" },
  { RGB_TO_ULONG(139, 0  , 0  ), "DarkRed" },
  { RGB_TO_ULONG(144, 238, 144), "light green" },
  { RGB_TO_ULONG(144, 238, 144), "LightGreen" }
};

static Lisp_Object
mac_color_map_lookup (const char *colorname)
{
  Lisp_Object ret = mac_color_lookup (colorname);

  if (NILP (ret))
    for (int i = 0; i < ARRAYELTS (mac_color_map); i++)
      if (xstrcasecmp (colorname, mac_color_map[i].name) == 0)
	{
	  ret = make_fixnum (mac_color_map[i].color);
	  break;
	}

  return ret;
}

static Lisp_Object
x_to_mac_color (const char *colorname)
{
  register Lisp_Object ret = Qnil;

  block_input ();

  if (colorname[0] == '#')
    {
      /* Could be an old-style RGB Device specification.  */
      int size;

      size = strlen (colorname + 1);
      if (size == 3 || size == 6 || size == 9 || size == 12)
	{
	  char *color = alloca (size + 1);
	  unsigned long colorval;
	  int i, pos;
	  pos = 16;
	  size /= 3;
	  colorval = 0;

	  strcpy (color, colorname + 1);
	  for (i = 0; i < 3; i++)
	    {
	      char *end;
	      char t;
	      unsigned long value;

	      /* The check for 'x' in the following conditional takes into
		 account the fact that strtol allows a "0x" in front of
		 our numbers, and we don't.  */
	      if (!isxdigit(color[0]) || color[1] == 'x')
		break;
	      t = color[size];
	      color[size] = '\0';
	      errno = 0;
	      value = strtoul(color, &end, 16);
	      color[size] = t;
	      if (errno == ERANGE || end - color != size)
		break;
	      switch (size)
		{
		case 1:
		  value = value * 0x10;
		  break;
		case 2:
		  break;
		case 3:
		  value /= 0x10;
		  break;
		case 4:
		  value /= 0x100;
		  break;
		}
	      colorval |= (value << pos);
	      pos -= 8;
	      if (i == 2)
		{
		  unblock_input ();
		  return make_fixnum (colorval);
		}
	      color = end;
	    }
	}
    }
  else if (strncasecmp(colorname, "rgb:", 4) == 0)
    {
      const char *color;
      unsigned long colorval;
      int i, pos;
      pos = 16;

      colorval = 0;
      color = colorname + 4;
      for (i = 0; i < 3; i++)
	{
	  char *end;
	  unsigned long value;

	  /* The check for 'x' in the following conditional takes into
	     account the fact that strtol allows a "0x" in front of
	     our numbers, and we don't.  */
	  if (!isxdigit(color[0]) || color[1] == 'x')
	    break;
	  errno = 0;
	  value = strtoul(color, &end, 16);
	  if (errno == ERANGE)
	    break;
	  switch (end - color)
	    {
	    case 1:
	      value = value * 0x10 + value;
	      break;
	    case 2:
	      break;
	    case 3:
	      value /= 0x10;
	      break;
	    case 4:
	      value /= 0x100;
	      break;
	    default:
	      value = ULONG_MAX;
	    }
	  if (value == ULONG_MAX)
	    break;
	  colorval |= (value << pos);
	  pos -= 0x8;
	  if (i == 2)
	    {
	      if (*end != '\0')
		break;
	      unblock_input ();
	      return make_fixnum (colorval);
	    }
	  if (*end != '/')
	    break;
	  color = end + 1;
	}
    }
  else if (strncasecmp(colorname, "rgbi:", 5) == 0)
    {
      /* This is an RGB Intensity specification.  */
      const char *color;
      unsigned long colorval;
      int i, pos;
      pos = 16;

      colorval = 0;
      color = colorname + 5;
      for (i = 0; i < 3; i++)
	{
	  char *end;
	  double value;
	  unsigned long val;

	  value = strtod(color, &end);
	  if (errno == ERANGE)
	    break;
	  if (value < 0.0 || value > 1.0)
	    break;
	  val = (unsigned long)(0x100 * value);
	  /* We used 0x100 instead of 0xFF to give a continuous
             range between 0.0 and 1.0 inclusive.  The next statement
             fixes the 1.0 case.  */
	  if (val == 0x100)
	    val = 0xFF;
	  colorval |= (val << pos);
	  pos -= 0x8;
	  if (i == 2)
	    {
	      if (*end != '\0')
		break;
	      unblock_input ();
	      return make_fixnum (colorval);
	    }
	  if (*end != '/')
	    break;
	  color = end + 1;
	}
    }

  ret = mac_color_map_lookup (colorname);

  unblock_input ();
  return ret;
}

/* Gamma-correct COLOR on frame F.  */

void
gamma_correct (struct frame *f, unsigned long *color)
{
  if (f->gamma)
    {
      unsigned long red, green, blue;

      red = pow (RED_FROM_ULONG (*color) / 255.0, f->gamma) * 255.0 + 0.5;
      green = pow (GREEN_FROM_ULONG (*color) / 255.0, f->gamma) * 255.0 + 0.5;
      blue = pow (BLUE_FROM_ULONG (*color) / 255.0, f->gamma) * 255.0 + 0.5;
      *color = RGB_TO_ULONG (red, green, blue);
    }
}


/* Decide if color named COLOR is valid for the display associated
   with the selected frame; if so, return the rgb values in COLOR_DEF.
   If ALLOC, allocate a new colormap cell.  */

bool
mac_defined_color (struct frame *f, const char *color, Emacs_Color *color_def,
		   bool alloc, bool _makeIndex)
{
  register Lisp_Object tem;
  unsigned long mac_color_ref;

  tem = x_to_mac_color (color);

  if (NILP (tem))
    return false;
  else
    {
      mac_color_ref = XUFIXNUM (tem);
      if (f)
        {
          /* Apply gamma correction.  */
          gamma_correct (f, &mac_color_ref);
        }

      color_def->pixel = mac_color_ref;
      color_def->red = RED16_FROM_ULONG (mac_color_ref);
      color_def->green = GREEN16_FROM_ULONG (mac_color_ref);
      color_def->blue = BLUE16_FROM_ULONG (mac_color_ref);

      return true;
    }
}

/* Given a string ARG naming a color, compute a pixel value from it
   suitable for screen F.
   If F is not a color screen, return DEF (default) regardless of what
   ARG says.  */

static int
mac_decode_color (struct frame *f, Lisp_Object arg, int def)
{
  Emacs_Color cdef;

  CHECK_STRING (arg);

  if (strcmp (SSDATA (arg), "black") == 0)
    return BLACK_PIX_DEFAULT (f);
  else if (strcmp (SSDATA (arg), "white") == 0)
    return WHITE_PIX_DEFAULT (f);

#if 0
  if (FRAME_DISPLAY_INFO (f)->n_planes == 1)
    return def;
#endif

  if (mac_defined_color (f, SSDATA (arg), &cdef, 1, false))
    return cdef.pixel;

  /* defined_color failed; return an ultimate default.  */
  return def;
}



static Lisp_Object
mac_inhibit_double_buffering_default_value (void)
{
  return ((mac_operating_system_version.major > 10
	   || mac_operating_system_version.minor >= 14)
	  ? Qnil : Qt);
}

static bool
mac_inhibit_double_buffering_force_default_p (void)
{
  /* macOS 10.14 uses layer-backed views by default.  */
  return (mac_operating_system_version.major > 10
	  || mac_operating_system_version.minor >= 14);
}

static void
mac_set_inhibit_double_buffering (struct frame *f,
				  Lisp_Object new_value,
				  Lisp_Object old_value)
{
  /* Only effective at the frame creation time.  */
  if (FRAME_MAC_WINDOW (f))
    return;

  if (mac_inhibit_double_buffering_force_default_p ())
    new_value = mac_inhibit_double_buffering_default_value ();

  FRAME_MAC_DOUBLE_BUFFERED_P (f) = NILP (new_value);
}

static void
mac_set_undecorated (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    {
      FRAME_UNDECORATED (f) = NILP (new_value) ? false : true;

      block_input ();
      mac_update_frame_window_style (f);
      unblock_input ();
    }
}

static void
mac_set_undecorated_round (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    {
      FRAME_UNDECORATED_ROUND (f) = NILP (new_value) ? false : true;

      block_input ();
      mac_update_frame_window_style (f);
      unblock_input ();
    }
}

static void
mac_set_parent_frame (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  struct frame *p = NULL;

  if (!NILP (new_value)
      && (!FRAMEP (new_value)
	  || !FRAME_LIVE_P (p = XFRAME (new_value))
	  || !FRAME_MAC_P (p)))
    {
      store_frame_param (f, Qparent_frame, old_value);
      error ("Invalid specification of `parent-frame'");
    }

  if (p != FRAME_PARENT_FRAME (f))
    {
      fset_parent_frame (f, new_value);

      block_input ();
      mac_update_frame_window_parent (f);
      unblock_input ();
    }
}

static void
mac_set_no_focus_on_map (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    FRAME_NO_FOCUS_ON_MAP (f) = !NILP (new_value);
}

static void
mac_set_no_accept_focus (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    FRAME_NO_ACCEPT_FOCUS (f) = !NILP (new_value);
}

static void
mac_set_override_redirect (struct frame *f, Lisp_Object new_value, Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    {
      block_input ();
      mac_change_frame_window_wm_state (f, (!NILP (new_value)
					    ? WM_STATE_OVERRIDE_REDIRECT : 0),
					(NILP (new_value)
					 ? WM_STATE_OVERRIDE_REDIRECT : 0));
      unblock_input ();

      FRAME_OVERRIDE_REDIRECT (f) = !NILP (new_value);
    }
}

/* Functions called only from `gui_set_frame_parameters'
   to set individual parameters.

   If FRAME_MAC_WINDOW (f) is 0,
   the frame is being created and its window does not exist yet.
   In that case, just record the parameter's new value
   in the standard place; do not attempt to change the window.  */

static void
mac_set_foreground_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  struct mac_output *mac = f->output_data.mac;
  unsigned long fg, old_fg;

  fg = mac_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  old_fg = FRAME_FOREGROUND_PIXEL (f);
  FRAME_FOREGROUND_PIXEL (f) = fg;

  if (FRAME_MAC_WINDOW (f) != 0)
    {
      block_input ();
      mac_set_foreground (mac->normal_gc, fg);

      if (mac->cursor_pixel == old_fg)
	{
	  mac->cursor_pixel = fg;
	  mac_set_background (mac->cursor_gc, mac->cursor_pixel);
	}

      unblock_input ();

      update_face_from_frame_parameter (f, Qforeground_color, arg);

      if (FRAME_VISIBLE_P (f))
        redraw_frame (f);
    }
}

static void
mac_set_background_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  struct mac_output *mac = f->output_data.mac;
  unsigned long bg;

  bg = mac_decode_color (f, arg, WHITE_PIX_DEFAULT (f));
  FRAME_BACKGROUND_PIXEL (f) = bg;

  if (FRAME_MAC_WINDOW (f) != 0)
    {
      block_input ();
      mac_set_background (mac->normal_gc, bg);
      if (NILP (CDR_SAFE (Fassq (Qscroll_bar_background, f->param_alist))))
	mac_set_frame_window_background (f, bg);
      mac_set_foreground (mac->cursor_gc, bg);

      unblock_input ();
      update_face_from_frame_parameter (f, Qbackground_color, arg);

      if (FRAME_VISIBLE_P (f))
        redraw_frame (f);
    }
}

/* This array must stay in sync with the mouse_cursor_types array below!  */
enum mouse_cursor {
  mouse_cursor_text,
  mouse_cursor_nontext,
  mouse_cursor_hourglass,
  mouse_cursor_mode,
  mouse_cursor_hand,
  mouse_cursor_horizontal_drag,
  mouse_cursor_vertical_drag,
  mouse_cursor_left_edge,
  mouse_cursor_top_left_corner,
  mouse_cursor_top_edge,
  mouse_cursor_top_right_corner,
  mouse_cursor_right_edge,
  mouse_cursor_bottom_right_corner,
  mouse_cursor_bottom_edge,
  mouse_cursor_bottom_left_corner,
  mouse_cursor_max
};

struct mouse_cursor_types {
  /* Printable name for error messages (optional).  */
  const char *name;

  /* Lisp variable controlling the cursor shape.  */
  /* FIXME: A couple of these variables are defined in the C code but
     are not actually accessible from Lisp.  They should probably be
     made accessible or removed.  */
  Lisp_Object *shape_var_ptr;

  /* The default shape.  */
  ThemeCursor default_shape;
};

/* This array must stay in sync with enum mouse_cursor above!  */
static const struct mouse_cursor_types mouse_cursor_types[] = {
  { "text",      &Vx_pointer_shape,                kThemeIBeamCursor },
  { "nontext",   &Vx_nontext_pointer_shape,        kThemeArrowCursor },
  { "hourglass", &Vx_hourglass_pointer_shape,      kThemeWatchCursor },
  { "modeline",  &Vx_mode_pointer_shape,           kThemeArrowCursor },
  { NULL,        &Vx_sensitive_text_pointer_shape, kThemePointingHandCursor },
  { NULL,        &Vx_window_horizontal_drag_shape, kThemeResizeLeftRightCursor },
  { NULL,        &Vx_window_vertical_drag_shape,   kThemeResizeUpDownCursor },
  { NULL,        &Vx_window_left_edge_shape,
    THEME_RESIZE_EAST_WEST_CURSOR },
  { NULL,        &Vx_window_top_left_corner_shape,
    THEME_RESIZE_NORTHWEST_SOUTHEAST_CURSOR },
  { NULL,        &Vx_window_top_edge_shape,
    THEME_RESIZE_NORTH_SOUTH_CURSOR },
  { NULL,        &Vx_window_top_right_corner_shape,
    THEME_RESIZE_NORTHEAST_SOUTHWEST_CURSOR },
  { NULL,        &Vx_window_right_edge_shape,
    THEME_RESIZE_EAST_WEST_CURSOR },
  { NULL,        &Vx_window_bottom_right_corner_shape,
    THEME_RESIZE_NORTHWEST_SOUTHEAST_CURSOR },
  { NULL,        &Vx_window_bottom_edge_shape,
    THEME_RESIZE_NORTH_SOUTH_CURSOR },
  { NULL,        &Vx_window_bottom_left_corner_shape,
    THEME_RESIZE_NORTHEAST_SOUTHWEST_CURSOR },
};

static void
mac_set_mouse_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  struct mac_output *mac = f->output_data.mac;
  unsigned long pixel = mac_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  unsigned long mask_color = FRAME_BACKGROUND_PIXEL (f);
  Emacs_Color colors[2]; /* 0=foreground, 1=background */
  ThemeCursor shapes[mouse_cursor_max];
  int i;

  /* Don't let pointers be invisible.  */
  if (mask_color == pixel)
    pixel = FRAME_FOREGROUND_PIXEL (f);
  else
    {
      /* This workaround enables us to specify "black" as the mouse
	 color in Dark Mode on macOS 10.14 and later so that the mouse
	 pointer looks like in the other apps.  */
      /* See color_distance in xfaces.c.  */
      long r, g, b, r_mean;
      int color_distance;

      r = RED_FROM_ULONG (mask_color) - RED_FROM_ULONG (pixel);
      g = GREEN_FROM_ULONG (mask_color) - GREEN_FROM_ULONG (pixel);
      b = BLUE_FROM_ULONG (mask_color) - BLUE_FROM_ULONG (pixel);
      r_mean = (RED_FROM_ULONG (mask_color) + RED_FROM_ULONG (pixel)) / 2;

      color_distance = ((((512 + r_mean) * r * r) >> 8)
			+ 4 * g * g
			+ (((767 - r_mean) * b * b) >> 8));
      if (color_distance < face_near_same_color_threshold)
	mask_color = FRAME_FOREGROUND_PIXEL (f);
    }

  mac->mouse_pixel = pixel;

  for (i = 0; i < mouse_cursor_max; i++)
    {
      Lisp_Object shape_var = *mouse_cursor_types[i].shape_var_ptr;
      shapes[i]
	= (!NILP (shape_var)
	   ? check_uinteger_max (shape_var, UINT_MAX)
	   : mouse_cursor_types[i].default_shape);
    }

  block_input ();

  colors[0].pixel = mac->mouse_pixel;
  colors[1].pixel = mask_color;
  mac_query_colors (f, colors, 2);

#define INSTALL_CURSOR(FIELD, SHORT_INDEX)				\
  mac_cursor_release (mac->FIELD);					\
  mac->FIELD = mac_cursor_create (shapes[mouse_cursor_ ## SHORT_INDEX], \
				  &colors[0], &colors[1]);

  INSTALL_CURSOR (text_cursor, text);
  INSTALL_CURSOR (nontext_cursor, nontext);
  INSTALL_CURSOR (hourglass_cursor, hourglass);
  INSTALL_CURSOR (modeline_cursor, mode);
  INSTALL_CURSOR (hand_cursor, hand);
  INSTALL_CURSOR (horizontal_drag_cursor, horizontal_drag);
  INSTALL_CURSOR (vertical_drag_cursor, vertical_drag);
  INSTALL_CURSOR (left_edge_cursor, left_edge);
  INSTALL_CURSOR (top_left_corner_cursor, top_left_corner);
  INSTALL_CURSOR (top_edge_cursor, top_edge);
  INSTALL_CURSOR (top_right_corner_cursor, top_right_corner);
  INSTALL_CURSOR (right_edge_cursor, right_edge);
  INSTALL_CURSOR (bottom_right_corner_cursor, bottom_right_corner);
  INSTALL_CURSOR (bottom_edge_cursor, bottom_edge);
  INSTALL_CURSOR (bottom_left_corner_cursor, bottom_left_corner);

#undef INSTALL_CURSOR

  if (FRAME_MAC_WINDOW (f) != 0)
    FRAME_TERMINAL (f)->rif->define_frame_cursor (f, mac->text_cursor);

  unblock_input ();

  update_face_from_frame_parameter (f, Qmouse_color, arg);
}

static void
mac_set_cursor_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  unsigned long fore_pixel, pixel;
  struct mac_output *mac = f->output_data.mac;

  if (!NILP (Vx_cursor_fore_pixel))
    fore_pixel = mac_decode_color (f, Vx_cursor_fore_pixel,
				   WHITE_PIX_DEFAULT (f));
  else
    fore_pixel = FRAME_BACKGROUND_PIXEL (f);

  pixel = mac_decode_color (f, arg, BLACK_PIX_DEFAULT (f));

  /* Make sure that the cursor color differs from the background color.  */
  if (pixel == FRAME_BACKGROUND_PIXEL (f))
    {
      pixel = mac->mouse_pixel;
      if (pixel == fore_pixel)
	fore_pixel = FRAME_BACKGROUND_PIXEL (f);
    }

  mac->cursor_foreground_pixel = fore_pixel;
  mac->cursor_pixel = pixel;

  if (FRAME_MAC_WINDOW (f) != 0)
    {
      block_input ();
      /* Update frame's cursor_gc.  */
      mac_set_background (mac->cursor_gc, pixel);
      mac_set_foreground (mac->cursor_gc, fore_pixel);
      unblock_input ();

      if (FRAME_VISIBLE_P (f))
	{
	  gui_update_cursor (f, false);
	  gui_update_cursor (f, true);
	}
    }

  update_face_from_frame_parameter (f, Qcursor_color, arg);
}

/* Set the border-color of frame F to pixel value PIX.
   Note that this does not fully take effect if done before
   F has a window.  */

static void
mac_set_border_pixel (struct frame *f, int pix)
{
  f->output_data.mac->border_pixel = pix;

  if (FRAME_MAC_WINDOW (f) != 0 && f->border_width > 0)
    {
      if (FRAME_VISIBLE_P (f))
        redraw_frame (f);
    }
}

/* Set the border-color of frame F to value described by ARG.
   ARG can be a string naming a color.
   The border-color is used for the border that is drawn by the server.
   Note that this does not fully take effect if done before
   F has a window; it must be redone when the window is created.  */

static void
mac_set_border_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  int pix;

  CHECK_STRING (arg);
  pix = mac_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  mac_set_border_pixel (f, pix);
  update_face_from_frame_parameter (f, Qborder_color, arg);
}


static void
mac_set_cursor_type (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  set_frame_cursor_types (f, arg);
}


static void
mac_set_menu_bar_lines (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  /* Make sure we redisplay all windows in this frame.  */
  fset_redisplay (f);

  FRAME_MENU_BAR_LINES (f) = 0;
  FRAME_MENU_BAR_HEIGHT (f) = 0;
  /* The menu bar is always shown.  */
  FRAME_EXTERNAL_MENU_BAR (f) = 1;
  if (FRAME_MAC_P (f) && f->output_data.mac->menubar_widget == 0)
    /* Make sure next redisplay shows the menu bar.  */
    XWINDOW (FRAME_SELECTED_WINDOW (f))->update_mode_line = true;
  adjust_frame_glyphs (f);

  /* We don't use this value for controlling visibility of the menu
     bar.  But use it as a hint for window collection behavior.  */
  block_input ();
  if (FIXNUMP (value) && XFIXNUM (value) > 0)
    mac_change_frame_window_wm_state (f, 0, WM_STATE_NO_MENUBAR);
  else
    mac_change_frame_window_wm_state (f, WM_STATE_NO_MENUBAR, 0);
  unblock_input ();
}


/* Set the number of lines used for the tab bar of frame F to VALUE.
   VALUE not an integer, or < 0 means set the lines to zero.  OLDVAL
   is the old number of tab bar lines.  This function may change the
   height of all windows on frame F to match the new tab bar height.
   The frame's height may change if frame_inhibit_implied_resize was
   set accordingly.  */

static void
mac_set_tab_bar_lines (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  int olines = FRAME_TAB_BAR_LINES (f);
  int nlines;

  /* Treat tab bars like menu bars.  */
  if (FRAME_MINIBUF_ONLY_P (f))
    return;

  /* Use VALUE only if an int >= 0.  */
  if (RANGED_FIXNUMP (0, value, INT_MAX))
    nlines = XFIXNAT (value);
  else
    nlines = 0;

  if (nlines != olines && (olines == 0 || nlines == 0))
    mac_change_tab_bar_height (f, nlines * FRAME_LINE_HEIGHT (f));
}


/* Set the pixel height of the tab bar of frame F to HEIGHT.  */
void
mac_change_tab_bar_height (struct frame *f, int height)
{
  int unit = FRAME_LINE_HEIGHT (f);
  int old_height = FRAME_TAB_BAR_HEIGHT (f);

  /* This differs from the tool bar code in that the tab bar height is
     not rounded up.  Otherwise, if redisplay_tab_bar decides to grow
     the tab bar by even 1 pixel, FRAME_TAB_BAR_LINES will be changed,
     leading to the tab bar height being incorrectly set upon the next
     call to x_set_font.  (bug#59285) */
  int lines = height / unit;
  if (lines == 0 && height != 0)
    lines = 1;

  /* Make sure we redisplay all windows in this frame.  */
  fset_redisplay (f);

  /* Recalculate tab bar and frame text sizes.  */
  FRAME_TAB_BAR_HEIGHT (f) = height;
  FRAME_TAB_BAR_LINES (f) = lines;
  store_frame_param (f, Qtab_bar_lines, make_fixnum (lines));

  if (FRAME_MAC_WINDOW (f) && FRAME_TAB_BAR_HEIGHT (f) == 0)
    {
      clear_frame (f);
      clear_current_matrices (f);
    }

  if ((height < old_height) && WINDOWP (f->tab_bar_window))
    clear_glyph_matrix (XWINDOW (f->tab_bar_window)->current_matrix);

  if (!f->tab_bar_resized)
    {
      Lisp_Object fullscreen = get_frame_param (f, Qfullscreen);

      /* As long as tab_bar_resized is false, effectively try to change
	 F's native height.  */
      if (NILP (fullscreen) || EQ (fullscreen, Qfullwidth))
	adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
			   1, false, Qtab_bar_lines);
      else
	adjust_frame_size (f, -1, -1, 4, false, Qtab_bar_lines);

      f->tab_bar_resized = f->tab_bar_redisplayed;
    }
  else
    /* Any other change may leave the native size of F alone.  */
    adjust_frame_size (f, -1, -1, 3, false, Qtab_bar_lines);

  /* adjust_frame_size might not have done anything, garbage frame
     here.  */
  adjust_frame_glyphs (f);
  SET_FRAME_GARBAGED (f);
  if (FRAME_MAC_WINDOW (f))
    mac_clear_under_internal_border (f);
}


/* Set the number of lines used for the tool bar of frame F to VALUE.
   VALUE not an integer, or < 0 means set the lines to zero.  OLDVAL
   is the old number of tool bar lines.  This function changes the
   height of all windows on frame F to match the new tool bar height.
   The frame's height doesn't change.  */

static void
mac_set_tool_bar_lines (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  int nlines;

  /* Treat tool bars like menu bars.  */
  if (FRAME_MINIBUF_ONLY_P (f))
    return;

  /* Use VALUE only if an int >= 0.  */
  if (RANGED_FIXNUMP (0, value, INT_MAX))
    nlines = XFIXNAT (value);
  else
    nlines = 0;

  mac_change_tool_bar_height (f, nlines * FRAME_LINE_HEIGHT (f));
}

/* Set the pixel height of the tool bar of frame F to HEIGHT.  */
void
mac_change_tool_bar_height (struct frame *f, int height)
{
  if (!FRAME_INTERNAL_TOOL_BAR_P (f))
    {
      FRAME_TOOL_BAR_LINES (f) = 0;
      FRAME_TOOL_BAR_HEIGHT (f) = 0;
      if (height)
	{
	  FRAME_EXTERNAL_TOOL_BAR (f) = true;
	  block_input ();
	  if (FRAME_MAC_P (f)
	      && !mac_is_frame_window_toolbar_visible (f))
	    /* Make sure next redisplay shows the tool bar.  */
	    XWINDOW (FRAME_SELECTED_WINDOW (f))->update_mode_line = true;
	  unblock_input ();
	}
      else
	{
	  if (FRAME_EXTERNAL_TOOL_BAR (f))
	    free_frame_tool_bar (f);
	  FRAME_EXTERNAL_TOOL_BAR (f) = false;
	}
    }
  else
    {
      int unit = FRAME_LINE_HEIGHT (f);
      int old_height = FRAME_TOOL_BAR_HEIGHT (f);
      int lines = (height + unit - 1) / unit;
      Lisp_Object fullscreen= get_frame_param (f, Qfullscreen);

      /* Make sure we redisplay all windows in this frame.  */
      fset_redisplay (f);

      FRAME_TOOL_BAR_HEIGHT (f) = height;
      FRAME_TOOL_BAR_LINES (f) = lines;
      store_frame_param (f, Qtool_bar_lines, make_fixnum (lines));

      if (FRAME_MAC_WINDOW (f) && FRAME_TOOL_BAR_HEIGHT (f) == 0)
	{
	  clear_frame (f);
	  clear_current_matrices (f);
	}

      if ((height < old_height) && WINDOWP (f->tool_bar_window))
	clear_glyph_matrix (XWINDOW (f->tool_bar_window)->current_matrix);

      if (!f->tool_bar_resized)
	{
	  /* As long as tool_bar_resized is false, effectively try to
	     change F's native height.  */
	  if (NILP (fullscreen) || EQ (fullscreen, Qfullwidth))
	    adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
			       1, false, Qtool_bar_lines);
	  else
	    adjust_frame_size (f, -1, -1, 4, false, Qtool_bar_lines);

	  f->tool_bar_resized =  f->tool_bar_redisplayed;
	}
      else
	/* Any other change may leave the native size of F alone.  */
	adjust_frame_size (f, -1, -1, 3, false, Qtool_bar_lines);

      /* adjust_frame_size might not have done anything, garbage frame
	 here.  */
      adjust_frame_glyphs (f);
      SET_FRAME_GARBAGED (f);
      if (FRAME_MAC_WINDOW (f))
	mac_clear_under_internal_border (f);
    }
}

static void
mac_set_child_frame_border_width (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  int border;

  if (NILP (arg))
    border = -1;
  else if (RANGED_FIXNUMP (0, arg, INT_MAX))
    border = XFIXNAT (arg);
  else
    signal_error ("Invalid child frame border width", arg);

  if (border != FRAME_CHILD_FRAME_BORDER_WIDTH (f))
    {
      f->child_frame_border_width = border;

      if (FRAME_MAC_WINDOW (f))
	{
	  adjust_frame_size (f, -1, -1, 3, false, Qchild_frame_border_width);
	  mac_clear_under_internal_border (f);
	}
    }

}

static void
mac_set_internal_border_width (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  int border = check_int_nonnegative (arg);

  if (border != FRAME_INTERNAL_BORDER_WIDTH (f))
    {
      f->internal_border_width = border;

      if (FRAME_MAC_WINDOW (f) != 0)
	{
	  adjust_frame_size (f, -1, -1, 3, false, Qinternal_border_width);

	  mac_clear_under_internal_border (f);
	}
    }
}


/* Set the foreground color for scroll bars on frame F to VALUE.
   VALUE should be a string, a color name.  If it isn't a string or
   isn't a valid color name, do nothing.  OLDVAL is the old value of
   the frame parameter.  */

static void
mac_set_scroll_bar_foreground (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  if (FRAME_MAC_WINDOW (f))
    update_face_from_frame_parameter (f, Qscroll_bar_foreground, value);
}


/* Set the background color for scroll bars on frame F to VALUE VALUE
   should be a string, a color name.  If it isn't a string or isn't a
   valid color name, do nothing.  OLDVAL is the old value of the frame
   parameter.  */

static void
mac_set_scroll_bar_background (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  unsigned long pixel;

  if (STRINGP (value))
    pixel = mac_decode_color (f, value, FRAME_BACKGROUND_PIXEL (f));
  else
    pixel = FRAME_BACKGROUND_PIXEL (f);

  if (FRAME_MAC_WINDOW (f))
    {
      update_face_from_frame_parameter (f, Qscroll_bar_background, value);
      mac_set_frame_window_background (f, pixel);
    }
}


/* Set the Mac window title to NAME for frame F.  */

static void
mac_set_name_internal (struct frame *f, Lisp_Object name)
{
  if (FRAME_MAC_WINDOW (f))
    {
      CFStringRef window_title;

      block_input ();

      window_title = cfstring_create_with_string (name);
      if (window_title)
	{
	  mac_set_frame_window_title (f, window_title);
	  CFRelease (window_title);
	}

      unblock_input ();
    }
}

/* Change the name of frame F to NAME.  If NAME is nil, set F's name to
       mac_id_name.

   If EXPLICIT is non-zero, that indicates that lisp code is setting the
       name; if NAME is a string, set F's name to NAME and set
       F->explicit_name; if NAME is Qnil, then clear F->explicit_name.

   If EXPLICIT is false, that indicates that Emacs redisplay code is
       suggesting a new name, which lisp code should override; if
       F->explicit_name is set, ignore the new name; otherwise, set it.  */

static void
mac_set_name (struct frame *f, Lisp_Object name, bool explicit)
{
  /* Make sure that requests from lisp code override requests from
     Emacs redisplay code.  */
  if (explicit)
    {
      /* If we're switching from explicit to implicit, we had better
	 update the mode lines and thereby update the title.  */
      if (f->explicit_name && NILP (name))
	update_mode_lines = 37;

      f->explicit_name = ! NILP (name);
    }
  else if (f->explicit_name)
    return;

  /* If NAME is nil, set the name to the mac_id_name.  */
  if (NILP (name))
    {
      /* Check for no change needed in this very common case
	 before we do any consing.  */
      if (!strcmp (FRAME_DISPLAY_INFO (f)->mac_id_name,
		   SSDATA (f->name)))
	return;
      name = build_string (FRAME_DISPLAY_INFO (f)->mac_id_name);
    }
  else
    CHECK_STRING (name);

  /* Don't change the name if it's already NAME.  */
  if (! NILP (Fstring_equal (name, f->name)))
    return;

  fset_name (f, name);

  /* For setting the frame title, the title parameter should override
     the name parameter.  */
  if (! NILP (f->title))
    name = f->title;

  mac_set_name_internal (f, name);
}

/* This function should be called when the user's lisp code has
   specified a name for the frame; the name will override any set by the
   redisplay code.  */
static void
mac_explicitly_set_name (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  mac_set_name (f, arg, true);
}

/* This function should be called by Emacs redisplay code to set the
   name; names set this way will never override names set by the user's
   lisp code.  */
void
mac_implicitly_set_name (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  mac_set_name (f, arg, false);
}

/* Change the title of frame F to NAME.
   If NAME is nil, use the frame name as the title.  */

static void
mac_set_title (struct frame *f, Lisp_Object name, Lisp_Object old_name)
{
  /* Don't change the title if it's already NAME.  */
  if (EQ (name, f->title))
    return;

  update_mode_lines = 38;

  fset_title (f, name);

  if (NILP (name))
    name = f->name;
  else
    CHECK_STRING (name);

  mac_set_name_internal (f, name);
}

void
mac_set_scroll_bar_default_width (struct frame *f)
{
  int minw, unit = FRAME_COLUMN_WIDTH (f);

  block_input ();
  minw = mac_get_default_scroll_bar_width (f);
  unblock_input ();

  FRAME_CONFIG_SCROLL_BAR_COLS (f) = (minw + unit - 1) / unit;
  FRAME_CONFIG_SCROLL_BAR_WIDTH (f) = minw;
}

void
mac_set_scroll_bar_default_height (struct frame *f)
{
  int minh, unit = FRAME_LINE_HEIGHT (f);

  block_input ();
  minh = mac_get_default_scroll_bar_height (f);
  unblock_input ();

  FRAME_CONFIG_SCROLL_BAR_LINES (f) = (minh + unit - 1) / unit;
  FRAME_CONFIG_SCROLL_BAR_HEIGHT (f) = minh;
}

/* Record in frame F the specified or default value according to ALIST
   of the parameter named PROP (a Lisp symbol).  If no value is
   specified for PROP, look for an X default for XPROP on the frame
   named NAME.  If that is not found either, use the value DEFLT.  */

static Lisp_Object
mac_default_scroll_bar_color_parameter (struct frame *f,
					Lisp_Object alist, Lisp_Object prop,
					const char *xprop, const char *xclass,
					bool foreground_p)
{
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  Lisp_Object tem;

  tem = gui_display_get_arg (dpyinfo, alist, prop, xprop, xclass,
                             RES_TYPE_STRING);
  if (BASE_EQ (tem, Qunbound))
    tem = Qnil;
  AUTO_FRAME_ARG (arg, prop, tem);
  gui_set_frame_parameters (f, arg);
  return tem;
}

static void
mac_set_font (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  gui_set_font (f, arg, oldval);
  {
    Lisp_Object focus_frame = FRAME_TERMINAL (f)->get_focus_frame (f);

    if (f == (FRAMEP (focus_frame) ? XFRAME (focus_frame) : SELECTED_FRAME ()))
      {
	block_input ();
	mac_set_font_info_for_selection (f, DEFAULT_FACE_ID, 0, -1, Qnil);
	unblock_input ();
      }
  }
}

static bool
mac_is_url_suitable_for_proxy (CFURLRef url)
{
  if (!CFURLResourceIsReachable (url, NULL))
    return false;

  /* Work around Mach port leaks in showing proxy icons for files on
     iCloud Drive on macOS 10.15 - 10.15.3 (Bug#38618).  */
  if (mac_operating_system_version.major == 10
      && mac_operating_system_version.minor == 15
      && mac_operating_system_version.patch < 4)
    {
      CFBooleanRef isUbiquitousItem = NULL;
      bool ubiquitous_item_p =
	(CFURLCopyResourcePropertyForKey (url,
					  kCFURLIsUbiquitousItemKey,
					  &isUbiquitousItem, NULL)
	 && isUbiquitousItem
	 && CFBooleanGetValue (isUbiquitousItem));
      if (isUbiquitousItem)
	CFRelease (isUbiquitousItem);

      return !ubiquitous_item_p;
    }

  return true;
}

void
mac_update_title_bar (struct frame *f, bool save_match_data)
{
  struct window *w;
  struct buffer *b;
  Lisp_Object file_name;
  bool modified_p;

  if (!FRAME_MAC_P (f))
    return;

  w = XWINDOW (FRAME_SELECTED_WINDOW (f));
  b = XBUFFER (w->contents);
  file_name = BVAR (b, filename);
  if (!STRINGP (file_name))
    file_name = Qnil;
  modified_p = (BUF_SAVE_MODIFF (b) < BUF_MODIFF (b));
  if (!EQ (file_name, f->mac_file_name)
      /* Minibuffer modification status shown in the close button is
	 confusing.  */
      || (!MINI_WINDOW_P (w)
	  && (modified_p != w->last_had_star)))
    {
      CFURLRef url = NULL;

      block_input ();
      mac_set_frame_window_modified (f, !MINI_WINDOW_P (w) && modified_p);
      if (STRINGP (file_name))
	{
	  char *name = SSDATA (ENCODE_FILE (remove_slash_colon (file_name)));

	  url = CFURLCreateFromFileSystemRepresentation (NULL, (UInt8 *) name,
							 strlen (name), false);
	  if (url && !mac_is_url_suitable_for_proxy (url))
	    {
	      CFRelease (url);
	      url = NULL;
	    }
	}
      mac_set_frame_window_proxy (f, url);
      if (url)
	CFRelease (url);
      unblock_input ();
      fset_mac_file_name (f, file_name);
    }
}


/* Subroutines of creating a frame.  */

/* Retrieve the string resource specified by NAME with CLASS from
   database RDB.

   The return value points to the contents of a Lisp string.  So it
   will not be valid after the next GC where string compaction will
   occur.  */

const char *
mac_get_string_resource (void *v_rdb, const char *name, const char *class)
{
  XrmDatabase *rdb = v_rdb;

  if (inhibit_x_resources)
    /* --quick was passed, so this is a no-op.  */
    return NULL;
  else
    {
      Lisp_Object value = xrm_get_resource (*rdb, name, class);

      if (STRINGP (value))
	return SSDATA (value);
      else
	return NULL;
    }
}


/* Create and set up the Mac window for frame F.  */

static void
mac_window (struct frame *f)
{
  block_input ();

  mac_create_frame_window (f);

  if (FRAME_MAC_WINDOW (f))
    mac_set_frame_window_background (f, FRAME_BACKGROUND_PIXEL (f));

  /* At the moment, the size of the tool bar is not yet known.  We
     record the gravity value of the newly created window and use it
     to adjust the position of the window (especially for a negative
     specification of its vertical position) when the tool bar is
     first redisplayed.  */
  if (FRAME_EXTERNAL_TOOL_BAR (f))
    f->output_data.mac->toolbar_win_gravity = f->win_gravity;

  validate_x_resource_name ();

  /* mac_set_name normally ignores requests to set the name if the
     requested name is the same as the current name.  This is the one
     place where that assumption isn't correct; f->name is set, but
     the server hasn't been told.  */
  {
    Lisp_Object name;
    bool explicit = f->explicit_name;

    f->explicit_name = false;
    name = f->name;
    fset_name (f, Qnil);
    mac_set_name (f, name, explicit);
  }

  f->output_data.mac->current_cursor = f->output_data.mac->text_cursor;

  unblock_input ();

  if (FRAME_MAC_WINDOW (f) == 0)
    error ("Unable to create window");
}

/* Make the GCs needed for this window, setting the background.  */

static void
mac_make_gc (struct frame *f)
{
  XGCValues gc_values;

  block_input ();

  /* Create the GCs of this frame.
     Note that many default values are used.  */

  gc_values.foreground = FRAME_FOREGROUND_PIXEL (f);
  gc_values.background = FRAME_BACKGROUND_PIXEL (f);
  f->output_data.mac->normal_gc = mac_create_gc (GCForeground | GCBackground,
						 &gc_values);

  /* Cursor has cursor-color background, background-color foreground.  */
  gc_values.foreground = FRAME_BACKGROUND_PIXEL (f);
  gc_values.background = f->output_data.mac->cursor_pixel;
  f->output_data.mac->cursor_gc = mac_create_gc (GCForeground | GCBackground,
						 &gc_values);

  unblock_input ();
}


/* Free what was allocated in mac_make_gc.  */

void
mac_free_gcs (struct frame *f)
{
  block_input ();

  if (f->output_data.mac->normal_gc)
    {
      mac_free_gc (f->output_data.mac->normal_gc);
      f->output_data.mac->normal_gc = 0;
    }

  if (f->output_data.mac->cursor_gc)
    {
      mac_free_gc (f->output_data.mac->cursor_gc);
      f->output_data.mac->cursor_gc = 0;
    }

  if (f->output_data.mac->white_relief.gc)
    {
      mac_free_gc (f->output_data.mac->white_relief.gc);
      f->output_data.mac->white_relief.gc = 0;
    }

  if (f->output_data.mac->black_relief.gc)
    {
      mac_free_gc (f->output_data.mac->black_relief.gc);
      f->output_data.mac->black_relief.gc = 0;
    }

  unblock_input ();
}


/* Handler for signals raised during x_create_frame and
   Fx_create_tip_frame.  FRAME is the frame which is partially
   constructed.  */

static Lisp_Object
unwind_create_frame (Lisp_Object frame)
{
  struct frame *f = XFRAME (frame);

  /* If frame is already dead, nothing to do.  This can happen if the
     display is disconnected after the frame has become official, but
     before Fx_create_frame removes the unwind protect.  */
  if (!FRAME_LIVE_P (f))
    return Qnil;

  /* If frame is ``official'', nothing to do.  */
  if (NILP (Fmemq (frame, Vframe_list)))
    {
#if defined GLYPH_DEBUG && defined ENABLE_CHECKING
      struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
#endif

      /* If the frame's image cache refcount is still the same as our
	 private shadow variable, it means we are unwinding a frame
	 for which we didn't yet call init_frame_faces, where the
	 refcount is incremented.  Therefore, we increment it here, so
	 that free_frame_faces, called in mac_free_frame_resources
	 below, will not mistakenly decrement the counter that was not
	 incremented yet to account for this new frame.  */
      if (FRAME_IMAGE_CACHE (f) != NULL
	  && FRAME_IMAGE_CACHE (f)->refcount == image_cache_refcount)
	FRAME_IMAGE_CACHE (f)->refcount++;

      mac_free_frame_resources (f);
      free_glyphs (f);

#if defined GLYPH_DEBUG && defined ENABLE_CHECKING
      /* Check that reference counts are indeed correct.  */
      eassert (dpyinfo->reference_count == dpyinfo_refcount);
      eassert (dpyinfo->terminal->image_cache->refcount == image_cache_refcount);
#endif
      return Qt;
    }

  return Qnil;
}

static void
do_unwind_create_frame (Lisp_Object frame)
{
  unwind_create_frame (frame);
}

static void
mac_default_font_parameter (struct frame *f, Lisp_Object parms)
{
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  Lisp_Object font_param = gui_display_get_arg (dpyinfo, parms, Qfont, NULL, NULL,
                                                RES_TYPE_STRING);
  Lisp_Object font;
  if (BASE_EQ (font_param, Qunbound))
    font_param = Qnil;
  font = (!NILP (font_param)
	  ? font_param
	  : gui_display_get_arg (dpyinfo, parms,
				 Qfont, "font", "Font",
				 RES_TYPE_STRING));

  if (! STRINGP (font))
    {
      const char *names[]
	= { "-*-monaco-medium-r-normal--12-*-*-*-*-*-iso10646-1",
	    "-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1",
	    "-misc-fixed-medium-r-normal-*-*-140-*-*-c-*-iso8859-1",
	    "-*-*-medium-r-normal-*-*-140-*-*-c-*-iso8859-1",
	    /* This was formerly the first thing tried, but it finds
	       too many fonts and takes too long.  */
	    "-*-*-medium-r-*-*-*-*-*-*-c-*-iso8859-1",
	    /* If those didn't work, look for something which will
	       at least work.  */
	    "-*-fixed-*-*-*-*-*-140-*-*-c-*-iso8859-1",
	    "fixed",
	    NULL };
      int i;

      for (i = 0; names[i]; i++)
	{
	  font = font_open_by_name (f, build_unibyte_string (names[i]));
	  if (! NILP (font))
	    break;
	}
      if (NILP (font))
	error ("No suitable font was found");
    }

  /* This call will make X resources override any system font setting.  */
  gui_default_parameter (f, parms, Qfont, font, "font", "Font", RES_TYPE_STRING);
}


DEFUN ("x-create-frame", Fx_create_frame, Sx_create_frame,
       1, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object parms)
{
  struct frame *f;
  Lisp_Object frame, tem;
  Lisp_Object name;
  bool minibuffer_only = false;
  bool undecorated = false, undecorated_round = false, override_redirect = false;
  long window_prompting = 0;
  specpdl_ref count = SPECPDL_INDEX ();
  Lisp_Object display;
  struct mac_display_info *dpyinfo = NULL;
  Lisp_Object parent, parent_frame;
  struct kboard *kb;

  parms = Fcopy_alist (parms);

  /* Use this general default value to start with
     until we know if this frame has a specified name.  */
  Vx_resource_name = Vinvocation_name;

  display = gui_display_get_arg (dpyinfo, parms, Qterminal, 0, 0,
                                 RES_TYPE_NUMBER);
  if (BASE_EQ (display, Qunbound))
    display = gui_display_get_arg (dpyinfo, parms, Qdisplay, 0, 0,
                                   RES_TYPE_STRING);
  if (BASE_EQ (display, Qunbound))
    display = Qnil;
  dpyinfo = check_x_display_info (display);
  kb = dpyinfo->terminal->kboard;

  if (!dpyinfo->terminal->name)
    error ("Terminal is not live, can't create new frames on it");

  name = gui_display_get_arg (dpyinfo, parms, Qname, "name", "Name",
                              RES_TYPE_STRING);
  if (!STRINGP (name)
      && ! BASE_EQ (name, Qunbound)
      && ! NILP (name))
    error ("Invalid frame name--not a string or nil");

  if (STRINGP (name))
    Vx_resource_name = name;

  /* See if parent window is specified.  */
  parent = gui_display_get_arg (dpyinfo, parms, Qparent_id, NULL, NULL,
                                RES_TYPE_NUMBER);
  if (BASE_EQ (parent, Qunbound))
    parent = Qnil;
  if (! NILP (parent))
    CHECK_FIXNUM (parent);

  frame = Qnil;
  tem = gui_display_get_arg (dpyinfo,
                             parms, Qminibuffer, "minibuffer", "Minibuffer",
                             RES_TYPE_SYMBOL);
  if (EQ (tem, Qnone) || NILP (tem))
    f = make_frame_without_minibuffer (Qnil, kb, display);
  else if (EQ (tem, Qonly))
    {
      f = make_minibuffer_frame ();
      minibuffer_only = true;
    }
  else if (WINDOWP (tem))
    f = make_frame_without_minibuffer (tem, kb, display);
  else
    f = make_frame (true);

  parent_frame = gui_display_get_arg (dpyinfo,
                                      parms,
                                      Qparent_frame,
                                      NULL,
                                      NULL,
                                      RES_TYPE_SYMBOL);
  /* Accept parent-frame iff parent-id was not specified.  */
  if (!NILP (parent)
      || BASE_EQ (parent_frame, Qunbound)
      || NILP (parent_frame)
      || !FRAMEP (parent_frame)
      || !FRAME_LIVE_P (XFRAME (parent_frame))
      || !FRAME_MAC_P (XFRAME (parent_frame)))
    parent_frame = Qnil;

  fset_parent_frame (f, parent_frame);
  store_frame_param (f, Qparent_frame, parent_frame);

  if (!NILP (tem = (gui_display_get_arg (dpyinfo,
                                         parms,
                                         Qundecorated,
                                         NULL,
                                         NULL,
                                         RES_TYPE_BOOLEAN)))
      && !(BASE_EQ (tem, Qunbound)))
    undecorated = true;

  FRAME_UNDECORATED (f) = undecorated;
  store_frame_param (f, Qundecorated, undecorated ? Qt : Qnil);

  if (!NILP (tem = (gui_display_get_arg (dpyinfo,
                                         parms,
                                         Qundecorated_round,
                                         NULL,
                                         NULL,
                                         RES_TYPE_BOOLEAN)))
      && !(BASE_EQ (tem, Qunbound)))
    undecorated_round = true;

  FRAME_UNDECORATED_ROUND (f) = undecorated_round;
  store_frame_param (f, Qundecorated_round, undecorated_round ? Qt : Qnil);

  if (!NILP (tem = (gui_display_get_arg (dpyinfo,
                                         parms,
                                         Qoverride_redirect,
                                         NULL,
                                         NULL,
                                         RES_TYPE_BOOLEAN)))
      && !(BASE_EQ (tem, Qunbound)))
    override_redirect = true;

  FRAME_OVERRIDE_REDIRECT (f) = override_redirect;
  store_frame_param (f, Qoverride_redirect, override_redirect ? Qt : Qnil);

  XSETFRAME (frame, f);

  f->terminal = dpyinfo->terminal;

  f->output_method = output_mac;
  f->output_data.mac = xzalloc (sizeof *f->output_data.mac);
  FRAME_FONTSET (f) = -1;
  f->output_data.mac->white_relief.pixel = -1;
  f->output_data.mac->black_relief.pixel = -1;
  FRAME_INTERNAL_TOOL_BAR_P (f) = undecorated || undecorated_round || !NILP (parent_frame);

  fset_icon_name (f, gui_display_get_arg (dpyinfo,
                                          parms,
                                          Qicon_name,
                                          "iconName",
                                          "Title",
                                          RES_TYPE_STRING));
  if (! STRINGP (f->icon_name))
    fset_icon_name (f, Qnil);

/*   FRAME_DISPLAY_INFO (f) = dpyinfo; */

  /* With FRAME_DISPLAY_INFO set up, this unwind-protect is safe.  */
  record_unwind_protect (do_unwind_create_frame, frame);

  /* Specify the parent under which to make this window.  */

  if (!NILP (parent))
    {
      f->output_data.mac->parent_desc = (Window) XFIXNAT (parent);
      f->output_data.mac->explicit_parent = true;
    }
  else
    {
      f->output_data.mac->parent_desc = FRAME_DISPLAY_INFO (f)->root_window;
      f->output_data.mac->explicit_parent = false;
    }

  /* Set the name; the functions to which we pass f expect the name to
     be set.  */
  if (BASE_EQ (name, Qunbound) || NILP (name))
    {
      fset_name (f, build_string (dpyinfo->mac_id_name));
      f->explicit_name = false;
    }
  else
    {
      fset_name (f, name);
      f->explicit_name = true;
      /* Use the frame's title when getting resources for this frame.  */
      specbind (Qx_resource_name, name);
    }

  mac_register_font_driver (f);

  image_cache_refcount =
    FRAME_IMAGE_CACHE (f) ? FRAME_IMAGE_CACHE (f)->refcount : 0;
#ifdef GLYPH_DEBUG
  dpyinfo_refcount = dpyinfo->reference_count;
#endif /* GLYPH_DEBUG */

  gui_default_parameter (f, parms, Qfont_backend, Qnil,
                         "fontBackend", "FontBackend", RES_TYPE_STRING);

  /* Extract the window parameters from the supplied values
     that are needed to determine window geometry.  */
  mac_default_font_parameter (f, parms);
  if (!FRAME_FONT (f))
    {
      delete_frame (frame, Qnoelisp);
      error ("Invalid frame font");
    }

  gui_default_parameter (f, parms, Qborder_width, make_fixnum (0),
			 "borderWidth", "BorderWidth", RES_TYPE_NUMBER);
  /* This defaults to 2 in order to match xterm.  We recognize either
     internalBorderWidth or internalBorder (which is what xterm calls
     it).  */
  if (NILP (Fassq (Qinternal_border_width, parms)))
    {
      Lisp_Object value;

      value = gui_display_get_arg (dpyinfo, parms, Qinternal_border_width,
                                   "internalBorder", "InternalBorder",
                                   RES_TYPE_NUMBER);
      if (! BASE_EQ (value, Qunbound))
	parms = Fcons (Fcons (Qinternal_border_width, value),
		       parms);
    }

  /* Default internalBorderWidth to 0 on Windows to match other programs.  */
  gui_default_parameter (f, parms, Qinternal_border_width, make_fixnum (0),
                         "internalBorderWidth", "InternalBorderWidth",
                         RES_TYPE_NUMBER);

  /* Same for child frames.  */
  if (NILP (Fassq (Qchild_frame_border_width, parms)))
    {
      Lisp_Object value;

      value = gui_display_get_arg (dpyinfo, parms, Qchild_frame_border_width,
                                   "childFrameBorder", "ChildFrameBorder",
                                   RES_TYPE_NUMBER);
      if (! BASE_EQ (value, Qunbound))
	parms = Fcons (Fcons (Qchild_frame_border_width, value),
		       parms);
    }

  gui_default_parameter (f, parms, Qchild_frame_border_width, Qnil,
			 "childFrameBorderWidth", "ChildFrameBorderWidth",
			 RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_divider_width, make_fixnum (0),
                         NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qbottom_divider_width, make_fixnum (0),
                         NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qvertical_scroll_bars, Qright,
                         "verticalScrollBars", "ScrollBars",
                         RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qhorizontal_scroll_bars, Qnil,
                         "horizontalScrollBars", "ScrollBars",
                         RES_TYPE_SYMBOL);
  /* Also do the stuff which must be set before the window exists.  */
  gui_default_parameter (f, parms, Qforeground_color,
			 build_string ("mac:textColor"),
                         "foreground", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qbackground_color,
			 build_string ("mac:textBackgroundColor"),
                         "background", "Background", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qmouse_color, build_string ("black"),
                         "pointerColor", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qborder_color, build_string ("black"),
                         "borderColor", "BorderColor", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qscreen_gamma, Qnil,
                         "screenGamma", "ScreenGamma", RES_TYPE_FLOAT);
  gui_default_parameter (f, parms, Qline_spacing, Qnil,
                         "lineSpacing", "LineSpacing", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qleft_fringe, Qnil,
                         "leftFringe", "LeftFringe", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_fringe, Qnil,
                         "rightFringe", "RightFringe", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qno_special_glyphs, Qnil,
                         NULL, NULL, RES_TYPE_BOOLEAN);
  /* Process alpha here (Bug#16619).  */
  gui_default_parameter (f, parms, Qalpha, Qnil,
                         "alpha", "Alpha", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qalpha_background, Qnil,
                         "alphaBackground", "AlphaBackground", RES_TYPE_NUMBER);

  mac_default_scroll_bar_color_parameter (f, parms, Qscroll_bar_foreground,
					  "scrollBarForeground",
					  "ScrollBarForeground", true);
  mac_default_scroll_bar_color_parameter (f, parms, Qscroll_bar_background,
					  "scrollBarBackground",
					  "ScrollBarBackground", false);

  /* Init faces before gui_default_parameter is called for the
     scroll-bar-width parameter because otherwise we end up in
     init_iterator with a null face cache, which should not happen.  */
  init_frame_faces (f);

  tem = gui_display_get_arg (dpyinfo, parms, Qmin_width, NULL, NULL,
                             RES_TYPE_NUMBER);
  if (FIXNUMP (tem))
    store_frame_param (f, Qmin_width, tem);
  tem = gui_display_get_arg (dpyinfo, parms, Qmin_height, NULL, NULL,
                             RES_TYPE_NUMBER);
  if (FIXNUMP (tem))
    store_frame_param (f, Qmin_height, tem);

  adjust_frame_size (f, FRAME_COLS (f) * FRAME_COLUMN_WIDTH (f),
		     FRAME_LINES (f) * FRAME_LINE_HEIGHT (f), 5, true,
		     Qx_create_frame_1);

  /* Set the menu-bar-lines and tool-bar-lines parameters.  We don't
     look up the X resources controlling the menu-bar and tool-bar
     here; they are processed specially at startup, and reflected in
     the values of the mode variables.  */

  gui_default_parameter (f, parms, Qtab_bar_lines,
                         NILP (Vtab_bar_mode)
                         ? make_fixnum (0) : make_fixnum (1),
                         NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qtool_bar_lines,
                         NILP (Vtool_bar_mode)
                         ? make_fixnum (0) : make_fixnum (1),
                         NULL, NULL, RES_TYPE_NUMBER);

  gui_default_parameter (f, parms, Qbuffer_predicate, Qnil,
                         "bufferPredicate", "BufferPredicate",
                         RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qtitle, Qnil,
                         "title", "Title", RES_TYPE_STRING);
#if 0
  gui_default_parameter (f, parms, Qtool_bar_position,
                         FRAME_TOOL_BAR_POSITION (f), 0, 0, RES_TYPE_SYMBOL);
#endif
  gui_default_parameter (f, parms, Qinhibit_double_buffering,
			 mac_inhibit_double_buffering_default_value (),
                         "inhibitDoubleBuffering", "InhibitDoubleBuffering",
                         RES_TYPE_BOOLEAN);

  /* Compute the size of the window.  */
  window_prompting = gui_figure_window_size (f, parms, true, true);

  tem = gui_display_get_arg (dpyinfo, parms, Qunsplittable, 0, 0,
                             RES_TYPE_BOOLEAN);
  f->no_split = minibuffer_only || EQ (tem, Qt);

  mac_window (f);

  mac_make_gc (f);

  /* Now consider the frame official.  */
  f->terminal->reference_count++;
  FRAME_DISPLAY_INFO (f)->reference_count++;
  Vframe_list = Fcons (frame, Vframe_list);

  /* We need to do this after creating the window, so that the window
     collection behavior can be changed according to this value.  On
     Mac, the menu bar is always external.  So x_figure_window_size
     above is not affected.  */
  gui_default_parameter (f, parms, Qmenu_bar_lines,
                         NILP (Vmenu_bar_mode)
                         ? make_fixnum (0) : make_fixnum (1),
                         NULL, NULL, RES_TYPE_NUMBER);

  /* We need to do this after creating the window, so that the
     icon-creation functions can say whose icon they're describing.  */
  gui_default_parameter (f, parms, Qicon_type, Qnil,
                         "bitmapIcon", "BitmapIcon", RES_TYPE_BOOLEAN);

  gui_default_parameter (f, parms, Qauto_raise, Qnil,
                         "autoRaise", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  gui_default_parameter (f, parms, Qauto_lower, Qnil,
                         "autoLower", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  gui_default_parameter (f, parms, Qcursor_type, Qbox,
                         "cursorType", "CursorType", RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qscroll_bar_width, Qnil,
                         "scrollBarWidth", "ScrollBarWidth",
                         RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qscroll_bar_height, Qnil,
                         "scrollBarHeight", "ScrollBarHeight",
                         RES_TYPE_NUMBER);

  if (!NILP (parent_frame))
    {
      block_input ();
      mac_update_frame_window_parent (f);
      unblock_input ();
    }

  gui_default_parameter (f, parms, Qno_focus_on_map, Qnil,
                         NULL, NULL, RES_TYPE_BOOLEAN);
  gui_default_parameter (f, parms, Qno_accept_focus, Qnil,
                         NULL, NULL, RES_TYPE_BOOLEAN);

  /* Consider frame official, now.  */
  f->can_set_window_size = true;

  /* Tell the server what size and position, etc, we want, and how
     badly we want them.  This should be done after we have the menu
     bar so that its size can be taken into account.  */
  block_input ();
  mac_wm_set_size_hint (f, window_prompting, false);
  unblock_input ();

  adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
		     0, true, Qx_create_frame_2);

  /* Process fullscreen parameter here in the hope that normalizing a
     fullheight/fullwidth frame will produce the size set by the last
     adjust_frame_size call.  */
  gui_default_parameter (f, parms, Qfullscreen, Qnil,
                         "fullscreen", "Fullscreen", RES_TYPE_SYMBOL);

  /* Make the window appear on the frame and enable display, unless
     the caller says not to.  However, with explicit parent, Emacs
     cannot control visibility, so don't try.  */
  if (! f->output_data.mac->explicit_parent)
    {
      /* When called from `x-create-frame-with-faces' visibility is
	 always explicitly nil.  */
      Lisp_Object visibility
	= gui_display_get_arg (dpyinfo, parms, Qvisibility, 0, 0,
                               RES_TYPE_SYMBOL);

      Lisp_Object height
	= gui_display_get_arg (dpyinfo, parms, Qheight, 0, 0, RES_TYPE_NUMBER);
      Lisp_Object width
	= gui_display_get_arg (dpyinfo, parms, Qwidth, 0, 0, RES_TYPE_NUMBER);

      if (EQ (visibility, Qicon))
	{
	  f->was_invisible = true;
	  mac_iconify_frame (f);
	}
      else
	{
	  if (BASE_EQ (visibility, Qunbound))
	    visibility = Qt;

	  if (!NILP (visibility))
	    mac_make_frame_visible (f);
	  else
	    f->was_invisible = true;
	}

      /* Leave f->was_invisible true only if height or width were
	 specified too.  This takes effect only when we are not called
	 from `x-create-frame-with-faces' (see above comment).  */
      f->was_invisible
	= (f->was_invisible
	   && (!BASE_EQ (height, Qunbound) || !BASE_EQ (width, Qunbound)));

      store_frame_param (f, Qvisibility, visibility);
    }

  /* Works iff frame has been already mapped.  */
  gui_default_parameter (f, parms, Qskip_taskbar, Qnil,
                         NULL, NULL, RES_TYPE_BOOLEAN);
  /* The `z-group' parameter works only for visible frames.  */
  gui_default_parameter (f, parms, Qz_group, Qnil,
                         NULL, NULL, RES_TYPE_SYMBOL);

  /* Initialize `default-minibuffer-frame' in case this is the first
     frame on this terminal.  */
  if (FRAME_HAS_MINIBUF_P (f)
      && (!FRAMEP (KVAR (kb, Vdefault_minibuffer_frame))
          || !FRAME_LIVE_P (XFRAME (KVAR (kb, Vdefault_minibuffer_frame)))))
    kset_default_minibuffer_frame (kb, frame);

  /* All remaining specified parameters, which have not been "used" by
     gui_display_get_arg and friends, now go in the misc. alist of the
     frame.  */
  for (tem = parms; CONSP (tem); tem = XCDR (tem))
    if (CONSP (XCAR (tem)) && !NILP (XCAR (XCAR (tem))))
      fset_param_alist (f, Fcons (XCAR (tem), f->param_alist));

  /* Make sure windows on this frame appear in calls to next-window
     and similar functions.  */
  Vwindow_list = Qnil;

  return unbind_to (count, frame);
}


DEFUN ("mac-color-list-alist", Fmac_color_list_alist, Smac_color_list_alist,
       0, 0, 0,
  doc: /* Return the available combinations of color list names and color names.
The value is an alist of COLOR-LIST-NAMEs vs lists of COLOR-NAMEs.
Using these names, a color can be specified as \"mac:COLOR-NAME\" or
\"mac:COLOR-LIST-NAME:COLOR-NAME\".  The former form is a shorthand
for \"mac:System:COLOR-NAME\".

Some combinations may represent image patterns rather than colors.
For such cases, `(color-values \"mac:COLOR-LIST-NAME:COLOR-NAME\")'
will return nil.  */)
  (void)
{
  Lisp_Object result;

  check_window_system (NULL);

  block_input ();
  result = mac_color_list_alist ();
  unblock_input ();

  return result;
}

DEFUN ("xw-color-defined-p", Fxw_color_defined_p, Sxw_color_defined_p, 1, 2, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  Emacs_Color foo;
  struct frame *f = decode_window_system_frame (frame);

  CHECK_STRING (color);

  if (mac_defined_color (f, SSDATA (color), &foo, false, false))
    return Qt;
  else
    return Qnil;
}

DEFUN ("xw-color-values", Fxw_color_values, Sxw_color_values, 1, 2, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  Emacs_Color foo;
  struct frame *f = decode_window_system_frame (frame);

  CHECK_STRING (color);

  if (mac_defined_color (f, SSDATA (color), &foo, false, false))
    return list3i (foo.red, foo.green, foo.blue);
  else
    return Qnil;
}

DEFUN ("xw-display-color-p", Fxw_display_color_p, Sxw_display_color_p, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  if (!dpyinfo->color_p)
    return Qnil;

  return Qt;
}

DEFUN ("x-display-grayscale-p", Fx_display_grayscale_p, Sx_display_grayscale_p,
       0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  if (dpyinfo->n_planes <= 1)
    return Qnil;

  return Qt;
}

DEFUN ("x-display-pixel-width", Fx_display_pixel_width, Sx_display_pixel_width,
       0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  return make_fixnum (mac_display_pixel_width (dpyinfo));
}

DEFUN ("x-display-pixel-height", Fx_display_pixel_height,
       Sx_display_pixel_height, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  return make_fixnum (mac_display_pixel_height (dpyinfo));
}

DEFUN ("x-display-planes", Fx_display_planes, Sx_display_planes,
       0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  return make_fixnum (dpyinfo->n_planes);
}

DEFUN ("x-display-color-cells", Fx_display_color_cells, Sx_display_color_cells,
       0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  /* We force 24+ bit depths to 24-bit to prevent an overflow.  */
  return make_fixnum (1 << min (dpyinfo->n_planes, 24));
}

DEFUN ("x-server-max-request-size", Fx_server_max_request_size,
       Sx_server_max_request_size,
       0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  check_x_display_info (terminal);

  return make_fixnum (1);
}

DEFUN ("x-server-vendor", Fx_server_vendor, Sx_server_vendor, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  return build_string ("Apple Inc.");
}

DEFUN ("x-server-version", Fx_server_version, Sx_server_version, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  return list3i (mac_operating_system_version.major,
		 mac_operating_system_version.minor,
		 mac_operating_system_version.patch);
}

DEFUN ("x-display-screens", Fx_display_screens, Sx_display_screens, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  return make_fixnum (1);
}

DEFUN ("x-display-mm-height", Fx_display_mm_height, Sx_display_mm_height, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);
  CGFloat mm_per_pixel;
  CGSize size;

  block_input ();
  size = CGDisplayScreenSize (kCGDirectMainDisplay);
  mm_per_pixel = size.height / CGDisplayPixelsHigh (kCGDirectMainDisplay);
  unblock_input ();

  return make_fixnum (mac_display_pixel_height (dpyinfo) * mm_per_pixel + 0.5f);
}

DEFUN ("x-display-mm-width", Fx_display_mm_width, Sx_display_mm_width, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);
  CGFloat mm_per_pixel;
  CGSize size;

  block_input ();
  size = CGDisplayScreenSize (kCGDirectMainDisplay);
  mm_per_pixel = size.width / CGDisplayPixelsWide (kCGDirectMainDisplay);
  unblock_input ();

  return make_fixnum (mac_display_pixel_width (dpyinfo) * mm_per_pixel + 0.5f);
}

DEFUN ("x-display-backing-store", Fx_display_backing_store,
       Sx_display_backing_store, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  return intern ("not-useful");
}

DEFUN ("x-display-visual-class", Fx_display_visual_class,
       Sx_display_visual_class, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  check_x_display_info (terminal);

  return (intern ("true-color"));
}

DEFUN ("x-display-save-under", Fx_display_save_under,
       Sx_display_save_under, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  check_x_display_info (terminal);

  return Qnil;
}

/* Return geometric attributes of FRAME.  According to the value of
   ATTRIBUTES return the outer edges of FRAME (Qouter_edges), the native
   edges of FRAME (Qnative_edges), or the inner edges of frame
   (Qinner_edges).  Any other value means to return the geometry as
   returned by Fmac_frame_geometry.  */
static Lisp_Object
frame_geometry (Lisp_Object frame, Lisp_Object attribute)
{
  struct frame *f = decode_live_frame (frame);
  NativeRectangle outer_bounds;
  Lisp_Object result;

  if (FRAME_INITIAL_P (f) || !FRAME_MAC_P (f))
    return Qnil;

  block_input ();
  mac_get_frame_window_structure_bounds (f, &outer_bounds);
  if (EQ (attribute, Qouter_edges))
    result = list4i (outer_bounds.x, outer_bounds.y,
		     outer_bounds.x + outer_bounds.width,
		     outer_bounds.y + outer_bounds.height);
  else if (EQ (attribute, Qnative_edges) || EQ (attribute, Qinner_edges))
    {
      CGRect rect = mac_get_frame_window_content_rect (f, EQ (attribute,
							      Qinner_edges));

      result = list4i (outer_bounds.x + lround (CGRectGetMinX (rect)),
		       outer_bounds.y + outer_bounds.height
		       - lround (CGRectGetMaxY (rect)),
		       outer_bounds.x + lround (CGRectGetMaxX (rect)),
		       outer_bounds.y + outer_bounds.height
		       - lround (CGRectGetMinY (rect)));
    }
  else
    {
      CGFloat title_bar_height = mac_get_frame_window_title_bar_height (f);
      CGSize menu_bar_size = mac_get_frame_window_menu_bar_size (f);
      CGRect rect = mac_get_frame_window_tool_bar_rect (f);
      int tool_bar_width =
	lround (CGRectGetMaxX (rect)) - lround (CGRectGetMinX (rect));
      int tool_bar_height =
	lround (CGRectGetMaxY (rect)) - lround (CGRectGetMinY (rect));
      int tab_bar_width, tab_bar_height = FRAME_TAB_BAR_HEIGHT (f);

      if (tab_bar_height)
	{
	  CGRect inner = mac_get_frame_window_content_rect (f, true);
	  tab_bar_width = (lround (CGRectGetMaxX (inner))
			   - lround (CGRectGetMinX (inner)));
	}
      else
	tab_bar_width = 0;

      result =
	list (Fcons (Qouter_position,
		     Fcons (make_fixnum (outer_bounds.x),
			    make_fixnum (outer_bounds.y))),
	      Fcons (Qouter_size,
		     Fcons (make_fixnum (outer_bounds.width),
			    make_fixnum (outer_bounds.height))),
	      Fcons (Qexternal_border_size,
		     Fcons (make_fixnum (0), make_fixnum (0))),
	      Fcons (Qtitle_bar_size,
		     Fcons (make_fixnum (outer_bounds.width),
			    make_fixnum (lround (title_bar_height)))),
	      Fcons (Qmenu_bar_external, Qt),
	      Fcons (Qmenu_bar_size,
		     Fcons (make_fixnum (lround (menu_bar_size.width)),
			    make_fixnum (lround (menu_bar_size.height)))),
	      Fcons (Qtab_bar_size,
		     Fcons (make_fixnum (tab_bar_width),
			    make_fixnum (tab_bar_height))),
	      Fcons (Qtool_bar_external,
		     !FRAME_INTERNAL_TOOL_BAR_P (f) ? Qt : Qnil),
	      Fcons (Qtool_bar_position, Qtop),
	      Fcons (Qtool_bar_size,
		     Fcons (make_fixnum (tool_bar_width),
			    make_fixnum (tool_bar_height))),
	      Fcons (Qinternal_border_width,
		     make_fixnum (FRAME_INTERNAL_BORDER_WIDTH (f))));
    }
  unblock_input ();

  return result;
}

DEFUN ("mac-display-monitor-attributes-list", Fmac_display_monitor_attributes_list,
       Smac_display_monitor_attributes_list,
       0, 1, 0,
       doc: /* Return a list of physical monitor attributes on the Mac display TERMINAL.

The optional argument TERMINAL specifies which display to ask about.
TERMINAL should be a terminal object, a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.

In addition to the standard attribute keys listed in
`display-monitor-attributes-list', the following keys are contained in
the attributes:

 backing-scale-factor -- Backing scale factor as a number, usually
			 2 for Retina displays and 1 for the others.
 metal-device-name -- String identifying the Metal device for the
		      display, or nil if the device does not support
		      Metal.  This key is not available if Emacs is
		      not linked with the Metal framework.

Internal use only, use `display-monitor-attributes-list' instead.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);
  Lisp_Object attributes_list;

  block_input ();
  attributes_list = mac_display_monitor_attributes_list (dpyinfo);
  unblock_input ();

  return attributes_list;
}

DEFUN ("mac-frame-geometry", Fmac_frame_geometry, Smac_frame_geometry, 0, 1, 0,
       doc: /* Return geometric attributes of FRAME.
FRAME must be a live frame and defaults to the selected one.  The return
value is an association list of the attributes listed below.  All height
and width values are in pixels.

`outer-position' is a cons of the outer left and top edges of FRAME
  relative to the origin - the position (0, 0) - of FRAME's display.

`outer-size' is a cons of the outer width and height of FRAME.  The
  outer size includes the title bar and the external borders as well as
  any menu and/or tool bar of frame.

`external-border-size' is a cons of the horizontal and vertical width of
  FRAME's external borders as supplied by the window manager.

`title-bar-size' is a cons of the width and height of the title bar of
  FRAME as supplied by the window manager.  If both of them are zero,
  FRAME has no title bar.  If only the width is zero, Emacs was not
  able to retrieve the width information.

`menu-bar-external', if non-nil, means the menu bar is external (never
  included in the inner edges of FRAME).

`menu-bar-size' is a cons of the width and height of the menu bar of
  FRAME.

`tool-bar-external', if non-nil, means the tool bar is external (never
  included in the inner edges of FRAME).

`tool-bar-position' tells on which side the tool bar on FRAME is and can
  be one of `left', `top', `right' or `bottom'.  If this is nil, FRAME
  has no tool bar.

`tool-bar-size' is a cons of the width and height of the tool bar of
  FRAME.

`internal-border-width' is the width of the internal border of
  FRAME.  */)
  (Lisp_Object frame)
{
  return frame_geometry (frame, Qnil);
}

DEFUN ("mac-frame-edges", Fmac_frame_edges, Smac_frame_edges, 0, 2, 0,
       doc: /* Return edge coordinates of FRAME.
FRAME must be a live frame and defaults to the selected one.  The return
value is a list of the form (LEFT, TOP, RIGHT, BOTTOM).  All values are
in pixels relative to the origin - the position (0, 0) - of FRAME's
display.

If optional argument TYPE is the symbol `outer-edges', return the outer
edges of FRAME.  The outer edges comprise the decorations of the window
manager (like the title bar or external borders) as well as any external
menu or tool bar of FRAME.  If optional argument TYPE is the symbol
`native-edges' or nil, return the native edges of FRAME.  The native
edges exclude the decorations of the window manager and any external
menu or tool bar of FRAME.  If TYPE is the symbol `inner-edges', return
the inner edges of FRAME.  These edges exclude title bar, any borders,
menu bar or tool bar of FRAME.  */)
  (Lisp_Object frame, Lisp_Object type)
{
  return frame_geometry (frame, ((EQ (type, Qouter_edges)
				  || EQ (type, Qinner_edges))
				 ? type
				 : Qnative_edges));
}

DEFUN ("mac-frame-list-z-order", Fmac_frame_list_z_order,
       Smac_frame_list_z_order, 0, 1, 0,
       doc: /* Return list of Emacs' frames, in Z (stacking) order.
The optional argument TERMINAL specifies which display to ask about.
TERMINAL should be either a frame or a display name (a string).  If
omitted or nil, that stands for the selected frame's display.  Return
nil if TERMINAL contains no Emacs frame.

As a special case, if TERMINAL is non-nil and specifies a live frame,
return the child frames of that frame in Z (stacking) order.

Frames are listed from topmost (first) to bottommost (last).  */)
  (Lisp_Object terminal)
{
  check_x_display_info (terminal);
  struct frame *f = NULL;

  if (FRAMEP (terminal) && FRAME_LIVE_P (XFRAME (terminal)))
    f = XFRAME (terminal);

  return mac_frame_list_z_order (f);
}

DEFUN ("mac-frame-restack", Fmac_frame_restack, Smac_frame_restack, 2, 3, 0,
       doc: /* Restack FRAME1 below FRAME2.
This means that if both frames are visible and the display areas of
these frames overlap, FRAME2 (partially) obscures FRAME1.  If optional
third argument ABOVE is non-nil, restack FRAME1 above FRAME2.  This
means that if both frames are visible and the display areas of these
frames overlap, FRAME1 (partially) obscures FRAME2.

This may be thought of as an atomic action performed in two steps: The
first step removes FRAME1's window-system window from the display.  The
second step reinserts FRAME1's window below (above if ABOVE is true)
that of FRAME2.  Hence the position of FRAME2 in its display's Z
\(stacking) order relative to all other frames excluding FRAME1 remains
unaltered.

Some window managers may refuse to restack windows.  */)
     (Lisp_Object frame1, Lisp_Object frame2, Lisp_Object above)
{
  struct frame *f1 = decode_live_frame (frame1);
  struct frame *f2 = decode_live_frame (frame2);

  if (FRAME_MAC_WINDOW (f1) && FRAME_MAC_WINDOW (f2))
    {
      mac_frame_restack (f1, f2, !NILP (above));
      return Qt;
    }
  else
    {
      error ("Cannot restack frames");
      return Qnil;
    }
}

DEFUN ("mac-mouse-absolute-pixel-position", Fmac_mouse_absolute_pixel_position,
       Smac_mouse_absolute_pixel_position, 0, 0, 0,
       doc: /* Return absolute position of mouse cursor in pixels.
The position is returned as a cons cell (X . Y) of the coordinates of
the mouse cursor position in pixels relative to a position (0, 0) of the
selected frame's display.  */)
  (void)
{
  struct frame *f = SELECTED_FRAME ();
  CGPoint point;

  if (FRAME_INITIAL_P (f) || !FRAME_MAC_P (f))
    return Qnil;

  block_input ();
  point = mac_get_global_mouse ();
  unblock_input ();

  return Fcons (make_fixnum (point.x), make_fixnum (point.y));
}

DEFUN ("mac-set-mouse-absolute-pixel-position",
       Fmac_set_mouse_absolute_pixel_position,
       Smac_set_mouse_absolute_pixel_position, 2, 2, 0,
       doc: /* Move mouse pointer to absolute pixel position (X, Y).
The coordinates X and Y are interpreted in pixels relative to a position
\(0, 0) of the selected frame's display.  */)
  (Lisp_Object x, Lisp_Object y)
{
  struct frame *f = SELECTED_FRAME ();

  if (FRAME_INITIAL_P (f) || !FRAME_MAC_P (f))
    return Qnil;

  int xval = check_integer_range (x, INT_MIN, INT_MAX);
  int yval = check_integer_range (y, INT_MIN, INT_MAX);

  block_input ();
  CGWarpMouseCursorPosition (CGPointMake (xval, yval));
  unblock_input ();

  return Qnil;
}

DEFUN ("x-begin-drag", Fx_begin_drag, Sx_begin_drag, 3, 6, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object targets, Lisp_Object action, Lisp_Object frame,
   Lisp_Object return_frame, Lisp_Object allow_current_frame,
   Lisp_Object follow_tooltip)
{
  struct frame *f = decode_window_system_frame (frame), *return_to = NULL;
  Lisp_Object lval, original, tem, t1, t2, class_names = Qnil;
  DragActions actions;

  CHECK_LIST (targets);
  original = targets;

  FOR_EACH_TAIL (targets)
    {
      CHECK_STRING (XCAR (targets));

      Lisp_Object prop = Fget (Fintern (XCAR (targets), Qnil),
			       Qmac_pasteboard_dnd_target_class);

      if (STRINGP (prop))
	class_names = Fcons (prop, class_names);
    }

  CHECK_LIST_END (targets, original);

  class_names = Fnreverse (class_names);

  if (NILP (action) || EQ (action, QXdndActionCopy))
    actions = kDragActionCopy;
  else if (EQ (action, QXdndActionMove))
    actions = kDragActionMove;
  else if (EQ (action, QXdndActionLink))
    actions = kDragActionAlias;
  else if (EQ (action, QXdndActionPrivate))
    actions = kDragActionPrivate;
  else if (EQ (action, QXdndActionAsk))
    actions = kDragActionAll;
#if 0
  else if (SYMBOLP (action))
    /* This is to accommodate non-standard DND protocols such as XDS
       that are explicitly implemented by Emacs, and is not documented
       for that reason.  */
    xaction = symbol_to_x_atom (FRAME_DISPLAY_INFO (f), action);
#endif
  else if (CONSP (action))
    {
      actions = kDragActionNothing;
      original = action;

      CHECK_LIST (action);
      FOR_EACH_TAIL (action)
	{
	  tem = XCAR (action);
	  CHECK_CONS (tem);
	  t1 = XCAR (tem);
	  t2 = XCDR (tem);
	  CHECK_SYMBOL (t1);
	  CHECK_STRING (t2);

	  if (EQ (t1, QXdndActionCopy))
	    actions |= kDragActionCopy;
	  else if (EQ (t1, QXdndActionMove))
	    actions |= kDragActionMove;
	  else if (EQ (t1, QXdndActionLink))
	    actions |= kDragActionAlias;
	  else if (EQ (t1, QXdndActionAsk))
	    actions |= kDragActionAll;
	  else if (EQ (t1, QXdndActionPrivate))
	    actions |= kDragActionPrivate;
	  else
	    signal_error ("Invalid drag-and-drop action", tem);
	}
      CHECK_LIST_END (action, original);
    }
  else
    signal_error ("Invalid drag-and-drop action", action);

  enum mac_return_frame_mode mode;

  if (EQ (return_frame, Qnow))
    mode = RETURN_FRAME_NOW;
  else if (!NILP (return_frame))
    mode = RETURN_FRAME_EVENTUALLY;
  else
    mode = RETURN_FRAME_NEVER;

  block_input ();
  actions = mac_dnd_begin_drag_and_drop (f, actions, mode, &return_to,
					 !NILP (allow_current_frame),
					 class_names, !NILP (follow_tooltip));
  unblock_input ();

  if (return_to)
    {
      XSETFRAME (lval, return_to);
      return lval;
    }

  switch (actions)
    {
    case kDragActionCopy:
      lval = QXdndActionCopy;
      break;
    case kDragActionMove:
      lval = QXdndActionMove;
      break;
    case kDragActionAlias:
      lval = QXdndActionLink;
      break;
    case kDragActionNothing:
      lval = Qnil;
      break;
    case kDragActionPrivate:
    default:
      lval = QXdndActionPrivate;
    }

  return lval;
}

/* Return the display structure for the display named NAME.
   Open a new connection if necessary.  */

static struct mac_display_info *
mac_display_info_for_name (Lisp_Object name)
{
  struct mac_display_info *dpyinfo;

  CHECK_STRING (name);

  for (dpyinfo = x_display_list; dpyinfo; dpyinfo = dpyinfo->next)
    if (!NILP (Fstring_equal (XCAR (dpyinfo->name_list_element), name)))
      return dpyinfo;

  /* Use this general default value to start with.  */
  Vx_resource_name = Vinvocation_name;

  validate_x_resource_name ();

  dpyinfo = mac_term_init (name, 0, SSDATA (Vx_resource_name));

  if (dpyinfo == 0)
    error ("Cannot connect to server %s", SDATA (name));

  return dpyinfo;
}


DEFUN ("x-open-connection", Fx_open_connection, Sx_open_connection,
       1, 3, 0, doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object display, Lisp_Object xrm_string, Lisp_Object must_succeed)
{
  char *xrm_option;
  struct mac_display_info *dpyinfo;

  CHECK_STRING (display);
  if (! NILP (xrm_string))
    CHECK_STRING (xrm_string);

#if 0
  if (! EQ (Vinitial_window_system, Qmac))
    error ("Not using Mac native windows"); /* That doesn't stop us anymore. */
#endif

  xrm_option = NILP (xrm_string) ? 0 : SSDATA (xrm_string);

  validate_x_resource_name ();

  /* This is what opens the connection and sets x_current_display.
     This also initializes many symbols, such as those used for input.  */
  dpyinfo = mac_term_init (display, xrm_option,
			   SSDATA (Vx_resource_name));

  if (dpyinfo == 0)
    {
      if (!NILP (must_succeed))
	fatal ("Cannot connect to server %s.\n",
	       SDATA (display));
      else
	error ("Cannot connect to server %s", SDATA (display));
    }

  return Qnil;
}

DEFUN ("x-close-connection", Fx_close_connection,
       Sx_close_connection, 1, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  struct mac_display_info *dpyinfo = check_x_display_info (terminal);

  if (dpyinfo->reference_count > 0)
    error ("Display still has frames on it");

  mac_delete_terminal (dpyinfo->terminal);

  return Qnil;
}

DEFUN ("x-display-list", Fx_display_list, Sx_display_list, 0, 0, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (void)
{
  Lisp_Object result = Qnil;
  struct mac_display_info *xdi;

  for (xdi = x_display_list; xdi; xdi = xdi->next)
    result = Fcons (XCAR (xdi->name_list_element), result);

  return result;
}

DEFUN ("x-synchronize", Fx_synchronize, Sx_synchronize, 1, 2, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object on, Lisp_Object terminal)
{
  check_x_display_info (terminal);

  return Qnil;
}


/***********************************************************************
				Tool tips
 ***********************************************************************/

static void compute_tip_xy (struct frame *, Lisp_Object, Lisp_Object,
			    Lisp_Object, int, int, int *, int *);

/* The frame of the currently visible tooltip, or nil if none.  */
static Lisp_Object tip_frame;

/* The X and Y deltas of the last call to `x-show-tip'.  */
static Lisp_Object tip_dx, tip_dy;

/* A timer that hides or deletes the currently visible tooltip when it
   fires.  */
static Lisp_Object tip_timer;

/* STRING argument of last `x-show-tip' call.  */
static Lisp_Object tip_last_string;

/* Normalized FRAME argument of last `x-show-tip' call.  */
static Lisp_Object tip_last_frame;

/* PARMS argument of last `x-show-tip' call.  */
static Lisp_Object tip_last_parms;


static void
unwind_create_tip_frame (Lisp_Object frame)
{
  Lisp_Object deleted;

  deleted = unwind_create_frame (frame);
  if (EQ (deleted, Qt))
    tip_frame = Qnil;
}


/* Create a frame for a tooltip on the display described by DPYINFO.
   PARMS is a list of frame parameters.  TEXT is the string to
   display in the tip frame.  Value is the frame.

   Note that functions called here, esp. gui_default_parameter can
   signal errors, for instance when a specified color name is
   undefined.  We have to make sure that we're in a consistent state
   when this happens.  */

static Lisp_Object
mac_create_tip_frame (struct mac_display_info *dpyinfo, Lisp_Object parms)
{
  struct frame *f;
  Lisp_Object frame;
  Lisp_Object name;
  specpdl_ref count = SPECPDL_INDEX ();
  bool face_change_before = face_change;

  if (!dpyinfo->terminal->name)
    error ("Terminal is not live, can't create new frames on it");

  parms = Fcopy_alist (parms);

  /* Get the name of the frame to use for resource lookup.  */
  name = gui_display_get_arg (dpyinfo, parms, Qname, "name", "Name",
                              RES_TYPE_STRING);
  if (!STRINGP (name)
      && !BASE_EQ (name, Qunbound)
      && !NILP (name))
    error ("Invalid frame name--not a string or nil");

  frame = Qnil;
  f = make_frame (false);
  f->wants_modeline = false;
  XSETFRAME (frame, f);
  record_unwind_protect (unwind_create_tip_frame, frame);

  f->terminal = dpyinfo->terminal;

  /* By setting the output method, we're essentially saying that
     the frame is live, as per FRAME_LIVE_P.  If we get a signal
     from this point on, mac_destroy_window might screw up reference
     counts etc.  */
  f->output_method = output_mac;
  f->output_data.mac = xzalloc (sizeof *f->output_data.mac);
  FRAME_FONTSET (f) = -1;
  f->output_data.mac->white_relief.pixel = -1;
  f->output_data.mac->black_relief.pixel = -1;

  f->tooltip = true;
  fset_icon_name (f, Qnil);
/*   FRAME_X_DISPLAY_INFO (f) = dpyinfo; */
  f->output_data.mac->parent_desc = FRAME_DISPLAY_INFO (f)->root_window;
  f->output_data.mac->explicit_parent = false;

  /* Set the name; the functions to which we pass f expect the name to
     be set.  */
  if (BASE_EQ (name, Qunbound) || NILP (name))
    {
      fset_name (f, build_string (dpyinfo->mac_id_name));
      f->explicit_name = false;
    }
  else
    {
      fset_name (f, name);
      f->explicit_name = true;
      /* use the frame's title when getting resources for this frame.  */
      specbind (Qx_resource_name, name);
    }

  mac_register_font_driver (f);

  image_cache_refcount =
    FRAME_IMAGE_CACHE (f) ? FRAME_IMAGE_CACHE (f)->refcount : 0;
#ifdef GLYPH_DEBUG
  dpyinfo_refcount = dpyinfo->reference_count;
#endif /* GLYPH_DEBUG */

  gui_default_parameter (f, parms, Qfont_backend, Qnil,
                         "fontBackend", "FontBackend", RES_TYPE_STRING);

  /* Extract the window parameters from the supplied values that are
     needed to determine window geometry.  */
  mac_default_font_parameter (f, parms);

  gui_default_parameter (f, parms, Qborder_width, make_fixnum (0),
                         "borderWidth", "BorderWidth", RES_TYPE_NUMBER);

  /* This defaults to 2 in order to match xterm.  We recognize either
     internalBorderWidth or internalBorder (which is what xterm calls
     it).  */
  if (NILP (Fassq (Qinternal_border_width, parms)))
    {
      Lisp_Object value;

      value = gui_display_get_arg (dpyinfo, parms, Qinternal_border_width,
                                   "internalBorder", "InternalBorder",
                                   RES_TYPE_NUMBER);
      if (! BASE_EQ (value, Qunbound))
	parms = Fcons (Fcons (Qinternal_border_width, value),
		       parms);
    }

  gui_default_parameter (f, parms, Qinternal_border_width, make_fixnum (1),
                         "internalBorderWidth", "InternalBorderWidth",
                         RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_divider_width, make_fixnum (0),
                         NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qbottom_divider_width, make_fixnum (0),
                         NULL, NULL, RES_TYPE_NUMBER);

  /* Also do the stuff which must be set before the window exists.  */
  gui_default_parameter (f, parms, Qforeground_color, build_string ("black"),
                         "foreground", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qbackground_color, build_string ("white"),
                         "background", "Background", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qmouse_color, build_string ("black"),
                         "pointerColor", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qcursor_color, build_string ("black"),
                         "cursorColor", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qborder_color, build_string ("black"),
                         "borderColor", "BorderColor", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qno_special_glyphs, Qnil,
                         NULL, NULL, RES_TYPE_BOOLEAN);

  gui_default_parameter (f, parms, Qinhibit_double_buffering,
			 mac_inhibit_double_buffering_default_value (),
                         "inhibitDoubleBuffering", "InhibitDoubleBuffering",
                         RES_TYPE_BOOLEAN);

  block_input ();

  mac_create_frame_window (f);

  if (FRAME_MAC_WINDOW (f))
    mac_set_frame_window_background (f, FRAME_BACKGROUND_PIXEL (f));

  unblock_input ();

  /* Init faces before gui_default_parameter is called for the
     scroll-bar-width parameter because otherwise we end up in
     init_iterator with a null face cache, which should not happen.  */
  init_frame_faces (f);

  gui_figure_window_size (f, parms, false, false);

  f->output_data.mac->parent_desc = FRAME_DISPLAY_INFO (f)->root_window;

  mac_make_gc (f);

  gui_default_parameter (f, parms, Qauto_raise, Qnil,
                         "autoRaise", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  gui_default_parameter (f, parms, Qauto_lower, Qnil,
                         "autoLower", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  gui_default_parameter (f, parms, Qcursor_type, Qbox,
                         "cursorType", "CursorType", RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qalpha, Qnil,
                         "alpha", "Alpha", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qalpha_background, Qnil,
                         "alphaBackground", "AlphaBackground", RES_TYPE_NUMBER);

  /* Add `tooltip' frame parameter's default value. */
  if (NILP (Fframe_parameter (frame, Qtooltip)))
    {
      AUTO_FRAME_ARG (arg, Qtooltip, Qt);
      Fmodify_frame_parameters (frame, arg);
    }

  /* FIXME - can this be done in a similar way to normal frames?
     https://lists.gnu.org/r/emacs-devel/2007-10/msg00641.html */

  /* Set the `display-type' frame parameter before setting up faces. */
  {
    Lisp_Object disptype;

    if (FRAME_DISPLAY_INFO (f)->n_planes == 1)
      disptype = Qmono;
    else if (!FRAME_DISPLAY_INFO (f)->color_p)
      disptype = intern ("grayscale");
    else
      disptype = intern ("color");

    if (NILP (Fframe_parameter (frame, Qdisplay_type)))
      {
	AUTO_FRAME_ARG (arg, Qdisplay_type, disptype);
	Fmodify_frame_parameters (frame, arg);
      }
  }

  /* Set up faces after all frame parameters are known.  This call
     also merges in face attributes specified for new frames.

     Frame parameters may be changed if .Xdefaults contains
     specifications for the default font.  For example, if there is an
     `Emacs.default.attributeBackground: pink', the `background-color'
     attribute of the frame gets set, which let's the internal border
     of the tooltip frame appear in pink.  Prevent this.  */
  {
    Lisp_Object bg = Fframe_parameter (frame, Qbackground_color);

    call2 (Qface_set_after_frame_default, frame, Qnil);

    if (!EQ (bg, Fframe_parameter (frame, Qbackground_color)))
      {
	AUTO_FRAME_ARG (arg, Qbackground_color, bg);
	Fmodify_frame_parameters (frame, arg);
      }
  }

  f->no_split = true;

  /* Now that the frame will be official, it counts as a reference to
     its display and terminal.  */
  FRAME_DISPLAY_INFO (f)->reference_count++;
  f->terminal->reference_count++;

  /* It is now ok to make the frame official even if we get an error
     below.  And the frame needs to be on Vframe_list or making it
     visible won't work.  */
  Vframe_list = Fcons (frame, Vframe_list);
  f->can_set_window_size = true;
  adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
		     0, true, Qtip_frame);

  /* Setting attributes of faces of the tooltip frame from resources
     and similar will set face_change, which leads to the clearing of
     all current matrices.  Since this isn't necessary here, avoid it
     by resetting face_change to the value it had before we created
     the tip frame.  */
  face_change = face_change_before;

  /* Discard the unwind_protect.  */
  return unbind_to (count, frame);
}


/* Compute where to display tip frame F.  PARMS is the list of frame
   parameters for F.  DX and DY are specified offsets from the current
   location of the mouse.  WIDTH and HEIGHT are the width and height
   of the tooltip.  Return coordinates relative to the root window of
   the display in *ROOT_X, and *ROOT_Y.  */

static void
compute_tip_xy (struct frame *f,
		Lisp_Object parms, Lisp_Object dx, Lisp_Object dy,
		int width, int height, int *root_x, int *root_y)
{
  Lisp_Object left, top, right, bottom;
  CGRect bounds =
    CGRectMake (0, 0, mac_display_pixel_width (FRAME_DISPLAY_INFO (f)),
		mac_display_pixel_height (FRAME_DISPLAY_INFO (f)));

  /* User-specified position?  */
  left = CDR (Fassq (Qleft, parms));
  top  = CDR (Fassq (Qtop, parms));
  right = CDR (Fassq (Qright, parms));
  bottom = CDR (Fassq (Qbottom, parms));

  /* Move the tooltip window where the mouse pointer is.  Resize and
     show it.  */
  if ((!FIXNUMP (left) && !FIXNUMP (right))
      || (!FIXNUMP (top) && !FIXNUMP (bottom)))
    {
      CGPoint point;
      CGError err;
      uint32_t count;

      block_input ();
      point = mac_get_global_mouse ();
      *root_x = point.x;
      *root_y = point.y;
      err = CGGetDisplaysWithPoint (point, 0, NULL, &count);
      if (err == kCGErrorSuccess)
	{
	  CGDirectDisplayID *displays =
	    alloca (sizeof (CGDirectDisplayID) * count);

	  err = CGGetDisplaysWithPoint (point, count, displays, &count);
	  if (err == kCGErrorSuccess && count > 0)
	    {
	      uint32_t i;

	      bounds = CGDisplayBounds (displays[0]);
	      for (i = 1; i < count; i++)
		bounds = CGRectIntersection (bounds,
					     CGDisplayBounds (displays[i]));
	    }
	}
      unblock_input ();
    }

  if (FIXNUMP (top))
    *root_y = XFIXNUM (top);
  else if (FIXNUMP (bottom))
    *root_y = XFIXNUM (bottom) - height;
  else if (*root_y + XFIXNUM (dy) <= CGRectGetMinY (bounds))
    *root_y = CGRectGetMinY (bounds); /* Can happen for negative dy */
  else if (*root_y + XFIXNUM (dy) + height <= CGRectGetMaxY (bounds))
    /* It fits below the pointer */
    *root_y += XFIXNUM (dy);
  else if (CGRectGetMinY (bounds) + height + XFIXNUM (dy) <= *root_y)
    /* It fits above the pointer.  */
    *root_y -= height + XFIXNUM (dy);
  else
    /* Put it on the top.  */
    *root_y = CGRectGetMinY (bounds);

  if (FIXNUMP (left))
    *root_x = XFIXNUM (left);
  else if (FIXNUMP (right))
    *root_x = XFIXNUM (right) - width;
  else if (*root_x + XFIXNUM (dx) <= CGRectGetMinX (bounds))
    *root_x = CGRectGetMinX (bounds); /* Can happen for negative dx */
  else if (*root_x + XFIXNUM (dx) + width <= CGRectGetMaxX (bounds))
    /* It fits to the right of the pointer.  */
    *root_x += XFIXNUM (dx);
  else if (CGRectGetMinX (bounds) + width + XFIXNUM (dx) <= *root_x)
    /* It fits to the left of the pointer.  */
    *root_x -= width + XFIXNUM (dx);
  else
    /* Put it left justified on the screen -- it ought to fit that way.  */
    *root_x = CGRectGetMinX (bounds);
}


/**
 * mac_hide_tip:
 *
 * Hide currently visible tooltip and cancel its timer.
 *
 * If GTK+ system tooltips are used, this will try to hide the tooltip
 * referenced by the x_output structure of tooltip_last_frame.  For
 * Emacs tooltips this will try to make tooltip_frame invisible (if
 * DELETE is false) or delete tooltip_frame (if DELETE is true).
 *
 * Return Qt if the tooltip was either deleted or made invisible, Qnil
 * otherwise.
 */
static Lisp_Object
mac_hide_tip (bool delete)
{
  if (!NILP (tip_timer))
    {
      call1 (Qcancel_timer, tip_timer);
      tip_timer = Qnil;
    }


  if (NILP (tip_frame)
      || (!delete
	  && !NILP (tip_frame)
	  && FRAME_LIVE_P (XFRAME (tip_frame))
	  && !FRAME_VISIBLE_P (XFRAME (tip_frame))))
    return Qnil;
  else
    {
      Lisp_Object was_open = Qnil;

      specpdl_ref count = SPECPDL_INDEX ();
      specbind (Qinhibit_redisplay, Qt);
      specbind (Qinhibit_quit, Qt);

      if (!NILP (tip_frame))
	{
	  struct frame *f = XFRAME (tip_frame);

	  if (FRAME_LIVE_P (f))
	    {
	      if (delete)
		{
		  /* Fx_hide_tip might be called just after Command-H
		     has hidden all the frames.  We pass Qt for the
		     `force' arg so as to avoid the "Attempt to delete
		     the sole visible or iconified frame" error in
		     that case.  */
		  delete_frame (tip_frame, Qt);
		  tip_frame = Qnil;
		}
	      else
		mac_make_frame_invisible (XFRAME (tip_frame));

	      was_open = Qt;
	    }
	  else
	    tip_frame = Qnil;
	}
      else
	tip_frame = Qnil;

      return unbind_to (count, was_open);
    }
}


DEFUN ("x-show-tip", Fx_show_tip, Sx_show_tip, 1, 6, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object string, Lisp_Object frame, Lisp_Object parms,
   Lisp_Object timeout, Lisp_Object dx, Lisp_Object dy)
{
  struct frame *f, *tip_f;
  struct window *w;
  int root_x, root_y;
  struct buffer *old_buffer;
  struct text_pos pos;
  int width, height;
  int old_windows_or_buffers_changed = windows_or_buffers_changed;
  specpdl_ref count = SPECPDL_INDEX ();
  Lisp_Object window, size, tip_buf;
  bool displayed;
#ifdef ENABLE_CHECKING
  struct glyph_row *row, *end;
#endif
  AUTO_STRING (tip, " *tip*");

  specbind (Qinhibit_redisplay, Qt);

  CHECK_STRING (string);
  if (SCHARS (string) == 0)
    string = make_unibyte_string (" ", 1);

  if (NILP (frame))
    frame = selected_frame;
  f = decode_window_system_frame (frame);

  if (NILP (timeout))
    timeout = Vx_show_tooltip_timeout;
  CHECK_FIXNAT (timeout);

  if (NILP (dx))
    dx = make_fixnum (5);
  else
    CHECK_FIXNUM (dx);

  if (NILP (dy))
    dy = make_fixnum (-10);
  else
    CHECK_FIXNUM (dy);

  tip_dx = dx;
  tip_dy = dy;

  if (!NILP (tip_frame) && FRAME_LIVE_P (XFRAME (tip_frame)))
    {
      if (FRAME_VISIBLE_P (XFRAME (tip_frame))
	  && !NILP (Fequal_including_properties (tip_last_string, string))
	  && !NILP (Fequal (tip_last_parms, parms)))
	{
	  /* Only DX and DY have changed.  */
	  tip_f = XFRAME (tip_frame);
	  if (!NILP (tip_timer))
	    {
	      call1 (Qcancel_timer, tip_timer);
	      tip_timer = Qnil;
	    }

	  block_input ();
	  compute_tip_xy (tip_f, parms, dx, dy, FRAME_PIXEL_WIDTH (tip_f),
			  FRAME_PIXEL_HEIGHT (tip_f), &root_x, &root_y);
	  mac_move_frame_window_structure (tip_f, root_x, root_y);
	  unblock_input ();

	  goto start_timer;
	}
      else if (tooltip_reuse_hidden_frame && BASE_EQ (frame, tip_last_frame))
	{
	  bool delete = false;
	  Lisp_Object tail, elt, parm, last;

	  /* Check if every parameter in PARMS has the same value in
	     tip_last_parms.  This may destruct tip_last_parms which,
	     however, will be recreated below.  */
	  for (tail = parms; CONSP (tail); tail = XCDR (tail))
	    {
	      elt = XCAR (tail);
	      parm = CAR (elt);
	      /* The left, top, right and bottom parameters are handled
		 by compute_tip_xy so they can be ignored here.  */
	      if (!EQ (parm, Qleft) && !EQ (parm, Qtop)
		  && !EQ (parm, Qright) && !EQ (parm, Qbottom))
		{
		  last = Fassq (parm, tip_last_parms);
		  if (NILP (Fequal (CDR (elt), CDR (last))))
		    {
		      /* We lost, delete the old tooltip.  */
		      delete = true;
		      break;
		    }
		  else
		    tip_last_parms =
		      call2 (Qassq_delete_all, parm, tip_last_parms);
		}
	      else
		tip_last_parms =
		  call2 (Qassq_delete_all, parm, tip_last_parms);
	    }

	  /* Now check if every parameter in what is left of
	     tip_last_parms with a non-nil value has an association in
	     PARMS.  */
	  for (tail = tip_last_parms; CONSP (tail); tail = XCDR (tail))
	    {
	      elt = XCAR (tail);
	      parm = CAR (elt);
	      if (!EQ (parm, Qleft) && !EQ (parm, Qtop) && !EQ (parm, Qright)
		  && !EQ (parm, Qbottom) && !NILP (CDR (elt)))
		{
		  /* We lost, delete the old tooltip.  */
		  delete = true;
		  break;
		}
	    }

	  mac_hide_tip (delete);
	}
      else
	mac_hide_tip (true);
    }
  else
    mac_hide_tip (true);

  tip_last_frame = frame;
  tip_last_string = string;
  tip_last_parms = parms;

  if (NILP (tip_frame) || !FRAME_LIVE_P (XFRAME (tip_frame)))
    {
      /* Add default values to frame parameters.  */
      if (NILP (Fassq (Qname, parms)))
	parms = Fcons (Fcons (Qname, build_string ("tooltip")), parms);
      if (NILP (Fassq (Qinternal_border_width, parms)))
	parms = Fcons (Fcons (Qinternal_border_width, make_fixnum (3)), parms);
      if (NILP (Fassq (Qborder_width, parms)))
	parms = Fcons (Fcons (Qborder_width, make_fixnum (1)), parms);
      if (NILP (Fassq (Qborder_color, parms)))
	parms = Fcons (Fcons (Qborder_color, build_string ("lightyellow")), parms);
      if (NILP (Fassq (Qbackground_color, parms)))
	parms = Fcons (Fcons (Qbackground_color, build_string ("lightyellow")),
		       parms);

      /* Create a frame for the tooltip, and record it in the global
	 variable tip_frame.  */
      if (NILP (tip_frame = mac_create_tip_frame (FRAME_DISPLAY_INFO (f), parms)))
	/* Creating the tip frame failed.  */
	return unbind_to (count, Qnil);
    }

  tip_f = XFRAME (tip_frame);
  window = FRAME_ROOT_WINDOW (tip_f);
  tip_buf = Fget_buffer_create (tip, Qnil);
  /* We will mark the tip window a "pseudo-window" below, and such
     windows cannot have display margins.  */
  bset_left_margin_cols (XBUFFER (tip_buf), make_fixnum (0));
  bset_right_margin_cols (XBUFFER (tip_buf), make_fixnum (0));
  set_window_buffer (window, tip_buf, false, false);
  w = XWINDOW (window);
  w->pseudo_window_p = true;
  /* Try to avoid that `other-window' select us (Bug#47207).  */
  Fset_window_parameter (window, Qno_other_window, Qt);

  /* Set up the frame's root window.  Note: The following code does not
     try to size the window or its frame correctly.  Its only purpose is
     to make the subsequent text size calculations work.  The right
     sizes should get installed when the toolkit gets back to us.  */
  w->left_col = 0;
  w->top_line = 0;
  w->pixel_left = 0;
  w->pixel_top = 0;

  if (CONSP (Vx_max_tooltip_size)
      && RANGED_FIXNUMP (1, XCAR (Vx_max_tooltip_size), INT_MAX)
      && RANGED_FIXNUMP (1, XCDR (Vx_max_tooltip_size), INT_MAX))
    {
      w->total_cols = XFIXNAT (XCAR (Vx_max_tooltip_size));
      w->total_lines = XFIXNAT (XCDR (Vx_max_tooltip_size));
    }
  else
    {
      w->total_cols = 80;
      w->total_lines = 40;
    }

  w->pixel_width = w->total_cols * FRAME_COLUMN_WIDTH (tip_f);
  w->pixel_height = w->total_lines * FRAME_LINE_HEIGHT (tip_f);
  FRAME_TOTAL_COLS (tip_f) = w->total_cols;
  adjust_frame_glyphs (tip_f);

  /* Insert STRING into root window's buffer and fit the frame to the
     buffer.  */
  specpdl_ref count_1 = SPECPDL_INDEX ();
  old_buffer = current_buffer;
  set_buffer_internal_1 (XBUFFER (w->contents));
  bset_truncate_lines (current_buffer, Qnil);
  specbind (Qinhibit_read_only, Qt);
  specbind (Qinhibit_modification_hooks, Qt);
  specbind (Qinhibit_point_motion_hooks, Qt);
  Ferase_buffer ();
  Finsert (1, &string);
  clear_glyph_matrix (w->desired_matrix);
  clear_glyph_matrix (w->current_matrix);
  SET_TEXT_POS (pos, BEGV, BEGV_BYTE);
  displayed = try_window (window, pos, TRY_WINDOW_IGNORE_FONTS_CHANGE);

  if (!displayed && NILP (Vx_max_tooltip_size))
    {
#ifdef ENABLE_CHECKING
      row = w->desired_matrix->rows;
      end = w->desired_matrix->rows + w->desired_matrix->nrows;

      while (row < end)
	{
	  if (!row->displays_text_p
	      || row->ends_at_zv_p)
	    break;
	  ++row;
	}

      eassert (row < end && row->ends_at_zv_p);
#endif
    }

  /* Calculate size of tooltip window.  */
  size = Fwindow_text_pixel_size (window, Qnil, Qnil, Qnil,
				  make_fixnum (w->pixel_height), Qnil,
				  Qnil);
  /* Add the frame's internal border to calculated size.  */
  width = XFIXNUM (CAR (size)) + 2 * FRAME_INTERNAL_BORDER_WIDTH (tip_f);
  height = XFIXNUM (CDR (size)) + 2 * FRAME_INTERNAL_BORDER_WIDTH (tip_f);

  /* Calculate position of tooltip frame.  */
  compute_tip_xy (tip_f, parms, dx, dy, width, height, &root_x, &root_y);

  /* Show tooltip frame.  */
  block_input ();
  mac_move_frame_window_structure (tip_f, root_x, root_y);
  mac_size_frame_window (tip_f, width, height, true);
  mac_show_frame_window (tip_f);
  /* Now that we have deferred creation of the window device and also
     turned off automatic display for tooltip windows, we have to draw
     the internal border ourselves after showing the window.  */
  mac_clear_area (tip_f, 0, 0, width, height);
  mac_bring_frame_window_to_front (tip_f);
  unblock_input ();

  FRAME_PIXEL_WIDTH (tip_f) = width;
  FRAME_PIXEL_HEIGHT (tip_f) = height;

  w->must_be_updated_p = true;
  update_single_window (w);
  flush_frame (tip_f);
  set_buffer_internal_1 (old_buffer);
  unbind_to (count_1, Qnil);
  windows_or_buffers_changed = old_windows_or_buffers_changed;

 start_timer:
  /* Let the tip disappear after timeout seconds.  */
  tip_timer = call3 (Qrun_at_time, timeout, Qnil,
		     Qx_hide_tip);

  return unbind_to (count, Qnil);
}


DEFUN ("x-hide-tip", Fx_hide_tip, Sx_hide_tip, 0, 0, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (void)
{
  return mac_hide_tip (!tooltip_reuse_hidden_frame);
}

void
mac_move_tooltip_to_mouse_location (void)
{
  int root_x, root_y;
  struct frame *tip_f;

  if (!FIXNUMP (tip_dx) || !FIXNUMP (tip_dy))
    return;

  if (!FRAMEP (tip_frame))
    return;

  tip_f = XFRAME (tip_frame);

  if (!FRAME_LIVE_P (tip_f)
      || !FRAME_VISIBLE_P (tip_f))
    return;

  /* We can directly use `compute_tip_xy' here, since it doesn't cons
     nearly as much as it does on X.  */
  compute_tip_xy (tip_f, Qnil, tip_dx, tip_dy, FRAME_PIXEL_WIDTH (tip_f),
		  FRAME_PIXEL_HEIGHT (tip_f), &root_x, &root_y);

  mac_move_frame_window_structure (tip_f, root_x, root_y);
}


/***********************************************************************
			File selection dialog
 ***********************************************************************/

DEFUN ("x-file-dialog", Fx_file_dialog, Sx_file_dialog, 2, 5, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object prompt, Lisp_Object dir, Lisp_Object default_filename,
   Lisp_Object mustmatch, Lisp_Object only_dir_p)
{
  return mac_file_dialog (prompt, dir, default_filename, mustmatch, only_dir_p);
}


/***********************************************************************
				Fonts
 ***********************************************************************/

DEFUN ("x-select-font", Fx_select_font, Sx_select_font, 0, 2, 0,
       doc: /* Read a font name using a Mac font selection dialog.
Return a fontconfig-style font string corresponding to the selection.

If FRAME is omitted or nil, it defaults to the selected frame. */)
  (Lisp_Object frame, Lisp_Object ignored)
{
  struct frame *f = decode_window_system_frame (frame);
  Lisp_Object font_spec, font = Qnil;

  check_window_system (f);

  if (display_hourglass_p)
    cancel_hourglass ();

  block_input ();
  font_spec = mac_font_dialog (f);
  unblock_input ();

  if (!NILP (font_spec))
    {
      char *name = alloca (256);
      int len = font_unparse_fcname (font_spec, 0, name, 256);

      font = make_unibyte_string (name, len);
    }

  if (NILP (font))
    quit ();

  return font;
}

DEFUN ("mac-set-font-panel-visible-p", Fmac_set_font_panel_visible_p,
       Smac_set_font_panel_visible_p, 1, 1, 0,
  doc: /* Make the font panel visible if and only if FLAG is non-nil.
This is for internal use only.  Use `mac-font-panel-mode' instead.  */)
  (Lisp_Object flag)
{
  OSStatus err = noErr;

  check_window_system (NULL);

  block_input ();
  if (NILP (flag) != !mac_font_panel_visible_p ())
    {
      err = mac_show_hide_font_panel ();
      if (err == noErr && !NILP (flag))
	{
	  struct frame *sf = SELECTED_FRAME ();
	  Lisp_Object focus_frame = FRAME_TERMINAL (sf)->get_focus_frame (sf);
	  struct frame *f = (NILP (focus_frame) ? SELECTED_FRAME ()
			     : XFRAME (focus_frame));

	  mac_set_font_info_for_selection (f, DEFAULT_FACE_ID, 0, -1, Qnil);
	}
    }
  unblock_input ();

  if (err != noErr)
    error ("Cannot change visibility of the font panel");
  return Qnil;
}


/***********************************************************************
			       Printing
 ***********************************************************************/

DEFUN ("mac-export-frames", Fmac_export_frames, Smac_export_frames, 0, 2, 0,
       doc: /* Return image data of FRAMES in TYPE format.
FRAMES should be nil (the selected frame), a frame, or a list of
frames (each of which corresponds to one page).  Each frame should be
visible.  Optional arg TYPE should be either `pdf' (default) or
`png'.  */)
     (Lisp_Object frames, Lisp_Object type)
{
  Lisp_Object rest, tmp;

  if (!CONSP (frames))
    frames = list1 (frames);

  tmp = Qnil;
  for (rest = frames; CONSP (rest); rest = XCDR (rest))
    {
      struct frame *f = decode_window_system_frame (XCAR (rest));
      Lisp_Object frame;

      XSETFRAME (frame, f);
      if (!FRAME_VISIBLE_P (f))
	error ("Frames to be exported must be visible.");
      tmp = Fcons (frame, tmp);
    }
  frames = Fnreverse (tmp);

  if (NILP (type))
    type = Qpdf;
  else if (EQ (type, Qpdf))
    ;
  else if (EQ (type, Qpng))
    {
      if (!NILP (XCDR (frames)))
	error ("PNG export cannot handle multiple frames.");
    }
  else
    error ("Unsupported export type");

  return mac_export_frames (frames, type);
}

DEFUN ("mac-page-setup-dialog", Fmac_page_setup_dialog, Smac_page_setup_dialog,
       0, 0, 0,
       doc: /* Pop up a page setup dialog.
The current page setup can be obtained using `mac-get-page-setup'.  */)
     (void)
{
  block_input ();
  mac_page_setup_dialog ();
  unblock_input ();

  return Qnil;
}

DEFUN ("mac-get-page-setup", Fmac_get_page_setup, Smac_get_page_setup, 0, 0, 0,
       doc: /* Return the value of the current page setup.
The return value is an alist containing the following keys:

  orientation: page orientation (symbol `portrait' or `landscape').
  width, height: page width/height in points not including margins.
  left-margin, right-margin, top-margin, bottom-margin: print margins,
	which is the parts of the page that the printer cannot print
	on, in points.

The paper width can be obtained as the sum of width, left-margin, and
right-margin values if the page orientation is `portrait'.  Otherwise,
it is the sum of width, top-margin, and bottom-margin values.
Likewise, the paper height is the sum of height, top-margin, and
bottom-margin values if the page orientation is `portrait'.
Otherwise, it is the sum of height, left-margin, and right-margin
values.  */)
     (void)
{
  Lisp_Object result;

  block_input ();
  result = mac_get_page_setup ();
  unblock_input ();

  return result;
}

DEFUN ("mac-print-frames-dialog", Fmac_print_frames_dialog,
       Smac_print_frames_dialog, 0, 1, "",
       doc: /* Pop up a print dialog to print the current contents of FRAMES.
FRAMES should be nil (the selected frame), a frame, or a list of
frames (each of which corresponds to one page).  Each frame should be
visible.  */)
     (Lisp_Object frames)
{
  Lisp_Object rest, tmp;

  if (!CONSP (frames))
    frames = list1 (frames);

  tmp = Qnil;
  for (rest = frames; CONSP (rest); rest = XCDR (rest))
    {
      struct frame *f = decode_window_system_frame (XCAR (rest));
      Lisp_Object frame;

      XSETFRAME (frame, f);
      if (!FRAME_VISIBLE_P (f))
	error ("Frames to be printed must be visible.");
      tmp = Fcons (frame, tmp);
    }
  frames = Fnreverse (tmp);

  /* Make sure the current matrices are up-to-date.  */
  specpdl_ref count = SPECPDL_INDEX ();
  specbind (Qredisplay_dont_pause, Qt);
  redisplay_preserve_echo_area (32);
  unbind_to (count, Qnil);

  block_input ();
  mac_print_frames_dialog (frames);
  unblock_input ();

  return Qnil;
}


/***********************************************************************
			  Text Input Source
 ***********************************************************************/

/* Return true if and only if LIST is a non-circular list of
   symbols.  */

static bool
mac_symbol_list_p (Lisp_Object list)
{
  Lisp_Object tortoise, hare;

  hare = tortoise = list;

  while (CONSP (hare))
    {
      if (!SYMBOLP (XCAR (hare)))
	return false;
      hare = XCDR (hare);
      if (!CONSP (hare))
	break;

      if (!SYMBOLP (XCAR (hare)))
	return false;
      hare = XCDR (hare);
      tortoise = XCDR (tortoise);

      if (EQ (hare, tortoise))
	return false;
    }

  return NILP (hare);
}

/* Signal an error if SOURCE is invalid as a Lisp representation of an
   input source.  If NO_KEYBOARD_SYMBOLS_P in non-zero, then the
   symbol that always represents a keyboard input source is also
   treated as invalid.  */

static void
mac_check_input_source (Lisp_Object source, bool no_keyboard_symbols_p)
{
  if (SYMBOLP (source))
    {
      if ((!no_keyboard_symbols_p
	   && (NILP (source) || EQ (source, Qkeyboard)
	       || EQ (source, Qkeyboard_layout)
	       || EQ (source, Qascii_capable_keyboard)
	       || EQ (source, Qascii_capable_keyboard_layout)
	       || EQ (source, Qkeyboard_layout_override)))
	  || EQ (source, Qt))
	return;
    }
  else if (STRINGP (source))
    return;

  error ("Invalid input source");
}

/* Create and return a TISInputSource object from a Lisp
   representation of the input source SOURCE.  Return NULL if a
   TISInputSource object cannot be created.  */

static TISInputSourceRef
mac_create_input_source_from_lisp (Lisp_Object source)
{
  TISInputSourceRef __block result = NULL;

  mac_within_gui (^{
      if (SYMBOLP (source))
	{
	  if (NILP (source) || EQ (source, Qkeyboard))
	    result = TISCopyCurrentKeyboardInputSource ();
	  else if (EQ (source, Qkeyboard_layout))
	    result = TISCopyCurrentKeyboardLayoutInputSource ();
	  else if (EQ (source, Qascii_capable_keyboard))
	    result = TISCopyCurrentASCIICapableKeyboardInputSource ();
	  else if (EQ (source, Qascii_capable_keyboard_layout))
	    result = TISCopyCurrentASCIICapableKeyboardLayoutInputSource ();
	  else if (EQ (source, Qkeyboard_layout_override))
	    result = TISCopyInputMethodKeyboardLayoutOverride ();
	  else if (EQ (source, Qt))
	    {
	      CFLocaleRef locale = CFLocaleCopyCurrent ();

	      if (locale)
		{
		  CFStringRef language =
		    CFLocaleGetValue (locale, kCFLocaleLanguageCode);

		  if (language)
		    result = TISCopyInputSourceForLanguage (language);
		  CFRelease (locale);
		}
	    }
	}
      else if (STRINGP (source))
	{
	  CFStringRef string = cfstring_create_with_string (source);

	  if (string)
	    {
	      CFArrayRef sources = NULL;
	      CFDictionaryRef properties =
		CFDictionaryCreate (NULL,
				    (const void **) &kTISPropertyInputSourceID,
				    (const void **) &string, 1,
				    &kCFTypeDictionaryKeyCallBacks,
				    &kCFTypeDictionaryValueCallBacks);

	      if (properties)
		{
		  sources = TISCreateInputSourceList (properties, false);
		  if (sources == NULL)
		    sources = TISCreateInputSourceList (properties, true);
		  CFRelease (properties);
		}
	      if (sources)
		{
		  if (CFArrayGetCount (sources) > 0)
		    result = ((TISInputSourceRef)
			      CFRetain (CFArrayGetValueAtIndex (sources, 0)));
		  CFRelease (sources);
		}
	      else
		{
		  CFStringRef language =
		    CFLocaleCreateCanonicalLanguageIdentifierFromString (NULL,
									 string);

		  if (language)
		    {
		      result = TISCopyInputSourceForLanguage (language);
		      CFRelease (language);
		    }
		}
	      CFRelease (string);
	    }
	}
    });

  return result;
}

/* Create and return a fixed icon image URL.  The given URL value may
   refer to a nonexistent PNG file rather than the actual TIFF file on
   OS X 10.10.  Return NULL if it cannot (or need not) be fixed.  */

static CFURLRef
mac_tis_create_fixed_icon_image_url (CFURLRef url)
{
  CFURLRef result = NULL, sansext = NULL, tiff = NULL;
  CFStringRef extension = CFURLCopyPathExtension (url);

  if (extension)
    {
      if (CFEqual (extension, CFSTR ("png"))
	  && !CFURLResourceIsReachable (url, NULL))
	sansext = CFURLCreateCopyDeletingPathExtension (NULL, url);
      CFRelease (extension);
    }
  if (sansext)
    {
      tiff = CFURLCreateCopyAppendingPathExtension (NULL, sansext,
						    CFSTR ("tiff"));
      CFRelease (sansext);
    }
  if (tiff)
    {
      if (CFURLResourceIsReachable (tiff, NULL))
	result = tiff;
      else
	CFRelease (tiff);
    }

  return result;
}

/* Return a Lisp representation of the input souce SOURCE, optionally
   with its properties if FORMAT is non-nil.  */

static Lisp_Object
mac_input_source_properties (TISInputSourceRef source, Lisp_Object format)
{
  struct {
    CFStringRef cf;
    Lisp_Object sym;
  } keys[] = {
    {kTISPropertyInputSourceCategory,		QCcategory},
    {kTISPropertyInputSourceType,		QCtype},
    {kTISPropertyInputSourceIsASCIICapable,	QCascii_capable_p},
    {kTISPropertyInputSourceIsEnableCapable,	QCenable_capable_p},
    {kTISPropertyInputSourceIsSelectCapable,	QCselect_capable_p},
    {kTISPropertyInputSourceIsEnabled,		QCenabled_p},
    {kTISPropertyInputSourceIsSelected,		QCselected_p},
    /* kTISPropertyInputSourceID (used as the main key) */
    {kTISPropertyBundleID,			QCbundle_id},
    {kTISPropertyInputModeID,			QCinput_mode_id},
    {kTISPropertyLocalizedName,			QClocalized_name},
    {kTISPropertyInputSourceLanguages,		QClanguages},
    /* kTISPropertyUnicodeKeyLayoutData (unused) */
    /* kTISPropertyIconRef (unused) */
    /* kTISPropertyIconImageURL (handled separately) */
  };
  Lisp_Object result = Qnil;
  CFStringRef __block source_id;

  mac_within_gui (^{
      source_id = TISGetInputSourceProperty (source, kTISPropertyInputSourceID);
    });
  if (source_id)
    {
      result = cfstring_to_lisp (source_id);
      int i;

      if (!NILP (format))
	{
	  Lisp_Object plist = Qnil;

	  if (EQ (format, Qt)
	      || (SYMBOLP (format) ? EQ (format, QCicon_image_file)
		  : !NILP (Fmemq (QCicon_image_file, format))))
	    {
	      CFURLRef __block url;

	      mac_within_gui (^{
		  url = TISGetInputSourceProperty (source,
						   kTISPropertyIconImageURL);
		});
	      if (url)
		{
		  CFStringRef str = NULL;

		  /* Workaround for wrong icon image URL on OS X 10.10. */
		  if (mac_operating_system_version.major == 10
		      && mac_operating_system_version.minor == 10)
		    {
		      CFURLRef fixed =
			mac_tis_create_fixed_icon_image_url (url);

		      if (fixed)
			{
			  CFRelease (url);
			  url = fixed;
			}
		    }

		  url = CFURLCopyAbsoluteURL (url);
		  if (url)
		    {
		      str = CFURLCopyFileSystemPath (url, kCFURLPOSIXPathStyle);
		      CFRelease (url);
		    }
		  if (str)
		    {
		      plist = Fcons (QCicon_image_file,
				     Fcons (cfstring_to_lisp (str), plist));
		      CFRelease (str);
		    }
		}
	    }

	  for (i = ARRAYELTS (keys); i > 0; i--)
	    if (EQ (format, Qt)
		|| (SYMBOLP (format) ? EQ (format, keys[i-1].sym)
		    : !NILP (Fmemq (keys[i-1].sym, format))))
	      {
		CFStringRef key = keys[i-1].cf;
		CFTypeRef __block value;

		mac_within_gui (^{
		    value = TISGetInputSourceProperty (source, key);
		  });
		if (value)
		  plist = Fcons (keys[i-1].sym,
				 Fcons (cfobject_to_lisp (value, 0, -1),
					plist));
	      }

	  if (!EQ (format, Qt) && SYMBOLP (format) && CONSP (plist))
	    plist = XCAR (XCDR (plist));
	  result = Fcons (result, plist);
	}
    }

  return result;
}

DEFUN ("mac-input-source", Fmac_input_source, Smac_input_source, 0, 2, 0,
       doc: /* Return ID optionally with properties of input source SOURCE.
Optional 1st arg SOURCE specifies an input source.  It can be a symbol
or a string.  If it is a symbol, it has the following meaning:

nil or `keyboard'
    The currently-selected keyboard input source.
`keyboard-layout'
    The keyboard layout currently being used.
`ascii-capable-keyboard'
    The most-recently-used ASCII-capable keyboard input source.
`ascii-capable-keyboard-layout'
    The most-recently-used ASCII-capable keyboard layout.
`keyboard-layout-override'
    Currently-selected input method's keyboard layout override.
    This may return nil.
t
    The input source that should be used to input the language of the
    current user setting.  This may return nil.

If SOURCE is a string, it is interpreted as either an input source ID,
which should be an element of the result of `(mac-input-source-list
t)', or a language code in the BCP 47 format.  Return nil if the
specified input source ID does not exist or no enabled input source is
available for the specified language.

Optional 2nd arg FORMAT must be a symbol or a list of symbols, and
controls the format of the result.

If FORMAT is nil or unspecified, then the result is a string of input
source ID, which is the unique reverse DNS name associated with the
input source.

If FORMAT is t, then the result is a cons (ID . PLIST) of an input
source ID string and a property list containing the following names
and values:

`:category'
    The category of input source.  The possible values are
    "TISCategoryKeyboardInputSource", "TISCategoryPaletteInputSource",
    and "TISCategoryInkInputSource".
`:type'
    The specific type of input source.  The possible values are
    "TISTypeKeyboardLayout", "TISTypeKeyboardInputMethodWithoutModes",
    "TISTypeKeyboardInputMethodModeEnabled",
    "TISTypeKeyboardInputMode", "TISTypeCharacterPalette",
    "TISTypeKeyboardViewer", and "TISTypeInk".
`:ascii-capable-p'
    Whether the input source identifies itself as ASCII-capable.
`:enable-capable-p'
    Whether the input source can ever be programmatically enabled.
`:select-capable-p'
    Whether the input source can ever be programmatically selected.
`:enabled-p'
    Whether the input source is currently enabled.
`:selected-p'
    Whether the input source is currently selected.
`:bundle-id'
    The reverse DNS BundleID associated with the input source.
`:input-mode-id'
    A particular usage class for input modes.
`:localized-name'
    The localized name for UI purposes.
`:languages'
    Codes for languages that can be input using the input source.
    Languages codes are in the BCP 47 format.  The first element is
    the language for which the input source is intended.
`:icon-image-file' (optional)
    The file containing the image to be used as the input source icon.

The value corresponding to a name ending with "-p" is nil or t.  The
value for `:languages' is a vector of strings.  The other values are
strings.

If FORMAT is a list of symbols, then it is interpreted as a list of
properties above.  The result is a cons (ID . PLIST) as in the case of
t, but PLIST only contains the properties given in FORMAT.

If FORMAT is a symbol, then it is interpreted as a property above and
the result is a cons (ID . VALUE) of an input source ID string and a
value corresponding to the property.  */)
  (Lisp_Object source, Lisp_Object format)
{
  Lisp_Object result = Qnil;
  TISInputSourceRef input_source;

  check_window_system (NULL);
  mac_check_input_source (source, false);
  if (!(SYMBOLP (format) || mac_symbol_list_p (format)))
    error ("FORMAT must be a symbol or a list of symbols");

  block_input ();
  input_source = mac_create_input_source_from_lisp (source);
  if (input_source)
    {
      result = mac_input_source_properties (input_source, format);
      CFRelease (input_source);
    }
  unblock_input ();

  return result;
}

DEFUN ("mac-input-source-list", Fmac_input_source_list, Smac_input_source_list, 0, 2, 0,
       doc: /* Return a list of input sources.
If optional 1st arg TYPE is nil or unspecified, then all enabled input
sources are listed.  If TYPE is `ascii-capable-keyboard', then all
ASCII compatible enabled input sources are listed.  If TYPE is t, then
all installed input sources, whether enabled or not, are listed, but
this can have significant memory impact.

Optional 2nd arg FORMAT must be a symbol or a list of symbols, and
controls the format of the result.  See `mac-input-source' for their
meanings.  */)
  (Lisp_Object type, Lisp_Object format)
{
  Lisp_Object result = Qnil;
  CFArrayRef __block list = NULL;

  check_window_system (NULL);
  if (!(NILP (type) || EQ (type, Qt) || EQ (type, Qascii_capable_keyboard)))
    error ("TYPE must be nil, t, or `ascii-capable-keyboard'");
  if (!(SYMBOLP (format) || mac_symbol_list_p (format)))
    error ("FORMAT must be a symbol or a list of symbols");

  block_input ();
  mac_within_gui (^{
      if (EQ (type, Qascii_capable_keyboard))
	list = TISCreateASCIICapableInputSourceList ();
      else
	list = TISCreateInputSourceList (NULL, !NILP (type));
    });
  if (list)
    {
      CFIndex index, count = CFArrayGetCount (list);

      for (index = 0; index < count; index++)
	{
	  Lisp_Object properties =
	    mac_input_source_properties (((TISInputSourceRef)
					  CFArrayGetValueAtIndex (list, index)),
					 format);

	  result = Fcons (properties, result);
	}
      CFRelease (list);
    }
  unblock_input ();

  return result;
}

DEFUN ("mac-select-input-source", Fmac_select_input_source, Smac_select_input_source, 1, 2, 0,
       doc: /* Select the input source SOURCE.
SOURCE is either a symbol or a string (see `mac-input-source').
Specifying nil results in re-selecting the current keyboard input
source and thus that is not meaningful.  So, unlike
`mac-input-source', SOURCE is not optional.

If optional 2nd arg SET-KEYBOARD-LAYOUT-OVERRIDE-P is non-nil, then
SOURCE is set as the keyboard layout override rather than the new
current keyboard input source.

Return t if SOURCE could be successfully selected.  Otherwise, return
nil.  */)
  (Lisp_Object source, Lisp_Object set_keyboard_layout_override_p)
{
  Lisp_Object __block result = Qnil;
  TISInputSourceRef input_source;

  check_window_system (NULL);
  mac_check_input_source (source, false);

  block_input ();
  input_source = mac_create_input_source_from_lisp (source);
  if (input_source)
    mac_within_gui (^{
	if (NILP (set_keyboard_layout_override_p))
	  {
	    if (TISSelectInputSource (input_source) == noErr)
	      result = Qt;
	  }
	else
	  {
	    if (TISSetInputMethodKeyboardLayoutOverride (input_source) == noErr)
	      result = Qt;
	  }
	CFRelease (input_source);
      });
  unblock_input ();

  return result;
}

DEFUN ("mac-deselect-input-source", Fmac_deselect_input_source, Smac_deselect_input_source, 1, 1, 0,
       doc: /* Deselect the input source SOURCE.
This function is only intended for use with palette or ink input
sources; calling it has no effect on other input sources.  So, unlike
`mac-select-input-source', specifying a symbolic SOURCE other than t
causes an error.  SOURCE must be t or a string, and cannot be omitted.

Return t if SOURCE could be successfully deselected.  Otherwise,
return nil.  */)
  (Lisp_Object source)
{
  Lisp_Object __block result = Qnil;
  TISInputSourceRef input_source;

  check_window_system (NULL);
  mac_check_input_source (source, true);

  block_input ();
  input_source = mac_create_input_source_from_lisp (source);
  if (input_source)
    mac_within_gui (^{
	if (TISDeselectInputSource (input_source) == noErr)
	  result = Qt;
	CFRelease (input_source);
      });
  unblock_input ();

  return result;
}


/***********************************************************************
			     Application
 ***********************************************************************/

DEFUN ("mac-application-state", Fmac_application_state, Smac_application_state, 0, 0, 0,
       doc: /* Return the current state of Emacs as a GUI application.
The result is a property list containing the following names and values:

`:active-p'
    Non-nil means the application is active.
`:hidden-p'
    Non-nil means the application is hidden.
`:appearance' (only on macOS 10.14 and later)
    String representing the global appearance.
    Examples: \"NSAppearanceNameAqua\" and \"NSAppearanceNameDarkAqua\".

If Emacs is not running as a GUI application, then the result is nil.  */)
  (void)
{
  Lisp_Object result;

  block_input ();
  result = mac_application_state ();
  unblock_input ();

  return result;
}

DEFUN ("mac-send-action", Fmac_send_action, Smac_send_action, 1, 2, 0,
       doc: /* Send ACTION using the responder chain for action messages.
ACTION is a symbol whose name is a Cocoa action message without the
trailing colon.  Some useful examples are `zoom', `hide', `unhide',
`activate', `hideOtherApplications', `unhideAllApplications', and
`orderFrontCharacterPalette'.

Return t if the action is successfully sent, and nil otherwise.  If
optional DRY-RUN-P is non-nil, then check if ACTION will actually be
sent.  It will be useful in the `:enable' property of a menu item.  */)
  (Lisp_Object action, Lisp_Object dry_run_p)
{
  bool sent_p;

  check_window_system (NULL);
  CHECK_SYMBOL (action);

  block_input ();
  sent_p = mac_send_action (action, !NILP (dry_run_p));
  unblock_input ();

  return sent_p ? Qt : Qnil;
}


/***********************************************************************
			      Tab Group
 ***********************************************************************/

DEFUN ("mac-set-frame-tab-group-property", Fmac_set_frame_tab_group_property, Smac_set_frame_tab_group_property, 3, 3, 0,
       doc: /* Set the value of property PROP of the tab group for FRAME to VALUE.
FRAME nil means use the selected frame.  FRAME should be visible.
PROP should be a non-nil symbol listed in the documentation of the
function `mac-frame-tab-group-property'.  Below are additional notes:

`:frames'
    VALUE must be a list of visible frames.  If there are frames
    belonging to the tab group for FRAME but are not elements in
    VALUE, then a new GUI window will be poped up for them to
    constitute a new tab group.  The frame selected in the tab group
    for FRAME before the call will also be selected after the call.
`:selected-frame'
    VALUE must be a frame belonging to the tab group for FRAME.

Return nil if the value of the property is already the same as VALUE.
Return t if the value was changed without error.  */)
  (Lisp_Object frame, Lisp_Object prop, Lisp_Object value)
{
  struct frame *f = decode_window_system_frame (frame);
  Lisp_Object result, selected;
  struct {
    Lisp_Object prop;
    Lisp_Object (*func) (struct frame *, Lisp_Object);
  } setters[] = {
    {QCframes, mac_set_tab_group_frames},
    {QCselected_frame, mac_set_tab_group_selected_frame},
    {QCtab_bar_visible_p, mac_set_tab_group_tab_bar_visible_p},
    {QCoverview_visible_p, mac_set_tab_group_overview_visible_p}
  };
  int i;

  CHECK_SYMBOL (prop);

  block_input ();
  selected = mac_get_tab_group_selected_frame (f);
  unblock_input ();

  if (NILP (selected))
    error ("Tabbing is not supported on this macOS version or frame");

  for (i = 0; i < ARRAYELTS (setters); i++)
    if (EQ (setters[i].prop, prop))
      {
	if (!FRAME_VISIBLE_P (f))
	  error ("Frame should be visible to set tab group property `%s'",
		 SDATA (SYMBOL_NAME (prop)));

	block_input ();
	result = setters[i].func (f, value);
	unblock_input ();

	break;
      }

  if (!(i < ARRAYELTS (setters)))
    error ("Invalid tab group property: %s", SDATA (SYMBOL_NAME (prop)));
  if (STRINGP (result))
    xsignal1 (Qerror, result);

  return result;
}

DEFUN ("mac-frame-tab-group-property", Fmac_frame_tab_group_property, Smac_frame_tab_group_property, 0, 2, 0,
       doc: /* Return the value of property PROP of the tab group for FRAME.
FRAME nil or omitted means use the selected frame.
PROP should be nil or one of the following symbols:

`:frames'
    The entire group (stack) of frames that are all visually shown
    together in one virtual tabbed frame and associated with the tab
    group that FRAME belongs to.  The value is a list of frames,
    ordered in the same order as the tabs visually shown (leading to
    trailing).  It is nil if FRAME or the operating system does not
    support tabbing.
`:selected-frame'
    The current frame that is selected.  The value is nil if FRAME or
    the operating system does not support tabbing.
`:tab-bar-visible-p'
    Whether the tab bar is visible or not.
`:overview-visible-p'
    Whether the Tab Picker / Tab Overview UI is visible or not.

If PROP is nil, then return the property list containing all of the
above properties instead of just a property value.  */)
  (Lisp_Object frame, Lisp_Object prop)
{
  struct frame *f = decode_window_system_frame (frame);
  Lisp_Object result = Qnil;
  struct {
    Lisp_Object prop;
    Lisp_Object (*func) (struct frame *);
  } getters[] = {
    {QCframes, mac_get_tab_group_frames},
    {QCselected_frame, mac_get_tab_group_selected_frame},
    {QCtab_bar_visible_p, mac_get_tab_group_tab_bar_visible_p},
    {QCoverview_visible_p, mac_get_tab_group_overview_visible_p}
  };
  int i;

  CHECK_SYMBOL (prop);

  block_input ();
  for (i = 0; i < ARRAYELTS (getters); i++)
    if (NILP (prop))
      result = Fcons (getters[i].prop, Fcons (getters[i].func (f), result));
    else if (EQ (getters[i].prop, prop))
      {
	result = getters[i].func (f);
	break;
      }
  unblock_input ();

  if (!(NILP (prop) || i < ARRAYELTS (getters)))
    error ("Invalid tab group property: %s", SDATA (SYMBOL_NAME (prop)));

  return result;
}


/***********************************************************************
			      Animation
 ***********************************************************************/

DEFUN ("mac-start-animation", Fmac_start_animation, Smac_start_animation, 1, MANY, 0,
       doc: /* Start animation effect for FRAME-OR-WINDOW.
The current display contents of FRAME-OR-WINDOW (the selected frame if
nil) is captured and its actual animation effect will begin when the
event is read from the window system next time.  The animation is
processed asynchronously, and the drawing is done in a special window
overlaid on the ordinary one so the contents of the ordinary window
can be seen through the transparent part of the overlay window.

PROPERTIES is a property list consisting of the followings:

  Name	        Value           Meaning
  -------------------------------------------------------------------
  :type         symbol (`fade-out', 'fade-in', `move-out', `move-in',
                        `none', or transition filters listed below)
                                animation type
  :duration     number          animation duration in seconds
  :direction    symbol (`left', `right', `up', or `down')
                                direction for move-out, move-in or
                                some transition filters
  (other properties specific to transition filters listed below)

All the properties are optional, and ill-formed ones are silently
ignored.  If the :type property is unspecified, then the type of the
animation defaults to move-out if the :direction property is
properly specified, and fade-out otherwise.

The value for the :type property may be one of the following symbols
specifying the built-in Core Image transition filters:

`bars-swipe': Pass a bar over the source image.
  numeric properties:  :angle, :width, :bar-offset
  symbolic properties: :direction

`copy-machine': Simulate the effect of a copy machine.
  numeric properties:  :angle, :width, :opacity
  symbolic properties: :direction
  other properties:    :color

`dissolve': Use a dissolve.

`flash': Create a flash.
  numeric properties:  :max-striation-radius, :striation-strength,
                       :striation-contrast, :input-fade-threshold
  other properties:    :color

`mod': Reveal the target image through irregularly shaped holes.
  numeric properties:  :angle, :radius, :compression

`page-curl': Simulate a curling page, revealing the new image as the
             page curls.
  numeric properties:  :angle, :radius
  symbolic properties: :direction

`page-curl-with-shadow': Like `page-curl', but with shadow.
  numeric properties:  :angle, :radius, :shadow-size, :shadow-amount
  symbolic properties: :direction

`ripple': Create a circular wave that expands from the center point,
          revealing the new image in the wake of the wave.
  numeric properties: :width, :scale

`swipe': Simulate a swiping action.
  numeric properties:  :angle, :width, :opacity
  symbolic properties: :direction
  other properties:    :color

The :direction property for the transition filters is in effect only
when :angle is unspecified or ill-formed.  The :angle property is
specified in radians.  The value for the :color property is either a
string or a list of three numbers, (RED GREEN BLUE), each of which is
either an integer between 0 and 65535 inclusive (as in the result of
the function `color-values'), or a floating-point number between 0.0
and 1.0 inclusive.

These transition filters use the current display contents of
FRAME-OR-WINDOW as the source image, and the completely transparent
image as the target, so the result of display changes that follow
becomes visible gradually through the transparent part.
usage: (mac-start-animation FRAME-OR-WINDOW &rest PROPERTIES) */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  Lisp_Object frame_or_window, properties;
  struct frame *f;

  frame_or_window = args[0];
  if (NILP (frame_or_window))
    frame_or_window = selected_frame;
  if (WINDOWP (frame_or_window))
    CHECK_LIVE_WINDOW (frame_or_window);
  else
    CHECK_LIVE_FRAME (frame_or_window);

  f = (FRAMEP (frame_or_window) ? XFRAME (frame_or_window)
       : WINDOW_XFRAME (XWINDOW (frame_or_window)));
  check_window_system (f);
  properties = Flist (nargs - 1, args + 1);

  specpdl_ref count = SPECPDL_INDEX ();
  specbind (Qredisplay_dont_pause, Qt);
  redisplay_preserve_echo_area (30);
  unbind_to (count, Qnil);

  block_input ();
  mac_start_animation (frame_or_window, properties);
  unblock_input ();

  return Qt;
}


/***********************************************************************
			    Initialization
 ***********************************************************************/

/* Keep this list in the same order as frame_parms in frame.c.
   Use 0 for unsupported frame parameters.  */

frame_parm_handler mac_frame_parm_handlers[] =
{
  gui_set_autoraise,
  gui_set_autolower,
  mac_set_background_color,
  mac_set_border_color,
  gui_set_border_width,
  mac_set_cursor_color,
  mac_set_cursor_type,
  mac_set_font,
  mac_set_foreground_color,
  0, /* MAC_TODO: mac_set_icon_name, */
  0, /* MAC_TODO: mac_set_icon_type, */
  mac_set_child_frame_border_width,
  mac_set_internal_border_width,
  gui_set_right_divider_width,
  gui_set_bottom_divider_width,
  mac_set_menu_bar_lines,
  mac_set_mouse_color,
  mac_explicitly_set_name,
  gui_set_scroll_bar_width,
  gui_set_scroll_bar_height,
  mac_set_title,
  gui_set_unsplittable,
  gui_set_vertical_scroll_bars,
  gui_set_horizontal_scroll_bars,
  gui_set_visibility,
  mac_set_tab_bar_lines,
  mac_set_tool_bar_lines,
  mac_set_scroll_bar_foreground,
  mac_set_scroll_bar_background,
  gui_set_screen_gamma,
  gui_set_line_spacing,
  gui_set_left_fringe,
  gui_set_right_fringe,
  0, /* mac_set_wait_for_wm, */
  gui_set_fullscreen,
  gui_set_font_backend,
  gui_set_alpha,
  mac_set_sticky,
  0, /* mac_set_tool_bar_position, */
  mac_set_inhibit_double_buffering,
  mac_set_undecorated,
  mac_set_undecorated_round,
  mac_set_parent_frame,
  mac_set_skip_taskbar,
  mac_set_no_focus_on_map,
  mac_set_no_accept_focus,
  mac_set_z_group,
  mac_set_override_redirect,
  gui_set_no_special_glyphs,
  gui_set_alpha_background,
  NULL, /* mac_set_use_frame_synchronization */
  NULL, /* mac_set_shaded */
};

void
syms_of_macfns (void)
{
  DEFSYM (Qundefined_color, "undefined-color");
  DEFSYM (Qcancel_timer, "cancel-timer");
  DEFSYM (Qmono, "mono");
  DEFSYM (Qassq_delete_all, "assq-delete-all");
  DEFSYM (Qbacking_scale_factor, "backing-scale-factor");
#if HAVE_MAC_METAL
  DEFSYM (Qmetal_device_name, "metal-device-name");
#endif
  DEFSYM (Qfont_parameter, "font-parameter");
  DEFSYM (Qpdf, "pdf");
  DEFSYM (Qorientation, "orientation");
  DEFSYM (Qtop_margin, "top-margin");
  DEFSYM (Qbottom_margin, "bottom-margin");
  DEFSYM (Qportrait, "portrait");
  DEFSYM (Qlandscape, "landscape");
  DEFSYM (Qkeyboard, "keyboard");
  DEFSYM (Qkeyboard_layout, "keyboard-layout");
  DEFSYM (Qascii_capable_keyboard, "ascii-capable-keyboard");
  DEFSYM (Qascii_capable_keyboard_layout, "ascii-capable-keyboard-layout");
  DEFSYM (Qkeyboard_layout_override, "keyboard-layout-override");
  DEFSYM (QCascii_capable_p, ":ascii-capable-p");
  DEFSYM (QCenable_capable_p, ":enable-capable-p");
  DEFSYM (QCselect_capable_p, ":select-capable-p");
  DEFSYM (QCenabled_p, ":enabled-p");
  DEFSYM (QCselected_p, ":selected-p");
  DEFSYM (QCbundle_id, ":bundle-id");
  DEFSYM (QCinput_mode_id, ":input-mode-id");
  DEFSYM (QClocalized_name, ":localized-name");
  DEFSYM (QClanguages, ":languages");
  DEFSYM (QCicon_image_file, ":icon-image-file");
  DEFSYM (QCactive_p, ":active-p");
  DEFSYM (QChidden_p, ":hidden-p");
  DEFSYM (QCappearance, ":appearance");
  DEFSYM (QCoverview_visible_p, ":overview-visible-p");
  DEFSYM (QCtab_bar_visible_p, ":tab-bar-visible-p");
  DEFSYM (QCselected_frame, ":selected-frame");
  DEFSYM (QCframes, ":frames");
  DEFSYM (QCdirection, ":direction");
  DEFSYM (QCduration, ":duration");
  DEFSYM (Qfade_in, "fade-in");
  DEFSYM (Qmove_in, "move-in");
  DEFSYM (Qbars_swipe, "bars-swipe");
  DEFSYM (Qcopy_machine, "copy-machine");
  DEFSYM (Qdissolve, "dissolve");
  DEFSYM (Qflash, "flash");
  DEFSYM (Qmod, "mod");
  DEFSYM (Qpage_curl, "page-curl");
  DEFSYM (Qpage_curl_with_shadow, "page-curl-with-shadow");
  DEFSYM (Qripple, "ripple");
  DEFSYM (Qswipe, "swipe");

  DEFSYM (QXdndSelection, "XdndSelection");
  DEFSYM (QXdndActionCopy, "XdndActionCopy");
  DEFSYM (QXdndActionMove, "XdndActionMove");
  DEFSYM (QXdndActionLink, "XdndActionLink");
  DEFSYM (QXdndActionAsk, "XdndActionAsk");
  DEFSYM (QXdndActionPrivate, "XdndActionPrivate");
  DEFSYM (Qnow, "now");
  DEFSYM (Qmac_handle_drag_motion, "mac-handle-drag-motion");

  Fput (Qundefined_color, Qerror_conditions,
	pure_list (Qundefined_color, Qerror));
  Fput (Qundefined_color, Qerror_message,
	build_pure_c_string ("Undefined color"));

  DEFVAR_LISP ("x-pointer-shape", Vx_pointer_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_pointer_shape = Qnil;

#if false /* This doesn't really do anything.  */
  DEFVAR_LISP ("x-nontext-pointer-shape", Vx_nontext_pointer_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
#endif
  Vx_nontext_pointer_shape = Qnil;

  DEFVAR_LISP ("x-hourglass-pointer-shape", Vx_hourglass_pointer_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_hourglass_pointer_shape = Qnil;

#if false /* This doesn't really do anything.  */
  DEFVAR_LISP ("x-mode-pointer-shape", Vx_mode_pointer_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
#endif
  Vx_mode_pointer_shape = Qnil;

  DEFVAR_LISP ("x-sensitive-text-pointer-shape",
	       Vx_sensitive_text_pointer_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_sensitive_text_pointer_shape = Qnil;

  DEFVAR_LISP ("x-window-horizontal-drag-cursor",
	       Vx_window_horizontal_drag_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_horizontal_drag_shape = Qnil;

  DEFVAR_LISP ("x-window-vertical-drag-cursor",
	       Vx_window_vertical_drag_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_vertical_drag_shape = Qnil;

  DEFVAR_LISP ("x-window-left-edge-cursor",
	       Vx_window_left_edge_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_left_edge_shape = Qnil;

  DEFVAR_LISP ("x-window-top-left-corner-cursor",
	       Vx_window_top_left_corner_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_top_left_corner_shape = Qnil;

  DEFVAR_LISP ("x-window-top-edge-cursor",
	       Vx_window_top_edge_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_top_edge_shape = Qnil;

  DEFVAR_LISP ("x-window-top-right-corner-cursor",
	       Vx_window_top_right_corner_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_top_right_corner_shape = Qnil;

  DEFVAR_LISP ("x-window-right-edge-cursor",
	       Vx_window_right_edge_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_right_edge_shape = Qnil;

  DEFVAR_LISP ("x-window-bottom-right-corner-cursor",
	       Vx_window_bottom_right_corner_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_bottom_right_corner_shape = Qnil;

  DEFVAR_LISP ("x-window-bottom-edge-cursor",
	       Vx_window_bottom_edge_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_bottom_edge_shape = Qnil;

  DEFVAR_LISP ("x-window-bottom-left-corner-cursor",
	       Vx_window_bottom_left_corner_shape,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_window_bottom_left_corner_shape = Qnil;

  DEFVAR_LISP ("x-cursor-fore-pixel", Vx_cursor_fore_pixel,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_cursor_fore_pixel = Qnil;

  DEFVAR_LISP ("x-max-tooltip-size", Vx_max_tooltip_size,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_max_tooltip_size = Qnil;

  DEFVAR_LISP ("x-no-window-manager", Vx_no_window_manager,
	       doc: /* SKIP: real doc in xfns.c.  */);
  /* We don't have any way to find this out, so set it to nil
     and maybe the user would like to set it to t.  */
  Vx_no_window_manager = Qnil;

  DEFVAR_LISP ("x-pixel-size-width-font-regexp",
	       Vx_pixel_size_width_font_regexp,
	       doc: /* SKIP: real doc in xfns.c.  */);
  Vx_pixel_size_width_font_regexp = Qnil;

  Fprovide (Qmac, Qnil);

  /* Used by Fx_show_tip.  */
  DEFSYM (Qrun_at_time, "run-at-time");
  DEFSYM (Qx_hide_tip, "x-hide-tip");

  DEFVAR_LISP ("mac-drag-motion-function", Vmac_drag_motion_function,
    doc: /* Function called when another program drags items over Emacs.

It is called with three arguments FRAME, X, and Y, whenever the user
moves the mouse over an Emacs frame as part of a drag-and-drop
operation.  FRAME is the frame the mouse is on top of, and X and Y are
the frame-relative positions of the mouse in the X and Y axes
respectively.  */);
  Vmac_drag_motion_function = Qmac_handle_drag_motion;

  DEFVAR_LISP ("mac-carbon-version-string", Vmac_carbon_version_string,
    doc: /* Version info for Carbon API.  */);
  {
    Lisp_Object version = mac_carbon_version_string ();

    AUTO_STRING (format, "%s AppKit %g");
    if (!STRINGP (version))
      version = build_string ("Unknown");
    Vmac_carbon_version_string =
      Fpurecopy (CALLN (Fformat, format, version,
			make_float (mac_appkit_version ())));
  }

  defsubr (&Sxw_display_color_p);
  defsubr (&Sx_display_grayscale_p);
  defsubr (&Smac_color_list_alist);
  defsubr (&Sxw_color_defined_p);
  defsubr (&Sxw_color_values);
  defsubr (&Sx_server_max_request_size);
  defsubr (&Sx_server_vendor);
  defsubr (&Sx_server_version);
  defsubr (&Sx_display_pixel_width);
  defsubr (&Sx_display_pixel_height);
  defsubr (&Sx_display_mm_width);
  defsubr (&Sx_display_mm_height);
  defsubr (&Sx_display_screens);
  defsubr (&Sx_display_planes);
  defsubr (&Sx_display_color_cells);
  defsubr (&Sx_display_visual_class);
  defsubr (&Sx_display_backing_store);
  defsubr (&Sx_display_save_under);
  defsubr (&Smac_display_monitor_attributes_list);
  defsubr (&Smac_frame_geometry);
  defsubr (&Smac_frame_edges);
  defsubr (&Smac_frame_list_z_order);
  defsubr (&Smac_frame_restack);
  defsubr (&Smac_mouse_absolute_pixel_position);
  defsubr (&Smac_set_mouse_absolute_pixel_position);
  defsubr (&Sx_create_frame);
  defsubr (&Sx_open_connection);
  defsubr (&Sx_close_connection);
  defsubr (&Sx_display_list);
  defsubr (&Sx_synchronize);
  defsubr (&Sx_show_tip);
  defsubr (&Sx_hide_tip);
  defsubr (&Sx_begin_drag);

  tip_timer = Qnil;
  staticpro (&tip_timer);
  tip_frame = Qnil;
  staticpro (&tip_frame);
  tip_last_frame = Qnil;
  staticpro (&tip_last_frame);
  tip_last_string = Qnil;
  staticpro (&tip_last_string);
  tip_last_parms = Qnil;
  staticpro (&tip_last_parms);
  tip_dx = Qnil;
  staticpro (&tip_dx);
  tip_dy = Qnil;
  staticpro (&tip_dy);

  defsubr (&Sx_file_dialog);

  defsubr (&Sx_select_font);
  defsubr (&Smac_set_font_panel_visible_p);
  defsubr (&Smac_export_frames);
  defsubr (&Smac_page_setup_dialog);
  defsubr (&Smac_get_page_setup);
  defsubr (&Smac_print_frames_dialog);
  defsubr (&Smac_input_source);
  defsubr (&Smac_input_source_list);
  defsubr (&Smac_select_input_source);
  defsubr (&Smac_deselect_input_source);
  defsubr (&Smac_application_state);
  defsubr (&Smac_set_frame_tab_group_property);
  defsubr (&Smac_frame_tab_group_property);
  defsubr (&Smac_send_action);
  defsubr (&Smac_start_animation);
}
