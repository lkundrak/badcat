/*
 * badcat - Simple & Stupid Input Lock
 * Copyright (C) 2021  Lubomir Rintel <lkundrak@v3.sk>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <linux/input.h>
#include <libudev.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int grabbed = 0;
struct pollfd *fds;
nfds_t nfds;

static void
update_grab (int fd)
{
	if (ioctl (fd, EVIOCGRAB, grabbed) == -1)
		perror ("EVIOCGRAB");
}

static void
update_grab_all ()
{
	int i;

	for (i = 1; i < nfds; i++)
		update_grab (fds[i].fd);
}

static int
add_device (struct udev_device *dev)
{
	const char *devnode = udev_device_get_devnode (dev);
	const char *sysname = udev_device_get_sysname (dev);
	int version;
	int fd;

	if (devnode == NULL || sysname == NULL)
		return 0;

	if (!udev_device_get_is_initialized (dev))
		return 0;

	if (strncmp (sysname, "event", 5) != 0)
		return 0;

	fd = open (devnode, O_RDWR);
	if (fd == -1) {
		perror (devnode);
		return -1;
	}

	if (ioctl (fd, EVIOCGVERSION, &version) == -1) {
		perror ("Could not read input protocol version");
		goto fail;
	}
	if (version >> 16 != EV_VERSION >> 16) {
		fprintf (stderr, "Bad input subsystem version");
		goto fail;
	}

	fds = realloc (fds, sizeof (*fds) * (nfds + 1));
	if (fds == NULL) {
		perror ("realloc");
		goto fail;
	}
	fds[nfds].fd = fd;
	fds[nfds].events = POLLIN;
	nfds++;

	if (grabbed)
		update_grab (fd);

	return 0;
fail:
	close (fd);
	return -1;
}

int
main (int argc, char *argv[])
{
	struct udev_enumerate *uenum;
	struct udev_list_entry *entry;
	struct udev_monitor *mon;
	struct udev_device *dev;
	struct udev *udev;
	int ret;
	int i;

	udev = udev_new ();
	if (!udev) {
		fprintf (stderr, "Can not create udev context.\n");
		return 1;
	}

	mon = udev_monitor_new_from_netlink (udev, "udev");
	if (!mon) {
		fprintf (stderr, "Can not create udev monitor.\n");
		return 1;
	}

	ret = udev_monitor_filter_add_match_subsystem_devtype (mon, "input", NULL);
	if (ret < 0) {
		fprintf (stderr, "Error setting up udev event filter: %d.\n", ret);
		return 1;
	}

	ret = udev_monitor_enable_receiving (mon);
	if (ret < 0) {
		fprintf (stderr, "Error enabling udev event filter: %d.\n", ret);
		return 1;
	}

	uenum = udev_enumerate_new (udev);
	if (!udev) {
		fprintf (stderr, "Can not create device enumeration context.\n");
		return 1;
	}

	ret = udev_enumerate_add_match_subsystem (uenum, "input");
	if (ret < 0) {
		fprintf (stderr, "Error enabling filtering on udev subsystem: %d.\n", ret);
		return 1;
	}

	ret = udev_enumerate_scan_devices (uenum);
	if (ret < 0) {
		fprintf (stderr, "Error getting the devices from udev: %d.\n", ret);
		return 1;
	}

	nfds = 1;
	fds = malloc (sizeof (*fds));
	if (fds == NULL) {
		perror ("malloc");
		return 1;
	}
	fds[0].fd = udev_monitor_get_fd (mon);
	fds[0].events = POLLIN;

	for (entry = udev_enumerate_get_list_entry (uenum); entry; entry = udev_list_entry_get_next (entry)) {
		dev = udev_device_new_from_syspath (udev, udev_list_entry_get_name (entry));
		if (!udev) {
			fprintf (stderr, "Error getting a device from udev.\n");
			return 1;
		}

		add_device (dev);
	}

	while (1) {
		if (poll (fds, nfds, -1) == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			perror ("poll");
			return 1;
		}

		for (i = 0; i < nfds; i++) {
			struct input_event event;

			if (fds[i].revents == 0)
				continue;

			/* Activity on udev monitor */
			if (i == 0) {
				dev = udev_monitor_receive_device (mon);
				if (strcmp (udev_device_get_action (dev), "add") == 0)
					add_device (dev);
				continue;
			}

			/* Activity on event device */
			switch (read (fds[i].fd, &event, sizeof (event))) {
			case -1:
				if (errno != ENODEV)
					perror ("Error reading from an event device");
				fds[i].events = 0;
				continue;
			case sizeof (event):
				break;
			default:
				fprintf (stderr, "Short read from the event device.\n");
				continue;
			}

			if (event.type == EV_KEY && event.code == KEY_SCROLLLOCK && event.value == 0) {
				grabbed = !grabbed;
				update_grab_all ();
			}
		}

		/* Clean up stale fds */
		for (i = 1; i < nfds; i++) {
			if (fds[i].events != 0)
				continue;
			close (fds[i].fd);
			fds[i] = fds[--nfds];
			fds = realloc (fds, sizeof (*fds) * nfds);
			if (fds == NULL) {
				perror ("realloc");
				return 1;
			}
		}
	}
}
