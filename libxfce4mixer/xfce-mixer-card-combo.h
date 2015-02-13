/* vi:set expandtab sw=2 sts=2: */
/*-
 * Copyright (c) 2008 Jannis Pohlmann <jannis@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA 02110-1301, USA.
 */

#ifndef __XFCE_MIXER_CARD_COMBO_H__
#define __XFCE_MIXER_CARD_COMBO_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS;

typedef struct _XfceMixerCardComboClass XfceMixerCardComboClass;
typedef struct _XfceMixerCardCombo      XfceMixerCardCombo;

#define TYPE_XFCE_MIXER_CARD_COMBO            (xfce_mixer_card_combo_get_type ())
#define XFCE_MIXER_CARD_COMBO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_XFCE_MIXER_CARD_COMBO, XfceMixerCardCombo))
#define XFCE_MIXER_CARD_COMBO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_XFCE_MIXER_CARD_COMBO, XfceMixerCardComboClass))
#define IS_XFCE_MIXER_CARD_COMBO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_XFCE_MIXER_CARD_COMBO))
#define IS_XFCE_MIXER_CARD_COMBO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_XFCE_MIXER_CARD_COMBO))
#define XFCE_MIXER_CARD_COMBO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_XFCE_MIXER_CARD_COMBO, XfceMixerCardComboClass))

GType       xfce_mixer_card_combo_get_type        (void) G_GNUC_CONST;

GtkWidget  *xfce_mixer_card_combo_new             (GstElement         *element);
GstElement *xfce_mixer_card_combo_get_active_card (XfceMixerCardCombo *combo);

G_END_DECLS;

#endif /* !__XFCE_MIXER_CARD_COMBO_H__ */
