#include <stdio.h>
#include <dmx_recv.h>
#include <stdlib.h>
#include <glib-unix.h>
#include "httpd.h"
#include <json-glib/json-glib.h>
#include <json-glib/json-glib.h>

static gboolean
sigint_handler(gpointer user_data)
{
  g_main_loop_quit(user_data);
  return TRUE;
}

typedef struct AppContext AppContext;
struct AppContext
{
  char *config_filename;
  GKeyFile *config_file;
  gchar *dmx_device;
  int dmx_speed;
  DMXRecv *dmx_recv;
  HTTPServer *http_server;
};
  
AppContext app_ctxt  = {
  NULL,
  NULL,
  NULL,
  250000,
  NULL,
  NULL
};

static void
app_init(AppContext *app)
{
}

static void
app_cleanup(AppContext* app)
{
  g_clear_object(&app->dmx_recv);
  g_clear_object(&app->http_server);

  g_free(app->config_filename);
  if (app->config_filename) g_key_file_unref(app->config_file);
  g_free(app->dmx_device);
}

static void
configure_string_property(void *obj, const gchar *property, GKeyFile *config,
			  const gchar *group, const gchar *key)
{
  GError *err = NULL;
  guchar *str;
  str = (guchar*)g_key_file_get_string(config, group, key, &err);
  if (str) {
    g_object_set(obj, property, str, NULL);
  } else {
    g_clear_error(&err);
  }
}

static void
configure_http_server(AppContext *app)
{
  GError *err = NULL;
  guint port;
  JsonBuilder *builder;
  port = g_key_file_get_integer(app_ctxt.config_file, "HTTP", "Port", &err);
  if (!err) {
    g_object_set(app->http_server, "http-port", port, NULL);
  } else {
    g_clear_error(&err);
  }
  configure_string_property(app->http_server, "user",
			    app_ctxt.config_file, "HTTP", "User");
  configure_string_property(app->http_server, "password",
			    app_ctxt.config_file, "HTTP", "Password");
  configure_string_property(app->http_server, "http-root",
			    app_ctxt.config_file, "HTTP", "Root");

  builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "foo");
  json_builder_add_int_value(builder, 78);
  json_builder_set_member_name(builder, "bar");
  json_builder_begin_array(builder);
  json_builder_add_double_value(builder, 3.1415);
  json_builder_add_double_value(builder, -1.41);
  json_builder_end_array(builder);
  json_builder_end_object(builder);
  g_object_set(app->http_server,
	       "value-root", json_builder_get_root(builder), NULL);
  g_object_unref(builder);
}


const GOptionEntry app_options[] = {
  {"config-file", 'c', 0, G_OPTION_ARG_FILENAME,
   &app_ctxt.config_filename, "Configuration file", "FILE"},
  {NULL}
};

int
main(int argc, char *argv[])
{
  GError *err = NULL;
  GOptionContext *opt_ctxt;
  GMainLoop *loop;  
  app_init(&app_ctxt);
  g_type_init();
  opt_ctxt = g_option_context_new (" - map DMX to C2IP");
  g_option_context_add_main_entries(opt_ctxt, app_options, NULL);
  if (!g_option_context_parse(opt_ctxt, &argc, &argv, &err)) {
    g_printerr("Failed to parse options: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  g_option_context_free(opt_ctxt);
  if (app_ctxt.config_filename) {
    app_ctxt.config_file = g_key_file_new();
    if (!g_key_file_load_from_file(app_ctxt.config_file,
				   app_ctxt.config_filename,
				   G_KEY_FILE_NONE, &err)) {
      g_printerr("Failed to read configuration file: %s\n", err->message);
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
    }
  }
  app_ctxt.dmx_device =
    g_key_file_get_string(app_ctxt.config_file, "DMXPort", "Device", &err);
  if (!app_ctxt.dmx_device) {
    g_printerr("No device: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  app_ctxt.dmx_speed =
    g_key_file_get_integer(app_ctxt.config_file, "DMXPort", "Speed", &err);
  if (err) {
    app_ctxt.dmx_speed = 250000;
    g_clear_error(&err);
  }

  app_ctxt.dmx_recv = dmx_recv_new(app_ctxt.dmx_device, &err);
  if (!app_ctxt.dmx_recv) {
    g_printerr("Failed setup DMX port: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  app_ctxt.http_server = http_server_new();
  configure_http_server(&app_ctxt);
  if (!http_server_start(app_ctxt.http_server, &err)) {
    g_printerr("Failed to setup HTTP server: %s\n", err->message);
    g_clear_object(&app_ctxt.http_server);
    g_clear_error(&err);
  }
  loop = g_main_loop_new(NULL, FALSE);
  g_unix_signal_add(SIGINT, sigint_handler, loop);
  g_debug("Starting");
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  g_debug("Exiting");
  app_cleanup(&app_ctxt);
  return EXIT_SUCCESS;
}
