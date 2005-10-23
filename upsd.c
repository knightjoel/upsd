/* $jwk$
 *
 * ups.c
 *
 * Get status from an APC Back-UPS 800 via USB
 *
 * Joel Knight
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <syslog.h>
#include <sys/wait.h>
#include <pwd.h>


#define UID			"nobody"
#define GID			"nobody"
#define UPS_DEV			"/dev/uhid0"
#define CHROOT			"/var/empty"

#define MSG_SIZE		9

#define UPS_POWER		7
#define UPS_BATTERY		12
#define UPS_LOAD		13

/* UPS_POWER */
#define UPS_POWER_BAT		10
#define UPS_POWER_UTIL		12
#define UPS_POWER_UTIL_CHRG	13

/* UPS_LOAD */
#define UPS_LOAD_FACTOR		256

extern char *__progname;

float get_runtime(int);


int main(int argc, char *argv[])
{
	struct passwd *pwent;
	char *ups_dev = UPS_DEV;
	unsigned char ups_msg[MSG_SIZE];
	char src[BUFSIZ] = "?";
	int bat = 0;
        float rt = 0;
	int fd, msize, i;
	int lastsrc = 0;
	time_t t;

	tzset();
	openlog("upsd", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	if (argc > 2) 
		errx(1, "Usage: %s [device]\n", __progname);

	if (getuid())
		errx(1, "Must be run as root");

	if (argc == 2)
		ups_dev = argv[1];

	if ((fd = open(ups_dev, O_RDONLY)) == -1) {
		perror("Unable to open USB device");
		return (1);
	}

	if (daemon(1, 1) == -1)
		errx(1, "daemon");

	pwent = getpwnam(UID);
	if (!pwent) {
		perror("getpwnam");
		return (1);
	}

	if (chroot(CHROOT) == -1 || chdir("/") == -1) {
		syslog(LOG_ERR, "cannot chdir to %s", CHROOT);
		exit(1);
	}

	setgroups(1, &pwent->pw_gid);
	setegid(pwent->pw_gid);
	setgid(pwent->pw_gid);
	seteuid(pwent->pw_uid);
	setuid(pwent->pw_uid);

	syslog(LOG_INFO, "starting up (device = %s)", ups_dev);

	memset(&ups_msg, 0, sizeof(ups_msg));
	while ((msize = read(fd, ups_msg, sizeof(ups_msg))) > 0) {
		switch (ups_msg[0]) {
			case UPS_POWER:
				switch (ups_msg[1]) {
					case UPS_POWER_BAT:
						sprintf(src, "%s", "on battery");
						break;
					case UPS_POWER_UTIL:
						sprintf(src, "%s", "utility power");
						break;
					case UPS_POWER_UTIL_CHRG:
						sprintf(src, "%s", "charging battery");
						break;
					default:
						sprintf(src, "%s", "unknown");
						break;
				}
				if (ups_msg[1] != lastsrc) {
					syslog(LOG_INFO, "state change to: %s", src);
					t = 0;
				}
				lastsrc = ups_msg[1];
				break;
			case UPS_BATTERY:
				bat = ups_msg[1];
				rt = get_runtime(ups_msg[2]);
				if ((lastsrc == UPS_POWER_BAT || lastsrc == UPS_POWER_UTIL_CHRG)
						&& t <= time(NULL)) {
					syslog(LOG_INFO, "battery status: battery=%d%%, runtime=%.1fmin", 
						bat, rt);
					t = time(NULL)+60;
				}
				break;
			case UPS_LOAD:
				//printf("%% ups load = %d; estimated runtime = %.1fmin (%d)\n", 
				//	ups_msg[1], get_runtime(ups_msg[2]), ups_msg[2]);
				break;
			default:
				/*
				printf("Got (%d) ", msize);
				for (i = 0; i < msize; i++)
					printf("%3d ", ups_msg[i]);
				printf("\n");
				*/
				break;
		}
		
		setproctitle("%s [%d%%/%.1fmin]", src, bat, rt);
		memset(&ups_msg, 0, sizeof(ups_msg));
	}

	syslog(LOG_INFO, "exiting");
	close(fd);

	return (0);
}


float get_runtime(int ups_runtime)
{
	float rt;

	rt = ups_runtime * UPS_LOAD_FACTOR;
	rt = (rt / 3600) * 60;
	return (rt);
}

