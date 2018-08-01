#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>

static ssize_t lxc_write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;
again:
	ret = write(fd, buf, count);
	if (ret < 0 && errno == EINTR)
		goto again;

	return ret;
}

static ssize_t lxc_read_nointr(int fd, void *buf, size_t count)
{
	ssize_t ret;
again:
	ret = read(fd, buf, count);
	if (ret < 0 && errno == EINTR)
		goto again;

	return ret;
}

int setup_ns() {
	int wstatus;
	ssize_t ret;
	int pipe_fds[2];
	char nspath[PATH_MAX];
	int nsfd = -1, fd = -1, pid = -1;

	// Create sync pipe
	ret = pipe2(pipe_fds, O_CLOEXEC);
	if (ret < 0) {
		return -1;
	}

	// Spawn child
	pid = fork();
	if (pid < 0) {
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		return -1;
	}

	if (pid == 0) {
		close(pipe_fds[1]);

		// Wait for unshare to be done
		ret = lxc_read_nointr(pipe_fds[0], nspath, 1);
		close(pipe_fds[0]);
		if (ret < 0) {
			return -1;
		}

		// Create the mountpoint
		if (mkdir("/var/snap/lxd/common/ns", 0700) < 0 && errno != EEXIST) {
			return -1;
		}

		// Mount a tmpfs
		if (mount("tmpfs", "/var/snap/lxd/common/ns", "tmpfs", 0, "size=1M,mode=0700") < 0) {
			return -1;
		}

		// Mark the tmpfs mount as MS_PRIVATE
		if (mount("none", "/var/snap/lxd/common/ns", NULL, MS_REC|MS_PRIVATE, NULL) < 0) {
			return -1;
		}

		// Store reference to the mntns
		if (snprintf(nspath, PATH_MAX, "/proc/%u/ns/mnt", (unsigned)getppid()) < 0) {
			return -1;
		}

		fd = open("/var/snap/lxd/common/ns/shmounts", O_CREAT | O_RDWR);
		if (fd < 0) {
			return -1;
		}
		close(fd);

		if (mount(nspath, "/var/snap/lxd/common/ns/shmounts", NULL, MS_BIND, NULL) < 0) {
			return -1;
		}

		return 0;
	}

	close(pipe_fds[0]);

	// Unshare the mount namespace
	if (unshare(CLONE_NEWNS) < 0) {
		close(pipe_fds[1]);
		return -1;
	}

	ret = lxc_write_nointr(pipe_fds[1], "1", 1);
	close(pipe_fds[1]);
	if (ret < 0) {
		return -1;
	}

	// Create the mountpoint
	if (mkdir("/var/snap/lxd/common/shmounts", 0711) < 0 && errno != EEXIST) {
		return -1;
	}

	// Create a mount entry
	if (mount("/var/snap/lxd/common/shmounts", "/var/snap/lxd/common/shmounts", NULL, MS_BIND, NULL) < 0) {
		return -1;
	}

	// Mark the mount entry as MS_PRIVATE (hide from PID1)
	if (mount("none", "/var/snap/lxd/common/shmounts", NULL, MS_REC|MS_PRIVATE, NULL) < 0) {
		return -1;
	}

	// Mount a tmpfs
	if (mount("tmpfs", "/var/snap/lxd/common/shmounts", "tmpfs", 0, "size=1M,mode=0711") < 0) {
		return -1;
	}

	// Mark the tmpfs mount as MS_SHARED
	if (mount("none", "/var/snap/lxd/common/shmounts", NULL, MS_REC|MS_SHARED, NULL) < 0) {
		return -1;
	}

	// Wait for the child to be done
	if (wait(&wstatus) < 0) {
		return -1;
	}

	if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
		return -1;
	}

	// Re-attach to PID1 mntns
	nsfd = open("/proc/1/ns/mnt", O_RDONLY);
	if (nsfd < 0) {
		return -1;
	}

	if (setns(nsfd, CLONE_NEWNS) < 0) {
		return -1;
	}
	close(nsfd);

	// Cleanup spare mount entry
	if (umount("/var/snap/lxd/common/shmounts") < 0) {
		return -1;
	}

	return 0;
}

int main() {
	bool setup = true;
	bool run_media = false;
	int nsfd = -1, newnsfd = -1;

	// Get a reference to current mtnns
	nsfd = open("/proc/self/ns/mnt", O_RDONLY);
	if (nsfd < 0) {
		return -1;
	}

	// Attach to PID1 mntns
	newnsfd = open("/proc/1/ns/mnt", O_RDONLY);
	if (newnsfd < 0) {
		return -1;
	}

	if (setns(newnsfd, CLONE_NEWNS) < 0) {
		return -1;
	}
	close(newnsfd);

	// Attempt to attach to our hidden mntns
	newnsfd = open("/var/snap/lxd/common/ns/shmounts", O_RDONLY);
	if (newnsfd >= 0) {
		if (setns(newnsfd, CLONE_NEWNS) == 0) {
			setup = false;
		}
	}

	// Run setup if needed
	if (setup) {
		if (setup_ns() < 0) {
			return -1;
		}

		// Attach to the new hidden mntns
		newnsfd = open("/var/snap/lxd/common/ns/shmounts", O_RDONLY);
		if (newnsfd < 0) {
			return -1;
		}

		if (setns(newnsfd, CLONE_NEWNS) < 0) {
			return -1;
		}

		setup = true;
	}

	// Look for /run/media
	if (access("/run/media", X_OK) == 0) {
		run_media = true;
	}

	// Create temporary mountpoint
	if (run_media) {
		if (mkdir("/run/media/.lxd-shmounts", 0700) < 0 && errno != EEXIST) {
			return -1;
		}
	} else {
		if (mkdir("/media/.lxd-shmounts", 0700) < 0 && errno != EEXIST) {
			return -1;
		}
	}

	// Bind-mount onto temporary mountpoint
	if (run_media) {
		if (mount("/var/snap/lxd/common/shmounts", "/run/media/.lxd-shmounts", NULL, MS_BIND|MS_REC, NULL) < 0) {
			return -1;
		}
	} else {
		if (mount("/var/snap/lxd/common/shmounts", "/media/.lxd-shmounts", NULL, MS_BIND|MS_REC, NULL) < 0) {
			return -1;
		}
	}

	// Attach to the snapd mntns
	if (setns(nsfd, CLONE_NEWNS) < 0) {
		return -1;
	}

	// Bind-mount onto final destination
	if (mount("/media/.lxd-shmounts", "/var/snap/lxd/common/shmounts", NULL, MS_BIND|MS_REC, NULL) < 0) {
		return -1;
	}

	// Mark temporary mountpoint private
	if (mount("none", "/media/.lxd-shmounts", NULL, MS_REC|MS_PRIVATE, NULL) < 0) {
		return -1;
	}

	// Get rid of the temporary mountpoint from snapd mntns
	if (umount2("/media/.lxd-shmounts", MNT_DETACH) < 0) {
		return -1;
	}

	// Attach to the snapd mntns
	if (setns(newnsfd, CLONE_NEWNS) < 0) {
		return -1;
	}

	// Attempt to cleanup mount there too (may be gone or may be there)
	if (run_media) {
		mount("none", "/run/media/.lxd-shmounts", NULL, MS_REC|MS_PRIVATE, NULL);
		umount2("/run/media/.lxd-shmounts", MNT_DETACH);
	} else {
		mount("none", "/media/.lxd-shmounts", NULL, MS_REC|MS_PRIVATE, NULL);
		umount2("/media/.lxd-shmounts", MNT_DETACH);
	}

	// Remove the temporary mountpoint
	if (run_media) {
		if (rmdir("/run//media/.lxd-shmounts") < 0 && errno != ENOENT) {
			return -1;
		}
	} else {
		if (rmdir("/media/.lxd-shmounts") < 0 && errno != ENOENT) {
			return -1;
		}
	}

	// Close open fds
	close(nsfd);
	close(newnsfd);
	return 0;
}
