/*
 *
 *  bluez-tools - a set of tools to manage bluetooth devices for linux
 *
 *  Copyright (C) 2010-2011  Alexander Orlenko <zxteam@gmail.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#define _BSD_SOURCE
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "lib/dbus-common.h"
#include "lib/helpers.h"
#include "lib/agent-helper.h"
#include "lib/bluez-api.h"
#include <syslog.h>
static gboolean need_unregister = TRUE;
static GMainLoop *mainloop = NULL;
static guint8 device_count = 0;
static gboolean isDeviceConnected = FALSE;
static GHashTable *pin_hash_table = NULL;
static gchar *pin_arg = NULL;
static
extern int wait_button_event(void);

static void _device_created(GDBusConnection *connection, const gchar *sender_name, const gchar *object_path, const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
	g_print("%s:\n", __FUNCTION__);
}

static void _manager_device_found(GDBusConnection *connection, const gchar *sender_name, const gchar *object_path, const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
    g_assert(user_data != NULL);
    const gchar *adapter_object_path = user_data;

    GVariant *arg0 = g_variant_get_child_value(parameters, 0);
    const gchar *str_object_path = g_variant_get_string(arg0, NULL);
    g_variant_unref(arg0);

    if (!g_str_has_prefix(str_object_path, adapter_object_path))
        return;

    GVariant *interfaces_and_properties = g_variant_get_child_value(parameters, 1);
    GVariant *properties = NULL;
    if (g_variant_lookup(interfaces_and_properties, DEVICE_DBUS_INTERFACE, "@a{sv}", &properties))
    {
        g_print("[%s]\n", g_variant_get_string(g_variant_lookup_value(properties, "Address", NULL), NULL));
        g_print("  Name: %s\n", g_variant_lookup_value(properties, "Name", NULL) != NULL ? g_variant_get_string(g_variant_lookup_value(properties, "Name", NULL), NULL) : NULL);
        g_print("  Alias: %s\n", g_variant_lookup_value(properties, "Alias", NULL) != NULL ? g_variant_get_string(g_variant_lookup_value(properties, "Alias", NULL), NULL) : NULL);
        g_print("  Address: %s\n", g_variant_lookup_value(properties, "Address", NULL) != NULL ? g_variant_get_string(g_variant_lookup_value(properties, "Address", NULL), NULL) : NULL);
        g_print("  Icon: %s\n", g_variant_lookup_value(properties, "Icon", NULL) != NULL ? g_variant_get_string(g_variant_lookup_value(properties, "Icon", NULL), NULL) : NULL);
        g_print("  Class: 0x%x\n", g_variant_lookup_value(properties, "Class", NULL) != NULL ? g_variant_get_uint32(g_variant_lookup_value(properties, "Class", NULL)) : 0x0);
        g_print("  LegacyPairing: %d\n", g_variant_lookup_value(properties, "LegacyPairing", NULL) != NULL ? g_variant_get_boolean(g_variant_lookup_value(properties, "LegacyPairing", NULL)) : FALSE);
        g_print("  Paired: %d\n", g_variant_lookup_value(properties, "Paired", NULL) != NULL ? g_variant_get_boolean(g_variant_lookup_value(properties, "Paired", NULL)) : FALSE);
        g_print("  RSSI: %d\n", g_variant_lookup_value(properties, "RSSI", NULL) != NULL ? g_variant_get_int16(g_variant_lookup_value(properties, "RSSI", NULL)) : 0x0);
        g_print("\n");

        g_variant_unref(properties);
    }
    g_variant_unref(interfaces_and_properties);
}

void call_shell_cmd(void)
{
	char *buffer[100];
	FILE* file = (FILE *)popen("/usr/bin/mpg321 /etc/sound/beeeep.mp3 > /dev/null 2>&1", "r");
	fgets((char *)buffer, 100, file);
	// printf("%s:%s\n",__FUNCTION__, buffer);
}

static void _adapter_property_changed(GDBusConnection *connection, const gchar *sender_name, const gchar *object_path, const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data)
{
	static gchar *current_device = NULL;
    g_assert(user_data != NULL);
    Adapter *adapter = user_data;
    
    GVariant *arg0 = g_variant_get_child_value(parameters, 0);
    const gchar *str_object_path = g_variant_get_string(arg0, NULL);
    g_variant_unref(arg0);

    //g_print("%s:%s %s\n", __FUNCTION__, object_path, str_object_path);
    if (g_strcmp0(str_object_path, DEVICE_DBUS_INTERFACE) == 0)
    {
		GVariant *changed_properties = g_variant_get_child_value(parameters, 1);

		Device *device = device_new(object_path);
		GVariant * device_properties = device_get_properties(device, NULL);

		if(g_variant_get_boolean(g_variant_lookup_value(device_properties, "Connected", NULL)))
		{
			if(current_device == NULL)
			{
				current_device = g_strdup (object_path);
				g_print("Connected to a new device\n");

		        syslog(LOG_NOTICE,"Connected to a new device\n");
				isDeviceConnected = TRUE;
				device_count++;
				adapter_set_discoverable(adapter, g_variant_get_boolean(g_variant_new_boolean(FALSE)), NULL);
			}
			else
			{
				if(g_strcmp0(current_device, object_path) != 0)
				{
					g_print("  Trusted: %d\n", g_variant_lookup_value(device_properties, "Trusted", NULL) != NULL ? g_variant_get_boolean(g_variant_lookup_value(device_properties, "Trusted", NULL)) : FALSE);
					g_print("Replacing the old device by the new one\n");
					Device *kicked_device = device_new(current_device);
					device_disconnect(kicked_device, NULL);
					g_free(current_device);
					current_device = g_strdup (object_path);
				}
				
			}

		}
		else
		{
			static int disconnect_count = 0;
			disconnect_count++;
			g_print("device Disconnected %d\n", disconnect_count);
		    syslog(LOG_NOTICE,"device Disconnected\n");
		    device_count --;
			if(g_strcmp0(current_device, object_path) == 0)
			{
				g_free(current_device);
				current_device = NULL;
				adapter_set_discoverable(adapter, g_variant_get_boolean(g_variant_new_boolean(TRUE)), NULL);
			}
		}

		g_variant_unref(device_properties);

		g_variant_unref(changed_properties);
    }
}

static void signal_handler(int sig)
{
	g_message("%s received", sig == SIGTERM ? "SIGTERM" : (sig == SIGUSR1 ? "SIGUSR1" : "SIGINT"));

	if (sig == SIGUSR1 && pin_arg)
        {
            /* Re-read PIN's file */
            g_print("Re-reading PIN's file\n");
            // _read_pin_file(pin_arg, pin_hash_table, FALSE);
	}
        else if (sig == SIGTERM || sig == SIGINT)
        {
	    	syslog(LOG_NOTICE,"kill process\n");
            if (g_main_loop_is_running(mainloop))
                g_main_loop_quit(mainloop);
	}
}

static gchar *capability_arg = NULL;
static gboolean daemon_arg = FALSE;

static GOptionEntry entries[] = {
	{"capability", 'c', 0, G_OPTION_ARG_STRING, &capability_arg, "Agent capability", "<capability>"},
	{"pin", 'p', 0, G_OPTION_ARG_STRING, &pin_arg, "Path to the PIN's file"},
	{"daemon", 'd', 0, G_OPTION_ARG_NONE, &daemon_arg, "Run in background (as daemon)"},
	{NULL}
};
void *setTimeOut(void *data)
{
   static int count = 0;
   // printf("%s\n", __FUNCTION__);
   while(1)
   {
   		if(device_count > 0)
   		{
   			count = 0;
   		}
   		else
   		{
   			count++;
   		}

   		if(count > 30)
   		{
   			g_print("session timeout %d\n", count);
   			count = 0;
            if (g_main_loop_is_running(mainloop))
                g_main_loop_quit(mainloop);
   			// g_main_loop_quit(mainloop);
   		}
       // count ++;
       // if(count > 30 && isDeviceConnected == FALSE){
       //       g_main_loop_quit(mainloop);
       // }
       // else if(count < 30 && isDeviceConnected == TRUE) {
       //     g_thread_exit(NULL);
       // }
       sleep(1);
   }
}

int main(int argc, char *argv[])
{
	GError *error = NULL;
    gchar *capability_arg = "NoInputNoOutput";

	if (daemon_arg) {
		pid_t pid, sid;

		/* Fork off the parent process */
		pid = fork();
		if (pid < 0)
			exit(EXIT_FAILURE);
		/* Ok, terminate parent proccess */
		if (pid > 0)
			exit(EXIT_SUCCESS);

		/* Create a new SID for the child process */
		sid = setsid();
		if (sid < 0)
			exit(EXIT_FAILURE);

		/* Close out the standard file descriptors */
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		openlog ("bt-agent", LOG_PID, LOG_DAEMON);
		syslog(LOG_NOTICE,"start bt-agent\n");
	}

	/* Query current locale */
	setlocale(LC_CTYPE, "");

	while(1)
	{
		// Deprecated
		dbus_init();

		if (!dbus_system_connect(&error))
		{
			g_printerr("Couldn't connect to DBus system bus: %s\n", error->message);
			exit(EXIT_FAILURE);
		}

		/* Check, that bluetooth daemon is running */
		if (!intf_supported(BLUEZ_DBUS_SERVICE_NAME, MANAGER_DBUS_PATH, MANAGER_DBUS_INTERFACE))
		{
			g_printerr("%s: bluez service is not found\n", g_get_prgname());
			g_printerr("Did you forget to run bluetoothd?\n");
			exit(EXIT_FAILURE);
		}
	        
	    wait_button_event();
	    call_shell_cmd();      
	    GThread *setTimeOutThread = g_thread_new("setTimeOut", (GThreadFunc)setTimeOut, NULL);       

		mainloop = g_main_loop_new(NULL, FALSE);

		Manager *manager = g_object_new(MANAGER_TYPE, NULL);
		
		Adapter *adapter = find_adapter(NULL, &error);
		exit_if_error(error);

		adapter_set_discoverable(adapter, g_variant_get_boolean(g_variant_new_boolean(TRUE)), &error);
		exit_if_error(error);

		g_dbus_connection_signal_subscribe(system_conn, "org.bluez", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, _manager_device_found, (gpointer) adapter_get_dbus_object_path(adapter), NULL);
	    exit_if_error(error);
	    g_print("%s\n", adapter_get_dbus_object_path(adapter));
		g_dbus_connection_signal_subscribe(system_conn, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, _adapter_property_changed, adapter, NULL);
	    exit_if_error(error);

		g_dbus_connection_signal_subscribe(system_conn, "org.bluez", "org.freedesktop.DBus.Properties", "DeviceCreated", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, _device_created, NULL, NULL);
	    exit_if_error(error);
	    
		AgentManager *agent_manager = agent_manager_new();

		if(daemon_arg)
			register_agent_callbacks(TRUE, pin_hash_table, mainloop, &error);
		else
			register_agent_callbacks(TRUE, pin_hash_table, mainloop, &error);

		exit_if_error(error);

	    agent_manager_register_agent(agent_manager, AGENT_PATH, capability_arg, &error);
		exit_if_error(error);
		g_print("Agent registered\n"); 
		syslog(LOG_NOTICE,"Agent registered\n"); 

        agent_manager_request_default_agent(agent_manager, AGENT_PATH, &error);
		exit_if_error(error);
        g_print("Default agent requested\n");
		syslog(LOG_NOTICE,"Default agent requested\n"); 


		/* Add SIGTERM/SIGINT/SIGUSR1 handlers */
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = signal_handler;
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGUSR1, &sa, NULL);

		g_main_loop_run(mainloop);

		if (need_unregister) {
			g_print("unregistering agent...\n");
			agent_manager_unregister_agent(agent_manager, AGENT_PATH, &error);
			exit_if_error(error);

			/* Waiting for AgentReleased signal */
			// g_main_loop_run(mainloop);
		}

		g_main_loop_unref(mainloop);

	    unregister_agent_callbacks(NULL);
	    g_object_unref(agent_manager);
		g_object_unref(manager);

		dbus_disconnect();
		// exit(EXIT_SUCCESS);
	}

	
}
