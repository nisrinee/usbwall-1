#include "core.h"

#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "devusb.h"
#include "devuser.h"
#include "ipc_pam.h"
#include "ldap_config.h"

/* Globals */
/**
 * \brief core internal global used to trigger the terminaison of the main
 * process.
 */
static int g_terminaison = 0;
/**
 * \brief core internal global used to notify the main process that a cfg update
 * is needed.
 */
static int g_cfgupdate = 0;
/* ****** */

/**
 * \brief core internal function to handle globals lookup.
 * \param cfg the configuration structure. Modified only if an update
 * notification is received
 * \return 1 if the program need to terminate, 0 else way.
 *
 * The function will lookup globals, usually setted by signal handler and
 * show that a change need to be made to the program state.
 */
static int notifs_lookup(struct ldap_cfg **cfg)
{
    if (g_terminaison)
    {
      syslog(LOG_INFO, "Terminaison notif received, terminating program");
      return 1;
    }

    if (g_cfgupdate)
    {
      syslog(LOG_INFO, "Update notif received, updating config");
      *cfg = make_ldap_cfg(cfg_file_find());
      if (*cfg == NULL)
      {
        syslog(LOG_WARNING, "Configuration update failed, terminating program");

        return 1; // no configs found
      }
    }

    return 0;
}

/**
 * \brief core internal function to handle a user connection
 * \param cfg the configuration structure, needed to use devuser functions
 * \param username the login of the connected user.
 *
 * The function will get plugged device list from devusb, it will get the list
 * of authorized devices from devuser, and update the access of devices in the
 * sysfs.
 */
static void handle_login(struct ldap_cfg *cfg, const char *username)
{
  struct devusb **device_list = devices_get();
  char **devids = devids_get(username, cfg);

  /**
   * \todo
   * TODO: update devices_list depending of devids.
   * Only authorized devices should be kept
   */

  if (update_devices(device_list))
    syslog(LOG_WARNING, "Device update failed");

  free_devids(devids);
  free_devices(device_list);
}

static void terminaison_handler(int signo)
{
  syslog(LOG_INFO, "Received signal : %d", signo);

  if (signo == SIGTERM)
    g_terminaison = 1;
  else if (signo == SIGHUP)
    g_cfgupdate = 1;
}

int usbwall_run(void)
{
  if (init_devusb())
    return 1; // devusb initialization error

  int domain_socket_fd = init_socket();
  if (domain_socket_fd == -1)
    return 1; // Unix Domain Socket initialization error

  struct ldap_cfg *cfg = make_ldap_cfg(cfg_file_find());
  if (!cfg)
    return 1; // no configs found

  char *username = NULL;
  while ((username = wait_for_logging(domain_socket_fd)))
  {
    sleep(10);
    if (notifs_lookup(&cfg))
      break; // program terminaison requested

    if (!username)
    {
      syslog(LOG_WARNING, "Current user can not be fetched");
      continue;
    }

    handle_login(cfg, username);
    free(username);
  }

  close_devusb();
  destroy_ldap_cfg(cfg);

  return 0;
}

int signal_handling(void)
{
  struct sigaction action;
  action.sa_handler = terminaison_handler;
  sigfillset(&action.sa_mask);
  action.sa_flags = SA_RESTART;

  if (sigaction(SIGTERM, &action, NULL) == -1)
    return 1;
  if (sigaction(SIGHUP, &action, NULL) == -1)
    return 1;

  return 0;
}