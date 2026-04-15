/* Selection processing for Emacs on macOS.
   Copyright (C) 2005-2008 Free Software Foundation, Inc.
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

#include <config.h>

#include "lisp.h"
#include "macterm.h"
#include "blockinput.h"
#include "keymap.h"
#include "termhooks.h"
#include "keyboard.h"

#define LOCAL_SELECTION(selection_symbol,dpyinfo)			\
  assq_no_quit (selection_symbol, dpyinfo->terminal->Vselection_alist)


static bool
mac_selection_owner_p (Lisp_Object selection, struct mac_display_info *dpyinfo)
{
  OSStatus err;
  Selection sel;
  Lisp_Object local_selection_data;
  bool result = false;

  local_selection_data = LOCAL_SELECTION (selection, dpyinfo);

  if (NILP (local_selection_data))
    return false;

  block_input ();

  err = mac_get_selection_from_symbol (selection, false, &sel);
  if (err == noErr && sel)
    {
      Lisp_Object ownership_info;

      ownership_info = XCAR (XCDR (XCDR (XCDR (XCDR (local_selection_data)))));
      if (!NILP (Fequal (ownership_info,
			 mac_get_selection_ownership_info (sel))))
	result = true;
    }
  else
    result = true;

  unblock_input ();

  return result;
}

/* Do protocol to assert ourself as a selection owner.
   FRAME shall be the owner; it must be a valid X frame.
   Update the Vselection_alist so that we can reply to later requests for
   our selection.  */

static void
mac_own_selection (Lisp_Object selection_name, Lisp_Object selection_value,
		   Lisp_Object frame)
{
  struct frame *f = XFRAME (frame);
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  Time timestamp = mac_system_uptime () * 1000;
  OSStatus err;
  Selection sel;
  Lisp_Object rest, handler_fn, value, target_type;

  block_input ();

  err = mac_get_selection_from_symbol (selection_name, true, &sel);
  if (err == noErr && sel)
    {
      /* Don't allow a quit within the converter.
	 When the user types C-g, he would be surprised
	 if by luck it came during a converter.  */
      specpdl_ref count = SPECPDL_INDEX ();
      specbind (Qinhibit_quit, Qt);

      for (rest = Vselection_converter_alist; CONSP (rest); rest = XCDR (rest))
	{
	  if (!(CONSP (XCAR (rest))
		&& (target_type = XCAR (XCAR (rest)),
		    SYMBOLP (target_type))
		&& mac_valid_selection_target_p (target_type)))
	    continue;

	  handler_fn = XCDR (XCAR (rest));
	  if (CONSP (handler_fn))
	    handler_fn = XCDR (handler_fn);
	  if (!SYMBOLP (handler_fn))
	    continue;

	  Lisp_Object tem = selection_value;

	  if (STRINGP (tem))
	    {
	      Lisp_Object local_value = Fget_text_property (make_fixnum (0),
							    target_type, tem);

	      if (!NILP (local_value))
		tem = local_value;
	    }

	  if (!NILP (handler_fn))
	    value = call3 (handler_fn, selection_name, target_type, tem);
	  else
	    value = Qnil;

	  if (NILP (value))
	    continue;

	  if (mac_valid_selection_value_p (value, target_type))
	    err = mac_put_selection_value (sel, target_type, value);
	  else if (CONSP (value)
		   && EQ (XCAR (value), target_type)
		   && mac_valid_selection_value_p (XCDR (value), target_type))
	    err = mac_put_selection_value (sel, target_type, XCDR (value));
	}

      unbind_to (count, Qnil);
    }

  unblock_input ();

  if (sel && err != noErr)
    error ("Can't set selection");

  /* Now update the local cache */
  {
    Lisp_Object selection_data;
    Lisp_Object ownership_info;
    Lisp_Object prev_value;

    if (sel)
      {
	block_input ();
	ownership_info = mac_get_selection_ownership_info (sel);
	unblock_input ();
      }
    else
      ownership_info = Qnil; 	/* dummy value for local-only selection */
    selection_data = list5 (selection_name, selection_value,
			    INT_TO_INTEGER (timestamp), frame, ownership_info);
    prev_value = LOCAL_SELECTION (selection_name, dpyinfo);

    tset_selection_alist
      (dpyinfo->terminal,
       Fcons (selection_data, dpyinfo->terminal->Vselection_alist));

    /* If we already owned the selection, remove the old selection
       data.  Don't use Fdelq as that may QUIT.  */
    if (!NILP (prev_value))
      {
	/* We know it's not the CAR, so it's easy.  */
	Lisp_Object rest = dpyinfo->terminal->Vselection_alist;
	for (; CONSP (rest); rest = XCDR (rest))
	  if (EQ (prev_value, CAR (XCDR (rest))))
	    {
	      XSETCDR (rest, XCDR (XCDR (rest)));
	      break;
	    }
      }
  }
}

/* Given a selection-name and desired type, look up our local copy of
   the selection value and convert it to the type.
   Return nil, a string, a vector, a symbol, an integer, or a cons
   that CONS_TO_INTEGER could plausibly handle.
   This function is used both for remote requests (LOCAL_REQUEST is zero)
   and for local mac-get-selection-internal (LOCAL_REQUEST is nonzero).

   This calls random Lisp code, and may signal or gc.  */

static Lisp_Object
mac_get_local_selection (Lisp_Object selection_symbol, Lisp_Object target_type,
			 bool local_request, struct mac_display_info *dpyinfo)
{
  Lisp_Object local_value, tem;
  Lisp_Object handler_fn, value, type, check;

  if (!mac_selection_owner_p (selection_symbol, dpyinfo))
    return Qnil;

  local_value = LOCAL_SELECTION (selection_symbol, dpyinfo);

  /* TIMESTAMP is a special case.  */
  if (EQ (target_type, QTIMESTAMP))
    {
      handler_fn = Qnil;
      value = XCAR (XCDR (XCDR (local_value)));
    }
  else
    {
      /* Don't allow a quit within the converter.
	 When the user types C-g, he would be surprised
	 if by luck it came during a converter.  */
      specpdl_ref count = SPECPDL_INDEX ();
      specbind (Qinhibit_quit, Qt);

      CHECK_SYMBOL (target_type);
      handler_fn = CDR (Fassq (target_type, Vselection_converter_alist));

      if (CONSP (handler_fn))
	handler_fn = XCDR (handler_fn);

      tem = XCAR (XCDR (local_value));

      if (STRINGP (tem))
	{
	  local_value = Fget_text_property (make_fixnum (0),
					    target_type, tem);

	  if (!NILP (local_value))
	    tem = local_value;
	}

      if (!NILP (handler_fn))
	value = call3 (handler_fn,
		       selection_symbol, (local_request ? Qnil : target_type),
		       tem);
      else
	value = Qnil;
      value = unbind_to (count, value);
    }

  if (local_request)
    return value;

  /* Make sure this value is of a type that we could transmit
     to another application.  */

  type = target_type;
  check = value;
  if (CONSP (value)
      && SYMBOLP (XCAR (value)))
    type = XCAR (value),
    check = XCDR (value);

  if (NILP (value) || mac_valid_selection_value_p (check, type))
    return value;

  signal_error ("Invalid data returned by selection-conversion function",
		list2 (handler_fn, value));
}


/* Clear all selections that were made from frame F.
   We do this when about to delete a frame.  */

void
mac_clear_frame_selections (struct frame *f)
{
  Lisp_Object frame;
  Lisp_Object rest;
  struct mac_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  struct terminal *t = dpyinfo->terminal;

  XSETFRAME (frame, f);

  /* Delete elements from the beginning of Vselection_alist.  */
  while (CONSP (t->Vselection_alist)
	 && EQ (frame, XCAR (XCDR (XCDR (XCDR (XCAR (t->Vselection_alist)))))))
    {
      /* Run the `x-lost-selection-functions' abnormal hook.  */
      Lisp_Object selection = CAR (CAR (t->Vselection_alist));

      if (mac_selection_owner_p (selection, dpyinfo))
	CALLN (Frun_hook_with_args, Qx_lost_selection_functions, selection);

      tset_selection_alist (t, XCDR (t->Vselection_alist));
    }

  /* Delete elements after the beginning of Vselection_alist.  */
  for (rest = t->Vselection_alist; CONSP (rest); rest = XCDR (rest))
    if (CONSP (XCDR (rest))
	&& EQ (frame, XCAR (XCDR (XCDR (XCDR (XCAR (XCDR (rest))))))))
      {
	Lisp_Object selection = XCAR (XCAR (XCDR (rest)));

	if (mac_selection_owner_p (selection, dpyinfo))
	  CALLN (Frun_hook_with_args, Qx_lost_selection_functions, selection);
	XSETCDR (rest, XCDR (XCDR (rest)));
	break;
      }
}

/* Do protocol to read selection-data from the server.
   Converts this to Lisp data and returns it.  */

static Lisp_Object
mac_get_foreign_selection (Lisp_Object selection_symbol,
			   Lisp_Object target_type, Lisp_Object time_stamp,
			   Lisp_Object frame)
{
  struct frame *f = XFRAME (frame);
  OSStatus err;
  Selection sel;
  Lisp_Object result = Qnil;

  if (!FRAME_LIVE_P (f))
    return Qnil;

  block_input ();

  err = mac_get_selection_from_symbol (selection_symbol, false, &sel);
  if (err == noErr && sel)
    {
      if (EQ (target_type, QTARGETS))
	{
	  AUTO_LIST1 (targets, QTARGETS);
	  result = CALLN (Fvconcat, targets,
			  mac_get_selection_target_list (sel));
	}
      else
	{
	  result = mac_get_selection_value (sel, target_type);
	  if (STRINGP (result))
	    Fput_text_property (make_fixnum (0), make_fixnum (SBYTES (result)),
				Qforeign_selection, target_type, result);
	}
    }

  unblock_input ();

  return result;
}


/* From a Lisp_Object, return a suitable frame for selection
   operations.  OBJECT may be a frame, a terminal object, or nil
   (which stands for the selected frame--or, if that is not a Mac
   frame, the first Mac display on the list).  If no suitable frame can
   be found, return NULL.  */

static struct frame *
frame_for_mac_selection (Lisp_Object object)
{
  Lisp_Object tail, frame;
  struct frame *f;

  if (NILP (object))
    {
      f = XFRAME (selected_frame);
      if (FRAME_MAC_P (f) && FRAME_LIVE_P (f))
	return f;

      FOR_EACH_FRAME (tail, frame)
	{
	  f = XFRAME (frame);
	  if (FRAME_MAC_P (f) && FRAME_LIVE_P (f))
	    return f;
	}
    }
  else if (TERMINALP (object))
    {
      struct terminal *t = decode_live_terminal (object);
      if (t->type == output_mac)
	FOR_EACH_FRAME (tail, frame)
	  {
	    f = XFRAME (frame);
	    if (FRAME_LIVE_P (f) && f->terminal == t)
	      return f;
	  }
    }
  else if (FRAMEP (object))
    {
      f = XFRAME (object);
      if (FRAME_MAC_P (f) && FRAME_LIVE_P (f))
	return f;
    }

  return NULL;
}


DEFUN ("mac-own-selection-internal", Fmac_own_selection_internal,
       Smac_own_selection_internal, 2, 3, 0,
       doc: /* Assert a selection of type SELECTION and value VALUE.
SELECTION is a symbol, typically `PRIMARY', `SECONDARY', or `CLIPBOARD'.
\(Those are literal upper-case symbol names, since that's what X expects.)
VALUE is typically a string, or a cons of two markers, but may be
anything that the functions on `selection-converter-alist' know about.

FRAME should be a frame that should own the selection.  If omitted or
nil, it defaults to the selected frame.  */)
  (Lisp_Object selection, Lisp_Object value, Lisp_Object frame)
{
  if (NILP (frame)) frame = selected_frame;
  if (!FRAME_LIVE_P (XFRAME (frame)) || !FRAME_MAC_P (XFRAME (frame)))
    error ("Selection unavailable for this frame");

  CHECK_SYMBOL (selection);
  if (NILP (value)) error ("VALUE may not be nil");
  mac_own_selection (selection, value, frame);
  return value;
}


/* Request the selection value from the owner.  If we are the owner,
   simply return our selection value.  If we are not the owner, this
   will block until all of the data has arrived.  */

DEFUN ("mac-get-selection-internal", Fmac_get_selection_internal,
       Smac_get_selection_internal, 2, 4, 0,
       doc: /* Return text selected from some Mac application.
SELECTION-SYMBOL is typically `PRIMARY', `SECONDARY', or `CLIPBOARD'.
\(Those are literal upper-case symbol names, since that's what X expects.)
TARGET-TYPE is the type of data desired, typically `STRING'.

On Mac, TIME-STAMP and TERMINAL are unused.  */)
  (Lisp_Object selection_symbol, Lisp_Object target_type,
   Lisp_Object time_stamp, Lisp_Object terminal)
{
  Lisp_Object val = Qnil;
  struct frame *f = frame_for_mac_selection (terminal);

  CHECK_SYMBOL (selection_symbol);
  CHECK_SYMBOL (target_type);
  if (!f)
    error ("Selection unavailable for this frame");

  val = mac_get_local_selection (selection_symbol, target_type, true,
				 FRAME_DISPLAY_INFO (f));

  if (NILP (val) && FRAME_LIVE_P (f))
    {
      Lisp_Object frame;
      XSETFRAME (frame, f);
      return mac_get_foreign_selection (selection_symbol, target_type,
					time_stamp, frame);
    }

  if (CONSP (val) && SYMBOLP (XCAR (val)))
    {
      val = XCDR (val);
      if (CONSP (val) && NILP (XCDR (val)))
	val = XCAR (val);
    }
  return val;
}

DEFUN ("mac-disown-selection-internal", Fmac_disown_selection_internal,
       Smac_disown_selection_internal, 1, 3, 0,
       doc: /* If we own the selection SELECTION, disown it.
Disowning it means there is no such selection.

TERMINAL should be a terminal object or a frame specifying the
server to query.  If omitted or nil, that stands for the selected
frame's display, or the first available display.

On Mac, the TIME-OBJECT and TERMINAL arguments are unused.  */)
  (Lisp_Object selection, Lisp_Object time_object, Lisp_Object terminal)
{
  struct frame *f = frame_for_mac_selection (terminal);
  OSStatus err;
  Selection sel;
  Lisp_Object local_selection_data;
  struct mac_display_info *dpyinfo;
  Lisp_Object Vselection_alist;

  if (!f)
    return Qnil;

  dpyinfo = FRAME_DISPLAY_INFO (f);
  CHECK_SYMBOL (selection);

  if (!mac_selection_owner_p (selection, dpyinfo))
    return Qnil;  /* Don't disown the selection when we're not the owner.  */

  local_selection_data = LOCAL_SELECTION (selection, dpyinfo);

  /* Don't use Fdelq as that may QUIT;.  */

  Vselection_alist = dpyinfo->terminal->Vselection_alist;
  if (EQ (local_selection_data, CAR (Vselection_alist)))
    Vselection_alist = XCDR (Vselection_alist);
  else
    {
      Lisp_Object rest;
      for (rest = Vselection_alist; CONSP (rest); rest = XCDR (rest))
	if (EQ (local_selection_data, CAR (XCDR (rest))))
	  {
	    XSETCDR (rest, XCDR (XCDR (rest)));
	    break;
	  }
    }

  tset_selection_alist (dpyinfo->terminal, Vselection_alist);

  /* Run the `x-lost-selection-functions' abnormal hook.  */
  CALLN (Frun_hook_with_args, Qx_lost_selection_functions, selection);

  redisplay_preserve_echo_area (20);

  block_input ();

  err = mac_get_selection_from_symbol (selection, false, &sel);
  if (err == noErr && sel)
    mac_clear_selection (&sel);

  unblock_input ();

  return Qt;
}

DEFUN ("mac-selection-owner-p", Fmac_selection_owner_p, Smac_selection_owner_p,
       0, 2, 0,
       doc: /* Whether the current Emacs process owns the given SELECTION.
The arg should be the name of the selection in question, typically one of
the symbols `PRIMARY', `SECONDARY', or `CLIPBOARD'.
\(Those are literal upper-case symbol names, since that's what X expects.)
For convenience, the symbol nil is the same as `PRIMARY',
and t is the same as `SECONDARY'.

TERMINAL should be a terminal object or a frame specifying the
server to query.  If omitted or nil, that stands for the selected
frame's display, or the first available display.  */)
  (Lisp_Object selection, Lisp_Object terminal)
{
  struct frame *f = frame_for_mac_selection (terminal);

  CHECK_SYMBOL (selection);
  if (NILP (selection)) selection = QPRIMARY;
  if (EQ (selection, Qt)) selection = QSECONDARY;

  if (f && mac_selection_owner_p (selection, FRAME_DISPLAY_INFO (f)))
    return Qt;
  else
    return Qnil;
}

DEFUN ("mac-selection-exists-p", Fmac_selection_exists_p,
       Smac_selection_exists_p, 0, 2, 0,
       doc: /* Whether there is an owner for the given selection.
SELECTION should be the name of the selection in question, typically
one of the symbols `PRIMARY', `SECONDARY', or `CLIPBOARD' (X expects
these literal upper-case names.)  The symbol nil is the same as
`PRIMARY', and t is the same as `SECONDARY'.

TERMINAL should be a terminal object or a frame specifying the
server to query.  If omitted or nil, that stands for the selected
frame's display, or the first available display.  */)
  (Lisp_Object selection, Lisp_Object terminal)
{
  OSStatus err;
  Selection sel;
  Lisp_Object result = Qnil, rest;
  struct frame *f = frame_for_mac_selection (terminal);
  struct mac_display_info *dpyinfo;

  CHECK_SYMBOL (selection);
  if (NILP (selection)) selection = QPRIMARY;
  if (EQ (selection, Qt)) selection = QSECONDARY;

  if (!f)
    return Qnil;

  dpyinfo = FRAME_DISPLAY_INFO (f);

  if (mac_selection_owner_p (selection, dpyinfo))
    return Qt;

  block_input ();

  err = mac_get_selection_from_symbol (selection, false, &sel);
  if (err == noErr && sel)
    for (rest = Vselection_converter_alist; CONSP (rest); rest = XCDR (rest))
      {
	if (CONSP (XCAR (rest)) && SYMBOLP (XCAR (XCAR (rest)))
	    && mac_selection_has_target_p (sel, XCAR (XCAR (rest))))
	  {
	    result = Qt;
	    break;
	  }
      }

  unblock_input ();

  return result;
}


/***********************************************************************
			 Apple event support
***********************************************************************/
static bool mac_ready_for_apple_events = false;

struct suspended_ae_info
{
  double expiration_uptime;
  UInt32 suspension_id;
  AppleEvent apple_event, reply;
  struct suspended_ae_info *next;
};

/* List of apple events deferred at the startup time.  */
static struct suspended_ae_info *deferred_apple_events = NULL;

/* List of suspended apple events, in order of expiration_uptime.  */
static struct suspended_ae_info *suspended_apple_events = NULL;

static Lisp_Object
find_event_binding (Lisp_Object keymap, Lisp_Object propname,
		    UInt32 code, Lisp_Object *key)
{
  if (code == 0)
    return access_keymap (keymap, *key, 0, 1, 0);
  else
    {
      Lisp_Object __block result = Qnil;

      mac_map_keymap (keymap, false, ^(Lisp_Object key1, Lisp_Object binding1) {
	  UInt32 code1;

	  if (SYMBOLP (key1)
	      && mac_string_to_four_char_code (Fget (key1, propname), &code1)
	      && code1 == code)
	    {
	      *key = key1;
	      result = binding1;
	    }
	});

      return result;
    }
}

Lisp_Object
mac_find_apple_event_spec (AEEventClass class, AEEventID id,
			   Lisp_Object *class_key, Lisp_Object *id_key)
{
  Lisp_Object keymap, binding;

  keymap = get_keymap (Vmac_apple_event_map, 0, 0);
  if (NILP (keymap))
    return Qnil;

  binding = find_event_binding (keymap, Qmac_apple_event_class,
				class, class_key);
  keymap = get_keymap (binding, 0, 0);
  if (NILP (keymap))
    return Qnil;

  return find_event_binding (keymap, Qmac_apple_event_id, id, id_key);
}

static OSErr
defer_apple_events (const AppleEvent *apple_event, const AppleEvent *reply)
{
  OSErr err;
  struct suspended_ae_info *new;

  new = xzalloc (sizeof (struct suspended_ae_info));
  new->apple_event.descriptorType = typeNull;
  new->reply.descriptorType = typeNull;

  err = AESuspendTheCurrentEvent (apple_event);

  /* Mac OS X 10.3 Xcode manual says AESuspendTheCurrentEvent makes
     copies of the Apple event and the reply, but Mac OS X 10.4 Xcode
     manual says it doesn't.  Anyway we create copies of them and save
     them in `deferred_apple_events'.  */
  if (err == noErr)
    err = AEDuplicateDesc (apple_event, &new->apple_event);
  if (err == noErr)
    err = AEDuplicateDesc (reply, &new->reply);
  if (err == noErr)
    {
      new->next = deferred_apple_events;
      deferred_apple_events = new;
    }
  else
    {
      AEDisposeDesc (&new->apple_event);
      AEDisposeDesc (&new->reply);
      xfree (new);
    }

  return err;
}

static OSErr
mac_handle_apple_event_1 (Lisp_Object class, Lisp_Object id,
			  const AppleEvent *apple_event, AppleEvent *reply)
{
  OSErr err;
  static UInt32 suspension_id = 0;
  struct suspended_ae_info *new;

  new = xzalloc (sizeof (struct suspended_ae_info));
  new->apple_event.descriptorType = typeNull;
  new->reply.descriptorType = typeNull;

  err = AESuspendTheCurrentEvent (apple_event);
  if (err == noErr)
    err = AEDuplicateDesc (apple_event, &new->apple_event);
  if (err == noErr)
    err = AEDuplicateDesc (reply, &new->reply);
  if (err == noErr)
    err = AEPutAttributePtr (&new->apple_event, KEY_EMACS_SUSPENSION_ID_ATTR,
			     typeUInt32, &suspension_id, sizeof (UInt32));
  if (err == noErr)
    {
      OSErr err1;
      SInt32 reply_requested;

      err1 = AEGetAttributePtr (&new->apple_event, keyReplyRequestedAttr,
				typeSInt32, NULL, &reply_requested,
				sizeof (SInt32), NULL);
      if (err1 != noErr)
	{
	  /* Emulate keyReplyRequestedAttr in older versions.  */
	  reply_requested = reply->descriptorType != typeNull;
	  err = AEPutAttributePtr (&new->apple_event, keyReplyRequestedAttr,
				   typeSInt32, &reply_requested,
				   sizeof (SInt32));
	}
    }
  if (err == noErr)
    {
      SInt32 timeout = 0;
      struct suspended_ae_info **p;

      new->suspension_id = suspension_id;
      suspension_id++;
      err = AEGetAttributePtr (apple_event, keyTimeoutAttr, typeSInt32,
			       NULL, &timeout, sizeof (SInt32), NULL);
      new->expiration_uptime = mac_system_uptime () + timeout / 60.0;

      for (p = &suspended_apple_events; *p; p = &(*p)->next)
	if ((*p)->expiration_uptime >= new->expiration_uptime)
	  break;
      new->next = *p;
      *p = new;

      mac_store_apple_event (class, id, &new->apple_event);
    }
  else
    {
      AEDisposeDesc (&new->reply);
      AEDisposeDesc (&new->apple_event);
      xfree (new);
    }

  return err;
}

pascal OSErr
mac_handle_apple_event (const AppleEvent *apple_event, AppleEvent *reply,
			SInt32 refcon)
{
  OSErr err;
  UInt32 suspension_id;
  AEEventClass event_class;
  AEEventID event_id;

  if (!mac_ready_for_apple_events)
    {
      err = defer_apple_events (apple_event, reply);
      if (err != noErr)
	return errAEEventNotHandled;
      return noErr;
    }

  err = AEGetAttributePtr (apple_event, KEY_EMACS_SUSPENSION_ID_ATTR,
			   typeUInt32, NULL,
			   &suspension_id, sizeof (UInt32), NULL);
  if (err == noErr)
    /* Previously suspended event.  Pass it to the next handler.  */
    return errAEEventNotHandled;

  err = AEGetAttributePtr (apple_event, keyEventClassAttr, typeType, NULL,
			   &event_class, sizeof (AEEventClass), NULL);
  if (err == noErr)
    err = AEGetAttributePtr (apple_event, keyEventIDAttr, typeType, NULL,
			     &event_id, sizeof (AEEventID), NULL);
  if (err == noErr)
    {
      if (event_class == 0 || event_id == 0)
	err = errAEEventNotHandled;
      else
	{
	  Lisp_Object class_key, id_key, binding;

	  binding = mac_find_apple_event_spec (event_class, event_id,
					       &class_key, &id_key);
	  if (!NILP (binding) && !EQ (binding, Qundefined))
	    {
	      if (FIXNUMP (binding))
		return XFIXNUM (binding);
	      err = mac_handle_apple_event_1 (class_key, id_key,
					      apple_event, reply);
	    }
	  else
	    err = errAEEventNotHandled;
	}
    }
  if (err == noErr)
    return noErr;
  else
    return errAEEventNotHandled;
}

static int
cleanup_suspended_apple_events (struct suspended_ae_info **head, bool all_p)
{
  double current_uptime = mac_system_uptime ();
  UInt32 nresumed = 0;
  struct suspended_ae_info *p, *next;

  for (p = *head; p; p = next)
    {
      if (!all_p && p->expiration_uptime > current_uptime)
	break;
      mac_within_gui (^{
	  AESetTheCurrentEvent (&p->apple_event);
	  AEResumeTheCurrentEvent (&p->apple_event, &p->reply,
				   (AEEventHandlerUPP) kAENoDispatch, 0);
	});
      AEDisposeDesc (&p->reply);
      AEDisposeDesc (&p->apple_event);
      nresumed++;
      next = p->next;
      xfree (p);
    }
  *head = p;

  return nresumed;
}

void
cleanup_all_suspended_apple_events (void)
{
  cleanup_suspended_apple_events (&deferred_apple_events, true);
  cleanup_suspended_apple_events (&suspended_apple_events, true);
}

static UInt32
get_suspension_id (Lisp_Object apple_event)
{
  Lisp_Object tem;

  CHECK_CONS (apple_event);
  CHECK_STRING_CAR (apple_event);
  if (SBYTES (XCAR (apple_event)) != 4
      || strcmp (SSDATA (XCAR (apple_event)), "aevt") != 0)
    error ("Not an apple event");

  tem = assq_no_quit (Qemacs_suspension_id, XCDR (apple_event));
  if (NILP (tem))
    error ("Suspension ID not available");

  tem = XCDR (tem);
  if (!(CONSP (tem)
	&& STRINGP (XCAR (tem)) && SBYTES (XCAR (tem)) == 4
	&& strcmp (SSDATA (XCAR (tem)), "magn") == 0
	&& STRINGP (XCDR (tem)) && SBYTES (XCDR (tem)) == 4))
    error ("Bad suspension ID format");

  return *((UInt32 *) SDATA (XCDR (tem)));
}


DEFUN ("mac-process-deferred-apple-events", Fmac_process_deferred_apple_events, Smac_process_deferred_apple_events, 0, 0, 0,
       doc: /* Process Apple events that are deferred at the startup time.  */)
  (void)
{
  if (mac_ready_for_apple_events)
    return Qnil;

  block_input ();
  mac_ready_for_apple_events = true;
  if (deferred_apple_events)
    {
      struct suspended_ae_info *prev, *tail, *next;

      /* `nreverse' deferred_apple_events.  */
      prev = NULL;
      for (tail = deferred_apple_events; tail; tail = next)
	{
	  next = tail->next;
	  tail->next = prev;
	  prev = tail;
	}

      /* Now `prev' points to the first cell.  */
      for (tail = prev; tail; tail = next)
	{
	  next = tail->next;
	  mac_within_gui (^{
	      AEResumeTheCurrentEvent (&tail->apple_event, &tail->reply,
				       ((AEEventHandlerUPP)
					kAEUseStandardDispatch), 0);
	    });
	  AEDisposeDesc (&tail->reply);
	  AEDisposeDesc (&tail->apple_event);
	  xfree (tail);
	}

      deferred_apple_events = NULL;
    }
  unblock_input ();

  return Qt;
}

DEFUN ("mac-cleanup-expired-apple-events", Fmac_cleanup_expired_apple_events,
       Smac_cleanup_expired_apple_events, 0, 0, 0,
       doc: /* Clean up expired Apple events.
Return the number of expired events.   */)
  (void)
{
  int nexpired;

  block_input ();
  nexpired = cleanup_suspended_apple_events (&suspended_apple_events, false);
  unblock_input ();

  return make_fixnum (nexpired);
}

DEFUN ("mac-ae-set-reply-parameter", Fmac_ae_set_reply_parameter,
       Smac_ae_set_reply_parameter, 3, 3, 0,
       doc: /* Set parameter KEYWORD to DESCRIPTOR on reply of APPLE-EVENT.
KEYWORD is a 4-byte string.  DESCRIPTOR is a Lisp representation of an
Apple event descriptor.  It has the form of (TYPE . DATA), where TYPE
is a 4-byte string.  Valid format of DATA is as follows:

  * If TYPE is "null", then DATA is nil.
  * If TYPE is "list", then DATA is a list (DESCRIPTOR1 ... DESCRIPTORn).
  * If TYPE is "reco", then DATA is a list ((KEYWORD1 . DESCRIPTOR1)
    ... (KEYWORDn . DESCRIPTORn)).
  * If TYPE is "aevt", then DATA is ignored and the descriptor is
    treated as null.
  * Otherwise, DATA is a string.

If a (sub-)descriptor is in an invalid format, it is silently treated
as null.

Return t if the parameter is successfully set.  Otherwise return nil.  */)
  (Lisp_Object apple_event, Lisp_Object keyword, Lisp_Object descriptor)
{
  Lisp_Object result = Qnil;
  UInt32 suspension_id, code;
  struct suspended_ae_info *p;

  suspension_id = get_suspension_id (apple_event);

  CHECK_STRING (keyword);
  if (!mac_string_to_four_char_code (keyword, &code))
    error ("Apple event keyword must be a 4-byte string: %s",
	   SDATA (keyword));

  block_input ();
  for (p = suspended_apple_events; p; p = p->next)
    if (p->suspension_id == suspension_id)
      break;
  if (p && p->reply.descriptorType != typeNull)
    {
      OSErr err;

      err = mac_ae_put_lisp (&p->reply, code, descriptor);
      if (err == noErr)
	result = Qt;
    }
  unblock_input ();

  return result;
}

DEFUN ("mac-resume-apple-event", Fmac_resume_apple_event,
       Smac_resume_apple_event, 1, 2, 0,
       doc: /* Resume handling of APPLE-EVENT.
Every Apple event handled by the Lisp interpreter is suspended first.
This function resumes such a suspended event either to complete Apple
event handling to give a reply, or to redispatch it to other handlers.

If optional ERROR-CODE is an integer, it specifies the error number
that is set in the reply.  If ERROR-CODE is t, the resumed event is
handled with the standard dispatching mechanism, but it is not handled
by Emacs again, thus it is redispatched to other handlers.

Return t if APPLE-EVENT is successfully resumed.  Otherwise return
nil, which means the event is already resumed or expired.  */)
  (Lisp_Object apple_event, Lisp_Object error_code)
{
  Lisp_Object result = Qnil;
  UInt32 suspension_id;
  struct suspended_ae_info **p, *ae;

  suspension_id = get_suspension_id (apple_event);

  block_input ();
  for (p = &suspended_apple_events; *p; p = &(*p)->next)
    if ((*p)->suspension_id == suspension_id)
      break;
  if (*p)
    {
      ae = *p;
      *p = (*p)->next;
      if (FIXNUMP (error_code)
	  && ae->reply.descriptorType != typeNull)
	{
	  SInt32 errn = XFIXNUM (error_code);

	  AEPutParamPtr (&ae->reply, keyErrorNumber, typeSInt32,
			 &errn, sizeof (SInt32));
	}
      mac_within_gui (^{
	  AESetTheCurrentEvent (&ae->apple_event);
	  AEResumeTheCurrentEvent (&ae->apple_event, &ae->reply,
				   (EQ (error_code, Qt)
				    ? (AEEventHandlerUPP) kAEUseStandardDispatch
				    : (AEEventHandlerUPP) kAENoDispatch), 0);
	});
      AEDisposeDesc (&ae->reply);
      AEDisposeDesc (&ae->apple_event);
      xfree (ae);
      result = Qt;
    }
  unblock_input ();

  return result;
}

DEFUN ("mac-send-apple-event-internal", Fmac_send_apple_event_internal,
       Smac_send_apple_event_internal, 1, 2, 0,
       doc: /* Send APPLE-EVENT with SEND-MODE.
This is for internal use only.  Use `mac-send-apple-event' instead.

APPLE-EVENT is a Lisp representation of an Apple event.  SEND-MODE
specifies a send mode for the Apple event.  It must be either an
integer, nil for kAENoReply, or t for kAEQueueReply.

If sent successfully, return the Lisp representation of the sent event
so the reply handler can use the value of the `return-id' attribute.
Otherwise, return the error code as an integer.  */)
  (Lisp_Object apple_event, Lisp_Object send_mode)
{
  OSStatus __block err;
  Lisp_Object result;
  AESendMode mode;
  AppleEvent event;

  CHECK_CONS (apple_event);
  CHECK_STRING_CAR (apple_event);
  if (SBYTES (XCAR (apple_event)) != 4
      || strcmp (SSDATA (XCAR (apple_event)), "aevt") != 0)
    error ("Not an apple event");

  if (NILP (send_mode))
    mode = kAENoReply;
  else if (EQ (send_mode, Qt))
    mode = kAEQueueReply;
  else
    {
      CHECK_FIXNUM (send_mode);
      mode = XFIXNUM (send_mode);
      mode &= ~kAEWaitReply;
      if ((mode & (kAENoReply | kAEQueueReply)) == 0)
	mode |= kAENoReply;
    }

  block_input ();
  err = create_apple_event_from_lisp (apple_event, &event);
  if (err == noErr)
    {
      mac_within_gui (^{
	  err = AESendMessage (&event, NULL, mode, kAEDefaultTimeout);
	});
      if (err == noErr)
	result = mac_aedesc_to_lisp (&event);
      else
	result = make_fixnum (err);
      AEDisposeDesc (&event);
    }
  else
    result = make_fixnum (err);
  unblock_input ();

  return result;
}

DEFUN ("mac-update-apple-event-map", Fmac_update_apple_event_map,
       Smac_update_apple_event_map, 0, 0, 0,
       doc: /* Register changes to the `mac-apple-event-map' keymap. */)
  (void)
{
  mac_update_apple_event_map ();
  mac_update_apple_event_handler ();
  return Qnil;
}



void
syms_of_macselect (void)
{
  defsubr (&Smac_get_selection_internal);
  defsubr (&Smac_own_selection_internal);
  defsubr (&Smac_disown_selection_internal);
  defsubr (&Smac_selection_owner_p);
  defsubr (&Smac_selection_exists_p);
  defsubr (&Smac_process_deferred_apple_events);
  defsubr (&Smac_cleanup_expired_apple_events);
  defsubr (&Smac_resume_apple_event);
  defsubr (&Smac_ae_set_reply_parameter);
  defsubr (&Smac_send_apple_event_internal);
  defsubr (&Smac_update_apple_event_map);
  
  DEFVAR_LISP ("selection-converter-alist", Vselection_converter_alist,
	       doc: /* An alist associating selection-types with functions.
These functions are called to convert the selection, with three args:
the name of the selection (typically `PRIMARY', `SECONDARY', or
`CLIPBOARD'); a desired type to which the selection should be
converted; and the local selection value (whatever was given to
`mac-own-selection').

The function should return the value to send to the Pasteboard Manager
\(must be a string).  A return value of nil
means that the conversion could not be done.  */);
  Vselection_converter_alist = Qnil;

  DEFVAR_LISP ("x-lost-selection-functions", Vx_lost_selection_functions,
	       doc: /* A list of functions to be called when Emacs loses a selection.
\(This happens when a Lisp program explicitly clears the selection.)
The functions are called with one argument, the selection type
\(a symbol, typically `PRIMARY', `SECONDARY', or `CLIPBOARD').  */);
  Vx_lost_selection_functions = Qnil;

  DEFVAR_LISP ("mac-apple-event-map", Vmac_apple_event_map,
	       doc: /* Keymap for Apple events handled by Emacs.
Runtime changes to this keymap must be registered using the function
`mac-update-apple-event-map'. */);
  Vmac_apple_event_map = Qnil;

  DEFVAR_LISP ("mac-dnd-known-types", Vmac_dnd_known_types,
	       doc: /* The types accepted by default for dropped data.
The types are chosen in the order they appear in the list.  */);
  Vmac_dnd_known_types = mac_dnd_default_known_types ();

  DEFVAR_LISP ("mac-service-selection", Vmac_service_selection,
	       doc: /* Selection name for communication via Services menu.  */);
  Vmac_service_selection = intern_c_string ("PRIMARY");

  DEFSYM (QSECONDARY, "SECONDARY");
  DEFSYM (QTIMESTAMP, "TIMESTAMP");
  DEFSYM (QTARGETS, "TARGETS");
  DEFSYM (Qforeign_selection, "foreign-selection");
  DEFSYM (Qx_lost_selection_functions, "x-lost-selection-functions");

  /* A selection name (represented as a Lisp symbol) can be associated
     with a pasteboard via `mac-pasteboard-name' property.  Likewise
     for a selection type with a pasteboard data type via
     `mac-pasteboard-data-type'.  */
  DEFSYM (Qmac_pasteboard_name, "mac-pasteboard-name");
  DEFSYM (Qmac_pasteboard_data_type, "mac-pasteboard-data-type");
  DEFSYM (Qmac_pasteboard_dnd_target_class, "mac-pasteboard-dnd-target-class");
  DEFSYM (Qmac_apple_event_class, "mac-apple-event-class");
  DEFSYM (Qmac_apple_event_id, "mac-apple-event-id");
  DEFSYM (Qemacs_suspension_id, "emacs-suspension-id");
  DEFSYM (QCactions, ":actions");
  DEFSYM (QCitems, ":items");
  DEFSYM (Qcopy, "copy");
  DEFSYM (Qlink, "link");
  DEFSYM (Qgeneric, "generic");
  DEFSYM (Qprivate, "private");
  DEFSYM (Qmove, "move");
  DEFSYM (Qdelete, "delete");
}
