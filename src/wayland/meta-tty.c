/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/major.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "meta-wayland-private.h"
#include "meta-tty.h"

struct tty {
	int fd;
	struct termios terminal_attributes;

	struct wl_event_source *input_source;
	struct wl_event_source *enter_vt_source;
	struct wl_event_source *leave_vt_source;
};

void
meta_tty_enter_vt (struct tty *tty)
{
	ioctl(tty->fd, VT_RELDISP, VT_ACKACQ);
}

void
meta_tty_leave_vt (struct tty *tty)
{
	ioctl(tty->fd, VT_RELDISP, 1);
}

static int
on_tty_input(int fd, uint32_t mask, void *data)
{
	struct tty *tty = data;

	/* Ignore input to tty.  We get keyboard events from evdev
	 */
	tcflush(tty->fd, TCIFLUSH);

	return 1;
}

static int
try_open_vt(void)
{
	int tty0, vt, fd;
	char filename[16];

	tty0 = open("/dev/tty0", O_WRONLY | O_CLOEXEC);
	if (tty0 < 0) {
		g_warning ("could not open tty0: %m\n");
		return -1;
	}

	if (ioctl(tty0, VT_OPENQRY, &vt) < 0 || vt == -1) {
		g_warning ("could not open tty0: %m\n");
		close(tty0);
		return -1;
	}

	close(tty0);
	snprintf(filename, sizeof filename, "/dev/tty%d", vt);
	g_warning ("compositor: using new vt %s\n", filename);
	fd = open(filename, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		return fd;

	if (ioctl(fd, VT_ACTIVATE, vt) < 0 ||
	    ioctl(fd, VT_WAITACTIVE, vt) < 0) {
		g_warning ("failed to swtich to new vt\n");
		close(fd);
		return -1;
	}

	return fd;
}

struct tty *
meta_tty_create (MetaWaylandCompositor *compositor, int tty_nr)
{
	struct termios raw_attributes;
	struct vt_mode mode = { 0 };
	int ret;
	struct tty *tty;
	struct wl_event_loop *loop;
	struct stat buf;
	char filename[16];

	tty = malloc(sizeof *tty);
	if (tty == NULL)
		return NULL;

	memset(tty, 0, sizeof *tty);
	if (tty_nr > 0) {
		snprintf(filename, sizeof filename, "/dev/tty%d", tty_nr);
		g_warning ("compositor: using %s\n", filename);
		tty->fd = open(filename, O_RDWR | O_NOCTTY | O_CLOEXEC);
	} else if (fstat(tty->fd, &buf) == 0 &&
		   major(buf.st_rdev) == TTY_MAJOR &&
		   minor(buf.st_rdev) > 0) {
		tty->fd = fcntl(0, F_DUPFD_CLOEXEC, 0);
	} else {
		/* Fall back to try opening a new VT.  This typically
		 * requires root. */
		tty->fd = try_open_vt();
	}

	if (tty->fd <= 0) {
		g_warning ("failed to open tty: %m\n");
		return NULL;
	}

	if (tcgetattr(tty->fd, &tty->terminal_attributes) < 0) {
		g_warning ("could not get terminal attributes: %m\n");
		return NULL;
	}

	/* Ignore control characters and disable echo */
	raw_attributes = tty->terminal_attributes;
	cfmakeraw(&raw_attributes);

	/* Fix up line endings to be normal (cfmakeraw hoses them) */
	raw_attributes.c_oflag |= OPOST | OCRNL;

	if (tcsetattr(tty->fd, TCSANOW, &raw_attributes) < 0)
		g_warning ("could not put terminal into raw mode: %m\n");

	loop = wl_display_get_event_loop(compositor->wayland_display);
	tty->input_source =
		wl_event_loop_add_fd(loop, tty->fd,
				     WL_EVENT_READABLE, on_tty_input, tty);

	ret = ioctl(tty->fd, KDSETMODE, KD_GRAPHICS);
	if (ret) {
		g_warning ("failed to set KD_GRAPHICS mode on tty: %m\n");
		return NULL;
	}

	mode.mode = VT_PROCESS;
	mode.relsig = SIGUSR1;
	mode.acqsig = SIGUSR2;
	if (ioctl(tty->fd, VT_SETMODE, &mode) < 0) {
		g_warning ("failed to take control of vt handling\n");
		return NULL;
	}

	return tty;
}

void
meta_tty_destroy(struct tty *tty)
{
        if(!tty)
                return;

	if (ioctl(tty->fd, KDSETMODE, KD_TEXT))
		g_warning ("failed to set KD_TEXT mode on tty: %m\n");

	if (tcsetattr(tty->fd, TCSANOW, &tty->terminal_attributes) < 0)
		g_warning ("could not restore terminal to canonical mode\n");

	close(tty->fd);

	free(tty);
}
