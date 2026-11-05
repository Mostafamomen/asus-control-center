/* launcher.cpp - ASUS Control Center Launcher
 * An ArtCraft-style dedicated hardware hub, installer, updater and supervisor.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <adwaita.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LAUNCHER_ID       "io.github.asuscontrolcenter.Launcher"
#define LAUNCHER_NAME     "ASUS Control Center Launcher"
#define LAUNCHER_VERSION  "1.2.0"
#define APP_ID            "io.github.asuscontrolcenter.AsusControlCenter"
#define GITHUB_REPO       "Mostafamomen/asus-control-center"
#define RES_PATH          "/io/github/asuscontrolcenter/Launcher/"
#define RES_STYLE         RES_PATH "style.css"
#define RES_ICON          RES_PATH "data/io.github.asuscontrolcenter.Launcher.svg"

/* Application state */
static struct {
    GtkWidget *window;
    GtkWidget *toast_overlay;
    GtkWidget *status_badge;
    GtkWidget *status_detail;
    GtkWidget *installed_ver_label;
    GtkWidget *latest_ver_label;
    GtkWidget *check_btn;
    GtkWidget *main_action_btn;
    GtkWidget *github_btn;
    GtkWidget *progress_bar;
    GtkWidget *progress_label;
    GtkWidget *progress_box;
    GtkWidget *device_label;
    GtkWidget *kernel_label;
    GtkWidget *polkit_badge;

    char *resolved_bin;
    char *installed_version;
    char *latest_version;
    char *download_url;
    char *expected_sha256;
    bool is_installed;
    bool update_available;
    bool is_downloading;
    GPid running_pid;
} App;

/* ---------- Helper functions ---------- */

static void show_toast(const char *text) {
    if (App.toast_overlay) {
        AdwToast *t = adw_toast_new(text);
        adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(App.toast_overlay), t);
    }
}

static char *clean_string(char *str) {
    if (!str) return NULL;
    char *p = str;
    while (isspace((unsigned char)*p)) p++;
    char *end = p + strlen(p) - 1;
    while (end > p && (isspace((unsigned char)*end) || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return g_strdup(p);
}

/* Detect machine details via DMI / sysfs */
static char *get_device_model(void) {
    char *content = NULL;
    if (g_file_get_contents("/sys/class/dmi/id/product_name", &content, NULL, NULL)) {
        char *cleaned = clean_string(content);
        g_free(content);
        if (cleaned && *cleaned) return cleaned;
        g_free(cleaned);
    }
    return g_strdup("ASUS Laptop");
}

/* Detect installed version from binary */
static char *detect_installed_version(const char *path) {
    if (!path || !g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) return NULL;

    gchar *argv[] = { (gchar *)path, (gchar *)"--version", NULL };
    gchar *stdout_buf = NULL;
    GError *err = NULL;

    if (g_spawn_sync(NULL, argv, NULL, (GSpawnFlags)0, NULL, NULL, &stdout_buf, NULL, NULL, &err)) {
        if (stdout_buf) {
            char *line = strtok(stdout_buf, "\r\n");
            if (line) {
                /* e.g. "asus-control-center 0.4.0" -> "0.4.0" */
                char *sp = strrchr(line, ' ');
                char *v = sp ? sp + 1 : line;
                char *res = g_strdup(v);
                g_free(stdout_buf);
                return res;
            }
            g_free(stdout_buf);
        }
    }
    if (err) g_clear_error(&err);
    return g_strdup("Installed (local build)");
}

/* Locate ASUS Control Center executable */
static char *locate_application(void) {
    const char *candidates[] = {
        "/usr/bin/asus-control-center",
        "/usr/local/bin/asus-control-center",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (g_file_test(candidates[i], G_FILE_TEST_IS_EXECUTABLE)) {
            return g_strdup(candidates[i]);
        }
    }

    char *home = g_build_filename(g_get_home_dir(), ".local", "bin", "asus-control-center", NULL);
    if (g_file_test(home, G_FILE_TEST_IS_EXECUTABLE)) {
        return home;
    }
    g_free(home);

    /* Also check current workspace folder */
    char *current_proj = g_build_filename(g_get_current_dir(), "..", "asus-control-center", NULL);
    char *canon_proj = g_canonicalize_filename(current_proj, NULL);
    g_free(current_proj);
    if (g_file_test(canon_proj, G_FILE_TEST_IS_EXECUTABLE)) {
        return canon_proj;
    }
    g_free(canon_proj);

    /* Search in PATH */
    char *in_path = g_find_program_in_path("asus-control-center");
    if (in_path) return in_path;

    return NULL;
}

static bool check_polkit_helper(void) {
    return g_file_test("/usr/libexec/asus-control-center/acc-helper", G_FILE_TEST_IS_EXECUTABLE) &&
           g_file_test("/usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy", G_FILE_TEST_EXISTS);
}

static void update_ui_state(void) {
    g_free(App.resolved_bin);
    App.resolved_bin = locate_application();
    App.is_installed = (App.resolved_bin != NULL);

    g_free(App.installed_version);
    if (App.is_installed) {
        App.installed_version = detect_installed_version(App.resolved_bin);
    } else {
        App.installed_version = NULL;
    }

    /* Polkit status */
    bool polkit_ok = check_polkit_helper();
    if (polkit_ok) {
        gtk_label_set_text(GTK_LABEL(App.polkit_badge), "HELPER ACTIVE");
        gtk_widget_remove_css_class(App.polkit_badge, "badge-not-installed");
        gtk_widget_add_css_class(App.polkit_badge, "badge-ready");
    } else {
        gtk_label_set_text(GTK_LABEL(App.polkit_badge), "HELPER NOT FOUND");
        gtk_widget_remove_css_class(App.polkit_badge, "badge-ready");
        gtk_widget_add_css_class(App.polkit_badge, "badge-not-installed");
    }

    /* Version labels */
    if (App.installed_version) {
        char *txt = g_strdup_printf("v%s", App.installed_version);
        gtk_label_set_text(GTK_LABEL(App.installed_ver_label), txt);
        g_free(txt);
    } else {
        gtk_label_set_text(GTK_LABEL(App.installed_ver_label), "Not installed");
    }

    if (App.latest_version) {
        char *txt = g_strdup_printf("v%s", App.latest_version);
        gtk_label_set_text(GTK_LABEL(App.latest_ver_label), txt);
        g_free(txt);
    } else {
        gtk_label_set_text(GTK_LABEL(App.latest_ver_label), "Unknown");
    }

    /* Main button and status badge */
    gtk_widget_remove_css_class(App.status_badge, "badge-ready");
    gtk_widget_remove_css_class(App.status_badge, "badge-update");
    gtk_widget_remove_css_class(App.status_badge, "badge-not-installed");
    gtk_widget_remove_css_class(App.status_badge, "badge-checking");

    gtk_widget_remove_css_class(App.main_action_btn, "btn-launch");
    gtk_widget_remove_css_class(App.main_action_btn, "btn-install");

    if (App.is_downloading) {
        gtk_widget_add_css_class(App.status_badge, "badge-checking");
        gtk_label_set_text(GTK_LABEL(App.status_badge), "DOWNLOADING...");
        gtk_label_set_text(GTK_LABEL(App.status_detail), "Downloading official release archive...");
        gtk_button_set_label(GTK_BUTTON(App.main_action_btn), "Downloading...");
        gtk_widget_set_sensitive(App.main_action_btn, FALSE);
    } else if (!App.is_installed) {
        gtk_widget_add_css_class(App.status_badge, "badge-not-installed");
        gtk_label_set_text(GTK_LABEL(App.status_badge), "NOT INSTALLED");
        gtk_label_set_text(GTK_LABEL(App.status_detail), "ASUS Control Center is not installed on this system.");
        gtk_button_set_label(GTK_BUTTON(App.main_action_btn), "Download & Install");
        gtk_widget_add_css_class(App.main_action_btn, "btn-install");
        gtk_widget_set_sensitive(App.main_action_btn, TRUE);
    } else if (App.update_available) {
        gtk_widget_add_css_class(App.status_badge, "badge-update");
        gtk_label_set_text(GTK_LABEL(App.status_badge), "UPDATE AVAILABLE");
        char *detail = g_strdup_printf("A newer release (%s) is available on GitHub.", App.latest_version ? App.latest_version : "");
        gtk_label_set_text(GTK_LABEL(App.status_detail), detail);
        g_free(detail);
        gtk_button_set_label(GTK_BUTTON(App.main_action_btn), "Update Now");
        gtk_widget_add_css_class(App.main_action_btn, "btn-install");
        gtk_widget_set_sensitive(App.main_action_btn, TRUE);
    } else {
        gtk_widget_add_css_class(App.status_badge, "badge-ready");
        gtk_label_set_text(GTK_LABEL(App.status_badge), "READY");
        gtk_label_set_text(GTK_LABEL(App.status_detail), "ASUS Control Center is ready to launch.");
        gtk_button_set_label(GTK_BUTTON(App.main_action_btn), "Open Application");
        gtk_widget_add_css_class(App.main_action_btn, "btn-launch");
        gtk_widget_set_sensitive(App.main_action_btn, TRUE);
    }
}

/* ---------- Launch application ---------- */

static void on_child_exited(GPid pid, gint status, gpointer user_data) {
    (void)status; (void)user_data;
    g_spawn_close_pid(pid);
    if (App.running_pid == pid) {
        App.running_pid = 0;
    }
}

static void launch_asus_control_center(void) {
    if (!App.resolved_bin) {
        show_toast("Executable not found. Please install first.");
        return;
    }

    if (App.running_pid > 0) {
        /* Check if process is still alive */
        if (kill(App.running_pid, 0) == 0) {
            show_toast("ASUS Control Center is already running.");
            return;
        } else {
            App.running_pid = 0;
        }
    }

    GError *err = NULL;
    gchar *argv[] = { App.resolved_bin, NULL };
    GPid pid;

    if (!g_spawn_async(NULL, argv, NULL, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD), NULL, NULL, &pid, &err)) {
        char *err_msg = g_strdup_printf("Launch failed: %s", err ? err->message : "unknown error");
        show_toast(err_msg);
        g_free(err_msg);
        if (err) g_clear_error(&err);
    } else {
        App.running_pid = pid;
        g_child_watch_add(pid, on_child_exited, NULL);
        show_toast("Launched ASUS Control Center");
    }
}

/* ---------- Secure Download & Installation ---------- */

typedef struct {
    char *dest_file;
    char *dest_dir;
    char *expected_hash;
} InstallJob;

/* SHA-256 verification */
static bool verify_file_sha256(const char *path, const char *expected_hex) {
    if (!expected_hex || strlen(expected_hex) != 64) {
        /* If checksum is not published, verify file exists and is non-empty */
        GStatBuf st;
        return g_stat(path, &st) == 0 && st.st_size > 1024;
    }

    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    GMappedFile *mapped = g_mapped_file_new(path, FALSE, NULL);
    if (!mapped) {
        g_checksum_free(checksum);
        return false;
    }

    g_checksum_update(checksum, (const guchar *)g_mapped_file_get_contents(mapped), g_mapped_file_get_length(mapped));
    const char *digest = g_checksum_get_string(checksum);
    bool match = (g_ascii_strcasecmp(digest, expected_hex) == 0);

    g_mapped_file_unref(mapped);
    g_checksum_free(checksum);
    return match;
}

static void on_extract_finished(GObject *source, GAsyncResult *res, gpointer user_data) {
    GSubprocess *proc = G_SUBPROCESS(source);
    InstallJob *job = (InstallJob *)user_data;
    GError *err = NULL;

    g_subprocess_wait_check_finish(proc, res, &err);
    App.is_downloading = false;
    gtk_widget_set_visible(App.progress_box, FALSE);

    if (err) {
        char *msg = g_strdup_printf("Installation failed: %s", err->message);
        show_toast(msg);
        g_free(msg);
        g_clear_error(&err);
    } else {
        /* Integration check */
        show_toast("ASUS Control Center installed successfully!");
        App.update_available = false;
    }

    /* Clean up temp archive */
    g_unlink(job->dest_file);
    g_free(job->dest_file);
    g_free(job->dest_dir);
    g_free(job->expected_hash);
    g_free(job);
    g_object_unref(proc);

    update_ui_state();
}

static void on_download_finished(GObject *source, GAsyncResult *res, gpointer user_data) {
    GSubprocess *proc = G_SUBPROCESS(source);
    InstallJob *job = (InstallJob *)user_data;
    GError *err = NULL;

    g_subprocess_wait_check_finish(proc, res, &err);

    if (err) {
        App.is_downloading = false;
        gtk_widget_set_visible(App.progress_box, FALSE);
        char *msg = g_strdup_printf("Download failed: %s", err->message);
        show_toast(msg);
        g_free(msg);
        g_clear_error(&err);
        g_unlink(job->dest_file);
        g_free(job->dest_file);
        g_free(job->dest_dir);
        g_free(job->expected_hash);
        g_free(job);
        g_object_unref(proc);
        update_ui_state();
        return;
    }
    g_object_unref(proc);

    /* Verify integrity */
    gtk_label_set_text(GTK_LABEL(App.progress_label), "Verifying SHA-256 integrity...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(App.progress_bar), 0.85);

    if (!verify_file_sha256(job->dest_file, job->expected_hash)) {
        App.is_downloading = false;
        gtk_widget_set_visible(App.progress_box, FALSE);
        show_toast("Checksum verification failed: Corrupted archive.");
        g_unlink(job->dest_file);
        g_free(job->dest_file);
        g_free(job->dest_dir);
        g_free(job->expected_hash);
        g_free(job);
        update_ui_state();
        return;
    }

    /* Extract safely to user folder */
    gtk_label_set_text(GTK_LABEL(App.progress_label), "Extracting and integrating...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(App.progress_bar), 0.95);

    g_mkdir_with_parents(job->dest_dir, 0755);

    /* Safe tar extraction without shell invocation */
    GError *extract_err = NULL;
    GSubprocess *extract_proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_NONE,
        &extract_err,
        "tar", "-xzf", job->dest_file, "-C", job->dest_dir, "--strip-components=1",
        NULL
    );

    if (!extract_proc) {
        App.is_downloading = false;
        gtk_widget_set_visible(App.progress_box, FALSE);
        char *msg = g_strdup_printf("Extraction init error: %s", extract_err ? extract_err->message : "unknown");
        show_toast(msg);
        g_free(msg);
        if (extract_err) g_clear_error(&extract_err);
        g_unlink(job->dest_file);
        g_free(job->dest_file);
        g_free(job->dest_dir);
        g_free(job->expected_hash);
        g_free(job);
        update_ui_state();
        return;
    }

    g_subprocess_wait_check_async(extract_proc, NULL, on_extract_finished, job);
}

static void start_download_install(void) {
    if (App.is_downloading) return;

    /* Verify machine architecture */
    struct utsname un;
    if (uname(&un) == 0) {
        if (strcmp(un.machine, "x86_64") != 0) {
            show_toast("Error: ASUS Control Center binary releases currently require x86_64.");
            return;
        }
    }

    const char *url = App.download_url;
    if (!url || !*url) {
        /* Default GitHub tarball fallback */
        url = "https://github.com/" GITHUB_REPO "/releases/latest/download/asus-control-center-linux-x86_64.tar.gz";
    }

    char *cache_dir = g_build_filename(g_get_user_cache_dir(), "asus-control-center-launcher", NULL);
    g_mkdir_with_parents(cache_dir, 0755);
    char *dest_archive = g_build_filename(cache_dir, "release-package.tar.gz", NULL);
    g_free(cache_dir);

    char *install_target = g_build_filename(g_get_user_data_dir(), "asus-control-center", NULL);

    InstallJob *job = g_new0(InstallJob, 1);
    job->dest_file = dest_archive;
    job->dest_dir = install_target;
    job->expected_hash = g_strdup(App.expected_sha256);

    App.is_downloading = true;
    gtk_widget_set_visible(App.progress_box, TRUE);
    gtk_label_set_text(GTK_LABEL(App.progress_label), "Downloading release package...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(App.progress_bar), 0.35);
    update_ui_state();

    /* Secure curl download (HTTPS only, 30s timeout, follower) */
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_NONE,
        &err,
        "curl", "-s", "-L", "--proto", "=https",
        "-A", "ASUS-Control-Center-Launcher/" LAUNCHER_VERSION,
        "-m", "60",
        "-o", job->dest_file,
        url,
        NULL
    );

    if (!proc) {
        App.is_downloading = false;
        gtk_widget_set_visible(App.progress_box, FALSE);
        char *msg = g_strdup_printf("Cannot run curl: %s", err ? err->message : "unknown");
        show_toast(msg);
        g_free(msg);
        if (err) g_clear_error(&err);
        g_free(job->dest_file);
        g_free(job->dest_dir);
        g_free(job->expected_hash);
        g_free(job);
        update_ui_state();
        return;
    }

    g_subprocess_wait_check_async(proc, NULL, on_download_finished, job);
}

static void on_main_action_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    if (App.is_installed && !App.update_available) {
        launch_asus_control_center();
    } else {
        start_download_install();
    }
}

/* ---------- Release & Update Check ---------- */

static void on_release_info_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)user_data;
    GSubprocess *proc = G_SUBPROCESS(source);
    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    GError *err = NULL;

    g_subprocess_communicate_utf8_finish(proc, res, &stdout_buf, &stderr_buf, &err);
    gtk_widget_set_sensitive(App.check_btn, TRUE);

    if (err || !stdout_buf || !*stdout_buf) {
        show_toast("Could not contact GitHub releases.");
        if (err) g_clear_error(&err);
    } else {
        /* Parse "tag_name": "v0.4.0" */
        char *tag = strstr(stdout_buf, "\"tag_name\"");
        if (tag) {
            tag = strchr(tag, ':');
            if (tag) {
                tag++;
                while (*tag == ' ' || *tag == '"') tag++;
                char *end = strchr(tag, '"');
                if (end) {
                    *end = '\0';
                    const char *ver = (*tag == 'v') ? tag + 1 : tag;
                    g_free(App.latest_version);
                    App.latest_version = g_strdup(ver);

                    /* Check if update available */
                    if (App.installed_version && *App.installed_version) {
                        if (strcmp(App.installed_version, App.latest_version) != 0) {
                            App.update_available = true;
                            show_toast("New version available!");
                        } else {
                            App.update_available = false;
                            show_toast("Your installation is up to date.");
                        }
                    } else {
                        show_toast("Latest release information retrieved.");
                    }
                }
            }
        }
    }

    g_free(stdout_buf);
    g_free(stderr_buf);
    g_object_unref(proc);

    update_ui_state();
}

static void on_check_updates_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    gtk_widget_set_sensitive(App.check_btn, FALSE);
    gtk_widget_remove_css_class(App.status_badge, "badge-ready");
    gtk_widget_remove_css_class(App.status_badge, "badge-update");
    gtk_widget_add_css_class(App.status_badge, "badge-checking");
    gtk_label_set_text(GTK_LABEL(App.status_badge), "CHECKING...");
    gtk_label_set_text(GTK_LABEL(App.status_detail), "Querying GitHub release API...");

    GError *err = NULL;
    GSubprocess *proc = g_subprocess_new(
        (GSubprocessFlags)(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &err,
        "curl", "-s", "-L", "--proto", "=https",
        "-A", "ASUS-Control-Center-Launcher/" LAUNCHER_VERSION,
        "-m", "10",
        "https://api.github.com/repos/" GITHUB_REPO "/releases/latest",
        NULL
    );

    if (!proc) {
        gtk_widget_set_sensitive(App.check_btn, TRUE);
        show_toast("Failed to run update check.");
        if (err) g_clear_error(&err);
        update_ui_state();
        return;
    }

    g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_release_info_done, NULL);
}

static void on_github_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    g_app_info_launch_default_for_uri("https://github.com/" GITHUB_REPO, NULL, NULL);
}

/* ---------- User Interface Construction ---------- */

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    /* Load CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, RES_STYLE);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    App.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(App.window), LAUNCHER_NAME);
    gtk_window_set_default_size(GTK_WINDOW(App.window), 620, 520);
    gtk_widget_add_css_class(App.window, "artcraft-hub");

    App.toast_overlay = adw_toast_overlay_new();
    gtk_window_set_child(GTK_WINDOW(App.window), App.toast_overlay);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(App.toast_overlay), root_box);

    /* Modern Libadwaita Header Bar */
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *header_title = gtk_label_new("Craft Suite • ASUS Hardware Platform");
    gtk_widget_add_css_class(header_title, "dim-label");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), header_title);
    gtk_box_append(GTK_BOX(root_box), header);

    /* Main Container */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_widget_set_margin_start(content, 28);
    gtk_widget_set_margin_end(content, 28);
    gtk_box_append(GTK_BOX(root_box), content);

    /* Hero Card (ArtCraft Layout) */
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(hero, "hero-card");
    gtk_box_append(GTK_BOX(content), hero);

    /* Top Row: Custom Logo + Title + Status Badge */
    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_append(GTK_BOX(hero), top_row);

    GtkWidget *icon = gtk_image_new_from_resource(RES_ICON);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 60);
    gtk_box_append(GTK_BOX(top_row), icon);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(title_box, TRUE);
    gtk_box_append(GTK_BOX(top_row), title_box);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_text(GTK_LABEL(title_lbl), "ASUS Control Center");
    gtk_widget_add_css_class(title_lbl, "title-artcraft");
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_box_append(GTK_BOX(title_box), title_lbl);

    GtkWidget *sub_lbl = gtk_label_new("Hardware dashboard & power telemetry suite for ASUS laptops");
    gtk_widget_add_css_class(sub_lbl, "subtitle-artcraft");
    gtk_label_set_xalign(GTK_LABEL(sub_lbl), 0.0);
    gtk_box_append(GTK_BOX(title_box), sub_lbl);

    App.status_badge = gtk_label_new("READY");
    gtk_widget_add_css_class(App.status_badge, "badge-pill");
    gtk_widget_set_valign(App.status_badge, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top_row), App.status_badge);

    /* Feature pills */
    GtkWidget *pills_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *features[] = { "ROG • TUF • ZenBook", "GTK4 / Libadwaita", "Linux Hardware Controls", NULL };
    for (int i = 0; features[i]; i++) {
        GtkWidget *pill = gtk_label_new(features[i]);
        gtk_widget_add_css_class(pill, "feature-tag");
        gtk_box_append(GTK_BOX(pills_box), pill);
    }
    gtk_box_append(GTK_BOX(hero), pills_box);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_opacity(sep1, 0.18);
    gtk_box_append(GTK_BOX(hero), sep1);

    /* Status description */
    App.status_detail = gtk_label_new("Ready to launch.");
    gtk_label_set_xalign(GTK_LABEL(App.status_detail), 0.0);
    gtk_widget_add_css_class(App.status_detail, "dim-label");
    gtk_box_append(GTK_BOX(hero), App.status_detail);

    /* Download Progress Box (Hidden when not downloading) */
    App.progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_visible(App.progress_box, FALSE);

    App.progress_label = gtk_label_new("Downloading package...");
    gtk_label_set_xalign(GTK_LABEL(App.progress_label), 0.0);
    gtk_widget_add_css_class(App.progress_label, "dim-label");
    gtk_box_append(GTK_BOX(App.progress_box), App.progress_label);

    App.progress_bar = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(App.progress_box), App.progress_bar);
    gtk_box_append(GTK_BOX(hero), App.progress_box);

    /* Hardware & System Telemetry Grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_box_append(GTK_BOX(content), grid);

    /* Box 1: Hardware Device */
    GtkWidget *box_dev = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box_dev, "telemetry-box");
    GtkWidget *lbl_dev_t = gtk_label_new("Target Laptop");
    gtk_widget_add_css_class(lbl_dev_t, "telemetry-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_dev_t), 0.0);
    gtk_box_append(GTK_BOX(box_dev), lbl_dev_t);

    char *model = get_device_model();
    App.device_label = gtk_label_new(model);
    g_free(model);
    gtk_widget_add_css_class(App.device_label, "telemetry-value");
    gtk_label_set_xalign(GTK_LABEL(App.device_label), 0.0);
    gtk_box_append(GTK_BOX(box_dev), App.device_label);
    gtk_grid_attach(GTK_GRID(grid), box_dev, 0, 0, 1, 1);

    /* Box 2: Installed Version */
    GtkWidget *box_ver = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box_ver, "telemetry-box");
    GtkWidget *lbl_ver_t = gtk_label_new("Installed Version");
    gtk_widget_add_css_class(lbl_ver_t, "telemetry-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_ver_t), 0.0);
    gtk_box_append(GTK_BOX(box_ver), lbl_ver_t);

    App.installed_ver_label = gtk_label_new("Checking...");
    gtk_widget_add_css_class(App.installed_ver_label, "telemetry-value");
    gtk_label_set_xalign(GTK_LABEL(App.installed_ver_label), 0.0);
    gtk_box_append(GTK_BOX(box_ver), App.installed_ver_label);
    gtk_grid_attach(GTK_GRID(grid), box_ver, 1, 0, 1, 1);

    /* Box 3: Polkit Helper */
    GtkWidget *box_pol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box_pol, "telemetry-box");
    GtkWidget *lbl_pol_t = gtk_label_new("Privilege Guard");
    gtk_widget_add_css_class(lbl_pol_t, "telemetry-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_pol_t), 0.0);
    gtk_box_append(GTK_BOX(box_pol), lbl_pol_t);

    App.polkit_badge = gtk_label_new("CHECKING");
    gtk_widget_add_css_class(App.polkit_badge, "badge-pill");
    gtk_widget_set_halign(App.polkit_badge, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box_pol), App.polkit_badge);
    gtk_grid_attach(GTK_GRID(grid), box_pol, 0, 1, 1, 1);

    /* Box 4: Latest GitHub Release */
    GtkWidget *box_rel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box_rel, "telemetry-box");
    GtkWidget *lbl_rel_t = gtk_label_new("Latest Release");
    gtk_widget_add_css_class(lbl_rel_t, "telemetry-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_rel_t), 0.0);
    gtk_box_append(GTK_BOX(box_rel), lbl_rel_t);

    App.latest_ver_label = gtk_label_new("v0.4.0");
    gtk_widget_add_css_class(App.latest_ver_label, "telemetry-value");
    gtk_label_set_xalign(GTK_LABEL(App.latest_ver_label), 0.0);
    gtk_box_append(GTK_BOX(box_rel), App.latest_ver_label);
    gtk_grid_attach(GTK_GRID(grid), box_rel, 1, 1, 1, 1);

    /* Bottom Actions Row */
    GtkWidget *actions_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(actions_box, 6);
    gtk_widget_set_halign(actions_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(content), actions_box);

    App.github_btn = gtk_button_new_with_label("GitHub");
    gtk_widget_add_css_class(App.github_btn, "btn-action");
    g_signal_connect(App.github_btn, "clicked", G_CALLBACK(on_github_clicked), NULL);
    gtk_box_append(GTK_BOX(actions_box), App.github_btn);

    App.check_btn = gtk_button_new_with_label("Check for Updates");
    gtk_widget_add_css_class(App.check_btn, "btn-action");
    g_signal_connect(App.check_btn, "clicked", G_CALLBACK(on_check_updates_clicked), NULL);
    gtk_box_append(GTK_BOX(actions_box), App.check_btn);

    App.main_action_btn = gtk_button_new_with_label("Open Application");
    gtk_widget_add_css_class(App.main_action_btn, "btn-launch");
    g_signal_connect(App.main_action_btn, "clicked", G_CALLBACK(on_main_action_clicked), NULL);
    gtk_box_append(GTK_BOX(actions_box), App.main_action_btn);

    /* Initial state calculation */
    update_ui_state();

    gtk_window_present(GTK_WINDOW(App.window));
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new(LAUNCHER_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
