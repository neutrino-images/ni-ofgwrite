#include "ofgwrite.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <getopt.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <syslog.h>
#include <sys/mount.h>
#include <unistd.h>
#include <errno.h>

const char ofgwrite_version[] = "3.9.7";
int flash_kernel = 0;
int flash_rootfs = 0;
int no_write     = 0;
int quiet        = 0;
int show_help    = 0;
int found_kernel_device = 0;
int found_rootfs_device = 0;
int user_mtd_kernel = 0;
int user_mtd_rootfs = 0;
int newroot_mounted = 0;
char kernel_filename[1000];
char kernel_device[1000];
char kernel_mtd_device_arg[1000];
char rootfs_filename[1000];
char rootfs_device[1000];
char rootfs_mtd_device_arg[1000];
char rootfs_ubi_device[1000];
enum RootfsTypeEnum rootfs_type;
char media_mounts[30][500];
int media_mount_count = 0;
int stop_neutrino_needed = 1;


void my_printf(char const *fmt, ...)
{
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	// print to console
	vprintf(fmt, ap);
	va_end(ap);

	// print to syslog
	vsyslog(LOG_INFO, fmt, ap2);
	va_end(ap2);
}

void my_fprintf(FILE * f, char const *fmt, ...)
{
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	// print to file (normally stdout or stderr)
	vfprintf(f, fmt, ap);
	va_end(ap);

	// print to syslog
	vsyslog(LOG_INFO, fmt, ap2);
	va_end(ap2);
}

void printUsage()
{
	my_printf("Usage: ofgwrite <parameter> <image_directory>\n");
	my_printf("Options:\n");
	my_printf("   -k --kernel           flash kernel with automatic device recognition(default)\n");
	my_printf("   -kmtdx --kernel=mtdx  use mtdx device for kernel flashing\n");
	my_printf("   -r --rootfs           flash rootfs with automatic device recognition(default)\n");
	my_printf("   -rmtdy --rootfs=mtdy  use mtdy device for rootfs flashing\n");
	my_printf("   -mx --multi=x         flash multiboot partition x (x= 1, 2, 3,...). Only supported by some boxes.\n");
	my_printf("   -n --nowrite          show only found image and mtd partitions (no write)\n");
	my_printf("   -q --quiet            show less output\n");
	my_printf("   -h --help             show help\n");
}

int find_image_files(char* p)
{
	DIR *d;
	struct dirent *entry;
	char path[4097];

	if (realpath(p, path) == NULL)
	{
		my_printf("Searching image files: Error path couldn't be resolved\n");
		return 0;
	}
	my_printf("Searching image files in %s resolved to %s\n", p, path);
	kernel_filename[0] = '\0';
	rootfs_filename[0] = '\0';

	// add / to the end of the path
	if (path[strlen(path)-1] != '/')
	{
		path[strlen(path)+1] = '\0';
		path[strlen(path)] = '/';
	}

	d = opendir(path);

	if (!d)
	{
		perror("Error reading image_directory");
		my_printf("\n");
		return 0;
	}

	do
	{
		entry = readdir(d);
		if (entry)
		{
			if ((strstr(entry->d_name, "kernel") != NULL
			  && strstr(entry->d_name, ".bin")   != NULL)			// ET-xx00, XP1000, VU boxes, DAGS boxes
			 || strcmp(entry->d_name, "uImage") == 0)				// Spark boxes
			{
				strcpy(kernel_filename, path);
				strcpy(&kernel_filename[strlen(path)], entry->d_name);
				stat(kernel_filename, &kernel_file_stat);
				my_printf("Found kernel file: %s\n", kernel_filename);
			}
/* //NI
			if (strcmp(entry->d_name, "rootfs.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "root_cfe_auto.bin") == 0		// Solo2
			 || strcmp(entry->d_name, "root_cfe_auto.jffs2") == 0	// other VU boxes
			 || strcmp(entry->d_name, "oe_rootfs.bin") == 0			// DAGS boxes
			 || strcmp(entry->d_name, "e2jffs2.img") == 0			// Spark boxes
			 || strcmp(entry->d_name, "rootfs.tar.bz2") == 0)		// solo4k
*/
			if (strcmp(entry->d_name, "rootfs.tar.bz2") == 0)
			{
				strcpy(rootfs_filename, path);
				strcpy(&rootfs_filename[strlen(path)], entry->d_name);
				stat(rootfs_filename, &rootfs_file_stat);
				my_printf("Found rootfs file: %s\n", rootfs_filename);
			}
		}
	} while (entry);

	closedir(d);

	return 1;
}

int read_args(int argc, char *argv[])
{
	int option_index = 0;
	int opt;
	static const char *short_options = "k::r::nm:qh";
	static const struct option long_options[] = {
												{"kernel" , optional_argument, NULL, 'k'},
												{"rootfs" , optional_argument, NULL, 'r'},
												{"nowrite", no_argument      , NULL, 'n'},
												{"multi"  , required_argument, NULL, 'm'},
												{"quiet"  , no_argument      , NULL, 'q'},
												{"help"   , no_argument      , NULL, 'h'},
												{NULL     , no_argument      , NULL,  0} };

	multiboot_partition = -1;

	while ((opt= getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
	{
		switch (opt)
		{
			case 'k':
				flash_kernel = 1;
				if (optarg)
				{
					if (!strncmp(optarg, "mtd", 3))
					{
						my_printf("Flashing kernel with arg %s\n", optarg);
						strcpy(kernel_mtd_device_arg, optarg);
						user_mtd_kernel = 1;
					}
				}
				else
					my_printf("Flashing kernel\n");
				break;
			case 'r':
				flash_rootfs = 1;
				if (optarg)
				{
					if (!strncmp(optarg, "mtd", 3))
					{
						my_printf("Flashing rootfs with arg %s\n", optarg);
						strcpy(rootfs_mtd_device_arg, optarg);
						user_mtd_rootfs = 1;
					}
				}
				else
					my_printf("Flashing rootfs\n");
				break;
			case 'm':
				if (optarg)
					if (strlen(optarg) == 1 && ((int)optarg[0] >= 48) && ((int)optarg[0] <= 57))
					{
						multiboot_partition = strtol(optarg, NULL, 10);
						my_printf("Flashing multiboot partition %d\n", multiboot_partition);
					}
					else
					{
						my_printf("Error: Wrong multiboot partition value. Only values between 0 and 9 are allowed!\n");
						show_help = 1;
						return 0;
					}
				break;
			case 'n':
				no_write = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case '?':
				show_help = 1;
				return 0;
		}
	}

	if (argc == 1)
	{
		show_help = 1;
		return 0;
	}

	if (optind + 1 < argc)
	{
		my_printf("Wrong parameter: %s\n\n", argv[optind+1]);
		show_help = 1;
		return 0;
	}
	else if (optind + 1 == argc)
	{
		if (!find_image_files(argv[optind]))
			return 0;

		if (flash_kernel == 0 && flash_rootfs== 0) // set defaults
		{
			my_printf("Setting default parameter: Flashing kernel and rootfs\n");
			flash_kernel = 1;
			flash_rootfs = 1;
		}
	}
	else
	{
		my_printf("Error: Image_directory parameter missing!\n\n");
		show_help = 1;
		return 0;
	}

	return 1;
}

int read_mtd_file()
{
	FILE* f;

	f = fopen("/proc/mtd", "r");
	if (f == NULL)
	{ 
		perror("Error while opening /proc/mtd");
		// for testing try to open local mtd file
		f = fopen("./mtd", "r");
		if (f == NULL)
			return 0;
	}

	char line [1000];
	char dev  [1000];
	char size [1000];
	char esize[1000];
	char name [1000];
	char dev_path[] = "/dev/";
	int line_nr = 0;
	unsigned long devsize;
	int wrong_user_mtd = 0;

	my_printf("Found /proc/mtd entries:\n");
	my_printf("Device:   Size:     Erasesize:  Name:                   Image:\n");
	while (fgets(line, 1000, f) != NULL)
	{
		line_nr++;
		if (line_nr == 1) // check header
		{
			sscanf(line, "%s%s%s%s", dev, size, esize, name);
			if (strcmp(dev  , "dev:") != 0
			 || strcmp(size , "size") != 0
			 || strcmp(esize, "erasesize") != 0
			 || strcmp(name , "name") != 0)
			{
				my_printf("Error: /proc/mtd has an invalid format\n");
				fclose(f);
				return 0;
			}
		}
		else
		{
			sscanf(line, "%s%s%s%s", dev, size, esize, name);
			my_printf("%s %12s %9s    %-18s", dev, size, esize, name);
			devsize = strtoul(size, 0, 16);
			if (dev[strlen(dev)-1] == ':') // cut ':'
				dev[strlen(dev)-1] = '\0';
			// user selected kernel
			if (user_mtd_kernel && !strcmp(dev, kernel_mtd_device_arg))
			{
				strcpy(&kernel_device[0], dev_path);
				strcpy(&kernel_device[5], kernel_mtd_device_arg);
				if (kernel_file_stat.st_size <= devsize)
				{
/* //NI
					if ((strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0
						|| strcmp(name, "\"kernel2\"") == 0))
*/
					if ((strcmp(name, "\"kernel\"") == 0))
					{
						if (kernel_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", kernel_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_kernel_device = 1;
					}
					else
					{
						my_printf("  <-  Error: Selected by user is not a kernel mtd!!\n");
						wrong_user_mtd = 1;
					}
				}
				else
				{
					my_printf("  <-  Error: Kernel file is bigger than device size!!\n");
					wrong_user_mtd = 1;
				}
			}
			// user selected rootfs
			else if (user_mtd_rootfs && !strcmp(dev, rootfs_mtd_device_arg))
			{
				strcpy(&rootfs_device[0], dev_path);
				strcpy(&rootfs_device[5], rootfs_mtd_device_arg);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
/* //NI
					if (strcmp(name, "\"rootfs\"") == 0
						|| strcmp(name, "\"rootfs2\"") == 0)
*/
					if (strcmp(name, "\"rootfs\"") == 0)
					{
						if (rootfs_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", rootfs_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_rootfs_device = 1;
					}
					else
					{
						my_printf("  <-  Error: Selected by user is not a rootfs mtd!!\n");
						wrong_user_mtd = 1;
					}
				}
				else if (strcmp(esize, "0001f000") == 0)
				{
					my_printf("  <-  Error: Invalid erasesize\n");
					wrong_user_mtd = 1;
				}
				else
				{
					my_printf("  <-  Error: Rootfs file is bigger than device size!!\n");
					wrong_user_mtd = 1;
				}
			}
			// auto kernel
/* //NI
			else if (!user_mtd_kernel
					&& (strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0))
*/
			else if (!user_mtd_kernel
					&& (strcmp(name, "\"kernel\"") == 0))
			{
				if (found_kernel_device)
				{
					my_printf("\n");
					continue;
				}
				strcpy(&kernel_device[0], dev_path);
				strcpy(&kernel_device[5], dev);
				if (kernel_file_stat.st_size <= devsize)
				{
					if (kernel_filename[0] != '\0')
						my_printf("  ->  %s\n", kernel_filename);
					else
						my_printf("\n");
					found_kernel_device = 1;
				}
				else
					my_printf("  <-  Error: Kernel file is bigger than device size!!\n");
			}
			// auto rootfs
			else if (!user_mtd_rootfs && strcmp(name, "\"rootfs\"") == 0)
			{
				if (found_rootfs_device)
				{
					my_printf("\n");
					continue;
				}
				strcpy(&rootfs_device[0], dev_path);
				strcpy(&rootfs_device[5], dev);
				unsigned long devsize;
				devsize = strtoul(size, 0, 16);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					if (rootfs_filename[0] != '\0')
						my_printf("  ->  %s\n", rootfs_filename);
					else
						my_printf("\n");
					found_rootfs_device = 1;
				}
				else if (strcmp(esize, "0001f000") == 0)
					my_printf("  <-  Error: Invalid erasesize\n");
				else
					my_printf("  <-  Error: Rootfs file is bigger than device size!!\n");
			}
			else
				my_printf("\n");
		}
	}

	my_printf("Using kernel mtd device: %s\n", kernel_device);
	my_printf("Using rootfs mtd device: %s\n", rootfs_device);

	fclose(f);

	if (wrong_user_mtd)
	{
		my_printf("Error: User selected mtd device cannot be used!\n");
		return 0;
	}

	return 1;
}

int kernel_flash(char* device, char* filename)
{
	if (rootfs_type == EXT4)
		return flash_ext4_kernel(device, filename, kernel_file_stat.st_size, quiet, no_write);
	else
		return flash_ubi_jffs2_kernel(device, filename, quiet, no_write);
}

int rootfs_flash(char* device, char* filename)
{
	if (rootfs_type == EXT4)
		return flash_ext4_rootfs(filename, quiet, no_write);
	else
		return flash_ubi_jffs2_rootfs(device, filename, rootfs_type, quiet, no_write);
}

// read root filesystem and checks whether /newroot is mounted as tmpfs
void readMounts()
{
	FILE* f;
	char* pos_start;
	char* pos_end;
	int k;

	for (k = 0; k < 30; k++)
		media_mounts[k][0] = '\0';

	rootfs_type = UNKNOWN;

	f = fopen("/proc/mounts", "r");
	if (f == NULL)
	{ 
		perror("Error while opening /proc/mounts");
		return;
	}

	char line [1000];
	while (fgets(line, 1000, f) != NULL)
	{
		if (strstr (line, " / ") != NULL &&
			strstr (line, "rootfs") != NULL &&
			strstr (line, "ubifs") != NULL)
		{
			my_printf("Found UBIFS rootfs\n");
			rootfs_type = UBIFS;
		}
		else if (strstr (line, " / ") != NULL &&
				 strstr (line, "root") != NULL &&
				 strstr (line, "jffs2") != NULL)
		{
			my_printf("Found JFFS2 rootfs\n");
			rootfs_type = JFFS2;
		}
		else if (strstr (line, " / ") != NULL &&
				 strstr (line, "ext4") != NULL)
		{
			my_printf("Found EXT4 rootfs\n");
			rootfs_type = EXT4;
		}
		else if (strstr (line, "/newroot") != NULL &&
				 strstr (line, "tmpfs") != NULL)
		{
			my_printf("Found mounted /newroot\n");
			newroot_mounted = 1;
		}
		else if ((pos_start = strstr (line, " /media/")) != NULL && media_mount_count < 30)
		{
			pos_end = strstr(pos_start + 1, " ");
			if (pos_end)
			{
				strncpy(media_mounts[media_mount_count], pos_start + 1, pos_end - pos_start - 1);
				media_mount_count++;
			}
		}
	}

	fclose(f);

	if (rootfs_type == UNKNOWN)
		my_printf("Found unknown rootfs\n");
}

int exec_ps()
{
	// call ps
	optind = 0; // reset getopt_long
	char* argv[] = {
		"ps",		// program name
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Execute: ps\n");
	if (ps_main(argc, argv) == 9999)
	{
		return 1; // neutrino found
	}
	return 0; // neutrino not found
}

int check_neutrino_stopped()
{
	int time = 0;
	int max_time = 70;
	int neutrino_found = 1;

	set_step_progress(0);
	if (!quiet)
		my_printf("Checking Neutrino is running...\n");
	while (time < max_time && neutrino_found)
	{
		//NI neutrino_found = exec_ps(); //FIXME

		//NI
		system("killall start_neutrino 2>/dev/null");
		int ret = system("pidof neutrino >/dev/null");
		if (ret == 0)
			neutrino_found = system("killall neutrino && sleep 3");
		else
			neutrino_found = 0;

		if (!neutrino_found)
		{
			if (!quiet)
				my_printf("Neutrino is stopped\n");
		}
		else
		{
			sleep(2);
			time += 2;
			if (!quiet)
				my_printf("Neutrino still running\n");
		}
		set_step_progress(time * 100 / max_time);
	}

	if (neutrino_found)
		return 0;

	return 1;
}

int exec_fuser_kill()
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"fuser",		// program name
		"-k",			// kill
		"-m",			// mount point
		"/oldroot/",	// rootfs
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Execute: fuser -k -m /oldroot/\n");
	if (!no_write)
		if (fuser_main(argc, argv) != 0)
			return 0;

	return 1;
}

int daemonize()
{
	// Prevents that ofgwrite will be killed when init 1 is performed
	my_printf("daemonize\n");

	pid_t pid = fork();
	if (pid < 0)
	{
		my_printf("Error fork failed\n");
		return 0;
	}
	else if (pid > 0)
	{
		// stop parent
		exit(EXIT_SUCCESS);
	}

	if (setsid() < 0)
	{
		my_printf("Error setsid failed\n");
		return 0;
	}

	pid = fork();
	if (pid < 0)
	{
		my_printf("Error 2. fork failed\n");
		return 0;
	}
	else if (pid > 0)
	{
		// stop child
		exit(EXIT_SUCCESS);
	}

	umask(0);
	my_printf(" successful\n");
	return 1;
}

int umount_rootfs()
{
	int ret = 0;
	my_printf("start umount_rootfs\n");
	// the start script creates /newroot dir and mount tmpfs on it
	// create directories
	ret += chdir("/newroot");
	ret += mkdir("/newroot/bin", 777);
	ret += mkdir("/newroot/boot", 777); //NI
	ret += mkdir("/newroot/dev", 777);
	ret += mkdir("/newroot/etc", 777);
	ret += mkdir("/newroot/dev/pts", 777);
	ret += mkdir("/newroot/lib", 777);
	ret += mkdir("/newroot/media", 777);
	ret += mkdir("/newroot/mnt", 777); //NI
	ret += mkdir("/newroot/oldroot", 777);
	ret += mkdir("/newroot/oldroot_bind", 777);
	ret += mkdir("/newroot/proc", 777);
	ret += mkdir("/newroot/run", 777);
	ret += mkdir("/newroot/sbin", 777);
	ret += mkdir("/newroot/srv", 777); //NI
	ret += mkdir("/newroot/sys", 777);
	ret += mkdir("/newroot/tmp", 777); //NI
/* //NI
	ret += mkdir("/newroot/usr", 777);
	ret += mkdir("/newroot/usr/lib", 777);
	ret += mkdir("/newroot/usr/lib/autofs", 777);
*/
	ret += mkdir("/newroot/var", 777);
	ret += mkdir("/newroot/var/lib", 777); //NI
	ret += mkdir("/newroot/var/lib/nfs", 777); //NI
	ret += mkdir("/newroot/var/samba", 777); //NI
/* //NI
	ret += mkdir("/newroot/var/volatile", 777);
*/
	if (ret != 0)
	{
		my_printf("Error creating necessary directories\n");
		return 0;
	}

	// we need init and libs to be able to exec init u later
	ret =  system("cp -arf /bin/busybox*     /newroot/bin");
	ret += system("cp -arf /bin/bash*        /newroot/bin");
/* //NI
	ret =  system("cp -arf /bin/busybox*     /newroot/bin");
	ret += system("cp -arf /bin/sh*          /newroot/bin");
	ret += system("cp -arf /bin/bash*        /newroot/bin");
	ret += system("cp -arf /sbin/init*       /newroot/sbin");
	ret += system("cp -arf /lib/libcrypt*    /newroot/lib");
	ret += system("cp -arf /lib/libc*        /newroot/lib");
	ret += system("cp -arf /lib/ld*          /newroot/lib");
	ret += system("cp -arf /lib/libtinfo*    /newroot/lib");
	ret += system("cp -arf /lib/libdl*       /newroot/lib");

	if (ret != 0)
	{
		my_printf("Error copying binary and libs\n");
		return 0;
	}

	// copy for automount ignore errors as autofs is maybe not installed
	ret = system("cp -arf /usr/sbin/autom*  /newroot/bin");
	ret += system("cp -arf /etc/auto*        /newroot/etc");
	ret += system("cp -arf /lib/libpthread*  /newroot/lib");
	ret += system("cp -arf /lib/libnss*      /newroot/lib");
	ret += system("cp -arf /lib/libnsl*      /newroot/lib");
	ret += system("cp -arf /lib/libresolv*   /newroot/lib");
	ret += system("cp -arf /usr/lib/libtirp* /newroot/usr/lib");
	ret += system("cp -arf /usr/lib/autofs/* /newroot/usr/lib/autofs");
	ret += system("cp -arf /etc/nsswitch*    /newroot/etc");
	ret += system("cp -arf /etc/resolv*      /newroot/etc");

	// Switch to user mode 1
	my_printf("Switching to user mode 2\n");
	ret = system("init 2");
	if (ret)
	{
		my_printf("Error switching runmode!\n");
		set_error_text("Error switching runmode! Abort flashing.");
		sleep(5);
		return 0;
	}
*/

	// it can take several seconds until Neutrino is shut down
	// wait because otherwise remounting read only is not possible
	set_step("Wait until Neutrino is stopped");
	if (!check_neutrino_stopped())
	{
		my_printf("Error Neutrino can't be stopped! Abort flashing.\n");
		set_error_text("Error neutrio can't be stopped! Abort flashing.");
/* //NI
		ret = system("init 3");
*/
		return 0;
	}
	show_main_window(1, ofgwrite_version);
	set_overall_text("Flashing image");
	set_step_without_incr("Wait until Neutrino is stopped");
	sleep(2);

	ret = pivot_root("/newroot/", "oldroot");
	if (ret)
	{
		my_printf("Error executing pivot_root!\n");
		set_error_text("Error pivot_root! Abort flashing.");
		sleep(5);
/* //NI
		ret = system("init 3");
*/
		return 0;
	}

	ret = chdir("/");
	// move mounts to new root
	ret =  mount("/oldroot/dev/", "dev/", NULL, MS_MOVE, NULL);
	ret =  mount("/oldroot/boot/", "boot/", NULL, MS_MOVE, NULL); //NI
	ret += mount("/oldroot/proc/", "proc/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/sys/", "sys/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/mnt/", "mnt/", NULL, MS_MOVE, NULL); //NI
	ret += mount("/oldroot/srv/", "srv/", NULL, MS_MOVE, NULL); //NI
	ret += mount("/oldroot/tmp/", "tmp/", NULL, MS_MOVE, NULL); //NI
	ret += mount("/oldroot/var/lib/nfs/", "var/lib/nfs/", NULL, MS_MOVE, NULL); //NI
	ret += mount("/oldroot/var/samba/", "var/samba/", NULL, MS_MOVE, NULL); //NI
/* //NI
	ret += mount("/oldroot/var/volatile", "var/volatile/", NULL, MS_MOVE, NULL);
	// create link for tmp
	ret += symlink("/var/volatile/tmp", "/tmp");
*/
	if (ret != 0)
	{
		my_printf("Error move mounts to newroot\n");
		set_error_text1("Error move mounts to newroot. Abort flashing!");
		set_error_text2("Rebooting in 30 seconds!");
		sleep(30);
		reboot(LINUX_REBOOT_CMD_RESTART);
		return 0;
	}
	ret = mount("/oldroot/media/", "media/", NULL, MS_MOVE, NULL);
	if (ret != 0)
	{
		// /media is no tmpfs -> move every mount
		my_printf("/media is not tmpfs\n");
		int k;
		char oldroot_path[1000];
		for (k = 0; k < media_mount_count; k++)
		{
			strcpy(oldroot_path, "/oldroot");
			strcat(oldroot_path, media_mounts[k]);
			mkdir(media_mounts[k], 777);
			my_printf("Moving %s to %s\n", oldroot_path, media_mounts[k]);
			// mount move: ignore errors as e.g. network shares cannot be moved
			mount(oldroot_path, media_mounts[k], NULL, MS_MOVE, NULL);
		}
	}

	// create link for mount/umount for autofs
	ret = symlink("/bin/busybox", "/bin/mount");
	ret += symlink("/bin/busybox", "/bin/umount");
	ret += symlink("/bin/busybox", "/bin/sh"); //NI
	ret += symlink("/bin/busybox", "/sbin/init"); //NI

/* //NI
	// try to restart autofs
	ret =  system("/bin/automount");
	if (ret != 0)
	{
		my_printf("Error starting autofs\n");
	}

	// restart init process
	ret = system("exec init u");
*/
	ret = system("exec init");
	sleep(3);

	// kill all remaining open processes which prevent umounting rootfs
	ret = exec_fuser_kill();
	if (!ret)
		my_printf("fuser successful\n");
	sleep(3);

	ret = umount("/oldroot/newroot");
	ret = umount("/oldroot/");
	if (!ret)
		my_printf("umount successful\n");
	else
		my_printf("umount not successful\n");

	if (!ret && rootfs_type == EXT4) // umount success and ext4 -> remount again
	{
		ret = mount(rootfs_device, "/oldroot_bind/", "ext4", 0, NULL);
		if (!ret)
			my_printf("remount to /oldroot_bind/ successful\n");
		else
		{
			my_printf("Error mounting(bind) root! Abort flashing.\n");
			set_error_text1("Error remounting(bind) root! Abort flashing.");
			set_error_text2("Rebooting in 30 seconds");
			sleep(30);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return 0;
		}
	}
	else if (ret && rootfs_type == EXT4)
	// umount failed and ext4 -> bind mountpoint to new /oldroot_bind/
	// Using bind because otherwise all data in not moved filesystems under /oldroot will be deleted
	{
		ret = mount("/oldroot/", "/oldroot_bind/", "", MS_BIND, NULL);
		if (!ret)
			my_printf("bind to /oldroot_bind/ successful\n");
		else
		{
			my_printf("Error binding root! Abort flashing.\n");
			set_error_text1("Error binding root! Abort flashing.");
			set_error_text2("Rebooting in 30 seconds");
			sleep(30);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return 0;
		}
	}
	else if (ret && rootfs_type != EXT4) // umount failed -> remount read only
	{
		ret = mount("/oldroot/", "/oldroot/", "", MS_REMOUNT | MS_RDONLY, NULL);
		if (ret)
		{
			my_printf("Error remounting root! Abort flashing.\n");
			set_error_text1("Error remounting root! Abort flashing.");
			set_error_text2("Rebooting in 30 seconds");
			sleep(30);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return 0;
		}
	}

	return 1;
}

int check_env()
{
	if (!newroot_mounted)
	{
		my_printf("Please use ofgwrite command to start flashing!\n");
		return 0;
	}

	return 1;
}

void ext4_kernel_dev_found(const char* dev, int partition_number)
{
	found_kernel_device = 1;
	sprintf(kernel_device, "%sp%d", dev, partition_number);
	my_printf("Using %s as kernel device\n", kernel_device);
}

void ext4_rootfs_dev_found(const char* dev, int partition_number)
{
	// Check whether rootfs is on the same device as current used rootfs
	sprintf(rootfs_device, "%sp", dev);
	if (strncmp(rootfs_device, current_rootfs_device, strlen(rootfs_device)) != 0)
	{
		my_printf("Rootfs(%s) is on different device than current rootfs(%s). Maybe wrong device selected. -> Aborting\n", dev, current_rootfs_device);
		return;
	}

	found_rootfs_device = 1;
	sprintf(rootfs_device, "%sp%d", dev, partition_number);
	my_printf("Using %s as rootfs device\n", rootfs_device);
}

void determineCurrentUsedRootfs()
{
	my_printf("Determine current rootfs\n");
	// Read /proc/cmdline to distinguish whether current running image should be flashed
	FILE* f;

	f = fopen("/proc/cmdline", "r");
	if (f == NULL)
	{
		perror("Error while opening /proc/cmdline");
		return;
	}

	char line[1000];
	char dev [1000];
	char* pos;
	char* pos2;
	memset(current_rootfs_device, 0, sizeof(current_rootfs_device));

	if (fgets(line, 1000, f) != NULL)
	{
		pos = strstr(line, "root=");
		if (pos)
		{
			pos2 = strstr(pos, " ");
			if (pos2)
			{
				strncpy(current_rootfs_device, pos + 5, pos2-pos-5);
			}
			else
			{
				strcpy(current_rootfs_device, pos + 5);
			}
		}
	}
	my_printf("Current rootfs is: %s\n", current_rootfs_device);
	fclose(f);
}

void find_kernel_rootfs_device()
{
	determineCurrentUsedRootfs();

	// call fdisk -l
	optind = 0; // reset getopt_long
	char* argv[] = {
		"fdisk",		// program name
		"-l",			// list
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Execute: fdisk -l\n");
	if (fdisk_main(argc, argv) != 0)
		return;

	if (!found_kernel_device)
	{
		my_printf("Error: No kernel device found!\n");
		return;
	}

	if (!found_rootfs_device)
	{
		my_printf("Error: No rootfs device found!\n");
		return;
	}

	if (strcmp(rootfs_device, current_rootfs_device) != 0)
	{
		stop_neutrino_needed = 0;
		my_printf("Flashing currently not running image\n");
	}
}

// Checks whether kernel and rootfs device is bigger than the kernel and rootfs file
int check_device_size()
{
	unsigned long long devsize = 0;
	int fd = 0;
	// check kernel
	if (found_kernel_device && kernel_filename[0] != '\0')
	{
		fd = open(kernel_device, O_RDONLY);
		if (fd <= 0)
		{
			my_printf("Unable to open kernel device %s. Aborting\n", kernel_device);
			return 0;
		}
		if (ioctl(fd, BLKGETSIZE64, &devsize))
		{
			my_printf("Couldn't determine kernel device size. Aborting\n");
			return 0;
		}
		if (kernel_file_stat.st_size > devsize)
		{
			my_printf("Kernel file(%lld) is bigger than kernel device(%llu). Aborting\n", kernel_file_stat.st_size, devsize);
			return 0;
		}
	}

	// check rootfs
	if (found_rootfs_device && rootfs_filename[0] != '\0')
	{
		fd = open(rootfs_device, O_RDONLY);
		if (fd <= 0)
		{
			my_printf("Unable to open rootfs device %s. Aborting\n", rootfs_device);
			return 0;
		}
		if (ioctl(fd, BLKGETSIZE64, &devsize))
		{
			my_printf("Couldn't determine rootfs device size. Aborting\n");
			return 0;
		}
		if (rootfs_file_stat.st_size > devsize)
		{
			my_printf("Rootfs file (%lld) is bigger than rootfs device(%llu). Aborting\n", rootfs_file_stat.st_size, devsize);
			return 0;
		}
	}

	return 1;
}

int main(int argc, char *argv[])
{
	// Check if running on a box or on a PC. Stop working on PC to prevent overwriting important files
#if defined(__i386) || defined(__x86_64__)
	my_printf("You're running ofgwrite on a PC. Aborting...\n");
	exit(EXIT_FAILURE);
#endif

	// Open log
	openlog("ofgwrite", LOG_CONS | LOG_NDELAY, LOG_USER);

	my_printf("\nofgwrite Utility v%s\n", ofgwrite_version);
	my_printf("Author: Betacentauri\n");
	my_printf("Based upon: mtd-utils-native-1.5.1 and busybox 1.24.1\n");
	my_printf("Use at your own risk! Make always a backup before use!\n");
	my_printf("Don't use it if you use multiple ubi volumes in ubi layer!\n\n");

	int ret;

	ret = read_args(argc, argv);

	if (!ret || show_help)
	{
		printUsage();
		return EXIT_FAILURE;
	}

	// set rootfs type and more
	readMounts();

	if (rootfs_type == UBIFS || rootfs_type == JFFS2)
	{
		my_printf("\n");
		if (!read_mtd_file())
			return EXIT_FAILURE;
	}
	else if (rootfs_type == EXT4)
	{
		my_printf("\n");
		find_kernel_rootfs_device();
		if (flash_kernel && !found_kernel_device)
			return EXIT_FAILURE;
		if (flash_rootfs && !found_rootfs_device)
			return EXIT_FAILURE;
		if (!check_device_size())
			return EXIT_FAILURE;
	}

	my_printf("\n");

	if (flash_kernel && (!found_kernel_device || kernel_filename[0] == '\0'))
	{
		my_printf("Error: Cannot flash kernel");
		if (!found_kernel_device)
			my_printf(", because no kernel MTD entry was found\n");
		else
			my_printf(", because no kernel file was found\n");
		return EXIT_FAILURE;
	}

	if (flash_rootfs && (!found_rootfs_device || rootfs_filename[0] == '\0' || rootfs_type == UNKNOWN))
	{
		my_printf("Error: Cannot flash rootfs");
		if (!found_rootfs_device)
			my_printf(", because no rootfs MTD entry was found\n");
		else if (rootfs_filename[0] == '\0')
			my_printf(", because no rootfs file was found\n");
		else
			my_printf(", because rootfs type is unknown\n");
		return EXIT_FAILURE;
	}

	if (flash_kernel && !flash_rootfs) // flash only kernel
	{
		if (!quiet)
			my_printf("Flashing kernel ...\n");

		init_framebuffer(2);
		show_main_window(0, ofgwrite_version);
		set_overall_text("Flashing kernel");

		if (!kernel_flash(kernel_device, kernel_filename))
			ret = EXIT_FAILURE;
		else
			ret = EXIT_SUCCESS;

		if (!quiet && ret == EXIT_SUCCESS)
		{
			my_printf("done\n");
			set_step("Successfully flashed kernel!");
			sleep(5);
		}
		else if (ret == EXIT_FAILURE)
		{
			my_printf("failed. System won't boot. Please flash backup!\n");
			set_error_text1("Error flashing kernel. System won't boot!");
			set_error_text2("Please flash backup! Go back to Neutrino in 60 sec");
			sleep(60);
		}
		closelog();
		close_framebuffer();
		return ret;
	}

	if (flash_rootfs)
	{
		ret = 0;

		// Check whether /newroot exists and is mounted as tmpfs
		if (!check_env())
		{
			closelog();
			return EXIT_FAILURE;
		}

		int steps = 6;
		if (flash_kernel && rootfs_type != EXT4)
			steps+= 2;
		else if (flash_kernel && rootfs_type == EXT4)
			steps+= 1;
		init_framebuffer(steps);
		show_main_window(0, ofgwrite_version);
		set_overall_text("Flashing image");
		set_step("Killing processes");

		// kill nmbd, smbd, rpc.mountd and rpc.statd -> otherwise remounting root read-only is not possible
		if (!no_write && stop_neutrino_needed)
		{
/* //NI
			ret = system("killall nmbd");
			ret = system("killall smbd");
			ret = system("killall rpc.mountd");
			ret = system("killall rpc.statd");
			ret = system("/etc/init.d/softcam stop");
			ret = system("killall CCcam");
			ret = system("pkill -9 -f '[Oo][Ss][Cc][Aa][Mm]'");
			ret = system("ps w | grep -i oscam | grep -v grep | awk '{print $1}'| xargs kill -9");
			ret = system("pkill -9 -f '[Ww][Ii][Cc][Aa][Rr][Dd][Dd]'");
			ret = system("ps w | grep -i wicardd | grep -v grep | awk '{print $1}'| xargs kill -9");
			ret = system("killall kodi.bin");
			ret = system("killall hddtemp");
			ret = system("killall transmission-daemon");
			ret = system("killall openvpn");
			ret = system("/etc/init.d/sabnzbd stop");
			ret = system("pkill -9 -f cihelper");
			ret = system("pkill -9 -f ciplus_helper");
			ret = system("pkill -9 -f ciplushelper");
			// kill VMC
			ret = system("pkill -f vmc.sh");
			ret = system("pkill -f DBServer.py");
			// stop autofs
			ret = system("/etc/init.d/autofs stop");
*/
			ret = system("/etc/init.d/rcK");
			// ignore return values, because the processes might not run
		}

		// sync filesystem
		my_printf("Syncing filesystem\n");
		set_step("Syncing filesystem");
		sync();
		sleep(1);

		set_step("init 2");
		if (!no_write && stop_neutrino_needed)
		{
			if (!daemonize())
			{
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
			if (!umount_rootfs())
			{
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}
		// if not running rootfs is flashed then we need to mount it before start flashing
		if (!no_write && !stop_neutrino_needed && rootfs_type == EXT4)
		{
			set_step("Mount rootfs");
			mkdir("/oldroot_bind", 777);
			// mount rootfs device
			ret = mount(rootfs_device, "/oldroot_bind/", "ext4", 0, NULL);
			if (!ret)
				my_printf("Mount to /oldroot_bind/ successful\n");
			else if (errno == EINVAL)
			{
				// most likely partition is not formatted -> format it
				char mkfs_cmd[100];
				sprintf(mkfs_cmd, "mkfs.ext4 %s", rootfs_device);
				my_printf("Formatting %s\n", rootfs_device);
				ret = system(mkfs_cmd);
				if (!ret)
				{ // try to mount it again
					ret = mount(rootfs_device, "/oldroot_bind/", "ext4", 0, NULL);
					if (!ret)
						my_printf("Mount to /oldroot_bind/ successful\n");
				}
			}
			if (ret)
			{
				my_printf("Error mounting root! Abort flashing.\n");
				set_error_text1("Error mounting root! Abort flashing.");
				sleep(3);
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}

		//Flash kernel
		if (flash_kernel)
		{
			if (!quiet)
				my_printf("Flashing kernel ...\n");

			if (!kernel_flash(kernel_device, kernel_filename))
			{
				my_printf("Error flashing kernel. System won't boot. Please flash backup! Starting Neutrino in 60 seconds\n");
				set_error_text1("Error flashing kernel. System won't boot!");
				set_error_text2("Please flash backup! Starting Neutrino in 60 sec");
				if (stop_neutrino_needed)
				{
					sleep(60);
					reboot(LINUX_REBOOT_CMD_RESTART);
				}
				sleep(3);
				close_framebuffer();
				return EXIT_FAILURE;
			}
			sync();
			my_printf("Successfully flashed kernel!\n");
		}

		// Flash rootfs
		if (!rootfs_flash(rootfs_device, rootfs_filename))
		{
			my_printf("Error flashing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
			set_error_text1("Error flashing rootfs. System won't boot!");
			set_error_text2("Please flash backup! Rebooting in 60 sec");
			if (stop_neutrino_needed)
			{
				sleep(60);
				reboot(LINUX_REBOOT_CMD_RESTART);
			}
			sleep(3);
			close_framebuffer();
			return EXIT_FAILURE;
		}

		my_printf("Successfully flashed rootfs!\n"); //NI
		if (!stop_neutrino_needed)
		{
			ret = umount("/oldroot_bind/");
			ret = rmdir("/oldroot_bind/");
			ret = umount("/newroot/");
			ret = rmdir("/newroot/");
			set_step("Successfully flashed!");
		}
		else
			set_step("Successfully flashed!"); //NI
		fflush(stdout);
		fflush(stderr);
		sleep(3);
		if (!no_write && stop_neutrino_needed)
		{
			//NI
			my_printf("Rebooting in 3 seconds...\n");
			set_step("Successfully flashed! Rebooting in 3 seconds...");
			sleep(3);
			reboot(LINUX_REBOOT_CMD_RESTART);
		}
	}

	closelog();
	close_framebuffer();

	my_printf("Exiting with EXIT_SUCCESS\n"); //NI
	return EXIT_SUCCESS;
}
