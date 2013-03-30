/*
    This file is part of darktable,
    copyright (c) 2012 aldric renaudin.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/debug.h"
#include "control/control.h"
#include "control/conf.h"
#include "common/styles.h"
#include "develop/develop.h"
#include "develop/masks.h"
#include "develop/imageop.h"
#include "libs/lib.h"
#include "gui/draw.h"
#include "gui/gtk.h"
#include "gui/styles.h"
#include "dtgtk/button.h"

DT_MODULE(1)

static void _lib_masks_recreate_list(dt_lib_module_t *self);

typedef struct dt_lib_masks_t
{
  /* vbox with managed history items */
  GtkWidget *hbox;
  GtkWidget *bt_circle, *bt_curve;
  GtkWidget *treeview;
  GtkWidget *scroll_window;
  
  GdkPixbuf *ic_inverse, *ic_union, *ic_intersection, *ic_difference, *ic_exclusion, *ic_used;
  int gui_reset;
}
dt_lib_masks_t;


const char* name()
{
  return _("masks manager");
}

uint32_t views()
{
  return DT_VIEW_DARKROOM;
}

uint32_t container()
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position()
{
  return 10;
}

typedef enum dt_masks_tree_cols_t
{
  TREE_TEXT = 0,
  TREE_MODULE,
  TREE_GROUPID,
  TREE_FORMID,
  TREE_EDITABLE,
  TREE_IC_OP,
  TREE_IC_OP_VISIBLE,
  TREE_IC_INVERSE,
  TREE_IC_INVERSE_VISIBLE,
  TREE_IC_USED,
  TREE_IC_USED_VISIBLE,
  TREE_USED_TEXT,
  TREE_COUNT
}
dt_masks_tree_cols_t;

static void _tree_add_circle(GtkButton *button, dt_iop_module_t *module)
{
  //we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CIRCLE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = module;
  darktable.develop->form_gui->group_selected = 0;
  dt_control_queue_redraw_center();
}
static void _bt_add_circle (GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *module)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
  {
    //we unset the creation mode
    dt_masks_form_t *form = darktable.develop->form_visible;
    if (form) dt_masks_free_form(form);
    dt_masks_change_form_gui(NULL);
    dt_masks_init_formgui(darktable.develop);
    GTK_TOGGLE_BUTTON(widget)->active = FALSE;
  }
  else _tree_add_circle(NULL,NULL);
}
static void _tree_add_curve(GtkButton *button, dt_iop_module_t *module)
{
  //we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CURVE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = module;
  darktable.develop->form_gui->group_selected = 0;
  dt_control_queue_redraw_center();
}
static void _bt_add_curve (GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *module)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
  {
    //we unset the creation mode
    dt_masks_form_t *form = darktable.develop->form_visible;
    if (form) dt_masks_free_form(form);
    dt_masks_change_form_gui(NULL);
    dt_masks_init_formgui(darktable.develop);
    GTK_TOGGLE_BUTTON(widget)->active = FALSE;
  }
  else _tree_add_curve(NULL,NULL);
}
static void _tree_add_exist(GtkButton *button, dt_iop_module_t *module)
{
  if (!module) return;
  //we get the new formid
  int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"formid"));
  
  //is there already a masks group for this module ?
  int grpid = module->blend_params->mask_id;
  dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grpid);
  if (!grp)
  {
    //we create a new group
    grp = dt_masks_create(DT_MASKS_GROUP);
    snprintf(grp->name,128,"grp %s",module->name());
    darktable.develop->forms = g_list_append(darktable.develop->forms,grp);
    module->blend_params->mask_id = grpid = grp->formid;
  }
  //we add the form in this group
  dt_masks_point_group_t *grpt = malloc(sizeof(dt_masks_point_group_t));
  grpt->formid = id;
  grpt->parentid = grpid;
  grpt->state = DT_MASKS_STATE_SHOW | DT_MASKS_STATE_USE;
  if (g_list_length(grp->points)>0) grpt->state |= DT_MASKS_STATE_UNION;
  grpt->opacity = 1.0f;
  grp->points = g_list_append(grp->points,grpt);
  //we save the group
  dt_masks_write_form(grp,darktable.develop);
  
  //and we ensure that we are in edit mode
  dt_masks_set_edit_mode(module,TRUE);
  dt_dev_add_history_item(darktable.develop, module, TRUE);
  dt_masks_iop_update(module);
  dt_dev_masks_list_change(darktable.develop);
}

static void _tree_group(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  //we create the new group
  dt_masks_form_t *grp = dt_masks_create(DT_MASKS_GROUP);
  snprintf(grp->name,128,"group #%d",g_list_length(darktable.develop->forms));
  
  //we add all selected forms to this group
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));

  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv);
      int id = g_value_get_int(&gv);
      if (id > 0)
      {
        dt_masks_point_group_t *fpt = (dt_masks_point_group_t *) malloc(sizeof(dt_masks_point_group_t));
        fpt->formid = id;
        fpt->parentid = grp->formid;
        fpt->state = DT_MASKS_STATE_USE;
        grp->points = g_list_append(grp->points,fpt);
      }      
    }
    items = g_list_next(items);
  }
  
  //we add this group to the general list
  darktable.develop->forms = g_list_append(darktable.develop->forms,grp);
  
  //add we save
  dt_masks_write_forms(darktable.develop);
  _lib_masks_recreate_list(self);
  //dt_masks_change_form_gui(grp);
}

static void _set_iter_name(dt_masks_form_t *form, int state, float opacity, GtkTreeModel *model, GtkTreeIter *iter)
{
  if (!form) return;
  
  char str[256] = "";
  strcat(str,form->name);
  
  if (opacity != 1.0f)
  {
    char str2[256] = "";
    strcpy(str2,str);
    snprintf(str,256,"%s %d%%",str2,(int)(opacity*100));
  }
  strcat(str,"  ");
  GValue gv = {0,};
  g_value_init(&gv,G_TYPE_STRING);
  g_value_set_string(&gv,str);
  gtk_tree_store_set_value(GTK_TREE_STORE(model),iter,TREE_TEXT,&gv);
}

static void _tree_inverse(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  int change = 0;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);
      dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grid);
      if (grp && (grp->type & DT_MASKS_GROUP))
      {
        //we search the entry to inverse
        GList *pts = g_list_first(grp->points);
        while(pts)
        {
          dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
          if (pt->formid == id)
          {
            if (pt->state & DT_MASKS_STATE_INVERSE) pt->state &= ~DT_MASKS_STATE_INVERSE;
            else pt->state |= DT_MASKS_STATE_INVERSE;
            _set_iter_name(dt_masks_get_from_id(darktable.develop,id),pt->state,pt->opacity,model,&iter);
            change = 1;
            break;
          }
          pts = g_list_next(pts);
        }
      }
    }
    items = g_list_next(items);
  }
  
  if (change)
  {
    dt_masks_write_forms(darktable.develop);
    dt_masks_update_image(darktable.develop);
    dt_control_queue_redraw_center();
  }
}

static void _tree_intersection(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  int change = 0;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);
      dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grid);
      if (grp && (grp->type & DT_MASKS_GROUP))
      {
        //we search the entry to inverse
        GList *pts = g_list_first(grp->points);
        while(pts)
        {
          dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
          if (pt->formid == id)
          {
            if (!(pt->state & DT_MASKS_STATE_INTERSECTION))
            {
              if (pt->state & DT_MASKS_STATE_DIFFERENCE) pt->state &= ~DT_MASKS_STATE_DIFFERENCE;
              else if (pt->state & DT_MASKS_STATE_UNION) pt->state &= ~DT_MASKS_STATE_UNION;
              else if (pt->state & DT_MASKS_STATE_EXCLUSION) pt->state &= ~DT_MASKS_STATE_EXCLUSION;
              pt->state |= DT_MASKS_STATE_INTERSECTION;
              _set_iter_name(dt_masks_get_from_id(darktable.develop,id),pt->state,pt->opacity,model,&iter);
              change = 1;
            }
            break;
          }
          pts = g_list_next(pts);
        }
      }
    }
    items = g_list_next(items);
  }
  
  if (change)
  {
    dt_masks_write_forms(darktable.develop);
    dt_masks_update_image(darktable.develop);
    dt_control_queue_redraw_center();
  }
}

static void _tree_difference(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  int change = 0;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);
      dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grid);
      if (grp && (grp->type & DT_MASKS_GROUP))
      {
        //we search the entry to inverse
        GList *pts = g_list_first(grp->points);
        while(pts)
        {
          dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
          if (pt->formid == id)
          {
            if (!(pt->state & DT_MASKS_STATE_DIFFERENCE))
            {
              if (pt->state & DT_MASKS_STATE_UNION) pt->state &= ~DT_MASKS_STATE_UNION;
              else if (pt->state & DT_MASKS_STATE_INTERSECTION) pt->state &= ~DT_MASKS_STATE_INTERSECTION;
              else if (pt->state & DT_MASKS_STATE_EXCLUSION) pt->state &= ~DT_MASKS_STATE_EXCLUSION;
              pt->state |= DT_MASKS_STATE_DIFFERENCE;
              _set_iter_name(dt_masks_get_from_id(darktable.develop,id),pt->state,pt->opacity,model,&iter);
              change = 1;
            }
            break;
          }
          pts = g_list_next(pts);
        }
      }
    }
    items = g_list_next(items);
  }

  if (change)
  {
    dt_masks_write_forms(darktable.develop);
    dt_masks_update_image(darktable.develop);
    dt_control_queue_redraw_center();
  }
}

static void _tree_exclusion(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  int change = 0;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);
      dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grid);
      if (grp && (grp->type & DT_MASKS_GROUP))
      {
        //we search the entry to inverse
        GList *pts = g_list_first(grp->points);
        while(pts)
        {
          dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
          if (pt->formid == id)
          {
            if (!(pt->state & DT_MASKS_STATE_EXCLUSION))
            {
              if (pt->state & DT_MASKS_STATE_DIFFERENCE) pt->state &= ~DT_MASKS_STATE_DIFFERENCE;
              else if (pt->state & DT_MASKS_STATE_INTERSECTION) pt->state &= ~DT_MASKS_STATE_INTERSECTION;
              else if (pt->state & DT_MASKS_STATE_UNION) pt->state &= ~DT_MASKS_STATE_UNION;
              pt->state |= DT_MASKS_STATE_EXCLUSION;
              _set_iter_name(dt_masks_get_from_id(darktable.develop,id),pt->state,pt->opacity,model,&iter);
              change = 1;
            }
            break;
          }
          pts = g_list_next(pts);
        }
      }
    }
    items = g_list_next(items);
  }
  
  if (change)
  {
    dt_masks_write_forms(darktable.develop);
    dt_masks_update_image(darktable.develop);
    dt_control_queue_redraw_center();
  }
}

static void _tree_union(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  int change = 0;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);
      dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grid);
      if (grp && (grp->type & DT_MASKS_GROUP))
      {
        //we search the entry to inverse
        GList *pts = g_list_first(grp->points);
        while(pts)
        {
          dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
          if (pt->formid == id)
          {
            if (!(pt->state & DT_MASKS_STATE_UNION))
            {
              if (pt->state & DT_MASKS_STATE_DIFFERENCE) pt->state &= ~DT_MASKS_STATE_DIFFERENCE;
              else if (pt->state & DT_MASKS_STATE_INTERSECTION) pt->state &= ~DT_MASKS_STATE_INTERSECTION;
              else if (pt->state & DT_MASKS_STATE_EXCLUSION) pt->state &= ~DT_MASKS_STATE_EXCLUSION;
              pt->state |= DT_MASKS_STATE_UNION;
              _set_iter_name(dt_masks_get_from_id(darktable.develop,id),pt->state,pt->opacity,model,&iter);
              change = 1;
            }
            break;
          }
          pts = g_list_next(pts);
        }
      }
    }
    items = g_list_next(items);
  }
  
  if (change)
  {
    dt_masks_write_forms(darktable.develop);
    dt_masks_update_image(darktable.develop);
    dt_control_queue_redraw_center();
  }
}

static void _tree_moveup(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //we first discard all visible shapes
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = NULL;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  lm->gui_reset = 1;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);

      dt_masks_form_move(dt_masks_get_from_id(darktable.develop,grid),id,1);
    }
    items = g_list_next(items);
  }
  lm->gui_reset = 0;
  _lib_masks_recreate_list(self);
  dt_masks_update_image(darktable.develop);
}

static void _tree_movedown(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //we first discard all visible shapes
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = NULL;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  lm->gui_reset = 1;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);

      dt_masks_form_move(dt_masks_get_from_id(darktable.develop,grid),id,0);
    }
    items = g_list_next(items);
  }
  lm->gui_reset = 0;
  _lib_masks_recreate_list(self);
  dt_masks_update_image(darktable.develop);  
}
static void _tree_delete_shape(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  
  //we first discard all visible shapes
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = NULL;
  
  //now we go throught all selected nodes
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  dt_iop_module_t *module = NULL;
  lm->gui_reset = 1;
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv3 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
      int id = g_value_get_int(&gv3);
      GValue gv2 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_MODULE,&gv2);
      module = NULL;
      if (G_VALUE_TYPE(&gv2) == G_TYPE_POINTER) module = (dt_iop_module_t *)g_value_get_pointer(&gv2);

      dt_masks_form_remove(module,dt_masks_get_from_id(darktable.develop,grid),dt_masks_get_from_id(darktable.develop,id));
    }
    items = g_list_next(items);
  }
  lm->gui_reset = 0;
  _lib_masks_recreate_list(self);
}

static void _tree_cell_editing_started (GtkCellRenderer *cell, GtkCellEditable *editable, const gchar *path, gpointer data)
{
  dt_control_key_accelerators_off (darktable.control);
}
static void _tree_cell_edited (GtkCellRendererText *cell, gchar *path_string, gchar *new_text, dt_lib_module_t *self)
{
  dt_control_key_accelerators_on (darktable.control);
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(model,&iter,path_string)) return;
  GValue gv3 = {0,};
  gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv3);
  int id = g_value_get_int(&gv3);
  dt_masks_form_t *form = dt_masks_get_from_id(darktable.develop,id);
  if (!form) return;
     
  //first, we need to update the mask name
  
  strncpy(form->name,new_text,128);
  dt_masks_write_form(form,darktable.develop);
  
  //and we update the cell text
  _set_iter_name(form,0,1.0f,model,&iter);
}

static void _tree_selection_change (GtkTreeSelection *selection,dt_lib_masks_t *self)
{
  if (self->gui_reset) return;
  //we reset all "show mask" icon of iops
  GList *modules = g_list_first(darktable.develop->iop);
  while (modules)
  {
    dt_iop_module_t *m = (dt_iop_module_t *)modules->data;
    if ((m->flags() & IOP_FLAGS_SUPPORTS_BLENDING) && !(m->flags() & IOP_FLAGS_NO_MASKS))
    {
      dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t*)m->blend_data;
      GTK_TOGGLE_BUTTON(bd->masks_edit)->active = FALSE;
      gtk_widget_queue_draw (bd->masks_edit);
    }
    modules = g_list_next(modules);
  }
  
  //if selection empty, we hide all
  int nb = gtk_tree_selection_count_selected_rows(selection);
  if (nb == 0)
  {
    dt_masks_init_formgui(darktable.develop);
    darktable.develop->form_visible = NULL;    
    dt_control_queue_redraw_center();
    return;
  }
  
  //else, we create a new from group with the selection and display it
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(self->treeview));
  dt_masks_form_t *grp = dt_masks_create(DT_MASKS_GROUP);
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model,&iter,item))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_GROUPID,&gv);
      int grid = g_value_get_int(&gv);
      GValue gv2 = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv2);
      int id = g_value_get_int(&gv2);
      dt_masks_form_t *form = dt_masks_get_from_id(darktable.develop,id);
      if (form)
      {
        dt_masks_point_group_t *fpt = (dt_masks_point_group_t *) malloc(sizeof(dt_masks_point_group_t));
        fpt->formid = id;
        fpt->parentid = grid;
        fpt->state = DT_MASKS_STATE_USE;
        fpt->opacity = 1.0f;
        grp->points = g_list_append(grp->points,fpt);
        //we eventually set the "show masks" icon of iops
        if (nb==1 && (form->type & DT_MASKS_GROUP))
        {
          GValue gv2 = {0,};
          gtk_tree_model_get_value (model,&iter,TREE_MODULE,&gv2);
          dt_iop_module_t *module = g_value_peek_pointer(&gv2);
          if (module && (module->flags() & IOP_FLAGS_SUPPORTS_BLENDING) && !(module->flags() & IOP_FLAGS_NO_MASKS))
          {
            dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t*)module->blend_data;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bd->masks_edit),TRUE);
            gtk_widget_queue_draw (bd->masks_edit);
          }
        }
      }
    }
    items = g_list_next(items); 
  }
  dt_masks_form_t *grp2 = dt_masks_create(DT_MASKS_GROUP);
  dt_masks_group_ungroup(grp2,grp);
  free(grp);
  dt_masks_init_formgui(darktable.develop);
  darktable.develop->form_visible = grp2;    
  dt_control_queue_redraw_center();   
}

static int _tree_button_pressed (GtkWidget *treeview, GdkEventButton *event, dt_lib_module_t *self)
{
  //dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  //we first need to adjust selection
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
  
  GtkTreePath *mouse_path = NULL;
  GtkTreeIter iter;
  dt_iop_module_t *module = NULL;
  int on_row = 0;
  if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview), (gint) event->x, (gint) event->y, &mouse_path, NULL, NULL, NULL))
  {
    on_row = 1;
    //we retrive the iter and module from path
    if (gtk_tree_model_get_iter (model,&iter,mouse_path))
    {
      GValue gv = {0,};
      gtk_tree_model_get_value (model,&iter,TREE_MODULE,&gv);
      module = g_value_peek_pointer(&gv);
    }
  }
  /* single click with the right mouse button? */
  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 1)
  {
    //if click on a blank space, then deselect all
    if (!on_row)
    {
      gtk_tree_selection_unselect_all(selection);
    }
  }
  else if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
  {
    //if we are already inside the selection, no change
    if (on_row && !gtk_tree_selection_path_is_selected(selection,mouse_path))
    {
      if (!(event->state & GDK_CONTROL_MASK)) gtk_tree_selection_unselect_all(selection);
      gtk_tree_selection_select_path(selection, mouse_path);
      gtk_tree_path_free(mouse_path);
    }
    
    //and we display the context-menu
    GtkWidget *menu, *item;
    menu = gtk_menu_new();
    
    //we get all infos from selection
    int nb = gtk_tree_selection_count_selected_rows(selection);
    int from_group = 0;
    
    GtkTreePath *it0 = NULL;
    int depth=0;
    if (nb>0)
    {
      it0 = (GtkTreePath *)g_list_nth_data(gtk_tree_selection_get_selected_rows(selection,NULL),0);
      depth = gtk_tree_path_get_depth (it0);
    }
    if (depth > 1) from_group = 1;
    
    if (nb==0)
    {
      item = gtk_menu_item_new_with_label(_("add circle shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_add_circle, module);
      gtk_menu_append(menu, item);
      
      item = gtk_menu_item_new_with_label(_("add curve shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_add_curve, module);
      gtk_menu_append(menu, item);
      gtk_menu_append(menu, gtk_separator_menu_item_new());
    }
    
    if (nb==1)
    {
      //we check if the form is a group or not
      int grpid = 0;
      if (gtk_tree_model_get_iter (model,&iter,it0))
      {
        GValue gv = {0,};
        gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv);
        grpid = g_value_get_int(&gv);
      }
      dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grpid);
      if (grp && (grp->type & DT_MASKS_GROUP))
      {
        item = gtk_menu_item_new_with_label(_("add circle shape"));
        g_signal_connect(item, "activate",(GCallback) _tree_add_circle, module);
        gtk_menu_append(menu, item);
        
        item = gtk_menu_item_new_with_label(_("add curve shape"));
        g_signal_connect(item, "activate",(GCallback) _tree_add_curve, module);
        gtk_menu_append(menu, item);
        
        item = gtk_menu_item_new_with_label(_("add existing shape"));
        gtk_menu_append(menu, item);
        gtk_menu_append(menu, gtk_separator_menu_item_new());
        //existing forms
        GtkWidget *menu0 = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu0);
        GList *forms = g_list_first(darktable.develop->forms);
        while (forms)
        {
          dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
          if ((form->type & DT_MASKS_CLONE) || form->formid == grpid)
          {
            forms = g_list_next(forms);
            continue;
          }
          char str[10000] = "";
          strcat(str,form->name);
          int nbuse = 0;
          
          //we search were this form is used
          GList *modules = g_list_first(darktable.develop->iop);
          while (modules)
          {
            dt_iop_module_t *m = (dt_iop_module_t *)modules->data;
            dt_masks_form_t *grp = dt_masks_get_from_id(m->dev,m->blend_params->mask_id);
            if (grp && (grp->type & DT_MASKS_GROUP))
            {
              GList *pts = g_list_first(grp->points);
              while(pts)
              {
                dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
                if (pt->formid == form->formid)
                {
                  if (m == module)
                  {
                    nbuse = -1;
                    break;
                  }
                  if (nbuse==0) strcat(str," (");
                  strcat(str," ");
                  strcat(str,m->name());
                  nbuse++;
                }
                pts = g_list_next(pts);
              }
            }
            modules = g_list_next(modules);
          }
          if (nbuse != -1)
          {
            if (nbuse>0) strcat(str," )");
            
            //we add the menu entry
            item = gtk_menu_item_new_with_label(str);
            g_object_set_data(G_OBJECT(item), "formid", GUINT_TO_POINTER(form->formid));
            g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (_tree_add_exist), module);
            gtk_menu_append(menu0, item);
          }
      
          forms = g_list_next(forms);
        }
      }
    }
    
    if (!from_group && nb>0)
    {
      item = gtk_menu_item_new_with_label(_("delete this shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_delete_shape, self);
      gtk_menu_append(menu, item);
    }
    else if (nb>0 && depth < 3)
    {
      item = gtk_menu_item_new_with_label(_("remove from group"));
      g_signal_connect(item, "activate",(GCallback) _tree_delete_shape, self);
      gtk_menu_append(menu, item);
    }
    
    if (nb>1 && !from_group)
    {
      gtk_menu_append(menu, gtk_separator_menu_item_new());
      item = gtk_menu_item_new_with_label(_("group the forms"));
      g_signal_connect(item, "activate",(GCallback) _tree_group, self);
      gtk_menu_append(menu, item);
    }
        
    
    if (from_group && depth < 3)
    {
      gtk_menu_append(menu, gtk_separator_menu_item_new());
      item = gtk_menu_item_new_with_label(_("use inversed shape"));
      g_signal_connect(item, "activate",(GCallback) _tree_inverse, self);
      gtk_menu_append(menu, item);
      if (nb==1)
      {
        gtk_menu_append(menu, gtk_separator_menu_item_new());
        item = gtk_menu_item_new_with_label(_("mode : union"));
        g_signal_connect(item, "activate",(GCallback) _tree_union, self);
        gtk_menu_append(menu, item);
        item = gtk_menu_item_new_with_label(_("mode : intersection"));
        g_signal_connect(item, "activate",(GCallback) _tree_intersection, self);
        gtk_menu_append(menu, item);
        item = gtk_menu_item_new_with_label(_("mode : difference"));
        g_signal_connect(item, "activate",(GCallback) _tree_difference, self);
        gtk_menu_append(menu, item);
        item = gtk_menu_item_new_with_label(_("mode : exclusion"));
        g_signal_connect(item, "activate",(GCallback) _tree_exclusion, self);
        gtk_menu_append(menu, item);
      }
      gtk_menu_append(menu, gtk_separator_menu_item_new());
      item = gtk_menu_item_new_with_label(_("move up"));
      g_signal_connect(item, "activate",(GCallback) _tree_moveup, self);
      gtk_menu_append(menu, item);
      item = gtk_menu_item_new_with_label(_("move down"));
      g_signal_connect(item, "activate",(GCallback) _tree_movedown, self);
      gtk_menu_append(menu, item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,0, gdk_event_get_time((GdkEvent*)event));

    return 1;
  }

  return 0;
}

static gboolean _tree_restrict_select (GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path, gboolean path_currently_selected, gpointer data)
{
  dt_lib_masks_t *self = (dt_lib_masks_t *)data;
  if (self->gui_reset) return TRUE;
  
  //if the change is SELECT->UNSELECT no pb
  if (path_currently_selected) return TRUE;
  
  //if selection is empty, no pb
  if (gtk_tree_selection_count_selected_rows(selection) == 0) return TRUE;
  
  //now we unselect all members of selection with not the same parent node
  //idem for all those with a different depth
  int *indices = gtk_tree_path_get_indices (path);
  int depth = gtk_tree_path_get_depth (path);
  
  GList *items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
  while(items)
  {
    GtkTreePath *item = (GtkTreePath *)items->data;
    int dd = gtk_tree_path_get_depth (item);
    int *ii = gtk_tree_path_get_indices (item);
    int ok = 1;
    if (dd != depth) ok = 0;
    else if (dd==1) ok=1;
    else if (ii[dd-2] != indices[dd-2]) ok = 0;
    if (!ok)
    {
      gtk_tree_selection_unselect_path(selection,item);
      items = g_list_first(gtk_tree_selection_get_selected_rows(selection,NULL));
      continue;
    }
    items = g_list_next(items);
  }
  return TRUE;
}

static gboolean _tree_query_tooltip (GtkWidget *widget, gint x, gint y, gboolean keyboard_tip, GtkTooltip *tooltip, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
  GtkTreePath *path = NULL;
  gchar *tmp;
  gboolean show;

  char buffer[512];

  if (!gtk_tree_view_get_tooltip_context (tree_view, &x, &y, keyboard_tip, &model, &path, &iter)) return FALSE;
  
  gtk_tree_model_get (model, &iter, TREE_IC_USED_VISIBLE, &show, TREE_USED_TEXT, &tmp, -1);
  if (!show) return FALSE;
  
  g_snprintf (buffer, 511, "%s", tmp);
  gtk_tooltip_set_markup (tooltip, buffer);

  gtk_tree_view_set_tooltip_row (tree_view, tooltip, path);

  gtk_tree_path_free (path);
  g_free (tmp);

  return TRUE;
}

static int _is_form_used(int formid, dt_masks_form_t *grp, char *text)
{
  int nb = 0;
  //if searchid == 0 this mean we have to search in all forms
  if (!grp)
  {
    GList *forms = g_list_first(darktable.develop->forms);
    while (forms)
    {
      dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
      if (form->type & DT_MASKS_GROUP) nb += _is_form_used(formid,form,text);      
      forms = g_list_next(forms);
    }
  }
  else if (grp->type & DT_MASKS_GROUP)
  {
    GList *points = g_list_first(grp->points);
    while (points)
    {
      dt_masks_point_group_t *point = (dt_masks_point_group_t *)points->data;
      dt_masks_form_t *form = dt_masks_get_from_id(darktable.develop,point->formid);
      if (form)
      {
        if (point->formid == formid)
        {
          nb++;
          if (nb>1) strcat(text,"\n");
          strcat(text,grp->name);
        }
        if (form->type & DT_MASKS_GROUP) nb += _is_form_used(formid,form,text);
      }
      points = g_list_next(points);
    }
  }
  return nb;
}

static void _lib_masks_list_recurs(GtkTreeStore *treestore, GtkTreeIter *toplevel, dt_masks_form_t *form, 
                                      int grp_id, dt_iop_module_t *module, int gstate, float opacity,dt_lib_masks_t *lm)
{
  if (form->type & DT_MASKS_CLONE) return;
  //we create the text entry
  char str[256] = "";
  strcat(str,form->name);
  //we get the right pixbufs
  GdkPixbuf *icop = NULL;
  GdkPixbuf *icinv = NULL;
  GdkPixbuf *icuse = NULL;
  if (gstate & DT_MASKS_STATE_UNION) icop = lm->ic_union;
  else if (gstate & DT_MASKS_STATE_INTERSECTION) icop = lm->ic_intersection;
  else if (gstate & DT_MASKS_STATE_DIFFERENCE) icop = lm->ic_difference;
  else if (gstate & DT_MASKS_STATE_EXCLUSION) icop = lm->ic_exclusion;
  if (gstate & DT_MASKS_STATE_INVERSE) icinv = lm->ic_inverse;
  char str2[1000] = "";
  int nbuse = 0;
  if (grp_id == 0)
  {
    nbuse = _is_form_used(form->formid,NULL,str2);
    if (nbuse>0) icuse = lm->ic_used;
  }
  
  if (!(form->type & DT_MASKS_GROUP))
  {
    //we just add it to the tree
    GtkTreeIter child;
    gtk_tree_store_append(treestore, &child, toplevel);
    gtk_tree_store_set(treestore, &child, TREE_TEXT, str,TREE_MODULE,module,TREE_GROUPID,grp_id, TREE_FORMID, form->formid, 
                      TREE_EDITABLE, (grp_id==0), TREE_IC_OP, icop, TREE_IC_OP_VISIBLE, (icop != NULL), TREE_IC_INVERSE, icinv, 
                      TREE_IC_INVERSE_VISIBLE, (icinv!=NULL), TREE_IC_USED, icuse, TREE_IC_USED_VISIBLE, (nbuse>0), TREE_USED_TEXT, str2, -1);
    _set_iter_name(form,gstate,opacity,GTK_TREE_MODEL(treestore),&child);
  }
  else
  {
    //we first check if it's a "module" group or not
    if (grp_id==0 && !module)
    {
      GList *iops = g_list_first(darktable.develop->iop);
      while(iops)
      {
        dt_iop_module_t *iop = (dt_iop_module_t *)iops->data;
        if ((iop->flags() & IOP_FLAGS_SUPPORTS_BLENDING) && !(iop->flags() & IOP_FLAGS_NO_MASKS) && iop->blend_params->mask_id == form->formid)
        {
          module = iop;
          break;
        }
        iops = g_list_next(iops);
      }
    }
    
    //we add the group node to the tree
    GtkTreeIter child;
    gtk_tree_store_append(treestore, &child, toplevel);
    gtk_tree_store_set(treestore, &child, TREE_TEXT, str,TREE_MODULE,module,TREE_GROUPID,grp_id, TREE_FORMID, form->formid, 
                      TREE_EDITABLE, (grp_id==0), TREE_IC_OP, icop, TREE_IC_OP_VISIBLE, (icop != NULL), TREE_IC_INVERSE, icinv, 
                      TREE_IC_INVERSE_VISIBLE, (icinv!=NULL), TREE_IC_USED, icuse, TREE_IC_USED_VISIBLE, (nbuse>0), TREE_USED_TEXT, str2, -1);
    _set_iter_name(form,gstate,opacity,GTK_TREE_MODEL(treestore),&child);
    //we add all nodes to the tree
    GList *forms = g_list_first(form->points);
    while (forms)
    {
      dt_masks_point_group_t *grpt = (dt_masks_point_group_t *)forms->data;
      dt_masks_form_t *f = dt_masks_get_from_id(darktable.develop,grpt->formid);
      if (f) _lib_masks_list_recurs(treestore,&child,f,form->formid,module,grpt->state,grpt->opacity,lm);
      forms = g_list_next(forms);
    }
  }
}

static void _lib_masks_recreate_list(dt_lib_module_t *self)
{
  /* first destroy all buttons in list */
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  if (lm->gui_reset) return;
  
  //if (lm->treeview) gtk_widget_destroy(lm->treeview);
  //we set the add shape icons inactive
  GTK_TOGGLE_BUTTON(lm->bt_circle)->active = FALSE;
  GTK_TOGGLE_BUTTON(lm->bt_curve)->active = FALSE;
  
  GtkTreeStore *treestore;
  //we store : text ; *module ; groupid ; formid
  treestore = gtk_tree_store_new(TREE_COUNT, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_INT, G_TYPE_BOOLEAN, 
                                  GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN, GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN, GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN, G_TYPE_STRING);

  //we first add all groups
  GList *forms = g_list_first(darktable.develop->forms);
  while (forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
    if (form->type & DT_MASKS_GROUP) _lib_masks_list_recurs(treestore, NULL, form, 0,NULL,0,1.0,lm);
    forms = g_list_next(forms);
  }
  
  //and we add all forms
  forms = g_list_first(darktable.develop->forms);
  while (forms)
  {
    dt_masks_form_t *form = (dt_masks_form_t *)forms->data;
    if (!(form->type & DT_MASKS_GROUP)) _lib_masks_list_recurs(treestore, NULL, form, 0,NULL,0,1.0,lm);
    forms = g_list_next(forms);
  }

  gtk_tree_view_set_model(GTK_TREE_VIEW(lm->treeview), GTK_TREE_MODEL(treestore));
  g_object_unref(treestore);
}

static gboolean _update_foreach(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
  if (!iter) return 0;
  
  //we retrieve the ids
  GValue gv = {0,};
  gtk_tree_model_get_value (model,iter,TREE_GROUPID,&gv);
  int grid = g_value_get_int(&gv);
  GValue gv3 = {0,};
  gtk_tree_model_get_value (model,iter,TREE_FORMID,&gv3);
  int id = g_value_get_int(&gv3);
  
  //we retrieve the forms
  dt_masks_form_t *form = dt_masks_get_from_id(darktable.develop,id);
  if (!form) return 0;
  dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop,grid);
  
  //and the values
  int state = 0;
  float opacity = 1.0f;
  
  if (grp && (grp->type & DT_MASKS_GROUP))
  {
    GList *pts = g_list_first(grp->points);
    while(pts)
    {
      dt_masks_point_group_t *pt = (dt_masks_point_group_t *)pts->data;
      if (pt->formid == id)
      {
        state = pt->state;
        opacity = pt->opacity;
        break;
      }
      pts = g_list_next(pts);
    }
  }
  
  _set_iter_name(form,state,opacity,model,iter);
  return 0;
}

static void _lib_masks_update_list(dt_lib_module_t *self)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  //for each node , we refresh the string
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  gtk_tree_model_foreach(model,_update_foreach,NULL);
}

static void _lib_history_change_callback(gpointer instance, gpointer user_data)
{
  //dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  //_lib_masks_recreate_list(self);
}

static void _lib_masks_selection_change(dt_lib_module_t *self, int selectid, int throw_event)
{
  dt_lib_masks_t *lm = (dt_lib_masks_t *)self->data;
  if (!lm->treeview) return;

  //we first unselect all
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lm->treeview));
  lm->gui_reset = 1;
  gtk_tree_selection_unselect_all(selection);
  lm->gui_reset = 0;

  //we go throught all nodes
  lm->gui_reset = 1-throw_event;
  GtkTreeIter  iter;
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(lm->treeview));
  gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
  while (valid)
  {
    //we get the formid the the iter
    GValue gv = {0,};
    gtk_tree_model_get_value (model,&iter,TREE_FORMID,&gv);
    int id = g_value_get_int(&gv);
    if (id == selectid)
    {
      gtk_tree_selection_select_iter (selection,&iter);
      break;
    }
    valid = gtk_tree_model_iter_next(model, &iter);
  }
  lm->gui_reset = 0;
}

void gui_init(dt_lib_module_t *self)
{
  const int bs = 14;
  const int bs2 = 13;
  
  /* initialize ui widgets */
  dt_lib_masks_t *d = (dt_lib_masks_t *)g_malloc(sizeof(dt_lib_masks_t));
  self->data = (void *)d;
  memset(d,0,sizeof(dt_lib_masks_t));
  d->gui_reset = 0;
  
  //initialise all masks icons
  guchar* data = NULL;
  cairo_surface_t *inverse_cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bs2, bs2);
  cairo_t *inverse_cr = cairo_create(inverse_cst);
  cairo_set_source_rgb(inverse_cr, 0.7,0.7,0.7);
  dtgtk_cairo_paint_masks_inverse(inverse_cr, 0, 0, bs2,bs2, 0);
  data = cairo_image_surface_get_data(inverse_cst);
  dt_draw_cairo_to_gdk_pixbuf(data, bs2,bs2);
  d->ic_inverse = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, bs2, bs2, cairo_image_surface_get_stride(inverse_cst), NULL, NULL);
  
  cairo_surface_t *union_cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bs2, bs2);
  cairo_t *union_cr = cairo_create(union_cst);
  cairo_set_source_rgb(union_cr, 0.7,0.7,0.7);
  dtgtk_cairo_paint_masks_union(union_cr, 0, 0, bs2,bs2, 0);
  data = cairo_image_surface_get_data(union_cst);
  dt_draw_cairo_to_gdk_pixbuf(data, bs2,bs2);
  d->ic_union = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, bs2, bs2, cairo_image_surface_get_stride(union_cst), NULL, NULL);
  
  cairo_surface_t *intersection_cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bs2, bs2);
  cairo_t *intersection_cr = cairo_create(intersection_cst);
  cairo_set_source_rgb(intersection_cr, 0.7,0.7,0.7);
  dtgtk_cairo_paint_masks_intersection(intersection_cr, 0, 0, bs2,bs2, 0);
  data = cairo_image_surface_get_data(intersection_cst);
  dt_draw_cairo_to_gdk_pixbuf(data, bs2,bs2);
  d->ic_intersection = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, bs2, bs2, cairo_image_surface_get_stride(intersection_cst), NULL, NULL);
  
  cairo_surface_t *difference_cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bs2, bs2);
  cairo_t *difference_cr = cairo_create(difference_cst);
  cairo_set_source_rgb(difference_cr, 0.7,0.7,0.7);
  dtgtk_cairo_paint_masks_difference(difference_cr, 0, 0, bs2,bs2, 0);
  data = cairo_image_surface_get_data(difference_cst);
  dt_draw_cairo_to_gdk_pixbuf(data, bs2,bs2);
  d->ic_difference = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, bs2, bs2, cairo_image_surface_get_stride(difference_cst), NULL, NULL);
  
  cairo_surface_t *exclusion_cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bs2, bs2);
  cairo_t *exclusion_cr = cairo_create(exclusion_cst);
  cairo_set_source_rgb(exclusion_cr, 0.7,0.7,0.7);
  dtgtk_cairo_paint_masks_exclusion(exclusion_cr, 0, 0, bs2,bs2, 0);
  data = cairo_image_surface_get_data(exclusion_cst);
  dt_draw_cairo_to_gdk_pixbuf(data, bs2,bs2);
  d->ic_exclusion = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, bs2, bs2, cairo_image_surface_get_stride(exclusion_cst), NULL, NULL);
  
  cairo_surface_t *used_cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bs2, bs2);
  cairo_t *used_cr = cairo_create(used_cst);
  cairo_set_source_rgb(used_cr, 0.7,0.7,0.7);
  dtgtk_cairo_paint_masks_used(used_cr, 0, 0, bs2,bs2, 0);
  data = cairo_image_surface_get_data(used_cst);
  dt_draw_cairo_to_gdk_pixbuf(data, bs2,bs2);
  d->ic_used = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, bs2, bs2, cairo_image_surface_get_stride(used_cst), NULL, NULL);
  
  //initialise widgets
  self->widget =  gtk_vbox_new (FALSE,2);
  GtkWidget *hbox = gtk_hbox_new(FALSE,0);
  
  GtkWidget *label = gtk_label_new(_("created shapes"));
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
  
  d->bt_curve = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_curve, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
  g_signal_connect(G_OBJECT(d->bt_curve), "button-press-event", G_CALLBACK(_bt_add_curve), NULL);
  g_object_set(G_OBJECT(d->bt_curve), "tooltip-text", _("add curve shape"), (char *)NULL);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->bt_curve), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(d->bt_curve),bs,bs);
  gtk_box_pack_end (GTK_BOX (hbox),d->bt_curve,FALSE,FALSE,0);
  
  d->bt_circle = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_circle, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
  g_signal_connect(G_OBJECT(d->bt_circle), "button-press-event", G_CALLBACK(_bt_add_circle), NULL);
  g_object_set(G_OBJECT(d->bt_circle), "tooltip-text", _("add circular shape"), (char *)NULL);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->bt_circle), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(d->bt_circle),bs,bs);
  gtk_box_pack_end (GTK_BOX (hbox),d->bt_circle,FALSE,FALSE,0);
  
  gtk_box_pack_start (GTK_BOX (self->widget),hbox,TRUE,TRUE,0);

  d->scroll_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->scroll_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (self->widget),d->scroll_window,TRUE,TRUE,0);
  
  d->treeview = gtk_tree_view_new();
  GtkTreeViewColumn *col = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(col, "shapes");
  gtk_tree_view_append_column(GTK_TREE_VIEW(d->treeview), col);
  
  GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(col, renderer, FALSE);
  gtk_tree_view_column_set_attributes(col, renderer,"pixbuf", TREE_IC_OP, NULL);
  gtk_tree_view_column_add_attribute(col, renderer, "visible", TREE_IC_OP_VISIBLE);
  renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(col, renderer, FALSE);
  gtk_tree_view_column_set_attributes(col, renderer,"pixbuf", TREE_IC_INVERSE, NULL);
  gtk_tree_view_column_add_attribute(col, renderer, "visible", TREE_IC_INVERSE_VISIBLE);                                       
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  gtk_tree_view_column_add_attribute(col, renderer, "text", TREE_TEXT);
  gtk_tree_view_column_add_attribute(col, renderer, "editable", TREE_EDITABLE);
  g_signal_connect(renderer, "editing-started", (GCallback) _tree_cell_editing_started, self);
  g_signal_connect(renderer, "edited", (GCallback) _tree_cell_edited, self);
  renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_end(col, renderer, FALSE);
  gtk_tree_view_column_set_attributes(col, renderer,"pixbuf", TREE_IC_USED, NULL);
  gtk_tree_view_column_add_attribute(col, renderer, "visible", TREE_IC_USED_VISIBLE);
  
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(d->treeview));
  gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
  gtk_tree_selection_set_select_function(selection,_tree_restrict_select,d,NULL);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(d->treeview), FALSE);
  gtk_widget_set_size_request(d->treeview, -1, 300);
  gtk_container_add(GTK_CONTAINER(d->scroll_window), d->treeview);
  //gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(d->treeview),TREE_USED_TEXT);
  g_object_set (d->treeview, "has-tooltip", TRUE, NULL);
  g_signal_connect (d->treeview, "query-tooltip", G_CALLBACK (_tree_query_tooltip), NULL);
        
  g_signal_connect(selection, "changed", G_CALLBACK(_tree_selection_change), d);
  g_signal_connect(d->treeview, "button-press-event", (GCallback) _tree_button_pressed, self);
  
  gtk_widget_show_all (self->widget);

  /* connect to history change signal for updating the history view */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_HISTORY_CHANGE, G_CALLBACK(_lib_history_change_callback), self);

  // set proxy functions
  darktable.develop->proxy.masks.module = self;
  darktable.develop->proxy.masks.list_change = _lib_masks_recreate_list;
  darktable.develop->proxy.masks.list_update = _lib_masks_update_list;
  darktable.develop->proxy.masks.selection_change = _lib_masks_selection_change;
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_history_change_callback), self);
  
  g_free(self->data);
  self->data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;