/* launcher.cpp - ASUS Control Center Launcher
 * Launches the main app and checks for updates.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <adwaita.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME "ASUS Control Center Launcher"
#define MAIN_APP "/home/mostafa/Documents/Default Project/asus-control-center/asus-control-center"

static GtkWidget *status_label;
static GtkWidget *update_label;

/* Launch the main app */
static void launch_app(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    GError *error = NULL;
    gchar *argv[] = { (gchar *)MAIN_APP, NULL };
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        gchar *msg = g_strdup_printf("Failed to launch: %s", error ? error->message : "unknown");
        gtk_label_set_text(GTK_LABEL(status_label), msg);
        g_free(msg);
        g_clear_error(&error);
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "Launched!");
    }
}

/* Check for updates using curl subprocess */
static void check_updates(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    gtk_label_set_text(GTK_LABEL(update_label), "Checking for updates...");

    GError *error = NULL;
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint exit_status = 0;

    gchar *argv[] = {
        (gchar *)"/usr/bin/curl",
        (gchar *)"-s",
        (gchar *)"-L",
        (gchar *)"-A", (gchar *)"ACC-Launcher/1.0",
        (gchar *)"-m", (gchar *)"10",
        (gchar *)"https://api.github.com/repos/mostafamomen/asus-control-center/releases/latest",
        NULL
    };

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &stdout_data, &stderr_data,
                      &exit_status, &error)) {
        gtk_label_set_text(GTK_LABEL(update_label), "Could not check for updates (curl not found)");
        g_clear_error(&error);
        return;
    }

    if (exit_status != 0 || !stdout_data || !*stdout_data) {
        gtk_label_set_text(GTK_LABEL(update_label), "Offline - could not check updates");
        g_free(stdout_data);
        g_free(stderr_data);
        return;
    }

    /* Simple parse: look for "tag_name" in the JSON response */
    gchar *tag = strstr(stdout_data, "\"tag_name\"");
    if (tag) {
        tag = strchr(tag, ':');
        if (tag) {
            tag++;
            while (*tag == ' ' || *tag == '"') tag++;
            gchar *end = strchr(tag, '"');
            if (end) {
                *end = '\0';
                gchar *msg = g_strdup_printf("Latest release: %s", tag);
                gtk_label_set_text(GTK_LABEL(update_label), msg);
                g_free(msg);
            }
        }
    } else {
        gtk_label_set_text(GTK_LABEL(update_label), "No releases found yet");
    }

    g_free(stdout_data);
    g_free(stderr_data);
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 280);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);

    GtkWidget *title = gtk_label_new("ASUS Control Center");
    gtk_widget_add_css_class(title, "title-1");
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *subtitle = gtk_label_new("Launcher");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_box_append(GTK_BOX(box), subtitle);

    status_label = gtk_label_new("Ready to launch");
    gtk_box_append(GTK_BOX(box), status_label);

    update_label = gtk_label_new("Click 'Check for Updates' to check");
    gtk_label_set_wrap(GTK_LABEL(update_label), TRUE);
    gtk_box_append(GTK_BOX(box), update_label);

    GtkWidget *launch_btn = gtk_button_new_with_label("Launch ASUS Control Center");
    gtk_widget_add_css_class(launch_btn, "suggested-action");
    gtk_widget_set_halign(launch_btn, GTK_ALIGN_CENTER);
    g_signal_connect(launch_btn, "clicked", G_CALLBACK(launch_app), NULL);
    gtk_box_append(GTK_BOX(box), launch_btn);

    GtkWidget *check_btn = gtk_button_new_with_label("Check for Updates");
    gtk_widget_set_halign(check_btn, GTK_ALIGN_CENTER);
    g_signal_connect(check_btn, "clicked", G_CALLBACK(check_updates), NULL);
    gtk_box_append(GTK_BOX(box), check_btn);

    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.asus.controlcenter.launcher", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
