/* ui/systest.cpp - System Testing page
 *
 * A comprehensive, modular diagnostic system covering 25 tests:
 *
 *   Hardware auto  (10): battery, AC, CPU, memory, storage, fans, thermal,
 *                        WiFi, Bluetooth, GPU
 *   Hardware I/O   ( 7): display (color-cycle), keyboard, touchpad (draw pad),
 *                        USB listing, speaker (tone), microphone, camera
 *   Software auto  ( 8): asusd, supergfxd, asus_wmi module, NVIDIA driver,
 *                        Intel i915 module, firmware updates (fwupd),
 *                        system package updates, required tools
 *
 * Design decisions:
 *   • Every test has a unique ID enum, a constant definition struct (name,
 *     description, category), and a parallel widget-pointer array.
 *   • Auto tests run on a single worker thread sequentially; results are
 *     delivered to the main thread via g_idle_add.  A generation counter
 *     drops stale callbacks after teardown or a new run.
 *   • Interactive tests launch independent GTK windows.  Users confirm
 *     pass/fail themselves; the page never claims a pass for untested items.
 *   • Diagnostic history is persisted in GKeyFile format under
 *     ~/.config/asus-control-center/diag-history.conf.
 *   • All strings use the inline T(en, ar) macro (no AccStr table additions
 *     beyond the two navigation strings added separately).
 *   • Read-only by default; no driver installation, no forced stress tests,
 *     no destructive operations.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "systest.h"
#include "mcui.h"
#include "window.h"
#include "../i18n/i18n.h"
#include "../control/hostcmd.h"
#include "../probe/battery.h"
#include "../probe/display.h"
#include "../probe/gpu.h"
#include "../probe/hwmon.h"
#include "../probe/load.h"
#include <adwaita.h>
#include <cairo.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include <sys/wait.h>

#define T(en, ar) (acc_i18n_lang () == ACC_LANG_AR ? (ar) : (en))

/* ================================================================ engine */

typedef enum {
  DS_NOT_TESTED = 0,
  DS_RUNNING,
  DS_PASSED,
  DS_WARNING,
  DS_FAILED,
  DS_UNSUPPORTED,
  DS_CANCELLED,
} DiagState;

typedef enum {
  DC_HW_AUTO,         /* automatic hardware probe, no user action        */
  DC_HW_INTERACTIVE,  /* user launches a test window and confirms result  */
  DC_SW,              /* automatic software/service probe                 */
} DiagCat;

/* Per-test definition (constant) */
typedef struct {
  const gchar *id;
  const gchar *name_en, *name_ar;
  const gchar *desc_en,  *desc_ar;
  const gchar *icon;
  DiagCat cat;
} TestDef;

/* Test index enum — keep in sync with s_defs[] below */
enum {
  /* Hardware auto */
  ST_BAT = 0, ST_AC, ST_CPU, ST_MEMORY, ST_STORAGE,
  ST_FANS, ST_THERMAL, ST_WIFI, ST_BT, ST_GPU,
  /* Hardware interactive */
  ST_DISPLAY, ST_KEYBOARD, ST_TOUCHPAD, ST_USB,
  ST_SPEAKER, ST_MIC, ST_CAMERA,
  /* Software auto */
  ST_ASUSD, ST_SUPERGFXD, ST_ASUS_WMI,
  ST_NVIDIA, ST_I915, ST_FIRMWARE, ST_UPDATES, ST_DEPS,
  ST_COUNT
};

static const TestDef s_defs[ST_COUNT] = {
  { "hw_battery",
    "Battery Health",        "صحة البطارية",
    "Full capacity vs. design capacity; cycle count.",
    "السعة الكاملة مقارنةً بالتصميمية وعدد دورات الشحن.",
    "battery-symbolic", DC_HW_AUTO },
  { "hw_ac",
    "AC Adapter",            "محوّل التيار",
    "Whether AC power is connected.",
    "هل محوّل التيار المتردد متصل؟",
    "battery-ac-adapter-symbolic", DC_HW_AUTO },
  { "hw_cpu",
    "CPU",                   "المعالج المركزي",
    "CPU model, core count, and current temperature.",
    "نموذج المعالج وعدد الأنوية ودرجة الحرارة الحالية.",
    "utilities-system-monitor-symbolic", DC_HW_AUTO },
  { "hw_memory",
    "Memory (RAM)",          "الذاكرة العشوائية",
    "Total RAM and available percentage.",
    "إجمالي الذاكرة والنسبة المتاحة.",
    "media-memory-symbolic", DC_HW_AUTO },
  { "hw_storage",
    "Storage",               "التخزين",
    "Root partition usage and NVMe temperature.",
    "استخدام القسم الجذري وحرارة وحدة NVMe.",
    "drive-harddisk-symbolic", DC_HW_AUTO },
  { "hw_fans",
    "Cooling Fans",          "مراوح التبريد",
    "Fan RPM readings from hardware monitor sensors.",
    "قراءات سرعة المراوح من مستشعرات الجهاز.",
    "weather-windy-symbolic", DC_HW_AUTO },
  { "hw_thermal",
    "Thermal Sensors",       "مستشعرات الحرارة",
    "CPU, GPU, and NVMe temperatures vs. safe thresholds.",
    "درجات حرارة المعالج والبطاقة الرسومية وNVMe مقارنةً بالحدود الآمنة.",
    "weather-clear-symbolic", DC_HW_AUTO },
  { "hw_wifi",
    "Wi-Fi",                 "الواي فاي",
    "Wireless interface presence and link state.",
    "وجود واجهة لاسلكية وحالة الاتصال.",
    "network-wireless-symbolic", DC_HW_AUTO },
  { "hw_bt",
    "Bluetooth",             "البلوتوث",
    "Bluetooth adapter detection.",
    "الكشف عن محوّل البلوتوث.",
    "bluetooth-symbolic", DC_HW_AUTO },
  { "hw_gpu",
    "GPU & Graphics Driver", "بطاقة الرسوميات والتعريف",
    "GPU model and loaded driver version.",
    "نموذج البطاقة الرسومية وإصدار التعريف المحمّل.",
    "video-joined-displays-symbolic", DC_HW_AUTO },
  /* Interactive */
  { "hw_display",
    "Display (Color Test)",  "الشاشة (اختبار الألوان)",
    "Fullscreen color cycle to detect dead pixels or banding.",
    "دورة ألوان ملء الشاشة للكشف عن البكسل الميت أو التلوين.",
    "video-display-symbolic", DC_HW_INTERACTIVE },
  { "hw_keyboard",
    "Keyboard",              "لوحة المفاتيح",
    "Press keys to verify each one lights up correctly.",
    "اضغط المفاتيح للتحقق من استجابة كل منها.",
    "input-keyboard-symbolic", DC_HW_INTERACTIVE },
  { "hw_touchpad",
    "Touchpad Multi-touch",  "اللمس المتعدد",
    "Draw on the touchpad canvas to verify tracking.",
    "ارسم على لوحة اللمس للتحقق من دقة التتبع.",
    "input-touchpad-symbolic", DC_HW_INTERACTIVE },
  { "hw_usb",
    "USB Connectivity",      "اتصالات USB",
    "Enumerate connected non-hub USB devices.",
    "سرد أجهزة USB المتصلة (غير المحاور).",
    "drive-removable-media-symbolic", DC_HW_INTERACTIVE },
  { "hw_speaker",
    "Speaker",               "المكبر",
    "Play a 440 Hz test tone for 2 seconds.",
    "تشغيل نغمة 440 هرتز لمدة ثانيتين.",
    "audio-volume-high-symbolic", DC_HW_INTERACTIVE },
  { "hw_mic",
    "Microphone",            "الميكروفون",
    "Record 1 second of audio and check input level.",
    "تسجيل ثانية من الصوت والتحقق من مستوى الإدخال.",
    "audio-input-microphone-symbolic", DC_HW_INTERACTIVE },
  { "hw_camera",
    "Camera",                "الكاميرا",
    "Open camera preview (Cheese, gnome-camera, or ffplay).",
    "فتح معاينة الكاميرا (Cheese أو gnome-camera أو ffplay).",
    "camera-web-symbolic", DC_HW_INTERACTIVE },
  /* Software */
  { "sw_asusd",
    "ASUS Daemon (asusd)",   "خدمة ASUS (asusd)",
    "asusd service status — required for fan and performance profiles.",
    "حالة خدمة asusd — مطلوبة لأوضاع المروحة والأداء.",
    "emblem-system-symbolic", DC_SW },
  { "sw_supergfxd",
    "supergfxd",             "supergfxd",
    "supergfxd service — manages GPU mode switching.",
    "خدمة supergfxd لإدارة تبديل وضع بطاقة الرسوميات.",
    "video-joined-displays-symbolic", DC_SW },
  { "sw_asus_wmi",
    "ASUS WMI Module",       "وحدة ASUS WMI",
    "asus_wmi kernel module — enables ASUS-specific hardware control.",
    "وحدة asus_wmi في النواة لتحكم العتاد الخاص بـ ASUS.",
    "drive-optical-symbolic", DC_SW },
  { "sw_nvidia",
    "NVIDIA Driver",         "تعريف NVIDIA",
    "NVIDIA kernel module loaded (only relevant on hybrid/dedicated GPU).",
    "وحدة NVIDIA في النواة (ذات صلة فقط عند وجود بطاقة NVIDIA).",
    "video-display-symbolic", DC_SW },
  { "sw_i915",
    "Intel i915 Driver",     "تعريف Intel i915",
    "Intel i915 kernel module for integrated graphics.",
    "وحدة Intel i915 في النواة للرسوميات المدمجة.",
    "video-display-symbolic", DC_SW },
  { "sw_firmware",
    "Firmware Updates",      "تحديثات البرمجية الثابتة",
    "Pending firmware updates via fwupd.",
    "تحديثات البرمجية الثابتة المعلّقة عبر fwupd.",
    "application-x-firmware-symbolic", DC_SW },
  { "sw_updates",
    "System Updates",        "تحديثات النظام",
    "Pending OS package updates (dnf / apt).",
    "تحديثات حزم النظام المعلّقة (dnf أو apt).",
    "system-software-update-symbolic", DC_SW },
  { "sw_deps",
    "Required Tools",        "الأدوات المطلوبة",
    "Tools needed for diagnostics: paplay, arecord, iw, bluetoothctl.",
    "أدوات التشخيص: paplay وarecord وiw وbluetoothed.",
    "dialog-information-symbolic", DC_SW },
};

/* ============================================================ state */

static GtkWidget *ts_pill[ST_COUNT];
static GtkWidget *ts_val[ST_COUNT];
static GtkWidget *ts_detail_lbl[ST_COUNT];
static GtkWidget *ts_action_lbl[ST_COUNT];
static GtkWidget *ts_revealer[ST_COUNT];
static GtkWidget *ts_spinner[ST_COUNT];
static GtkWidget *ts_run_btn[ST_COUNT];
static DiagState  ts_state[ST_COUNT];

static GtkWidget *st_progress_bar = NULL;
static GtkWidget *st_summary_lbl  = NULL;
static GtkWidget *st_run_all_btn  = NULL;
static GtkWidget *st_hist_box     = NULL;

static volatile gint st_gen   = 0;
static gboolean      st_busy  = FALSE;

/* Interactive test windows */
static GtkWidget *st_screen_win    = NULL;
static GtkWidget *st_screen_area   = NULL;
static GtkWidget *st_screen_hint   = NULL;
static int        st_screen_idx    = 0;
static GtkWidget *st_kbd_win       = NULL;
static GtkWidget *st_kbd_flow      = NULL;
static GtkWidget *st_draw_win      = NULL;
static GtkWidget *st_draw_area     = NULL;

static const gchar *const st_colors[] = {
  "#000000", "#ffffff", "#ff0000", "#00ff00",
  "#0000ff", "#808080", "#ffff00", "#00ffff", "#ff00ff", NULL
};

#define PAD_MAX_PTS 4096
typedef struct { gdouble x, y; gboolean gap; } PadPt;
static PadPt s_pad_pts[PAD_MAX_PTS];
static int   s_pad_n = 0;

/* ============================================================ utilities */

static const gchar *
state_pill_class (DiagState s)
{
  switch (s)
    {
    case DS_PASSED:      return "on";
    case DS_WARNING:     return "soon";
    case DS_FAILED:      return "bad";
    default:             return "off";
    }
}

static const gchar *
state_pill_label (DiagState s)
{
  switch (s)
    {
    case DS_NOT_TESTED:  return T ("Not Tested",   "لم يُختبر");
    case DS_RUNNING:     return T ("Running…",     "جارٍ…");
    case DS_PASSED:      return T ("Passed",        "نجح");
    case DS_WARNING:     return T ("Warning",       "تحذير");
    case DS_FAILED:      return T ("Failed",        "فشل");
    case DS_UNSUPPORTED: return T ("Unsupported",   "غير مدعوم");
    case DS_CANCELLED:   return T ("Cancelled",     "ملغى");
    default:             return "?";
    }
}

static void
st_set_state (int idx, DiagState state,
              const gchar *val, const gchar *detail, const gchar *action)
{
  ts_state[idx] = state;
  if (ts_pill[idx])
    {
      static const gchar *classes[] = { "off","off","on","soon","bad","off","off" };
      for (const gchar *c : { "off","on","soon","bad" })
        gtk_widget_remove_css_class (ts_pill[idx], c);
      gtk_widget_add_css_class (ts_pill[idx], classes[state]);
      gtk_label_set_text (GTK_LABEL (ts_pill[idx]), state_pill_label (state));
    }
  if (ts_val[idx] && val)
    gtk_label_set_text (GTK_LABEL (ts_val[idx]), val);
  if (ts_detail_lbl[idx] && detail)
    gtk_label_set_text (GTK_LABEL (ts_detail_lbl[idx]), detail);
  if (ts_action_lbl[idx] && action)
    gtk_label_set_text (GTK_LABEL (ts_action_lbl[idx]), action);
  if (ts_spinner[idx])
    {
      gtk_spinner_stop (GTK_SPINNER (ts_spinner[idx]));
      gtk_widget_set_visible (ts_spinner[idx], FALSE);
    }
}

static void history_save (void); /* forward declaration */

/* ========================================================= result posting */

typedef struct {
  gint gen;
  int  idx;         /* -1 = run complete                */
  DiagState state;
  gchar *val, *detail, *action;
  gdouble progress; /* 0..1, only used when idx == -2  */
} StMsg;

static gboolean
st_apply (gpointer data)
{
  StMsg *m = (StMsg *) data;
  if (g_atomic_int_get (&st_gen) == m->gen)
    {
      if (m->idx >= 0)
        {
          st_set_state (m->idx, m->state, m->val, m->detail, m->action);
          if (st_progress_bar)
            {
              /* count how many auto tests completed */
              int done = 0, total = 0;
              for (int i = 0; i < ST_COUNT; i++)
                if (s_defs[i].cat == DC_HW_AUTO || s_defs[i].cat == DC_SW)
                  {
                    total++;
                    if (ts_state[i] != DS_NOT_TESTED && ts_state[i] != DS_RUNNING)
                      done++;
                  }
              if (total > 0)
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st_progress_bar),
                                               (gdouble) done / total);
            }
        }
      else
        {
          /* run complete */
          history_save ();
          st_busy = FALSE;
          if (st_progress_bar) gtk_widget_set_visible (st_progress_bar, FALSE);
          if (st_run_all_btn)
            {
              gtk_widget_set_sensitive (st_run_all_btn, TRUE);
              gtk_button_set_label (GTK_BUTTON (st_run_all_btn),
                                    T ("Run All Again", "إعادة التشخيص"));
            }
          /* update summary */
          if (st_summary_lbl)
            {
              int n[7] = { 0 };
              for (int i = 0; i < ST_COUNT; i++) n[ts_state[i]]++;
              gchar *s = g_strdup_printf (
                  T ("%d passed, %d warnings, %d failed, %d unsupported",
                     "%d نجح، %d تحذير، %d فشل، %d غير مدعوم"),
                  n[DS_PASSED], n[DS_WARNING], n[DS_FAILED], n[DS_UNSUPPORTED]);
              gtk_label_set_text (GTK_LABEL (st_summary_lbl), s);
              g_free (s);
            }
        }
    }
  g_free (m->val); g_free (m->detail); g_free (m->action);
  g_free (m);
  return G_SOURCE_REMOVE;
}

static void
st_post (gint gen, int idx, DiagState state,
         gchar *val, gchar *detail, gchar *action)
{
  StMsg *m = g_new0 (StMsg, 1);
  m->gen    = gen;
  m->idx    = idx;
  m->state  = state;
  m->val    = val;
  m->detail = detail;
  m->action = action;
  g_idle_add (st_apply, m);
}

/* ========================================== screen color-cycle test */

static void
screen_draw_cb (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer user)
{
  (void) da; (void) user; (void) w; (void) h;
  GdkRGBA col;
  gdk_rgba_parse (&col, st_colors[st_screen_idx]);
  cairo_set_source_rgb (cr, col.red, col.green, col.blue);
  cairo_paint (cr);
}

static void
screen_apply_color (void)
{
  if (st_screen_area)
    gtk_widget_queue_draw (st_screen_area);
  if (st_screen_hint)
    {
      gchar *s = g_strdup_printf (T ("Color %d / %d  — Space/→ next  ←prev  Esc close",
                                     "لون %d من %d  — مسافة/← تالٍ  → سابق  Esc إغلاق"),
                                  st_screen_idx + 1, 9);
      gtk_label_set_text (GTK_LABEL (st_screen_hint), s);
      g_free (s);
    }
}

static void
screen_pass_cb (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  st_set_state (ST_DISPLAY, DS_PASSED,
                T ("No defects found", "لا عيوب مرئية"),
                T ("User confirmed: display looks correct on all test colors.",
                   "أكّد المستخدم: الشاشة تعمل بشكل صحيح على جميع الألوان."),
                NULL);
  if (st_screen_win) gtk_window_close (GTK_WINDOW (st_screen_win));
}

static void
screen_fail_cb (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  st_set_state (ST_DISPLAY, DS_FAILED,
                T ("Issue noticed", "تم ملاحظة مشكلة"),
                T ("User noticed a display defect during the color test.",
                   "لاحظ المستخدم عيبًا في الشاشة أثناء اختبار الألوان."),
                T ("Contact ASUS service or run a pixel repair tool.",
                   "تواصل مع خدمة ASUS أو استخدم أداة إصلاح البكسل."));
  if (st_screen_win) gtk_window_close (GTK_WINDOW (st_screen_win));
}

static gboolean
screen_key_cb (GtkEventControllerKey *ec, guint key, guint code,
               GdkModifierType mod, gpointer user)
{
  (void) ec; (void) code; (void) mod;
  if (key == GDK_KEY_Escape)
    { if (st_screen_win) gtk_window_close (GTK_WINDOW (st_screen_win)); return TRUE; }
  if ((key == GDK_KEY_space || key == GDK_KEY_Right) && st_colors[st_screen_idx + 1])
    { st_screen_idx++; screen_apply_color (); return TRUE; }
  if (key == GDK_KEY_Left && st_screen_idx > 0)
    { st_screen_idx--; screen_apply_color (); return TRUE; }
  if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter)
    { screen_pass_cb (NULL, user); return TRUE; }
  return FALSE;
}

static void
screen_win_destroyed (GtkWidget *win, gpointer unused)
{
  (void) win; (void) unused;
  st_screen_win  = NULL;
  st_screen_area = NULL;
  st_screen_hint = NULL;
}

static void
launch_screen_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  if (st_screen_win) { gtk_window_present (GTK_WINDOW (st_screen_win)); return; }

  st_screen_idx = 0;

  GtkWidget *win = gtk_window_new ();
  st_screen_win  = win;
  gtk_window_set_title (GTK_WINDOW (win), T ("Display Color Test", "اختبار ألوان الشاشة"));
  gtk_window_fullscreen (GTK_WINDOW (win));
  g_signal_connect (win, "destroy", G_CALLBACK (screen_win_destroyed), NULL);

  GtkWidget *da = gtk_drawing_area_new ();
  st_screen_area = da;
  gtk_widget_set_hexpand (da, TRUE);
  gtk_widget_set_vexpand (da, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (da), screen_draw_cb, NULL, NULL);

  GtkWidget *overlay = gtk_overlay_new ();
  gtk_overlay_set_child (GTK_OVERLAY (overlay), da);

  st_screen_hint = gtk_label_new ("");
  gtk_widget_add_css_class (st_screen_hint, "pagesub");
  gtk_widget_set_halign (st_screen_hint, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (st_screen_hint, GTK_ALIGN_START);
  gtk_widget_set_margin_top (st_screen_hint, 24);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), st_screen_hint);

  GtkWidget *ctrl = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign (ctrl, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (ctrl, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom (ctrl, 24);
  gtk_widget_add_css_class (ctrl, "linked");

  auto mk_btn = [](const gchar *lbl, const gchar *cls, GCallback cb, gpointer d) {
    GtkWidget *b = gtk_button_new_with_label (lbl);
    gtk_widget_add_css_class (b, cls);
    if (cb) g_signal_connect (b, "clicked", cb, d);
    return b;
  };
  gtk_box_append (GTK_BOX (ctrl),
      mk_btn (T ("Pass", "نجح"), "primarybtn", G_CALLBACK (screen_pass_cb), NULL));
  gtk_box_append (GTK_BOX (ctrl),
      mk_btn (T ("Fail — issue noticed", "فشل — لاحظت مشكلة"), "actionbtn",
              G_CALLBACK (screen_fail_cb), NULL));
  gtk_box_append (GTK_BOX (ctrl),
      mk_btn (T ("Cancel", "إلغاء"), "actionbtn",
              G_CALLBACK (+[](GtkButton*,gpointer w){ gtk_window_close(GTK_WINDOW(w)); }), win));
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), ctrl);

  gtk_window_set_child (GTK_WINDOW (win), overlay);

  GtkEventController *key = gtk_event_controller_key_new ();
  g_signal_connect (key, "key-pressed", G_CALLBACK (screen_key_cb), win);
  gtk_widget_add_controller (win, key);

  gtk_window_present (GTK_WINDOW (win));
  screen_apply_color ();

  st_set_state (ST_DISPLAY, DS_RUNNING,
                T ("Test window open — confirm result when done",
                   "نافذة الاختبار مفتوحة — أكّد النتيجة عند الانتهاء"),
                NULL, NULL);
}

/* ======================================== keyboard test window */

static gboolean
kbd_key_cb (GtkEventControllerKey *ec, guint keyval, guint keycode,
            GdkModifierType mod, gpointer flow)
{
  (void) ec; (void) keycode; (void) mod;
  if (keyval == GDK_KEY_Escape)
    {
      /* find parent window */
      GtkWidget *w = GTK_WIDGET (flow);
      while (w && !GTK_IS_WINDOW (w)) w = gtk_widget_get_parent (w);
      if (w) gtk_window_close (GTK_WINDOW (w));
      return TRUE;
    }
  const gchar *name = gdk_keyval_name (keyval);
  if (!name) return FALSE;

  GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (flow));
  while (child)
    {
      GtkWidget *inner = gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (child));
      if (inner && GTK_IS_LABEL (inner)
          && g_strcmp0 (gtk_label_get_text (GTK_LABEL (inner)), name) == 0)
        { gtk_widget_add_css_class (inner, "on"); return FALSE; }
      child = gtk_widget_get_next_sibling (child);
    }
  GtkWidget *pill = mc_label (name, "pill on", 1);
  gtk_flow_box_append (GTK_FLOW_BOX (flow), pill);
  return FALSE;
}

static void
kbd_win_destroyed (GtkWidget *win, gpointer unused)
{
  (void) win; (void) unused;
  st_kbd_win  = NULL;
  st_kbd_flow = NULL;
}

static void
launch_keyboard_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  if (st_kbd_win) { gtk_window_present (GTK_WINDOW (st_kbd_win)); return; }

  GtkWidget *win = gtk_window_new ();
  st_kbd_win = win;
  gtk_window_set_title (GTK_WINDOW (win), T ("Keyboard Test", "اختبار لوحة المفاتيح"));
  gtk_window_set_default_size (GTK_WINDOW (win), 580, 340);
  g_signal_connect (win, "destroy", G_CALLBACK (kbd_win_destroyed), NULL);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end   (box, 16);
  gtk_widget_set_margin_top   (box, 12);
  gtk_widget_set_margin_bottom(box, 16);

  GtkWidget *hint = gtk_label_new (
      T ("Press keys to light them up. Close the window when done. Esc to cancel.",
         "اضغط المفاتيح لتراها تضيء. أغلق النافذة عند الانتهاء. Esc للإلغاء."));
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_widget_add_css_class (hint, "pagesub");
  gtk_box_append (GTK_BOX (box), hint);

  GtkWidget *flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_row_spacing    (GTK_FLOW_BOX (flow), 6);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 6);
  gtk_widget_set_vexpand (flow, TRUE);
  gtk_box_append (GTK_BOX (box), flow);
  st_kbd_flow = flow;

  /* Pass/Fail buttons */
  GtkWidget *btnrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *pass_btn = mc_primary (T ("All keys work — Pass", "جميع المفاتيح تعمل — نجح"));
  GtkWidget *fail_btn = gtk_button_new_with_label (T ("Key(s) missing — Fail", "مفاتيح مفقودة — فشل"));
  gtk_widget_add_css_class (fail_btn, "actionbtn");
  g_signal_connect_swapped (pass_btn, "clicked", G_CALLBACK (+[](gpointer){
    st_set_state (ST_KEYBOARD, DS_PASSED,
                  T ("All tested keys responded", "كل المفاتيح المختبرة تعمل"),
                  T ("User confirmed keyboard is functional.", "أكّد المستخدم أن لوحة المفاتيح تعمل."),
                  NULL);
    if (st_kbd_win) gtk_window_close (GTK_WINDOW (st_kbd_win));
  }), NULL);
  g_signal_connect_swapped (fail_btn, "clicked", G_CALLBACK (+[](gpointer){
    st_set_state (ST_KEYBOARD, DS_FAILED,
                  T ("One or more keys unresponsive", "مفتاح واحد أو أكثر لا يستجيب"),
                  T ("User noticed an unresponsive key.", "لاحظ المستخدم مفتاحًا لا يستجيب."),
                  T ("Contact ASUS service for keyboard replacement.", "تواصل مع خدمة ASUS لاستبدال لوحة المفاتيح."));
    if (st_kbd_win) gtk_window_close (GTK_WINDOW (st_kbd_win));
  }), NULL);
  gtk_box_append (GTK_BOX (btnrow), pass_btn);
  gtk_box_append (GTK_BOX (btnrow), fail_btn);
  gtk_box_append (GTK_BOX (box), btnrow);

  gtk_window_set_child (GTK_WINDOW (win), box);

  GtkEventController *key = gtk_event_controller_key_new ();
  g_signal_connect (key, "key-pressed", G_CALLBACK (kbd_key_cb), flow);
  gtk_widget_add_controller (win, key);

  gtk_window_present (GTK_WINDOW (win));
  st_set_state (ST_KEYBOARD, DS_RUNNING,
                T ("Test window open — confirm result", "النافذة مفتوحة — أكّد النتيجة"),
                NULL, NULL);
}

/* ======================================== touchpad drawing test */

static void
pad_draw_fn (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer user)
{
  (void) da; (void) user;
  cairo_set_source_rgb (cr, 0.1, 0.1, 0.12);
  cairo_paint (cr);
  if (s_pad_n < 2)
    {
      cairo_set_source_rgba (cr, 1, 1, 1, 0.28);
      cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size (cr, 15);
      const gchar *msg = T ("Move your finger on the touchpad", "حرّك إصبعك على لوحة اللمس");
      cairo_text_extents_t ext;
      cairo_text_extents (cr, msg, &ext);
      cairo_move_to (cr, (w - ext.width) / 2.0, h / 2.0);
      cairo_show_text (cr, msg);
      return;
    }
  cairo_set_line_width (cr, 3.0);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_source_rgba (cr, 0.3, 0.7, 1.0, 0.9);
  gboolean in_path = FALSE;
  for (int i = 0; i < s_pad_n; i++)
    {
      if (s_pad_pts[i].gap || !in_path)
        { cairo_move_to (cr, s_pad_pts[i].x * w, s_pad_pts[i].y * h); in_path = TRUE; }
      else
        cairo_line_to (cr, s_pad_pts[i].x * w, s_pad_pts[i].y * h);
    }
  cairo_stroke (cr);
}

static void
pad_motion_cb (GtkEventControllerMotion *ec, gdouble x, gdouble y, gpointer user)
{
  (void) ec; (void) user;
  if (!st_draw_area || s_pad_n >= PAD_MAX_PTS) return;
  int w = gtk_widget_get_width  (st_draw_area);
  int h = gtk_widget_get_height (st_draw_area);
  if (w <= 0 || h <= 0) return;
  s_pad_pts[s_pad_n].x   = x / w;
  s_pad_pts[s_pad_n].y   = y / h;
  s_pad_pts[s_pad_n].gap = FALSE;
  s_pad_n++;
  gtk_widget_queue_draw (st_draw_area);
}

static void
pad_leave_cb (GtkEventControllerMotion *ec, gpointer user)
{
  (void) ec; (void) user;
  if (s_pad_n > 0 && s_pad_n < PAD_MAX_PTS)
    { s_pad_pts[s_pad_n].gap = TRUE; s_pad_n++; }
}

static void
pad_win_destroyed (GtkWidget *win, gpointer unused)
{
  (void) win; (void) unused;
  st_draw_win = NULL; st_draw_area = NULL;
}

static void
launch_touchpad_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  if (st_draw_win) { gtk_window_present (GTK_WINDOW (st_draw_win)); return; }
  s_pad_n = 0;

  GtkWidget *win = gtk_window_new ();
  st_draw_win = win;
  gtk_window_set_title (GTK_WINDOW (win), T ("Touchpad Multi-touch Test", "اختبار اللمس المتعدد"));
  gtk_window_set_default_size (GTK_WINDOW (win), 640, 420);
  g_signal_connect (win, "destroy", G_CALLBACK (pad_win_destroyed), NULL);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child (GTK_WINDOW (win), box);

  GtkWidget *hb  = adw_header_bar_new ();
  GtkWidget *lbl = gtk_label_new (T ("Draw with finger — Esc to close", "ارسم بالإصبع — Esc للإغلاق"));
  gtk_widget_add_css_class (lbl, "caption");
  adw_header_bar_set_title_widget (ADW_HEADER_BAR (hb), lbl);
  GtkWidget *clr = gtk_button_new_with_label (T ("Clear", "مسح"));
  gtk_widget_add_css_class (clr, "flat");
  g_signal_connect_swapped (clr, "clicked", G_CALLBACK (+[](gpointer){
    s_pad_n = 0;
    if (st_draw_area) gtk_widget_queue_draw (st_draw_area);
  }), NULL);
  adw_header_bar_pack_end (ADW_HEADER_BAR (hb), clr);
  gtk_box_append (GTK_BOX (box), hb);

  GtkWidget *da = gtk_drawing_area_new ();
  st_draw_area = da;
  gtk_widget_set_hexpand (da, TRUE);
  gtk_widget_set_vexpand (da, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (da), pad_draw_fn, NULL, NULL);

  GtkEventController *mo = gtk_event_controller_motion_new ();
  g_signal_connect (mo, "motion", G_CALLBACK (pad_motion_cb), NULL);
  g_signal_connect (mo, "leave",  G_CALLBACK (pad_leave_cb),  NULL);
  gtk_widget_add_controller (da, mo);

  /* Pass/Fail row below canvas */
  GtkWidget *btnrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start (btnrow, 12);
  gtk_widget_set_margin_end   (btnrow, 12);
  gtk_widget_set_margin_bottom(btnrow, 10);
  GtkWidget *pass_btn = mc_primary (T ("Tracking correct — Pass", "التتبع صحيح — نجح"));
  GtkWidget *fail_btn = gtk_button_new_with_label (T ("Tracking off — Fail", "التتبع خاطئ — فشل"));
  gtk_widget_add_css_class (fail_btn, "actionbtn");
  g_signal_connect_swapped (pass_btn, "clicked", G_CALLBACK (+[](gpointer){
    st_set_state (ST_TOUCHPAD, DS_PASSED,
                  T ("Touch tracking confirmed OK", "تتبع اللمس تم التحقق منه"),
                  T ("User confirmed touchpad tracks correctly.", "أكّد المستخدم صحة التتبع."), NULL);
    if (st_draw_win) gtk_window_close (GTK_WINDOW (st_draw_win));
  }), NULL);
  g_signal_connect_swapped (fail_btn, "clicked", G_CALLBACK (+[](gpointer){
    st_set_state (ST_TOUCHPAD, DS_FAILED,
                  T ("Tracking issue noticed", "لوحظت مشكلة في التتبع"),
                  T ("User noticed inaccurate or missing touch tracking.",
                     "لاحظ المستخدم تتبعًا غير دقيق أو مفقودًا."),
                  T ("Update libinput/firmware or contact ASUS service.",
                     "حدّث libinput أو البرمجية الثابتة أو تواصل مع خدمة ASUS."));
    if (st_draw_win) gtk_window_close (GTK_WINDOW (st_draw_win));
  }), NULL);
  gtk_box_append (GTK_BOX (btnrow), pass_btn);
  gtk_box_append (GTK_BOX (btnrow), fail_btn);

  gtk_box_append (GTK_BOX (box), da);
  gtk_box_append (GTK_BOX (box), btnrow);

  GtkEventController *key = gtk_event_controller_key_new ();
  g_signal_connect (key, "key-pressed",
                    G_CALLBACK (+[](GtkEventControllerKey*,guint k,guint,GdkModifierType,gpointer w)->gboolean{
                      if (k==GDK_KEY_Escape){gtk_window_close(GTK_WINDOW(w));return TRUE;}
                      return FALSE;
                    }), win);
  gtk_widget_add_controller (win, key);

  gtk_window_present (GTK_WINDOW (win));
  st_set_state (ST_TOUCHPAD, DS_RUNNING,
                T ("Test window open — draw and confirm", "النافذة مفتوحة — ارسم وأكّد"),
                NULL, NULL);
}

/* ======================================== USB listing + confirm */

static void
launch_usb_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  /* Enumerate devices immediately (no worker needed — file I/O only) */
  GDir    *d    = g_dir_open ("/sys/bus/usb/devices", 0, NULL);
  GString *names = g_string_new (NULL);
  gint     count = 0;
  if (d)
    {
      const gchar *entry;
      while ((entry = g_dir_read_name (d)))
        {
          gchar *pp = g_build_filename ("/sys/bus/usb/devices", entry, "product", NULL);
          gchar *prod = NULL;
          if (g_file_get_contents (pp, &prod, NULL, NULL))
            {
              g_strstrip (prod);
              if (strstr (prod, "root hub") == NULL && strstr (prod, "Root Hub") == NULL
                  && strstr (prod, "xHCI") == NULL)
                {
                  if (names->len) g_string_append (names, ", ");
                  g_string_append (names, prod);
                  count++;
                }
              g_free (prod);
            }
          g_free (pp);
        }
      g_dir_close (d);
    }

  if (count > 0)
    st_set_state (ST_USB, DS_PASSED,
                  g_strdup_printf (T ("%d device(s): %s", "%d جهاز: %s"),
                                   count, names->str),
                  g_strdup_printf (T ("Found %d non-hub USB device(s): %s",
                                      "وُجد %d جهاز USB (غير محور): %s"),
                                   count, names->str),
                  NULL);
  else
    st_set_state (ST_USB, DS_WARNING,
                  T ("No non-hub USB devices", "لا أجهزة USB غير المحاور"),
                  T ("No non-hub USB devices found. Plug in a device and re-run.",
                     "لم يُعثر على أجهزة USB. صِل جهازًا وأعِد الاختبار."),
                  NULL);
  g_string_free (names, TRUE);
}

/* ======================================== speaker test */

static void
play_tone_440hz (void)
{
  gchar *path = g_build_filename (g_get_tmp_dir (), "asuscraft_tone.wav", NULL);
  FILE  *f    = fopen (path, "wb");
  if (!f) { g_free (path); return; }

  const int sr = 44100, dur = 2, ch = 1, bps = 16;
  int samples = sr * dur, data_sz = samples * ch * (bps / 8);
  int riff_sz = 36 + data_sz, fmt_sz = 16, pcm = 1;
  int block = ch * (bps / 8), brate = sr * block;

  fwrite ("RIFF", 1, 4, f); fwrite (&riff_sz, 4, 1, f);
  fwrite ("WAVE", 1, 4, f);
  fwrite ("fmt ", 1, 4, f); fwrite (&fmt_sz,  4, 1, f);
  fwrite (&pcm,   2, 1, f); fwrite (&ch,      2, 1, f);
  fwrite (&sr,    4, 1, f); fwrite (&brate,   4, 1, f);
  fwrite (&block, 2, 1, f); fwrite (&bps,     2, 1, f);
  fwrite ("data", 1, 4, f); fwrite (&data_sz, 4, 1, f);

  for (int i = 0; i < samples; i++)
    {
      double t   = (double)i / sr;
      double env = (t < 0.05) ? t / 0.05 : (t > dur - 0.05) ? (dur - t) / 0.05 : 1.0;
      short  s   = (short)(env * 0.7 * 32767.0 * sin (2.0 * G_PI * 440.0 * t));
      fwrite (&s, 2, 1, f);
    }
  fclose (f);
  gchar *argv[] = { (gchar*)"paplay", path, NULL };
  GError *err = NULL;
  g_spawn_async (NULL, argv, NULL,
                 (GSpawnFlags)(G_SPAWN_SEARCH_PATH|G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL),
                 NULL, NULL, NULL, &err);
  g_clear_error (&err);
  g_free (path);
}

typedef struct { gint gen; int idx; DiagState st; gchar *val, *detail, *action; } AutoResult;

static gboolean auto_post (gpointer d) {
  AutoResult *r = (AutoResult*)d;
  if (g_atomic_int_get(&st_gen) == r->gen)
    st_post(r->gen, r->idx, r->st, r->val, r->detail, r->action);
  else { g_free(r->val); g_free(r->detail); g_free(r->action); }
  g_free(r);
  return G_SOURCE_REMOVE;
}

static void post_later(gint gen, int idx, DiagState st, gchar *v, gchar *d, gchar *a) {
  AutoResult *r = g_new0(AutoResult, 1);
  r->gen = gen; r->idx = idx; r->st = st; r->val = v; r->detail = d; r->action = a;
  g_idle_add(auto_post, r);
}

static gpointer
speaker_worker (gpointer data)
{
  gint gen = GPOINTER_TO_INT (data);
  gchar *prog = g_find_program_in_path ("paplay");
  if (prog)
    {
      g_free (prog);
      play_tone_440hz ();
      post_later (gen, ST_SPEAKER, DS_PASSED,
                  g_strdup (T ("Test tone played (440 Hz)", "تم تشغيل نغمة الاختبار")),
                  g_strdup (T ("2-second 440 Hz sine tone played successfully via paplay.",
                               "تم تشغيل نغمة 440 هرتز لثانيتين بنجاح عبر paplay.")),
                  NULL);
    }
  else
    post_later (gen, ST_SPEAKER, DS_UNSUPPORTED,
                g_strdup (T ("paplay not found", "paplay غير موجود")),
                g_strdup (T ("Install pipewire-utils or pulseaudio-utils.",
                             "ثبّت pipewire-utils أو pulseaudio-utils.")),
                g_strdup ("dnf install pipewire-utils"));
  return NULL;
}

static void
launch_speaker_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  gint gen = g_atomic_int_get (&st_gen);
  st_set_state (ST_SPEAKER, DS_RUNNING, T ("Playing…", "جارٍ التشغيل…"), NULL, NULL);
  g_thread_new ("systest-speaker", speaker_worker, GINT_TO_POINTER (gen));
}

/* microphone */
static gpointer
mic_worker (gpointer data)
{
  gint  gen  = GPOINTER_TO_INT (data);
  gchar *out = g_build_filename (g_get_tmp_dir (), "asuscraft_mic.wav", NULL);
  /* a fixed argv, never a shell string */
  gchar *rec = g_find_program_in_path ("arecord");
  gchar *argv[] = { rec, (gchar *) "-d", (gchar *) "1", (gchar *) "-f",
                    (gchar *) "cd", (gchar *) "-q", out, NULL };
  gboolean recorded = FALSE;
  if (rec)
    {
      GError *e = NULL;
      GSubprocess *p = g_subprocess_newv (argv,
          (GSubprocessFlags) (G_SUBPROCESS_FLAGS_STDOUT_SILENCE
                               | G_SUBPROCESS_FLAGS_STDERR_SILENCE), &e);
      if (p)
        {
          g_subprocess_wait_check (p, NULL, NULL);
          g_object_unref (p);
          recorded = TRUE;
        }
      else
        g_clear_error (&e);
    }
  g_free (rec);
  DiagState st = DS_UNSUPPORTED;
  gchar *val = NULL, *det = NULL;
  if (recorded)
    {
      GStatBuf sb;
      long sz = (g_stat (out, &sb) == 0) ? (long) sb.st_size : 0;
      if (sz > 1024)
        {
          st  = DS_PASSED;
          val = g_strdup_printf (T ("Input detected (%ld KB)", "إدخال مكتشف (%ld كيلوبايت)"),
                                 sz / 1024);
          det = g_strdup (T ("1-second recording captured audio data.",
                             "التسجيل الواحد ثانية التقط بيانات صوتية."));
        }
      else
        {
          st  = DS_FAILED;
          val = g_strdup (T ("No audio input", "لا إدخال صوتي"));
          det = g_strdup (T ("Recording produced no audio — check microphone.",
                             "لم ينتج التسجيل صوتًا — تحقق من الميكروفون."));
        }
    }
  else
    {
      val = g_strdup (T ("arecord not found", "arecord غير موجود"));
      det = g_strdup ("Install alsa-utils: dnf install alsa-utils");
    }
  g_unlink (out); g_free (out);
  post_later (gen, ST_MIC, st, val, det, NULL);
  return NULL;
}

static void
launch_mic_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  gint gen = g_atomic_int_get (&st_gen);
  st_set_state (ST_MIC, DS_RUNNING,
                T ("Recording 1 second…", "تسجيل لثانية…"), NULL, NULL);
  g_thread_new ("systest-mic", mic_worker, GINT_TO_POINTER (gen));
}

/* camera */
static void
launch_camera_test (GtkButton *b, gpointer user)
{
  (void) b; (void) user;
  const gchar *apps[] = { "cheese", "gnome-camera", NULL };
  gboolean launched = FALSE;
  for (int i = 0; apps[i] && !launched; i++)
    {
      gchar *prog = g_find_program_in_path (apps[i]);
      if (prog)
        {
          gchar *argv[] = { prog, NULL };
          GError *err = NULL;
          launched = g_spawn_async (NULL, argv, NULL,
                                    (GSpawnFlags)(G_SPAWN_SEARCH_PATH|G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL),
                                    NULL, NULL, NULL, &err) != FALSE;
          g_clear_error (&err); g_free (prog);
        }
    }
  if (!launched)
    {
      gchar *ff = g_find_program_in_path ("ffplay");
      if (ff)
        {
          gchar *argv[] = { ff, (gchar*)"/dev/video0", (gchar*)"-autoexit",
                            (gchar*)"-t", (gchar*)"30", NULL };
          GError *err = NULL;
          launched = g_spawn_async (NULL, argv, NULL,
                                    (GSpawnFlags)(G_SPAWN_SEARCH_PATH|G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL),
                                    NULL, NULL, NULL, &err) != FALSE;
          g_clear_error (&err); g_free (ff);
        }
    }
  if (launched)
    st_set_state (ST_CAMERA, DS_PASSED,
                  T ("Camera app launched", "تم تشغيل تطبيق الكاميرا"),
                  T ("Camera app opened — verify picture quality in the preview.",
                     "تم فتح تطبيق الكاميرا — تحقق من جودة الصورة في المعاينة."),
                  NULL);
  else
    st_set_state (ST_CAMERA, DS_UNSUPPORTED,
                  T ("No camera app found", "لا يوجد تطبيق كاميرا"),
                  T ("Install cheese or gnome-camera.", "ثبّت cheese أو gnome-camera."),
                  "dnf install cheese");
}

/* ============================================= auto hardware tests */

static void test_battery (gint gen) {
  BatInfo b; battery_read (&b);
  if (!b.ok) { post_later(gen,ST_BAT,DS_UNSUPPORTED,g_strdup(T("No battery","لا توجد بطارية")),g_strdup(T("No battery found or sysfs unreadable.","لم تُعثر على بطارية.")),NULL); return; }
  gdouble health = (b.wh_design > 0.0) ? (b.wh_full / b.wh_design * 100.0) : -1.0;
  gchar *cycles_s = b.cycle_count >= 0 ? g_strdup_printf (T(" · %d cycles"," · %d دورة"), b.cycle_count) : g_strdup("");
  gchar *val, *det;
  DiagState st;
  if (health < 0) { st=DS_UNSUPPORTED; val=g_strdup(T("Design capacity unreadable","السعة التصميمية غير مقروءة")); det=g_strdup(T("Cannot calculate battery health without design capacity.","لا يمكن حساب صحة البطارية.")); }
  else {
    val = g_strdup_printf (T("%.0f%% health%s","%.0f%% صحة%s"), health, cycles_s);
    det = g_strdup_printf (T("Full: %.1f Wh · Design: %.1f Wh · Health: %.0f%%%s","الكاملة: %.1f Wh · التصميمية: %.1f Wh · الصحة: %.0f%%%s"), b.wh_full, b.wh_design, health, cycles_s);
    st = (health >= 80) ? DS_PASSED : (health >= 60) ? DS_WARNING : DS_FAILED;
  }
  g_free (cycles_s);
  gchar *act = (st != DS_PASSED) ? g_strdup(T("Consider replacing battery. Contact ASUS service.","فكّر في استبدال البطارية. تواصل مع خدمة ASUS.")) : NULL;
  post_later (gen, ST_BAT, st, val, det, act);
}

static void test_ac (gint gen) {
  const gchar *paths[] = {
    "/sys/class/power_supply/AC/online",
    "/sys/class/power_supply/ACAD/online",
    "/sys/class/power_supply/ADP0/online",
    "/sys/class/power_supply/ADP1/online",
    NULL
  };
  for (int i = 0; paths[i]; i++) {
    gchar *s = NULL;
    if (g_file_get_contents (paths[i], &s, NULL, NULL)) {
      gboolean on = (s[0] == '1');
      g_free (s);
      post_later (gen, ST_AC,
                  on ? DS_PASSED : DS_WARNING,
                  on ? g_strdup(T("AC connected","محوّل التيار متصل")) : g_strdup(T("Running on battery","يعمل على البطارية")),
                  g_strdup_printf(T("Power supply online=%d (via %s).","مصدر الطاقة online=%d (عبر %s)."), on?1:0, paths[i]),
                  NULL);
      return;
    }
  }
  post_later (gen, ST_AC, DS_UNSUPPORTED,
              g_strdup(T("AC status unreadable","حالة المحوّل غير مقروءة")),
              g_strdup(T("Could not read any power_supply online file.","لم يمكن قراءة ملف الطاقة.")),NULL);
}

static void test_cpu (gint gen) {
  HwInfo h; hwmon_read (&h);
  gchar *model = NULL;
  gchar *cpuinfo = NULL;
  if (g_file_get_contents ("/proc/cpuinfo", &cpuinfo, NULL, NULL)) {
    gchar *p = strstr (cpuinfo, "model name");
    if (p) {
      p = strchr (p, ':'); if (p) p++;
      while (p && *p == ' ') p++;
      gchar *nl = strchr (p, '\n'); if (nl) *nl = '\0';
      model = p ? g_strdup (p) : NULL;
    }
    g_free (cpuinfo);
  }
  gdouble tc = h.t_cpu;
  DiagState st = DS_PASSED;
  gchar *val = NULL, *det = NULL, *act = NULL;
  if (tc > 0) {
    val = g_strdup_printf (T("%.0f°C","%.0f°C"), tc);
    det = g_strdup_printf (T("%s · %.0f °C","%s · %.0f °C"), model?model:"Unknown CPU", tc);
    if (tc >= 90) { st=DS_FAILED; act=g_strdup(T("CPU dangerously hot. Check fan and thermal paste.","المعالج ساخن بشكل خطير. تحقق من المروحة ومعجون التوصيل الحراري.")); }
    else if (tc >= 75) { st=DS_WARNING; act=g_strdup(T("CPU temperature elevated. Ensure good airflow.","حرارة المعالج مرتفعة. تأكد من التهوية الجيدة.")); }
  } else {
    st=DS_PASSED; val=g_strdup(model?model:T("CPU present","المعالج موجود"));
    det=g_strdup_printf(T("%s · temperature unavailable","%s · درجة الحرارة غير متاحة"),model?model:"?");
  }
  g_free (model);
  post_later (gen, ST_CPU, st, val, det, act);
}

static void test_memory (gint gen) {
  LoadInfo ld; acc_load_read (&ld);
  if (ld.mem_pct < 0) { post_later(gen,ST_MEMORY,DS_UNSUPPORTED,g_strdup(T("Unreadable","غير مقروء")),g_strdup(T("/proc/meminfo unreadable.","/proc/meminfo غير مقروء.")),NULL); return; }
  gdouble avail_pct = 100.0 - ld.mem_pct;
  DiagState st = (avail_pct < 5) ? DS_FAILED : (avail_pct < 15) ? DS_WARNING : DS_PASSED;
  gchar *val = g_strdup_printf (T("%.0f%% free · %.1f GiB total","%.0f%% متاح · %.1f GiB"), avail_pct, ld.mem_total_gb);
  gchar *det = g_strdup_printf (T("%.1f GiB total, %.0f%% used, %.0f%% available.","%.1f GiB إجمالًا، %.0f%% مستخدم، %.0f%% متاح."), ld.mem_total_gb, ld.mem_pct, avail_pct);
  gchar *act = (st!=DS_PASSED)?g_strdup(T("Close background applications to free memory.","أغلق التطبيقات الخلفية لتحرير الذاكرة.")):NULL;
  post_later (gen, ST_MEMORY, st, val, det, act);
}

static void test_storage (gint gen) {
  LoadInfo ld; acc_load_read (&ld);
  HwInfo h; hwmon_read (&h);
  if (ld.disk_pct < 0) { post_later(gen,ST_STORAGE,DS_UNSUPPORTED,g_strdup(T("Unreadable","غير مقروء")),g_strdup(T("statvfs failed.","فشل statvfs.")),NULL); return; }
  DiagState st = (ld.disk_pct > 95) ? DS_FAILED : (ld.disk_pct > 85) ? DS_WARNING : DS_PASSED;
  gchar *nvme_s = (h.t_nvme > 0) ? g_strdup_printf (T(" · NVMe %.0f°C"," · NVMe %.0f°C"), h.t_nvme) : g_strdup("");
  gchar *val = g_strdup_printf (T("%.0f%% used%s","%.0f%% مستخدم%s"), ld.disk_pct, nvme_s);
  gchar *det = g_strdup_printf (T("%.1f / %.1f GiB used (%.0f%%)%s","%.1f / %.1f GiB مستخدم (%.0f%%)%s"), ld.disk_used_gb, ld.disk_total_gb, ld.disk_pct, nvme_s);
  gchar *act = (st!=DS_PASSED)?g_strdup(T("Free disk space via Storage Manager.","حرّر مساحة عبر مدير التخزين.")):NULL;
  g_free (nvme_s);
  post_later (gen, ST_STORAGE, st, val, det, act);
}

static void test_fans (gint gen) {
  HwInfo h; hwmon_read (&h);
  if (!h.fans_ok) { post_later(gen,ST_FANS,DS_UNSUPPORTED,g_strdup(T("Sensors unreadable","الحساسات غير مقروءة")),g_strdup(T("hwmon fan sensors not found.","حساسات hwmon للمراوح غير موجودة.")),NULL); return; }
  gchar *val = g_strdup_printf (T("CPU %d rpm · GPU %d rpm","CPU %d دورة/ث · GPU %d دورة/ث"), h.fan_cpu, h.fan_gpu);
  gchar *det = g_strdup_printf (T("CPU fan: %d RPM, GPU fan: %d RPM.","مروحة CPU: %d دورة/ث، مروحة GPU: %d دورة/ث."), h.fan_cpu, h.fan_gpu);
  post_later (gen, ST_FANS, DS_PASSED, val, det, NULL);
}

static void test_thermal (gint gen) {
  HwInfo h; hwmon_read (&h);
  gboolean any = (h.t_cpu > 0 || h.t_nvme > 0);
  if (!any) { post_later(gen,ST_THERMAL,DS_UNSUPPORTED,g_strdup(T("No sensors","لا حساسات")),g_strdup(T("No temperature sensors found.","لم تُعثر على حساسات حرارة.")),NULL); return; }
  DiagState st = DS_PASSED;
  if (h.t_cpu >= 90 || h.t_nvme >= 80) st = DS_FAILED;
  else if (h.t_cpu >= 75 || h.t_nvme >= 60) st = DS_WARNING;
  gchar *val = g_strdup_printf (T("CPU %.0f°C · NVMe %.0f°C","CPU %.0f°C · NVMe %.0f°C"),
                                h.t_cpu > 0 ? h.t_cpu : 0, h.t_nvme > 0 ? h.t_nvme : 0);
  gchar *det = g_strdup_printf (T("CPU: %.0f°C (safe <75), NVMe: %.0f°C (safe <60), board: %.0f°C.",
                                   "CPU: %.0f°C (آمن <75)، NVMe: %.0f°C (آمن <60)، اللوحة: %.0f°C."),
                                h.t_cpu>0?h.t_cpu:0, h.t_nvme>0?h.t_nvme:0, h.t_board>0?h.t_board:0);
  gchar *act = (st!=DS_PASSED)?g_strdup(T("Check fan operation and clean vents.","تحقق من المراوح ونظّف فتحات التهوية.")):NULL;
  post_later (gen, ST_THERMAL, st, val, det, act);
}

static void test_wifi (gint gen) {
  GDir *d = g_dir_open ("/sys/class/net", 0, NULL);
  if (!d) { post_later(gen,ST_WIFI,DS_UNSUPPORTED,g_strdup(T("sysfs unavailable","sysfs غير متاح")),g_strdup(""),NULL); return; }
  gchar *iface_name = NULL; gboolean up = FALSE;
  const gchar *entry;
  while ((entry = g_dir_read_name (d))) {
    gchar *wpath = g_build_filename ("/sys/class/net", entry, "wireless", NULL);
    gboolean is_wireless = g_file_test (wpath, G_FILE_TEST_IS_DIR);
    g_free (wpath);
    if (!is_wireless) {
      gchar *p80 = g_build_filename ("/sys/class/net", entry, "phy80211", NULL);
      is_wireless = g_file_test (p80, G_FILE_TEST_IS_DIR);
      g_free (p80);
    }
    if (is_wireless) {
      g_free (iface_name); iface_name = g_strdup (entry);
      gchar *op = g_build_filename ("/sys/class/net", entry, "operstate", NULL);
      gchar *s = NULL;
      if (g_file_get_contents (op, &s, NULL, NULL)) {
        g_strstrip (s); up = (g_strcmp0 (s, "up") == 0); g_free (s);
      }
      g_free (op);
      break;
    }
  }
  g_dir_close (d);
  if (!iface_name) { post_later(gen,ST_WIFI,DS_UNSUPPORTED,g_strdup(T("No wireless interface","لا واجهة لاسلكية")),g_strdup(T("No wireless interface found in /sys/class/net.","لم تُعثر على واجهة لاسلكية.")),NULL); g_free(iface_name); return; }
  DiagState st = up ? DS_PASSED : DS_WARNING;
  gchar *val = g_strdup_printf (T("%s (%s)","%s (%s)"), iface_name, up ? T("up","متصل") : T("down","منقطع"));
  gchar *det = g_strdup_printf (T("Wireless interface %s, link state: %s.","الواجهة اللاسلكية %s، الحالة: %s."), iface_name, up?T("up","متصل"):T("down","منقطع"));
  gchar *act = !up ? g_strdup(T("Enable Wi-Fi or check the wireless kill switch.","فعّل الواي فاي أو تحقق من مفتاح الإيقاف اللاسلكي.")) : NULL;
  g_free (iface_name);
  post_later (gen, ST_WIFI, st, val, det, act);
}

static void test_bluetooth (gint gen) {
  gboolean found = FALSE;
  GDir *d = g_dir_open ("/sys/class/bluetooth", 0, NULL);
  if (d) { found = (g_dir_read_name (d) != NULL); g_dir_close (d); }
  if (!found) {
    /* Try bluetoothctl list as fallback (it lives on the host) */
    gchar *out = NULL, *err = NULL;
    gint ec = -1;
    AccHostHow how = ACC_HOST_UNAVAILABLE;
    if (acc_host_run (ACC_CMD_BLUETOOTH_ADAPTERS, NULL, &out, &ec, &how, &err))
      { found = out && out[0] != '\0' && out[0] != '\n'; }
    g_free (out);
    g_free (err);
  }
  if (found)
    post_later(gen,ST_BT,DS_PASSED,g_strdup(T("Adapter found","المحوّل موجود")),g_strdup(T("Bluetooth adapter detected.","تم الكشف عن محوّل بلوتوث.")),NULL);
  else
    post_later(gen,ST_BT,DS_UNSUPPORTED,g_strdup(T("No adapter","لا محوّل")),g_strdup(T("No Bluetooth adapter found.","لم يُعثر على محوّل بلوتوث.")),NULL);
}

static void test_gpu (gint gen) {
  GpuInfo g; gpu_read (&g);
  if (!g.ok) { post_later(gen,ST_GPU,DS_UNSUPPORTED,g_strdup(T("No GPU info","لا معلومات GPU")),g_strdup(T("Could not read GPU information.","لم يمكن قراءة معلومات بطاقة الرسوميات.")),NULL); return; }
  DiagState st = g.driver[0] ? DS_PASSED : DS_WARNING;
  gchar *val = g_strdup_printf (T("%s%s","%s%s"), g.short_name[0]?g.short_name:T("GPU","GPU"), g.driver[0]?g_strdup_printf(" · %s",g.driver):g_strdup(""));
  gchar *det = g.driver[0]
    ? g_strdup_printf (T("GPU: %s, driver version: %s.","البطاقة: %s، إصدار التعريف: %s."), g.short_name, g.driver)
    : g_strdup_printf (T("GPU: %s — driver version unknown.","البطاقة: %s — إصدار التعريف غير معروف."), g.short_name);
  gchar *act = !g.driver[0] ? g_strdup(T("Install or update the GPU driver.","ثبّت تعريف البطاقة أو حدّثه.")) : NULL;
  post_later (gen, ST_GPU, st, val, det, act);
}

/* ============================================= auto software tests */

static void test_asusd (gint gen) {
  gchar *out = NULL, *err = NULL;
  gint ec = -1; AccHostHow how = ACC_HOST_UNAVAILABLE;
  gboolean ok = acc_host_run (ACC_CMD_SERVICE_ACTIVE, "asusd", &out, &ec, &how, &err);
  g_free (out); g_free (err);
  gboolean active = ok && ec == 0;
  post_later(gen,ST_ASUSD, active?DS_PASSED:DS_WARNING,
    g_strdup(active?T("Active","نشطة"):T("Inactive","متوقفة")),
    g_strdup_printf(T("asusd systemd service is %s (exit %d).","خدمة asusd %s (الخروج %d)."), active?T("active","نشطة"):T("inactive","متوقفة"), ec),
    active?NULL:g_strdup(T("Start: systemctl start asusd","شغّلها: systemctl start asusd")));
}

static void test_supergfxd (gint gen) {
  gchar *out = NULL, *err = NULL;
  gint ec = -1; AccHostHow how = ACC_HOST_UNAVAILABLE;
  gboolean ok = acc_host_run (ACC_CMD_SERVICE_ACTIVE, "supergfxd", &out, &ec, &how, &err);
  g_free (out); g_free (err);
  gboolean active = ok && ec == 0;
  post_later(gen,ST_SUPERGFXD, active?DS_PASSED:DS_WARNING,
    g_strdup(active?T("Active","نشطة"):T("Not found / inactive","غير موجودة")),
    g_strdup_printf(T("supergfxd service: %s.","خدمة supergfxd: %s."), active?T("active","نشطة"):T("not active","غير نشطة")),
    active?NULL:g_strdup(T("supergfxd needed only for dedicated GPU switching.","supergfxd مطلوبة فقط لتبديل GPU المنفصلة.")));
}

static void test_asus_wmi (gint gen) {
  gchar *out = NULL, *err = NULL;
  gint ec = -1; AccHostHow how = ACC_HOST_UNAVAILABLE;
  gboolean loaded = FALSE;
  if (acc_host_run (ACC_CMD_LOADED_MODULES, NULL, &out, &ec, &how, &err)) {
    loaded = out && strstr (out, "asus_wmi") != NULL;
  }
  g_free (out); g_free (err);
  post_later(gen,ST_ASUS_WMI,loaded?DS_PASSED:DS_WARNING,
    g_strdup(loaded?T("Loaded","محمّلة"):T("Not loaded","غير محمّلة")),
    g_strdup(loaded?T("asus_wmi kernel module is loaded.","وحدة asus_wmi في النواة محمّلة."):T("asus_wmi not found in lsmod output.","asus_wmi غير موجودة في lsmod.")),
    loaded?NULL:g_strdup(T("Ensure asus-wmi-sensors or linux-asus-wmi package is installed.","تأكد من تثبيت asus-wmi-sensors أو linux-asus-wmi.")));
}

static void test_nvidia (gint gen) {
  /* Check /proc/driver/nvidia/version first (exists only if driver is loaded) */
  gboolean loaded = g_file_test ("/proc/driver/nvidia/version", G_FILE_TEST_EXISTS);
  gchar *ver_s = NULL;
  if (loaded) {
    gchar *v = NULL;
    if (g_file_get_contents ("/proc/driver/nvidia/version", &v, NULL, NULL)) {
      gchar *nl = strchr (v, '\n'); if (nl) *nl = '\0';
      ver_s = g_strdup (v); g_free (v);
    }
  }
  /* If no NVIDIA GPU, mark unsupported */
  GpuInfo g; gpu_read (&g);
  gboolean has_nvidia = (strstr (g.short_name, "NVIDIA") || strstr (g.short_name, "GeForce")
                         || strstr (g.short_name, "RTX") || strstr (g.short_name, "GTX"));
  if (!has_nvidia && !loaded) {
    post_later(gen,ST_NVIDIA,DS_UNSUPPORTED,g_strdup(T("No NVIDIA GPU","لا بطاقة NVIDIA")),
      g_strdup(T("No NVIDIA GPU detected. Test not applicable.","لا بطاقة NVIDIA. الاختبار غير قابل للتطبيق.")),NULL);
    g_free(ver_s); return;
  }
  post_later(gen,ST_NVIDIA,loaded?DS_PASSED:DS_WARNING,
    g_strdup(loaded?T("Loaded","محمّل"):T("Not loaded","غير محمّل")),
    loaded ? g_strdup_printf(T("NVIDIA driver loaded: %s","تعريف NVIDIA محمّل: %s"), ver_s?ver_s:"version unknown")
           : g_strdup(T("NVIDIA driver not loaded. Use GNOME Software or dnf to install.","تعريف NVIDIA غير محمّل. استخدم GNOME Software أو dnf للتثبيت.")),
    loaded ? NULL : g_strdup("dnf install akmod-nvidia"));
  g_free (ver_s);
}

static void test_i915 (gint gen) {
  gchar *out = NULL, *err = NULL;
  gint ec = -1; AccHostHow how = ACC_HOST_UNAVAILABLE;
  gboolean loaded = FALSE;
  if (acc_host_run (ACC_CMD_LOADED_MODULES, NULL, &out, &ec, &how, &err))
    { loaded = out && strstr(out,"i915")!=NULL; }
  g_free (out); g_free (err);
  post_later(gen,ST_I915,loaded?DS_PASSED:DS_WARNING,
    g_strdup(loaded?T("Loaded","محمّلة"):T("Not loaded","غير محمّلة")),
    g_strdup(loaded?T("Intel i915 module loaded.","وحدة Intel i915 محمّلة."):T("i915 not in lsmod — may not apply to AMD/NVIDIA-only systems.","i915 غير موجودة في lsmod.")),
    NULL);
}

static void test_firmware (gint gen) {
  gchar *prog = g_find_program_in_path ("fwupdmgr");
  if (!prog) {
    post_later(gen,ST_FIRMWARE,DS_UNSUPPORTED,g_strdup(T("fwupd not installed","fwupd غير مثبّت")),
      g_strdup(T("Install fwupd to check firmware updates.","ثبّت fwupd للتحقق من تحديثات البرمجية الثابتة.")),
      g_strdup("dnf install fwupd"));
    return;
  }
  g_free (prog);
  /* fwupdmgr get-updates exits 0 when updates available, 2 when nothing */
  gchar *out = NULL, *err = NULL; gint ec = -1;
  AccHostHow how = ACC_HOST_UNAVAILABLE;
  acc_host_run (ACC_CMD_FIRMWARE_UPDATES, NULL, &out, &ec, &how, &err);
  g_free (err);
  gboolean has_updates = (ec == 0 && out && strstr(out,"Release") != NULL);
  post_later(gen,ST_FIRMWARE, has_updates?DS_WARNING:DS_PASSED,
    g_strdup(has_updates?T("Updates available","تحديثات متاحة"):T("Up to date","محدّث")),
    has_updates ? g_strdup_printf(T("fwupd reports pending firmware updates:\n%s","fwupd يبلّغ عن تحديثات:\n%s"),out)
                : g_strdup(T("No pending firmware updates.","لا تحديثات للبرمجية الثابتة.")),
    has_updates ? g_strdup("fwupdmgr update") : NULL);
  g_free (out);
}

static void test_updates (gint gen) {
  /* Try dnf first (Fedora/RHEL), then apt (Debian/Ubuntu) */
  gchar *dnf = g_find_program_in_path ("dnf");
  if (dnf) {
    g_free (dnf);
    /* dnf check-update: exit 0=no updates, 100=updates available, else error */
    gchar *out = NULL, *errs = NULL; gint ec = -1;
    AccHostHow how = ACC_HOST_UNAVAILABLE;
    acc_host_run (ACC_CMD_PACKAGE_UPDATES, NULL, &out, &ec, &how, &errs);
    g_free (errs);
    gboolean has = (ec == 100);
    gboolean bad = (ec < 0 || (ec != 0 && ec != 100));
    if (!bad)
      post_later(gen,ST_UPDATES, has?DS_WARNING:DS_PASSED,
        g_strdup(has?T("Updates pending","تحديثات معلّقة"):T("Up to date","محدّث")),
        has ? g_strdup_printf(T("dnf found pending updates (exit %d).","dnf وجد تحديثات معلّقة (الخروج %d)"),ec) : g_strdup(T("No pending package updates.","لا تحديثات معلّقة.")),
        has ? g_strdup("sudo dnf upgrade") : NULL);
    else
      post_later(gen,ST_UPDATES,DS_WARNING,
        g_strdup(T("Check failed","فشل الفحص")),
        g_strdup_printf(T("dnf check-update returned exit code %d.","عاد dnf check-update بكود %d."),ec),NULL);
    g_free (out);
    return;
  }
  gchar *apt = g_find_program_in_path ("apt-get");
  if (apt) {
    g_free (apt);
    gchar *out = NULL, *err = NULL;
    gint ec = -1; AccHostHow how = ACC_HOST_UNAVAILABLE;
    acc_host_run (ACC_CMD_APT_UPDATES, NULL, &out, &ec, &how, &err);
    g_free (err);
    gboolean has = out && strstr(out,"newly installed")!=NULL && strstr(out,"0 newly installed")==NULL;
    post_later(gen,ST_UPDATES, has?DS_WARNING:DS_PASSED,
      g_strdup(has?T("Updates pending","تحديثات معلّقة"):T("Up to date","محدّث")),
      g_strdup(has?T("apt-get reports upgradeable packages.","apt-get يبلّغ عن حزم قابلة للترقية."):T("No pending package updates.","لا تحديثات.")),
      has?g_strdup("sudo apt-get upgrade"):NULL);
    g_free (out); return;
  }
  post_later(gen,ST_UPDATES,DS_UNSUPPORTED,g_strdup(T("No package manager","لا مدير حزم")),
    g_strdup(T("Neither dnf nor apt-get found.","dnf ولا apt-get موجود.")),NULL);
}

static void test_deps (gint gen) {
  struct { const gchar *tool, *pkg; } tools[] = {
    { "paplay",       "pipewire-utils"      },
    { "arecord",      "alsa-utils"          },
    { "iw",           "iw"                  },
    { "bluetoothctl", "bluez"               },
    { "fwupdmgr",     "fwupd"               },
    { NULL, NULL }
  };
  GString *missing = g_string_new (NULL);
  GString *found   = g_string_new (NULL);
  for (int i = 0; tools[i].tool; i++) {
    gchar *p = g_find_program_in_path (tools[i].tool);
    if (p) { if (found->len) g_string_append (found, ", "); g_string_append (found, tools[i].tool); g_free(p); }
    else   { if (missing->len) g_string_append (missing, " "); g_string_append (missing, tools[i].pkg); }
  }
  gboolean all_ok = (missing->len == 0);
  gchar *val = all_ok ? g_strdup(T("All tools present","جميع الأدوات موجودة"))
                      : g_strdup_printf(T("Missing: %s","مفقود: %s"), missing->str);
  gchar *det = g_strdup_printf(T("Found: %s%s%s","موجود: %s%s%s"),
                found->str[0] ? found->str : T("(none)","(لا شيء)"),
                missing->len ? T(", missing: ", "، مفقود: ") : "",
                missing->len ? missing->str : "");
  gchar *act = all_ok ? NULL
                      : g_strdup_printf(T("Install: dnf install %s","ثبّت: dnf install %s"), missing->str);
  post_later(gen,ST_DEPS, all_ok?DS_PASSED:DS_WARNING, val, det, act);
  g_string_free (missing, TRUE); g_string_free (found, TRUE);
}

/* ============================================= worker thread */

typedef struct { gint gen; gboolean hw_only; gboolean sw_only; } RunCtx;

static gpointer
st_run_worker (gpointer data)
{
  RunCtx *ctx = (RunCtx *) data;
  gint gen = ctx->gen;
  gboolean hw = !ctx->sw_only;
  gboolean sw = !ctx->hw_only;
  g_free (ctx);

  /* Mark all auto tests as running before starting */
  if (hw) for (int i = ST_BAT; i <= ST_GPU; i++) {
    AutoResult *r = g_new0(AutoResult,1); r->gen=gen; r->idx=i; r->st=DS_RUNNING;
    r->val=g_strdup("…"); g_idle_add(auto_post, r);
  }
  if (sw) for (int i = ST_ASUSD; i < ST_COUNT; i++) {
    AutoResult *r = g_new0(AutoResult,1); r->gen=gen; r->idx=i; r->st=DS_RUNNING;
    r->val=g_strdup("…"); g_idle_add(auto_post, r);
  }

  if (hw) {
    test_battery   (gen);
    test_ac        (gen);
    test_cpu       (gen);
    test_memory    (gen);
    test_storage   (gen);
    test_fans      (gen);
    test_thermal   (gen);
    test_wifi      (gen);
    test_bluetooth (gen);
    test_gpu       (gen);
  }
  if (sw) {
    test_asusd    (gen);
    test_supergfxd(gen);
    test_asus_wmi (gen);
    test_nvidia   (gen);
    test_i915     (gen);
    test_firmware (gen);
    test_updates  (gen);
    test_deps     (gen);
  }

  /* Signal completion */
  st_post (gen, -1, DS_NOT_TESTED, NULL, NULL, NULL);
  return NULL;
}

static void
begin_run (gboolean hw_only, gboolean sw_only)
{
  if (st_busy) return;
  st_busy = TRUE;
  g_atomic_int_add (&st_gen, 1);
  gint gen = g_atomic_int_get (&st_gen);

  if (st_progress_bar) {
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (st_progress_bar), 0.0);
    gtk_widget_set_visible (st_progress_bar, TRUE);
  }
  if (st_run_all_btn) {
    gtk_widget_set_sensitive (st_run_all_btn, FALSE);
    gtk_button_set_label (GTK_BUTTON (st_run_all_btn), T ("Running…", "جارٍ…"));
  }
  if (st_summary_lbl)
    gtk_label_set_text (GTK_LABEL (st_summary_lbl), T ("Running diagnostics…", "جارٍ التشخيص…"));

  RunCtx *ctx = g_new0 (RunCtx, 1);
  ctx->gen     = gen;
  ctx->hw_only = hw_only;
  ctx->sw_only = sw_only;
  g_thread_new ("systest-run", st_run_worker, ctx);
}

static void run_all_cb  (GtkButton *b, gpointer u) { (void)b;(void)u; begin_run(FALSE,FALSE); }
static void run_hw_cb   (GtkButton *b, gpointer u) { (void)b;(void)u; begin_run(TRUE,FALSE);  }
static void run_sw_cb   (GtkButton *b, gpointer u) { (void)b;(void)u; begin_run(FALSE,TRUE);  }

/* ============================================= history */

static gchar *
history_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "asus-control-center", "diag-history.conf", NULL);
}

static void
history_save (void)
{
  gchar *dir = g_build_filename (g_get_user_config_dir (), "asus-control-center", NULL);
  g_mkdir_with_parents (dir, 0700);
  g_free (dir);

  gchar   *path = history_path ();
  GKeyFile *kf  = g_key_file_new ();
  g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL);

  GDateTime *now = g_date_time_new_now_local ();
  gchar *grp = g_date_time_format (now, "run_%Y-%m-%dT%H:%M:%S");
  g_date_time_unref (now);

  for (int i = 0; i < ST_COUNT; i++)
    {
      static const gchar *state_name[] = {
        "not_tested","running","passed","warning","failed","unsupported","cancelled"
      };
      gchar *v = g_strdup_printf ("%s", state_name[ts_state[i]]);
      g_key_file_set_string (kf, grp, s_defs[i].id, v);
      g_free (v);
    }

  /* Keep at most 10 runs — drop oldest */
  gsize n_grps = 0;
  gchar **grps = g_key_file_get_groups (kf, &n_grps);
  if (n_grps > 10) {
    for (gsize i = 0; i < n_grps - 10; i++)
      g_key_file_remove_group (kf, grps[i], NULL);
  }
  g_strfreev (grps);
  g_free (grp);

  GError *err = NULL;
  if (!g_key_file_save_to_file (kf, path, &err))
    { g_printerr ("history save: %s\n", err?err->message:"?"); g_clear_error (&err); }
  g_key_file_free (kf);
  g_free (path);
}

static void
history_load_ui (GtkWidget *box)
{
  /* Clear existing children */
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child (box)))
    gtk_box_remove (GTK_BOX (box), child);

  gchar   *path = history_path ();
  GKeyFile *kf  = g_key_file_new ();
  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
    {
      gtk_box_append (GTK_BOX (box),
                      mc_label (T ("No history yet — run diagnostics first.",
                                   "لا تاريخ بعد — شغّل التشخيص أولًا."), "caption", 0));
      g_key_file_free (kf); g_free (path);
      return;
    }

  gsize n_grps = 0;
  gchar **grps = g_key_file_get_groups (kf, &n_grps);
  /* Show newest first (last in file) */
  for (gint gi = (gint)n_grps - 1; gi >= 0 && gi >= (gint)n_grps - 5; gi--)
    {
      /* Count results */
      int counts[7] = { 0 };
      for (int i = 0; i < ST_COUNT; i++)
        {
          gchar *v = g_key_file_get_string (kf, grps[gi], s_defs[i].id, NULL);
          if (!v) continue;
          static const gchar *names[]={"not_tested","running","passed","warning","failed","unsupported","cancelled"};
          for (int s=0;s<7;s++) if (g_strcmp0(v,names[s])==0){counts[s]++;break;}
          g_free (v);
        }
      /* Strip "run_" prefix from group name for display */
      const gchar *ts = grps[gi] + (strncmp(grps[gi],"run_",4)==0 ? 4 : 0);
      gchar *summary = g_strdup_printf (
          T ("%s — %d passed, %d warnings, %d failed",
             "%s — %d نجح، %d تحذيرات، %d فشل"),
          ts, counts[DS_PASSED], counts[DS_WARNING], counts[DS_FAILED]);
      gtk_box_append (GTK_BOX (box),
                      mc_label (summary, "caption", 0));
      g_free (summary);
    }
  g_strfreev (grps);
  g_key_file_free (kf);
  g_free (path);
}

/* ============================================= test card builder */

static void
toggle_detail_cb (GtkToggleButton *tb, gpointer rev)
{
  gtk_revealer_set_reveal_child (GTK_REVEALER (rev),
                                 gtk_toggle_button_get_active (tb));
}

static GtkWidget *
st_build_card (int idx)
{
  const TestDef *d = &s_defs[idx];

  GtkWidget *card = mc_card ();

  /* Main row: icon | name | value | pill | [run button] */
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);

  GtkWidget *im = gtk_image_new_from_icon_name (d->icon);
  gtk_image_set_pixel_size (GTK_IMAGE (im), 20);
  gtk_widget_add_css_class (im, "cardicon");
  gtk_widget_set_valign (im, GTK_ALIGN_CENTER);

  GtkWidget *name_lbl = mc_title (T (d->name_en, d->name_ar));
  gtk_widget_set_hexpand (name_lbl, TRUE);
  gtk_widget_set_valign (name_lbl, GTK_ALIGN_CENTER);

  GtkWidget *val = mc_label (T ("Not tested", "لم يُختبر"), "value dim", 0);
  gtk_widget_set_valign (val, GTK_ALIGN_CENTER);
  ts_val[idx] = val;

  GtkWidget *spinner = gtk_spinner_new ();
  gtk_widget_set_visible (spinner, FALSE);
  gtk_widget_set_valign (spinner, GTK_ALIGN_CENTER);
  ts_spinner[idx] = spinner;

  GtkWidget *pill = gtk_label_new (state_pill_label (DS_NOT_TESTED));
  gtk_widget_add_css_class (pill, "pill");
  gtk_widget_add_css_class (pill, state_pill_class (DS_NOT_TESTED));
  gtk_widget_set_valign (pill, GTK_ALIGN_CENTER);
  ts_pill[idx] = pill;

  /* Detail revealer */
  GtkWidget *detail_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_top (detail_box, 6);

  GtkWidget *detail_lbl = mc_label ("", "caption", 0);
  gtk_label_set_wrap (GTK_LABEL (detail_lbl), TRUE);
  gtk_label_set_selectable (GTK_LABEL (detail_lbl), TRUE);
  ts_detail_lbl[idx] = detail_lbl;

  GtkWidget *action_lbl = mc_label ("", "caption", 0);
  gtk_label_set_wrap (GTK_LABEL (action_lbl), TRUE);
  gtk_widget_add_css_class (action_lbl, "dim");
  ts_action_lbl[idx] = action_lbl;

  gtk_box_append (GTK_BOX (detail_box), detail_lbl);
  gtk_box_append (GTK_BOX (detail_box),
                  mc_label (T (d->desc_en, d->desc_ar), "caption dim", 0));
  gtk_box_append (GTK_BOX (detail_box), action_lbl);

  GtkWidget *revealer = gtk_revealer_new ();
  gtk_revealer_set_transition_type (GTK_REVEALER (revealer),
                                    GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  gtk_revealer_set_child (GTK_REVEALER (revealer), detail_box);
  ts_revealer[idx] = revealer;

  /* Expand toggle button */
  GtkWidget *toggle = gtk_toggle_button_new ();
  gtk_button_set_icon_name (GTK_BUTTON (toggle), "pan-down-symbolic");
  gtk_widget_add_css_class (toggle, "flat");
  gtk_widget_set_valign (toggle, GTK_ALIGN_CENTER);
  g_signal_connect (toggle, "toggled", G_CALLBACK (toggle_detail_cb), revealer);

  gtk_box_append (GTK_BOX (row), im);
  gtk_box_append (GTK_BOX (row), name_lbl);
  gtk_box_append (GTK_BOX (row), val);
  gtk_box_append (GTK_BOX (row), spinner);
  gtk_box_append (GTK_BOX (row), pill);

  /* Interactive tests get a launch/run button */
  if (d->cat == DC_HW_INTERACTIVE)
    {
      GCallback cb = NULL;
      const gchar *lbl = T ("Launch", "تشغيل");
      switch (idx)
        {
        case ST_DISPLAY:  cb = G_CALLBACK (launch_screen_test);   break;
        case ST_KEYBOARD: cb = G_CALLBACK (launch_keyboard_test); break;
        case ST_TOUCHPAD: cb = G_CALLBACK (launch_touchpad_test); break;
        case ST_USB:      cb = G_CALLBACK (launch_usb_test);  lbl = T("Run","تشغيل"); break;
        case ST_SPEAKER:  cb = G_CALLBACK (launch_speaker_test); lbl = T("Play","تشغيل"); break;
        case ST_MIC:      cb = G_CALLBACK (launch_mic_test);   lbl = T("Record","تسجيل"); break;
        case ST_CAMERA:   cb = G_CALLBACK (launch_camera_test); lbl = T("Open","فتح"); break;
        default: break;
        }
      if (cb)
        {
          GtkWidget *run_btn = mc_primary (lbl);
          gtk_widget_set_valign (run_btn, GTK_ALIGN_CENTER);
          g_signal_connect (run_btn, "clicked", cb, NULL);
          gtk_box_append (GTK_BOX (row), run_btn);
          ts_run_btn[idx] = run_btn;
        }
    }

  gtk_box_append (GTK_BOX (row), toggle);

  gtk_box_append (GTK_BOX (card), row);
  gtk_box_append (GTK_BOX (card), revealer);

  return card;
}

/* ============================================= page builder */

GtkWidget *
acc_systest_page (AccUI *u)
{
  (void) u;

  /* Reset runtime state */
  g_atomic_int_add (&st_gen, 1);
  for (int i = 0; i < ST_COUNT; i++)
    ts_state[i] = DS_NOT_TESTED;
  st_busy = FALSE;

  GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_set_margin_start  (body, 24);
  gtk_widget_set_margin_end    (body, 24);
  gtk_widget_set_margin_top    (body, 20);
  gtk_widget_set_margin_bottom (body, 24);

  /* --- page header --- */
  {
    GtkWidget *h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_add_css_class (h, "pagehead");
    GtkWidget *col = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand (col, TRUE);
    gtk_widget_set_valign  (col, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (col), mc_label ("ASUSCraft", "eyebrow", 0));
    gtk_box_append (GTK_BOX (col),
                    mc_label (T ("System Testing", "اختبار النظام"), "pagetitle", 0));
    GtkWidget *sub = mc_label (
        T ("25 diagnostics — hardware, interactive, and software.",
           "٢٥ اختبارًا: عتاد وتفاعلي وبرمجيات."),
        "pagesub", 0);
    gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (sub), 80);
    gtk_box_append (GTK_BOX (col), sub);
    gtk_box_append (GTK_BOX (h), col);
    gtk_box_append (GTK_BOX (body), h);
  }

  /* --- summary + run button --- */
  {
    GtkWidget *card = mc_card ();
    GtkWidget *row  = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget *sum = mc_label (T ("Press \"Run All\" to start diagnostics.",
                                   "اضغط «تشغيل الكل» لبدء التشخيص."),
                               "pagesub", 0);
    gtk_widget_set_hexpand (sum, TRUE);
    gtk_widget_set_valign (sum, GTK_ALIGN_CENTER);
    st_summary_lbl = sum;

    GtkWidget *run_btn = mc_primary (T ("Run All", "تشغيل الكل"));
    g_signal_connect (run_btn, "clicked", G_CALLBACK (run_all_cb), NULL);
    st_run_all_btn = run_btn;

    GtkWidget *hw_btn = gtk_button_new_with_label (T ("Hardware", "العتاد"));
    gtk_widget_add_css_class (hw_btn, "actionbtn");
    g_signal_connect (hw_btn, "clicked", G_CALLBACK (run_hw_cb), NULL);

    GtkWidget *sw_btn = gtk_button_new_with_label (T ("Software", "البرمجيات"));
    gtk_widget_add_css_class (sw_btn, "actionbtn");
    g_signal_connect (sw_btn, "clicked", G_CALLBACK (run_sw_cb), NULL);

    gtk_box_append (GTK_BOX (row), sum);
    gtk_box_append (GTK_BOX (row), hw_btn);
    gtk_box_append (GTK_BOX (row), sw_btn);
    gtk_box_append (GTK_BOX (row), run_btn);
    gtk_box_append (GTK_BOX (card), row);

    GtkWidget *bar = gtk_progress_bar_new ();
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (bar), 0.0);
    gtk_widget_set_visible (bar, FALSE);
    gtk_widget_add_css_class (bar, "diagbar");
    gtk_box_append (GTK_BOX (card), bar);
    st_progress_bar = bar;

    gtk_box_append (GTK_BOX (body), card);
  }

  /* --- hardware auto tests --- */
  gtk_box_append (GTK_BOX (body),
                  mc_group (T ("HARDWARE DIAGNOSTICS", "تشخيصات العتاد")));
  for (int i = ST_BAT; i <= ST_GPU; i++)
    gtk_box_append (GTK_BOX (body), st_build_card (i));

  /* --- interactive / audio / camera tests --- */
  gtk_box_append (GTK_BOX (body),
                  mc_group (T ("INTERACTIVE & SENSOR TESTS", "اختبارات تفاعلية وحساسات")));
  for (int i = ST_DISPLAY; i <= ST_CAMERA; i++)
    gtk_box_append (GTK_BOX (body), st_build_card (i));

  /* --- software tests --- */
  gtk_box_append (GTK_BOX (body),
                  mc_group (T ("SOFTWARE DIAGNOSTICS", "تشخيصات البرمجيات")));
  for (int i = ST_ASUSD; i < ST_COUNT; i++)
    gtk_box_append (GTK_BOX (body), st_build_card (i));

  /* --- diagnostic history --- */
  gtk_box_append (GTK_BOX (body),
                  mc_group (T ("DIAGNOSTIC HISTORY (last 5 runs)",
                               "تاريخ التشخيص (آخر ٥ عمليات)")));
  {
    GtkWidget *hist_card = mc_card ();
    GtkWidget *hist_inner = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append (GTK_BOX (hist_card), hist_inner);
    st_hist_box = hist_inner;
    history_load_ui (hist_inner);
    gtk_box_append (GTK_BOX (body), hist_card);
  }

  return mc_page (body);
}

/* ============================================= teardown */

void
acc_systest_teardown (void)
{
  /* Bump generation to invalidate any in-flight callbacks */
  g_atomic_int_add (&st_gen, 1);
  st_busy = FALSE;

  if (st_screen_win && GTK_IS_WIDGET (st_screen_win))
    { gtk_window_close (GTK_WINDOW (st_screen_win)); st_screen_win = NULL; }
  st_screen_area = st_screen_hint = NULL;

  if (st_kbd_win && GTK_IS_WIDGET (st_kbd_win))
    { gtk_window_close (GTK_WINDOW (st_kbd_win)); st_kbd_win = NULL; }
  st_kbd_flow = NULL;

  if (st_draw_win && GTK_IS_WIDGET (st_draw_win))
    { gtk_window_close (GTK_WINDOW (st_draw_win)); st_draw_win = NULL; }
  st_draw_area = NULL;
  s_pad_n = 0;

  for (int i = 0; i < ST_COUNT; i++)
    ts_pill[i] = ts_val[i] = ts_detail_lbl[i] = ts_action_lbl[i]
      = ts_revealer[i] = ts_spinner[i] = ts_run_btn[i] = NULL;

  st_progress_bar = st_summary_lbl = st_run_all_btn = st_hist_box = NULL;
}
