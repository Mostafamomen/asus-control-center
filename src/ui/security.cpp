/* ui/security.cpp - Security Center page. SPDX-License-Identifier: GPL-3.0-or-later
 * Dashboard + 10 sub-pages (stack). Scans run on a worker thread; UI updates
 * via g_idle_add. No check modifies the system; fix buttons are intentionally
 * absent for firewall/SELinux/Secure Boot/account settings. */
#include "security.h"
#include "mcui.h"
#include "theme.h"
#include "window.h"
#include "../security/security_engine.h"
#include <glib/gstdio.h>
#include <adwaita.h>
static GtkWidget *g_summary,*g_progress,*g_status,*g_how,*g_stack,*g_tabs;
static GtkButton *g_run,*g_cancel;
static SecReport *g_report=NULL;
static gboolean g_running=FALSE;
static gint g_cancel_flag=0; /* g_atomic_int_* accessed; GTK main thread only otherwise */
static GThread *g_thread=NULL;
static GMutex g_mtx; static GOnce g_mtx_once=G_ONCE_INIT; static gpointer mtx_init(gpointer){ g_mutex_init(&g_mtx); return NULL; }
static guint g_idle_source=0; static gboolean g_teardown=FALSE;
static void mtx_lock(void){ g_once(&g_mtx_once,mtx_init,NULL); g_mutex_lock(&g_mtx); }
static void mtx_unlock(void){ g_mutex_unlock(&g_mtx); }
static void set_cancelled(gboolean v){ g_atomic_int_set(&g_cancel_flag, v?1:0); }
static const gchar* CATS[]={"Dashboard","System Protection","Malware & Threat Detection","Firewall","Network Security","Privacy","Permissions & Access","System Integrity","Security Reports","Security History",NULL};
static const gchar* CATKEYS[]={"dash","protection","malware","firewall","network","privacy","permissions","integrity","reports","history",NULL};
static GtkWidget* cat_pages[10]={NULL};
struct Job { SecReport* rep; };
static gboolean idle_done(gpointer user){
  Job* job=(Job*)user;
  g_idle_source=0;
  if(g_teardown)
    {
      /* teardown already ran: free job payload without touching widgets */
      if(job->rep) sec_report_free(job->rep);
      g_free(job);
      return G_SOURCE_REMOVE;
    }
  mtx_lock();
  SecReport* old=g_report; g_report=job->rep; g_running=FALSE;
  mtx_unlock();
  if(old) sec_report_free(old);
  g_free(job);
  if(g_thread)
    {
      g_thread_join(g_thread);
      g_thread=NULL;
    }
  if(g_run)
    gtk_widget_set_sensitive(GTK_WIDGET(g_run),TRUE);
  if(g_cancel)
    gtk_widget_set_sensitive(GTK_WIDGET(g_cancel),FALSE);
  if(g_progress)
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_progress),1.0);
  if(g_status)
    gtk_label_set_text(GTK_LABEL(g_status),"Scan complete.");
  if(g_summary&&g_report){ { gchar* txt=g_strdup_printf("Pass %d · Warning %d · Critical %d · Not available %d · Skipped %d · Error %d", g_report->pass,g_report->warn,g_report->crit,g_report->na,g_report->skip,g_report->err); gtk_label_set_text(GTK_LABEL(g_summary),txt); g_free(txt); sec_history_append(g_report); } }
  if(g_how) gtk_label_set_text(GTK_LABEL(g_how),"Assessment = counts above (PASS/WARNING/CRITICAL/NOT AVAILABLE/SKIPPED/ERROR). Unavailable checks are never counted as passes; no numeric score is invented.");
  for(int i=0;CATS[i];i++){ /* rebuild all */ GtkWidget* old=cat_pages[i]; if(old&&gtk_widget_get_parent(old)) gtk_stack_remove(GTK_STACK(g_stack),old); cat_pages[i]=NULL; }
  /* re-add in order */
  for(int i=0;CATS[i];i++){ cat_pages[i]=NULL; }
  /* build fresh pages in order */
  for(int i=0;CATS[i];i++){
    GtkWidget* v=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_widget_set_margin_start(v,16); gtk_widget_set_margin_end(v,16); gtk_widget_set_margin_top(v,12); gtk_widget_set_margin_bottom(v,12);
    GtkWidget* sc=gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),v); gtk_widget_set_vexpand(sc,TRUE); cat_pages[i]=sc;
    if(g_strcmp0(CATS[i],"Dashboard")==0){ GtkWidget* l=mc_label("Use Run Scan above. Results appear in the other tabs.", "dim",0); gtk_box_append(GTK_BOX(v),l); }
    else if(g_strcmp0(CATS[i],"Security Reports")==0){ GtkWidget* b1=gtk_button_new_with_label("Export HTML report"); GtkWidget* b2=gtk_button_new_with_label("Export JSON report"); GtkWidget* b3=gtk_button_new_with_label("Export TXT report"); gtk_widget_add_css_class(b1,"primarybtn"); gtk_widget_add_css_class(b2,"primarybtn"); gtk_widget_add_css_class(b3,"primarybtn");
      g_signal_connect(b1,"clicked",G_CALLBACK(+[](GtkButton*b,gpointer u){(void)b;(void)u; if(!g_report) return; gchar* p=g_build_filename(g_get_user_config_dir(),"asus-control-center","security-report.html",NULL); gchar* h=sec_report_to_html(g_report); { GError* e=NULL; if(!g_file_set_contents(p,h,-1,&e)) { g_printerr("security: report write failed: %s\n", e&&e->message?e->message:"?"); g_clear_error(&e); } else if(g_chmod(p,0600)!=0) g_printerr("security: chmod 0600 failed for report\n"); } g_free(p); g_free(h); /* overwrite allowed; no secrets in report */}),NULL);
      g_signal_connect(b2,"clicked",G_CALLBACK(+[](GtkButton*b,gpointer u){(void)b;(void)u; if(!g_report) return; gchar* p=g_build_filename(g_get_user_config_dir(),"asus-control-center","security-report.json",NULL); gchar* h=sec_report_to_json(g_report); { GError* e=NULL; if(!g_file_set_contents(p,h,-1,&e)) { g_printerr("security: report write failed: %s\n", e&&e->message?e->message:"?"); g_clear_error(&e); } else if(g_chmod(p,0600)!=0) g_printerr("security: chmod 0600 failed for report\n"); } g_free(p); g_free(h); /* overwrite allowed; no secrets in report */}),NULL);
      g_signal_connect(b3,"clicked",G_CALLBACK(+[](GtkButton*b,gpointer u){(void)b;(void)u; if(!g_report) return; gchar* p=g_build_filename(g_get_user_config_dir(),"asus-control-center","security-report.txt",NULL); gchar* h=sec_report_to_text(g_report); { GError* e=NULL; if(!g_file_set_contents(p,h,-1,&e)) { g_printerr("security: report write failed: %s\n", e&&e->message?e->message:"?"); g_clear_error(&e); } else if(g_chmod(p,0600)!=0) g_printerr("security: chmod 0600 failed for report\n"); } g_free(p); g_free(h); /* overwrite allowed; no secrets in report */}),NULL);
      gtk_box_append(GTK_BOX(v),GTK_WIDGET(b1)); gtk_box_append(GTK_BOX(v),GTK_WIDGET(b2)); gtk_box_append(GTK_BOX(v),GTK_WIDGET(b3));
      GtkWidget* note=mc_label("Exports are saved under ~/.config/asus-control-center/ and contain no passwords, keys, or credentials.", "dim",0); gtk_label_set_wrap(GTK_LABEL(note),TRUE); gtk_box_append(GTK_BOX(v),note); }
    else if(g_strcmp0(CATS[i],"Security History")==0){ GPtrArray* e=sec_history_entries(); if(e->len==0) gtk_box_append(GTK_BOX(v),mc_label("No scans recorded yet.", "dim",0)); for(guint k=0;k<e->len;k++){ GtkWidget* l=mc_label((const gchar*)g_ptr_array_index(e,k),"dim",0); gtk_label_set_wrap(GTK_LABEL(l),TRUE); gtk_box_append(GTK_BOX(v),l);} if(e) g_ptr_array_free(e,TRUE); }
    else { gboolean any=FALSE; for(guint j=0;j<g_report->len;j++){ auto &r=g_report->items[j]; if(g_strcmp0(r.category,CATS[i])!=0) continue; any=TRUE; GtkWidget* row=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); gtk_widget_add_css_class(row,"card"); gtk_widget_set_margin_start(row,8); gtk_widget_set_margin_end(row,8); gtk_widget_set_margin_top(row,6); gtk_widget_set_margin_bottom(row,6); GtkWidget* h=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_box_append(GTK_BOX(h),mc_label(r.name,"cardtitle",0)); gtk_box_append(GTK_BOX(h),mc_spacer()); gtk_box_append(GTK_BOX(h),mc_label(sec_status_name(r.status),"pill",0)); gtk_box_append(GTK_BOX(row),h); GtkWidget* d=mc_label(r.detail,"dim",0); gtk_label_set_wrap(GTK_LABEL(d),TRUE); gtk_box_append(GTK_BOX(row),d); GtkWidget* rec=mc_label(g_strdup_printf("Recommendation: %s%s",r.recommendation, r.needs_admin?" (admin required; reversible where applicable)":""),"dim",0); gtk_label_set_wrap(GTK_LABEL(rec),TRUE); gtk_box_append(GTK_BOX(row),rec); gtk_box_append(GTK_BOX(v),row);} if(!any) gtk_box_append(GTK_BOX(v),mc_label("No checks in this category.", "dim",0)); }
    gtk_stack_add_named(GTK_STACK(g_stack),sc,CATKEYS[i]);
  }
  return G_SOURCE_REMOVE;
}
static gpointer worker(gpointer user){
  (void)user;
  /* engine receives the live atomic storage; it polls it between checks
   * AND inside run_argv while a subprocess runs */
  SecReport* r=sec_run_all(&g_cancel_flag);
  Job* j=g_new0(Job,1); j->rep=r; g_idle_source=g_idle_add(idle_done,j); return NULL;
}
static void run_cb(GtkButton*b,gpointer u){(void)b;(void)u; mtx_lock(); gboolean was=g_running; if(!was){ g_running=TRUE; g_teardown=FALSE; set_cancelled(FALSE); } mtx_unlock(); if(was) return; if(g_run) gtk_widget_set_sensitive(GTK_WIDGET(g_run),FALSE); if(g_cancel) gtk_widget_set_sensitive(GTK_WIDGET(g_cancel),TRUE); if(g_progress) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_progress),0.05); if(g_status) gtk_label_set_text(GTK_LABEL(g_status),"Scanning (read-only)…"); g_thread=g_thread_new("secscan",worker,NULL); }
static void cancel_cb(GtkButton*b,gpointer u){(void)b;(void)u; set_cancelled(TRUE); if(g_status) gtk_label_set_text(GTK_LABEL(g_status),"Cancelling…");}
GtkWidget* acc_page_security(AccUI* u){
  (void)u; GtkWidget* v=gtk_box_new(GTK_ORIENTATION_VERTICAL,12); gtk_widget_set_margin_start(v,20); gtk_widget_set_margin_end(v,20); gtk_widget_set_margin_top(v,16); gtk_widget_set_margin_bottom(v,16);
  GtkWidget* title=mc_title("Security Center"); gtk_box_append(GTK_BOX(v),title);
  GtkWidget* sub=mc_label("Read-only security checks. Nothing is changed automatically. No fake passes.", "dim",0); gtk_label_set_wrap(GTK_LABEL(sub),TRUE); gtk_box_append(GTK_BOX(v),sub);
  g_summary=mc_label("Not scanned yet.", "cardtitle",0); gtk_box_append(GTK_BOX(v),g_summary);
  g_how=mc_label("Assessment is calculated from result counts shown above. Unavailable checks are not passes.", "dim",0); gtk_label_set_wrap(GTK_LABEL(g_how),TRUE); gtk_box_append(GTK_BOX(v),g_how);
  g_progress=gtk_progress_bar_new(); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_progress),0.0); gtk_box_append(GTK_BOX(v),g_progress);
  g_status=mc_label("Idle.", "dim",0); gtk_box_append(GTK_BOX(v),g_status);
  GtkWidget* btns=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); g_run=GTK_BUTTON(gtk_button_new_with_label("Run Scan")); gtk_widget_add_css_class(GTK_WIDGET(g_run),"primarybtn"); g_cancel=GTK_BUTTON(gtk_button_new_with_label("Cancel")); gtk_widget_set_sensitive(GTK_WIDGET(g_cancel),FALSE); g_signal_connect(g_run,"clicked",G_CALLBACK(run_cb),NULL); g_signal_connect(g_cancel,"clicked",G_CALLBACK(cancel_cb),NULL); gtk_box_append(GTK_BOX(btns),GTK_WIDGET(g_run)); gtk_box_append(GTK_BOX(btns),GTK_WIDGET(g_cancel)); gtk_box_append(GTK_BOX(v),btns);
  g_stack=gtk_stack_new(); gtk_widget_set_vexpand(g_stack,TRUE); gtk_box_append(GTK_BOX(v),g_stack);
  g_tabs=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
  GtkWidget* tabscroll=gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tabscroll),g_tabs); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tabscroll),GTK_POLICY_EXTERNAL,GTK_POLICY_NEVER); gtk_box_append(GTK_BOX(v),tabscroll);
  for(int i=0;CATS[i];i++){ GtkWidget* b=gtk_toggle_button_new_with_label(CATS[i]); gtk_widget_set_size_request(b,1,-1); gtk_widget_set_hexpand(b,FALSE); gtk_widget_set_margin_start(b,2); gtk_widget_set_margin_end(b,2); gtk_box_append(GTK_BOX(g_tabs),b); const gchar*k=CATKEYS[i]; g_signal_connect(b,"toggled",G_CALLBACK(+[](GtkToggleButton*btn,gpointer data){ if(!gtk_toggle_button_get_active(btn)) return; gtk_stack_set_visible_child_name(GTK_STACK(g_stack),(const gchar*)data); }), (gpointer)k); if(i==0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b),TRUE); }
  for(int i=0;CATS[i];i++){ GtkWidget* inner=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); GtkWidget* sc=gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc),inner); gtk_widget_set_vexpand(sc,TRUE); cat_pages[i]=sc; gtk_box_append(GTK_BOX(inner),mc_label("Run a scan to populate this page.", "dim",0)); gtk_stack_add_named(GTK_STACK(g_stack),sc,CATKEYS[i]); }
  GtkWidget* page=mc_page(v);
  return page;
}
void acc_security_teardown(void){
  g_teardown=TRUE;
  set_cancelled(TRUE);
  if(g_idle_source)
    {
      g_source_remove(g_idle_source);
      g_idle_source=0;
    }
  if(g_thread)
    {
      /* join WITHOUT holding g_mtx: worker/engine never locks g_mtx, and
       * idle_done would otherwise deadlock on it while we wait */
      g_thread_join(g_thread);
      g_thread=NULL;
    }
  mtx_lock();
  SecReport* old=g_report; g_report=NULL; g_running=FALSE;
  mtx_unlock();
  if(old) sec_report_free(old);
}
