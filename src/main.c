#include <adwaita.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#endif

#include "parakeet-audio.h"
#include "parakeet-engine.h"

#define APP_ID "net.sapples.LiveCaptions"
#define DBUS_PATH "/net/sapples/LiveCaptions/CaptionService"
#define DBUS_IFACE "net.sapples.LiveCaptions"
#define MAX_CAPTION_LINE_CHARS 72

static const char *languages[] = {
    "auto", "en-US", "en-GB", "es-US", "es-ES", "fr-FR", "fr-CA",
    "it-IT", "pt-BR", "pt-PT", "nl-NL", "de-DE", "tr-TR", "ru-RU",
    "ar-AR", "hi-IN", "ja-JP", "ko-KR", "vi-VN", "uk-UA", "pl-PL",
    "sv-SE", "cs-CZ", "nb-NO", "da-DK", "bg-BG", "fi-FI", "hr-HR",
    "sk-SK", "zh-CN", "hu-HU", "ro-RO", "et-EE", "el-GR", "lt-LT",
    "lv-LV", "mt-MT", "sl-SI", "he-IL", "th-TH", "nn-NO", NULL
};

typedef struct _LiveCaptionsApplication LiveCaptionsApplication;
typedef struct _LiveCaptionsApplicationClass LiveCaptionsApplicationClass;

struct _LiveCaptionsApplication {
    GApplication parent_instance;
    GDBusConnection *connection;
    guint object_id;
    guint name_owner_id;
    GtkWindow *window;
    GtkTextView *text_view;
    GtkTextBuffer *caption_buffer;
    GtkAdjustment *caption_scroll;
    GtkDropDown *language;
    GtkStringList *language_model;
    GtkDropDown *input;
    GtkStringList *input_model;
    ParakeetEngine *engine;
    ParakeetAudio *audio;
    char *selected_language;
    gboolean active;
    gboolean starting;
    gboolean microphone;
    GMutex caption_lock;
    GString *pending_caption;
    GString *caption_text;
    guint caption_idle_id;
};

struct _LiveCaptionsApplicationClass {
    GApplicationClass parent_class;
};

static gboolean place_window_bottom_center(gpointer user_data);
static void create_window(LiveCaptionsApplication *self);

static GdkMonitor *primary_monitor(GdkDisplay *display) {
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(display))
        return gdk_x11_display_get_primary_monitor(display);
#endif
    GListModel *monitors = gdk_display_get_monitors(display);
    return g_list_model_get_n_items(monitors) > 0
        ? GDK_MONITOR(g_list_model_get_item(monitors, 0)) : NULL;
}

G_DEFINE_TYPE(LiveCaptionsApplication, livecaptions_application, G_TYPE_APPLICATION)

static const char introspection_xml[] =
    "<node><interface name='" DBUS_IFACE "'>"
    "<method name='SetActive'><arg name='active' type='b' direction='in'/></method>"
    "<method name='SetLanguage'><arg name='language' type='s' direction='in'/></method>"
    "<method name='GetLanguages'><arg name='languages' type='as' direction='out'/></method>"
    "<property name='Active' type='b' access='read'/>"
    "<signal name='ActiveChanged'><arg name='active' type='b'/></signal>"
    "<signal name='LanguageChanged'><arg name='language' type='s'/></signal>"
    "</interface></node>";

static void emit_active(LiveCaptionsApplication *self) {
    if (!self->connection) return;
    g_dbus_connection_emit_signal(self->connection, NULL, DBUS_PATH, DBUS_IFACE,
                                  "ActiveChanged", g_variant_new("(b)", self->active), NULL);
}

static void render_caption_lines(LiveCaptionsApplication *self) {
    if (!self->caption_text || !self->caption_buffer) return;

    GString *wrapped = g_string_new(NULL);
    gchar **words = g_strsplit_set(self->caption_text->str, " \t\r\n", -1);
    guint line_length = 0;
    for (guint i = 0; words[i]; i++) {
        if (!words[i][0]) continue;
        guint word_length = g_utf8_strlen(words[i], -1);
        if (line_length && line_length + 1 + word_length > MAX_CAPTION_LINE_CHARS) {
            g_string_append_c(wrapped, '\n');
            line_length = 0;
        } else if (line_length) {
            g_string_append_c(wrapped, ' ');
            line_length++;
        }
        g_string_append(wrapped, words[i]);
        line_length += word_length;
    }
    g_strfreev(words);

    /* Keep the current line and the immediately preceding line only. */
    guint line_breaks = 0;
    for (gchar *p = wrapped->str; *p; p++)
        if (*p == '\n') line_breaks++;
    while (line_breaks > 1) {
        gchar *first_break = strchr(wrapped->str, '\n');
        g_string_erase(wrapped, 0, (gssize)(first_break - wrapped->str + 1));
        line_breaks--;
    }
    gtk_text_buffer_set_text(self->caption_buffer, wrapped->str, -1);
    g_string_free(wrapped, TRUE);
}

static gboolean update_caption(gpointer data) {
    LiveCaptionsApplication *self = data;
    g_mutex_lock(&self->caption_lock);
    char *text = self->pending_caption ? g_string_free(self->pending_caption, FALSE) : NULL;
    self->pending_caption = NULL;
    self->caption_idle_id = 0;
    g_mutex_unlock(&self->caption_lock);

    if (self->active && self->caption_buffer && text && text[0]) {
        if (!self->caption_text)
            self->caption_text = g_string_new(NULL);
        g_string_append(self->caption_text, text);
        if (self->caption_text->len > 640)
            g_string_erase(self->caption_text, 0, self->caption_text->len - 480);
        render_caption_lines(self);

        if (self->caption_scroll)
            gtk_adjustment_set_value(self->caption_scroll,
                                     gtk_adjustment_get_upper(self->caption_scroll));
    }
    g_free(text);
    return G_SOURCE_REMOVE;
}

static void on_caption(const char *text, void *user_data) {
    LiveCaptionsApplication *self = user_data;
    if (!text || !*text) return;
    GString *clean = g_string_new(NULL);
    gboolean in_tag = FALSE;
    for (const char *p = text; *p; p++) {
        if (*p == '<') {
            in_tag = TRUE;
            continue;
        }
        if (in_tag) {
            if (*p == '>') in_tag = FALSE;
            continue;
        }
        g_string_append_c(clean, *p);
    }
    if (!clean->len) {
        g_string_free(clean, TRUE);
        return;
    }
    g_mutex_lock(&self->caption_lock);
    if (!self->pending_caption)
        self->pending_caption = g_string_new(NULL);
    g_string_append(self->pending_caption, clean->str);
    if (!self->caption_idle_id)
        /* A short frame groups decoder fragments into a stable live update
         * without adding noticeable caption latency. */
        self->caption_idle_id = g_timeout_add(80, update_caption, self);
    g_mutex_unlock(&self->caption_lock);
    g_string_free(clean, TRUE);
}

static void stop_captioning(LiveCaptionsApplication *self) {
    self->starting = FALSE;
    if (self->audio) {
        parakeet_audio_stop(self->audio);
        self->audio = NULL;
    }
    if (self->engine) {
        parakeet_engine_free(self->engine);
        self->engine = NULL;
    }
    if (self->caption_buffer)
        gtk_text_buffer_set_text(self->caption_buffer, "", 0);
    if (self->caption_text) {
        g_string_free(self->caption_text, TRUE);
        self->caption_text = NULL;
    }
    g_mutex_lock(&self->caption_lock);
    if (self->pending_caption) {
        g_string_free(self->pending_caption, TRUE);
        self->pending_caption = NULL;
    }
    g_mutex_unlock(&self->caption_lock);
    if (self->window) gtk_widget_set_visible(GTK_WIDGET(self->window), FALSE);
    if (self->active) {
        self->active = FALSE;
        emit_active(self);
    }
}

static void on_audio(const short *samples, size_t count, void *user_data) {
    LiveCaptionsApplication *self = user_data;
    if (self->engine) parakeet_engine_feed(self->engine, samples, count);
}

static void start_captioning(LiveCaptionsApplication *self) {
    if (self->active || self->starting) return;
    self->starting = TRUE;
    const char *configured_model = g_getenv("PARAKEET_MODEL_PATH");
    char *default_model = g_build_filename(g_get_user_data_dir(),
                                           "net.sapples.LiveCaptions", "models",
                                           "nemotron-3.5-asr-streaming-0.6b-q4_k.gguf", NULL);
    char *model = g_strdup(configured_model && *configured_model ? configured_model : default_model);
    g_free(default_model);
    self->engine = parakeet_engine_new(model, self->selected_language, on_caption, self);
    g_free(model);
    if (!self->engine) {
        self->starting = FALSE;
        return;
    }
    if (!self->window) create_window(self);
    self->audio = parakeet_audio_start(self->microphone, on_audio, self);
    if (!self->audio) {
        parakeet_engine_free(self->engine);
        self->engine = NULL;
        self->starting = FALSE;
        return;
    }
    gtk_widget_set_visible(GTK_WIDGET(self->window), TRUE);
    gtk_window_present(GTK_WINDOW(self->window));
    g_idle_add(place_window_bottom_center, self);
    self->active = TRUE;
    self->starting = FALSE;
    emit_active(self);
}

static gboolean start_captioning_idle(gpointer user_data) {
    start_captioning(user_data);
    return G_SOURCE_REMOVE;
}

static void set_active(LiveCaptionsApplication *self, gboolean active) {
    if (active) start_captioning(self);
    else stop_captioning(self);
}

static void on_language_changed(GtkDropDown *drop_down, GParamSpec *pspec, gpointer user_data) {
    LiveCaptionsApplication *self = user_data;
    guint index = gtk_drop_down_get_selected(drop_down);
    if (index >= g_strv_length((char **)languages)) return;
    const char *language = languages[index];
    if (g_strcmp0(language, self->selected_language) == 0) return;
    g_free(self->selected_language);
    self->selected_language = g_strdup(language);
    if (self->connection)
        g_dbus_connection_emit_signal(self->connection, NULL, DBUS_PATH, DBUS_IFACE,
                                      "LanguageChanged", g_variant_new("(s)", language), NULL);
    if (self->engine && !parakeet_engine_set_language(self->engine, language))
        g_warning("Parakeet rejected language %s", language);
}

static void on_input_changed(GtkDropDown *drop_down, GParamSpec *pspec, gpointer user_data) {
    LiveCaptionsApplication *self = user_data;
    gboolean microphone = gtk_drop_down_get_selected(drop_down) == 1;
    if (microphone == self->microphone) return;
    self->microphone = microphone;
    if (self->active) {
        if (self->audio) {
            parakeet_audio_stop(self->audio);
            self->audio = NULL;
        }
        self->audio = parakeet_audio_start(self->microphone, on_audio, self);
        if (!self->audio) stop_captioning(self);
    }
}

static void close_clicked(GtkButton *button, gpointer user_data) {
    set_active(user_data, FALSE);
}

static gboolean window_close_requested(GtkWindow *window, gpointer user_data) {
    set_active(user_data, FALSE);
    return TRUE;
}

static gboolean place_window_bottom_center(gpointer user_data) {
    LiveCaptionsApplication *self = user_data;
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(self->window));
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(display)) {
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self->window));
        GdkMonitor *monitor = primary_monitor(display);
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        int width = gtk_widget_get_width(GTK_WIDGET(self->window));
        int height = gtk_widget_get_height(GTK_WIDGET(self->window));
        Display *xdisplay = gdk_x11_display_get_xdisplay(display);
        XMoveWindow(xdisplay, gdk_x11_surface_get_xid(surface),
                    geometry.x + (geometry.width - width) / 2,
                    geometry.y + geometry.height - height - 32);
        XFlush(xdisplay);
    }
#endif
    return G_SOURCE_REMOVE;
}

static void create_window(LiveCaptionsApplication *self) {
    gtk_init();
    adw_init();
    self->window = GTK_WINDOW(gtk_window_new());
    gtk_widget_add_css_class(GTK_WIDGET(self->window), "caption-shell");
    gtk_window_set_title(GTK_WINDOW(self->window), "Live Captions");
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(self->window));
    GdkMonitor *monitor = primary_monitor(display);
    GdkRectangle geometry = { 0, 0, 1920, 1080 };
    if (monitor) gdk_monitor_get_geometry(monitor, &geometry);
    /* Windows 11's floating live-caption panel starts compact and shows a few
     * lines. It remains freely resizable for longer utterances. */
    gtk_window_set_default_size(GTK_WINDOW(self->window), 720, 150);
    gtk_window_set_resizable(GTK_WINDOW(self->window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(self->window), FALSE);
    g_signal_connect(self->window, "close-request", G_CALLBACK(window_close_requested), self);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "caption-window");
    gtk_widget_set_overflow(root, GTK_OVERFLOW_HIDDEN);
    GtkWidget *handle = gtk_window_handle_new();
    gtk_widget_set_overflow(handle, GTK_OVERFLOW_HIDDEN);
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), root);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "caption-toolbar");
    gtk_widget_set_margin_top(bar, 6);
    gtk_widget_set_margin_start(bar, 10);
    gtk_widget_set_margin_end(bar, 10);
    gtk_widget_set_halign(bar, GTK_ALIGN_END);
    self->input_model = gtk_string_list_new((const char *[]) { "Desktop audio", "Microphone", NULL });
    self->input = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(self->input_model), NULL));
    gtk_widget_add_css_class(GTK_WIDGET(self->input), "caption-control");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->input), "Audio input");
    gtk_drop_down_set_selected(self->input, self->microphone ? 1 : 0);
    g_signal_connect(self->input, "notify::selected", G_CALLBACK(on_input_changed), self);
    self->language_model = gtk_string_list_new(languages);
    self->language = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(self->language_model), NULL));
    gtk_widget_add_css_class(GTK_WIDGET(self->language), "caption-control");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->language), "Language");
    guint selected_language = 0;
    for (guint i = 0; languages[i]; i++) {
        if (g_str_equal(languages[i], self->selected_language)) {
            selected_language = i;
            break;
        }
    }
    gtk_drop_down_set_selected(self->language, selected_language);
    g_signal_connect(self->language, "notify::selected", G_CALLBACK(on_language_changed), self);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close, "caption-control");
    gtk_widget_set_tooltip_text(close, "Close live captions");
    gtk_widget_add_css_class(close, "flat");
    g_signal_connect(close, "clicked", G_CALLBACK(close_clicked), self);
    gtk_box_append(GTK_BOX(bar), GTK_WIDGET(self->input));
    gtk_box_append(GTK_BOX(bar), GTK_WIDGET(self->language));
    gtk_box_append(GTK_BOX(bar), close);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scrolled, "caption-text-area");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_NEVER);
    gtk_widget_set_margin_start(scrolled, 16);
    gtk_widget_set_margin_end(scrolled, 16);
    gtk_widget_set_margin_top(scrolled, 4);
    gtk_widget_set_margin_bottom(scrolled, 12);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);

    self->text_view = GTK_TEXT_VIEW(gtk_text_view_new());
    self->caption_buffer = gtk_text_view_get_buffer(self->text_view);
    gtk_text_view_set_editable(self->text_view, FALSE);
    gtk_text_view_set_cursor_visible(self->text_view, FALSE);
    gtk_text_view_set_wrap_mode(self->text_view, GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(self->text_view, 2);
    gtk_text_view_set_right_margin(self->text_view, 2);
    gtk_text_view_set_top_margin(self->text_view, 2);
    gtk_text_view_set_bottom_margin(self->text_view, 2);
    gtk_widget_set_hexpand(GTK_WIDGET(self->text_view), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self->text_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(self->text_view));
    self->caption_scroll = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));

    gtk_box_append(GTK_BOX(root), bar);
    gtk_box_append(GTK_BOX(root), scrolled);
    gtk_window_set_child(self->window, handle);
    gtk_widget_set_visible(GTK_WIDGET(self->window), FALSE);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".caption-shell { background-color: transparent; }"
        ".caption-window { background-color: alpha(@theme_bg_color, 0.94); color: @theme_fg_color; border: 1px solid alpha(@theme_fg_color, 0.16); border-radius: 12px; }"
        ".caption-window textview, .caption-window textview text { background-color: transparent; color: @theme_fg_color; }"
        ".caption-window textview { font-size: 16px; }"
        ".caption-window dropdown, .caption-window button { color: @theme_fg_color; border-radius: 8px; }"
        ".caption-window dropdown > button, .caption-window button.caption-control { background-color: alpha(@theme_fg_color, 0.08); }"
        "popover, popover > contents, popover listview, popover row { background-color: @theme_bg_color; color: @theme_fg_color; border-radius: 10px; }"
        "popover row:hover, popover row:selected { background-color: alpha(@theme_fg_color, 0.14); color: @theme_fg_color; }", -1);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

static void dbus_method_call(GDBusConnection *connection, const char *sender, const char *path,
                             const char *interface, const char *method, GVariant *parameters,
                             GDBusMethodInvocation *invocation, gpointer user_data) {
    LiveCaptionsApplication *self = user_data;
    if (g_str_equal(method, "SetActive")) {
        gboolean active;
        g_variant_get(parameters, "(b)", &active);
        if (active) {
            if (!self->active && !self->starting)
                g_idle_add(start_captioning_idle, self);
        } else {
            stop_captioning(self);
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, "SetLanguage")) {
        const char *language;
        g_variant_get(parameters, "(&s)", &language);
        for (guint i = 0; languages[i]; i++) {
            if (g_str_equal(language, languages[i])) {
                g_free(self->selected_language);
                self->selected_language = g_strdup(language);
                if (self->language) gtk_drop_down_set_selected(self->language, i);
                break;
            }
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, "GetLanguages")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(^as)", languages));
    }
}

static GVariant *dbus_get_property(GDBusConnection *connection, const char *sender, const char *path,
                                   const char *interface, const char *property, GError **error,
                                   gpointer user_data) {
    LiveCaptionsApplication *self = user_data;
    if (g_str_equal(property, "Active")) return g_variant_new_boolean(self->active);
    return NULL;
}

static const GDBusInterfaceVTable dbus_vtable = { dbus_method_call, dbus_get_property, NULL };

static void on_name_acquired(GDBusConnection *connection, const char *name, gpointer user_data) {
    (void)name;
    LiveCaptionsApplication *self = user_data;
    GError *error = NULL;
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (node) {
        self->object_id = g_dbus_connection_register_object(connection, DBUS_PATH,
                                                              node->interfaces[0], &dbus_vtable,
                                                              self, NULL, &error);
        g_dbus_node_info_unref(node);
    }
    if (error) {
        g_warning("D-Bus setup failed: %s", error->message);
        g_clear_error(&error);
    }
}

static void app_startup(GApplication *application) {
    LiveCaptionsApplication *self = (LiveCaptionsApplication *)application;
    G_APPLICATION_CLASS(livecaptions_application_parent_class)->startup(application);
    g_application_hold(application);
    self->selected_language = g_strdup("auto");
    GError *error = NULL;
    self->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (self->connection)
        self->name_owner_id = g_bus_own_name_on_connection(self->connection, APP_ID,
                                                            G_BUS_NAME_OWNER_FLAGS_NONE,
                                                            on_name_acquired, NULL, self, NULL);
    if (error) { g_warning("D-Bus setup failed: %s", error->message); g_clear_error(&error); }
}

static void app_activate(GApplication *application) {
    (void)application;
}

static void app_shutdown(GApplication *application) {
    LiveCaptionsApplication *self = (LiveCaptionsApplication *)application;
    stop_captioning(self);
    if (self->connection && self->object_id) g_dbus_connection_unregister_object(self->connection, self->object_id);
    if (self->name_owner_id) g_bus_unown_name(self->name_owner_id);
    self->object_id = 0;
    self->name_owner_id = 0;
    g_clear_object(&self->connection);
    g_free(self->selected_language);
    g_mutex_clear(&self->caption_lock);
    G_APPLICATION_CLASS(livecaptions_application_parent_class)->shutdown(application);
}

static void livecaptions_application_class_init(LiveCaptionsApplicationClass *klass) {
    GApplicationClass *app = G_APPLICATION_CLASS(klass);
    app->startup = app_startup;
    app->activate = app_activate;
    app->shutdown = app_shutdown;
}

static void livecaptions_application_init(LiveCaptionsApplication *self) {
    g_mutex_init(&self->caption_lock);
}

int main(int argc, char **argv) {
    LiveCaptionsApplication *app = g_object_new(livecaptions_application_get_type(),
                                                 "application-id", APP_ID,
                                                 "flags", G_APPLICATION_NON_UNIQUE,
                                                 NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
