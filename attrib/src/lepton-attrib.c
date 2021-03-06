/* Lepton EDA attribute editor
 * Copyright (C) 2003-2010 Stuart D. Brorson.
 * Copyright (C) 2005-2016 gEDA Contributors
 * Copyright (C) 2017-2020 Lepton EDA Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*!
 * \file lepton-attrib.c
 */


#include <config.h>
#include <locale.h>
#include <version.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/*------------------------------------------------------------------*/
/* Includes originally from testgtksheet -- stuff needed to deal with
 * spreadsheet widget.
 *------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include <glib.h>
#include <glib-object.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

/*------------------------------------------------------------------*/
/* Gattrib specific includes -- stuff dealing with gattrib data structs.
 *------------------------------------------------------------------*/
#include <liblepton/liblepton.h>
#include "../include/struct.h"     /* typdef and struct declarations */
#include "../include/prototype.h"  /* function prototypes */
#include "../include/globals.h"

TOPLEVEL *pr_current;
SHEET_DATA *sheet_head;
GtkWidget *window;
GtkWidget *notebook;
GtkSheet **sheets;
GtkWidget *entry;
GtkWidget *label;

/*------------------------------------------------------------------*/
/*! \brief GTK callback to quit the program.
 *
 * This is called when the user quits the program using the UI. The
 * callback is attached to the GTK window_delete event in
 * x_window_init() and attached to the File->Quit menu item in
 * x_window_create_menu().  On execution, the function checks for
 * unsaved changes before calling gattrib_quit() to quit the program.
 *
 *  \return value 0 to the shell to denote a successful quit.
 */
gboolean gattrib_really_quit(void)
{
  /* Save main window's geometry:
  */
  gint x = 0;
  gint y = 0;
  gtk_window_get_position (GTK_WINDOW (window), &x, &y);

  gint width  = 0;
  gint height = 0;
  gtk_window_get_size (GTK_WINDOW (window), &width, &height);

  EdaConfig* cfg = eda_config_get_cache_context();

  eda_config_set_int (cfg, "attrib.window-geometry", "x", x);
  eda_config_set_int (cfg, "attrib.window-geometry", "y", y);
  eda_config_set_int (cfg, "attrib.window-geometry", "width", width);
  eda_config_set_int (cfg, "attrib.window-geometry", "height", height);

  eda_config_save (cfg, NULL);


  if (sheet_head->CHANGED == TRUE) {
    x_dialog_unsaved_data();
  } else {
    gattrib_quit(0);
  }
  return TRUE;
}

/*------------------------------------------------------------------*/
/*! \brief Quit the program.
 *
 *  Unconditionally quit gattrib. Flushes caches and I/O channels,
 *  calls the GTK function to quit the application then calls exit()
 *  with the appropriate return code.
 *
 *  \param return_code Value to pass to the exit() system call.
 */
gint gattrib_quit(gint return_code)
{
  /*   s_clib_cache_free(); */
  s_clib_free();
#ifdef DEBUG
  fflush(stderr);
  fflush(stdout);
  printf ("gattrib_quit: ");
  printf ("Calling gtk_main_quit().\n");
#endif
  gtk_main_quit();
  exit(return_code);
}

/*------------------------------------------------------------------*/
/*! \brief The "real" main for gattrib.
 *
 * This is the main program body for gattrib. A pointer to this
 * function is passed to scm_boot_guile() at startup.
 *
 * This function:
 * - initialises threading, if the underlying GTK library is threaded.
 *   However, gattrib itself isn't threaded.
 * - initialises libgeda;
 * - parses the command line;
 * - starts logging;
 * - parses the RC files;
 * - initialises the GTK UI;
 * - populates the spreadsheet data structure;
 * - calls gtk_main() to start the event loop.
 *
 * \param closure
 * \param argc Number of command line arguments
 * \param argv Command line arguments
 */
void gattrib_main(void *closure, int argc, char *argv[])
{
  /* TOPLEVEL *pr_current is a global */
  /* SHEET_DATA *sheet_head is a global */
  /* GtkWidget *main_window is a global */

  int argv_index;

#ifdef HAVE_GTHREAD
  /* Gattrib isn't threaded, but some of GTK's file chooser
   * backends uses threading so we need to call g_thread_init().
   * GLib requires threading be initialised before any other GLib
   * functions are called. Do it now if its not already setup.  */
  if (!g_thread_supported ())
    g_thread_init (NULL);
#endif

  /* Initialize gEDA stuff */
  liblepton_init();

  /* Note that argv_index holds index to first non-flag command line option
   * (that is, to the first file name) */
  argv_index = parse_commandline(argc, argv);

  /* ----------  create log file right away ---------- */
  /* ----------  even if logging is enabled ---------- */
  s_log_init ("attrib");

  s_log_message
    (_("Lepton EDA/lepton-attrib version %1$s%2$s.%3$s git: %4$.7s"),
     PREPEND_VERSION_STRING, PACKAGE_DOTTED_VERSION,
     PACKAGE_DATE_VERSION, PACKAGE_GIT_COMMIT);

  /* ---------- Start creation of new project: (TOPLEVEL *pr_current) ---------- */
  pr_current = s_toplevel_new();

  /* ----- Read in RC files.   ----- */
  g_rc_parse (pr_current, argv[0], NULL, NULL);

  i_vars_set(pr_current);

  gtk_init(&argc, &argv);

  x_window_init();

  /* ---------- Initialize SHEET_DATA data structure ---------- */
  sheet_head = s_sheet_data_new();   /* sheet_head was declared in globals.h */

  GSList *file_list = NULL;
  if (argv_index >= argc) {
     /* No files specified on the command line, pop up the File open dialog. */
     file_list = x_fileselect_open();
     if(file_list == NULL)
        exit(0);
  } else {
     /* Construct the list of filenames from the command line.
      * argv_index holds the position of the first filename  */
     while (argv_index < argc) {
        gchar *filename = f_normalize_filename(argv[argv_index], NULL);
        if (filename != NULL) {
            file_list = g_slist_append(file_list, filename);
        } else {
            fprintf(stderr, _("Couldn't find file [%1$s]\n"), argv[argv_index]);
            exit(1);
        }
        argv_index++;
     }
  }

  /* Load the files */
  if(x_fileselect_load_files(file_list) == FALSE) {
     /* just exit the program */
     exit(1);
  }

  g_slist_foreach(file_list, (GFunc)g_free, NULL);
  g_slist_free(file_list);

  gtk_main();
  exit(0);
}

/*------------------------------------------------------------------*/
/*! \brief Entry point to gattrib
 *
 * This is just a wrapper which
 * invokes the guile stuff, and points to the real main program,
 * gattrib_main().  Note that I still need some vestigial
 * guile stuff in order to read the rc files.
 *
 * \param argc Number of command line arguments
 * \param argv Command line arguments
 */
int main(int argc, char *argv[])
{

#if ENABLE_NLS
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");
  bindtextdomain("lepton-attrib", LOCALEDIR);
  textdomain("lepton-attrib");
  bind_textdomain_codeset("lepton-attrib", "UTF-8");
#endif


  set_guile_compiled_path();


  /* Initialize the Guile Scheme interpreter. This function does not
   * return but calls exit(0) on completion.
   */
  scm_boot_guile( argc, argv, gattrib_main, NULL);

  exit(0);   /* This is not real exit point.  Real exit is in gattrib_quit. */
}
